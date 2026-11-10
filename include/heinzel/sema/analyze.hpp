#pragma once

// The analysis pass: works out every value's shape, element type and precision, then runs the checks.
//
// No branch here asks which operation it is looking at: it asks the rule table which kind of rule the
// operation declared, so adding an operation adds a table row and leaves this file alone. Shapes and
// element types (with promotion) resolve together in one pass; the frontend decides none of them.

#include <cstddef>
#include <cstdint>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/sema/attribute_checks.hpp"
#include "heinzel/sema/declared_variables.hpp"
#include "heinzel/sema/framing.hpp"
#include "heinzel/sema/matmul.hpp"
#include "heinzel/sema/order.hpp"
#include "heinzel/sema/overlap_add.hpp"
#include "heinzel/sema/solver.hpp"
#include "heinzel/sema/spectral.hpp"
#include "heinzel/sema/time_checks.hpp"
#include "heinzel/sema/untyped.hpp"
#include "heinzel/sema/view_shape.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel {

namespace detail {

// One axis of a broadcast. Two extents combine if they are the same extent, or if one of them is a
// literal 1. `agreed` is set false when they cannot.
//
// Every question about an extent goes through a named helper, never `a == 1` on a raw integer: that
// would be wrong as soon as a symbol reached it.
constexpr Extent combine_extent(Extent a, Extent b, ExtentSolver& axes, OpName at, bool& agreed, bool& asked_the_solver,
                                Diagnostic& why) {
    if (same_extent(a, b)) {
        return a;
    }
    if (is_literal(a) && literal_of(a) == 1) {
        return b;
    }
    if (is_literal(b) && literal_of(b) == 1) {
        return a;
    }
    // Two different sizes. This is the plain disagreement, and the caller reports it with both
    // whole shapes, which is more use than one axis on its own.
    if (is_literal(a) && is_literal(b)) {
        agreed = false;
        return a;
    }
    // At least one is a symbol, so whether these can be the same axis is the solver's question and
    // not a guess to make here. It answers with a size when the class already has one, and it
    // refuses — naming both sizes and both operations — when the graph has contradicted itself.
    asked_the_solver = true;
    if (!axes.unify(a, b, at, why)) {
        agreed = false;
        return a;
    }
    return axes.resolve(a);
}

// numpy's rule: line the shapes up from the innermost axis outwards.
constexpr Shape broadcast(const Shape& a, const Shape& b, ExtentSolver& axes, OpName at, bool& agreed,
                          bool& asked_the_solver, Diagnostic& why) {
    Shape out{};
    out.rank = a.rank > b.rank ? a.rank : b.rank;
    for (std::int32_t k = 0; k < out.rank; ++k) {
        const std::int32_t ia = a.rank - out.rank + k; // negative ⇒ this shape has no such axis
        const std::int32_t ib = b.rank - out.rank + k;
        const Extent ea = ia >= 0 ? extent_at(a, ia) : literal_extent(1);
        const Extent eb = ib >= 0 ? extent_at(b, ib) : literal_extent(1);
        set_extent(out, k, combine_extent(ea, eb, axes, at, agreed, asked_the_solver, why));
    }
    return out;
}

// Which of two element types the result takes. A literal constant adapts; it never drives: otherwise
// writing a coefficient as `0.5` would make a whole expression double precision.
constexpr ElemType wider(ElemType a, ElemType b) {
    if (a == ElemType::Unknown) {
        return b;
    }
    if (b == ElemType::Unknown) {
        return a;
    }
    return (a == ElemType::F64 || b == ElemType::F64) ? ElemType::F64 : ElemType::F32;
}

// "shapes disagree at _add: [4] and [8]": the operation, and both shapes.
constexpr Diagnostic shapes_disagree(const Graph& g, const Node& n) {
    MessageWriter m;
    m << std::string_view{"shapes disagree at "};
    write_op(m, n.op);
    m << std::string_view{": "};
    write_shape(m, g.values[static_cast<std::size_t>(n.inputs[0])].shape);
    m << std::string_view{" and "};
    write_shape(m, g.values[static_cast<std::size_t>(n.inputs[1])].shape);
    return m.problem();
}

// What is left of a shape when the named axes are collapsed. `keep` is the difference between
// dropping an axis and leaving it behind with an extent of one. Shared by a reduction (one operand's
// axes) and a contraction (the axes of a product of two).
constexpr Shape collapsed_shape(const Shape& in, const Axes& over, bool keep) {
    Shape shape{};
    for (std::int32_t k = 0; k < in.rank; ++k) {
        bool collapses = false;
        for (std::int32_t j = 0; j < over.rank; ++j) {
            collapses = collapses || over.axes[j] == k;
        }
        if (!collapses) {
            set_extent(shape, shape.rank++, extent_at(in, k));
        } else if (keep) {
            set_extent(shape, shape.rank++, literal_extent(1));
        }
    }
    return shape;
}

constexpr Shape reduced_shape(const Shape& in, const ReduceAttr& r) {
    return collapsed_shape(in, r.axes, r.keep_axes);
}

// The shape of a view node (`sema/view_shape.hpp` has the shapes): `_reshape`'s rearrangements and the
// two-operand join. A join is shaped from two operands, its axis extent a sum. Every other view is shaped
// from its one operand and may derive an output extent from a symbolic source, which is told to the solver
// here, where it is in hand (a contradiction comes back `agreed=false`). No refusal is worded here: a shape
// the sweep cannot make is its operand's, and `check_attributes` words why.
constexpr Shape view_node_shape(const Graph& g, const Node& n, ExtentSolver& axes, bool& asked_the_solver,
                                bool& agreed, Diagnostic& why) {
    const Shape& src = g.values[static_cast<std::size_t>(n.inputs[0])].shape;
    if (n.attr.view.kind == ViewKind::Concat) {
        return concat_shape(src, g.values[static_cast<std::size_t>(n.inputs[1])].shape, n.op, n.attr.view);
    }
    const ViewAttr view = as_read(src, n.attr.view);
    const Shape shape = viewed_shape(src, n.op, view);
    if (!record_derived_extent(src, shape, view, shown_name(n.op), axes, asked_the_solver, why)) {
        agreed = false;
    }
    return shape;
}

// The shape column, read for one node. `agreed` comes back false when two operands could not be
// broadcast together, and then `why` may already say more than the caller could.
constexpr Shape shape_of_node(const Graph& g, const Node& n, const OpRules& rules, ExtentSolver& axes,
                              bool& asked_the_solver, bool& agreed, Diagnostic& why) {
    Shape shape{};
    switch (rules.shape) {
    case ShapeRule::Elementwise:
        for (std::uint8_t k = 0; k < rules.arity; ++k) {
            const Value& in = g.values[static_cast<std::size_t>(n.inputs[k])];
            shape = k == 0 ? in.shape
                           : broadcast(shape, in.shape, axes, shown_name(n.op), agreed, asked_the_solver, why);
        }
        break;
    case ShapeRule::Passthrough:
        shape = g.values[static_cast<std::size_t>(n.inputs[0])].shape;
        break;
    case ShapeRule::ReduceAxes:
        shape = reduced_shape(g.values[static_cast<std::size_t>(n.inputs[0])].shape, n.attr.reduce);
        break;
    case ShapeRule::ByViewKind:
        shape = view_node_shape(g, n, axes, asked_the_solver, agreed, why);
        break;
    case ShapeRule::ContractAxes:
        // A sum of products (`_contr`): a broadcast, then a collapse.
        shape = collapsed_shape(broadcast(g.values[static_cast<std::size_t>(n.inputs[0])].shape,
                                          g.values[static_cast<std::size_t>(n.inputs[1])].shape, axes,
                                          shown_name(n.op), agreed, asked_the_solver, why),
                                n.attr.contract.axes, false);
        break;
    case ShapeRule::ContractGeneral:
        // A general, transposing or batched contraction (`_matmul`); its refusals are in its attribute check.
        shape = contract_general_node_shape(g, n);
        break;
    case ShapeRule::SpectralAxis:
        // The innermost axis becomes its half-spectrum length (or back), shaped from the attribute.
        shape = fft_shape(g.values[static_cast<std::size_t>(n.inputs[0])].shape, n.op, n.attr.fft);
        break;
    case ShapeRule::OverlapAddAxes:
        // The two innermost element axes `[.., F, K]` collapse into one reconstructed length.
        shape = overlap_add_shape(g.values[static_cast<std::size_t>(n.inputs[0])].shape, n.op, n.attr.overlap);
        break;
    case ShapeRule::OrderAxis:
        // One element axis is kept (Sort), resized to k (TopK) or dropped (Quantile).
        shape = order_shape(g.values[static_cast<std::size_t>(n.inputs[0])].shape, n.op, n.attr.order);
        break;
    case ShapeRule::Declared:
        // A generator states its own shape (`_iota`, `_const`); `check_generator` refuses rank 0 or a symbolic element.
        shape = n.attr.generator.shape;
        break;
    case ShapeRule::OneNumber:
        shape = Shape{}; // (`_first`: no axis, broadcast where it is read)
        break;
    case ShapeRule::Unclassified:
        fail("heinzel::infer: an operation with no shape rule reached the pass");
    }
    return shape;
}

// The element-type column, read for one node. `Unknown` means the column said nothing yet, not
// that anything is wrong: a literal's type is settled later, by the operation that uses it.
constexpr ElemType type_of_node(const Graph& g, const Node& n, const OpRules& rules) {
    switch (rules.type) {
    case TypeRule::FromOperands:
    case TypeRule::FromOperandsExceptFirst:
    case TypeRule::SameAsFirst:
        break;
    case TypeRule::FromAttribute:
        return n.attr.cast.to;
    case TypeRule::Declared:
        // A generator states its own element type (`_iota`, `_const`): it has no operand to carry one.
        return n.attr.generator.type;
    case TypeRule::Adapts:
        break; // typed where it is read, as a literal is (`type_the_numbers`)
    case TypeRule::Unclassified:
        fail("heinzel::infer: an operation with no element-type rule reached the pass");
    }
    ElemType type = ElemType::Unknown;
    const TypedOperands read = typed_operands(rules);
    for (std::uint8_t k = read.first; k < read.end; ++k) {
        type = wider(type, g.values[static_cast<std::size_t>(n.inputs[k])].type);
    }
    return type;
}

// A literal that still has no type takes the one the operation settled on: the other half of "a
// literal adapts". Nothing produces a literal, so this is the only place it can learn its type. Given
// the values made of numbers (`untyped`), each of them adapts as a literal does.
constexpr void adapt_literal_operands(Graph& g, const Node& n, const OpRules& rules, ElemType type,
                                      const bool* untyped, bool& learned_something) {
    if (type == ElemType::Unknown) {
        return;
    }
    for (std::uint8_t k = 0; k < rules.arity; ++k) {
        const auto v = static_cast<std::size_t>(n.inputs[k]);
        Value& operand = g.values[v];
        const bool adapts = untyped != nullptr ? untyped[v] : operand.category == ValueCategory::Const;
        if (adapts && operand.type == ElemType::Unknown) {
            operand.type = type;
            learned_something = true;
        }
    }
}

// A recurrence is what gives a self-referential value its shape and type: nothing else produces it,
// so this is where it learns what it is, unless it declared it: the declaration wins (checked after the
// sweep). One that takes the graph's length has it last, and keeps the f32 it is given where nothing typed
// it (`undriven_default_to_f32`) until its definition is typed. It reads the attribute kind, not an
// operation code.
constexpr void propagate_recurrence(Graph& g, const Node& n, const Declared& declared, bool& learned_something) {
    if (n.attr.kind != AttrKind::Recurrence || n.attr.recurrence.defines == kNoValue ||
        declared.at[static_cast<std::size_t>(n.attr.recurrence.defines)]) {
        return;
    }
    const auto v = static_cast<std::size_t>(n.attr.recurrence.defines);
    const Value& out = g.values[static_cast<std::size_t>(n.output)];
    Value& named = g.values[v];
    const Shape shape = declared.takes[v] ? with_length(out.shape, declared.length) : out.shape;
    const ElemType type = declared.takes[v] && out.type == ElemType::Unknown ? named.type : out.type;
    if (!(named.shape == shape) || named.type != type) {
        named.shape = shape;
        named.type = type;
        learned_something = true;
    }
}

// One node's shape and element type, written onto its output. Returns false only when the graph is
// wrong, and then `why` says how.
constexpr bool infer_node(Graph& g, const Node& n, const Declared& declared, const bool* untyped, ExtentSolver& axes,
                          bool& asked_the_solver, bool& learned_something, Diagnostic& why) {
    const OpRules rules = rules_of(n.op);
    bool agreed = true;
    const Shape shape = shape_of_node(g, n, rules, axes, asked_the_solver, agreed, why);

    if (!agreed) {
        // The solver refused and said why — one axis given two sizes, or a size that breaks what
        // was declared about the axis. Its message names both sizes and the operation that
        // introduced each, which is more than the shape complaint can say.
        if (why.ok) {
            why = shapes_disagree(g, n);
        }
        return false;
    }

    Value& out = g.values[static_cast<std::size_t>(n.output)];
    if (!(out.shape == shape)) {
        out.shape = shape;
        learned_something = true;
    }

    const ElemType type = type_of_node(g, n, rules);
    if (type != ElemType::Unknown && out.type != type) {
        out.type = type;
        learned_something = true;
    }
    // A value made of numbers that its reader typed hands that type on to the numbers it is made of.
    const ElemType settled = untyped != nullptr && type == ElemType::Unknown ? out.type : type;
    adapt_literal_operands(g, n, rules, settled, untyped, learned_something);
    propagate_recurrence(g, n, declared, learned_something);
    return true;
}

// Resolve every shape through the solver before the next sweep. A symbol pinned at the last node
// has to reach the values decided at the first one, and canonicalising an unpinned symbol to its
// class representative is what makes TRANSITIVE equality visible in a resolved shape: `a`, `b` and
// `c` all come back as one extent even though nothing ever related `a` and `c`.
constexpr void resolve_through_solver(Graph& g, const ExtentSolver& axes, bool& learned_something) {
    for (Value& v : g.values) {
        for (std::int32_t k = 0; k < v.shape.rank; ++k) {
            const Extent was = extent_at(v.shape, k);
            const Extent now = axes.resolve(was);
            if (!same_extent(was, now)) {
                set_extent(v.shape, k, now);
                learned_something = true;
            }
        }
    }
}

// Which axes are the same axis, before any node is looked at. Seeded with whatever the caller
// supplied for the symbols this graph declares.
constexpr bool seed_solver(ExtentSolver& axes, const Bindings& sizes, bool& asked_the_solver,
                           Diagnostic& why) {
    for (const SymbolBinding& b : sizes.at) {
        asked_the_solver = true;
        if (!axes.unify(Extent{Dim{b.symbol}}, literal_extent(b.size), OpName{"binding"}, why)) {
            return false;
        }
    }
    return true;
}

// Repeated forward sweeps, because a value defined by its own past is read before it is defined:
// the delay taps of a recurrence only learn their shape once the recurrence itself has one. A
// correct graph settles in a couple of sweeps; the bound on the loop is only there so a malformed
// graph stops rather than spins.
constexpr bool sweep_to_fixpoint(Graph& g, const Declared& declared, const bool* untyped, ExtentSolver& axes,
                                 bool& asked_the_solver, std::size_t sweep_limit, Diagnostic& why) {
    for (std::size_t sweep = 0; sweep < sweep_limit; ++sweep) {
        bool learned_something = false;
        for (const Node& n : g.nodes) {
            if (!infer_node(g, n, declared, untyped, axes, asked_the_solver, learned_something, why)) {
                return false;
            }
        }
        // Skipped when the graph never gave the solver anything to decide: then `resolve` is the
        // identity on every extent, and walking every axis of every value would learn nothing. A graph
        // whose extents are all literal, the common case, never asks.
        if (asked_the_solver) {
            resolve_through_solver(g, axes, learned_something);
        }
        if (!learned_something) {
            break;
        }
    }
    return true;
}

// What the sweep left untyped (B2, Decision 76), and only then, so a graph the sweep types stays as it was. A value
// made of numbers takes the type of the first reader that types it, as a literal does. Then, readers first, one of
// `TypeRule::FromOperandsExceptFirst` (`where`) still untyped takes its condition's, the operand the rule skips, and
// hands it on to the values made of numbers it reads, a `where` among them; one more sweep settles the rest.
constexpr bool type_the_numbers(Graph& g, const Declared& declared, ExtentSolver& axes, bool& asked_the_solver,
                                std::size_t sweep_limit, Diagnostic& why) {
    Untyped untyped{};
    if (!mark_untyped(g, untyped)) {
        return true;
    }
    if (!sweep_to_fixpoint(g, declared, untyped, axes, asked_the_solver, sweep_limit, why)) {
        return false;
    }
    bool learned_something = false;
    for (std::size_t i = g.nodes.size(); i-- > 0;) {
        const Node& n = g.nodes[i];
        const OpRules rules = rules_of(n.op);
        Value& out = g.values[static_cast<std::size_t>(n.output)];
        const bool skips_the_condition = rules.type == TypeRule::FromOperandsExceptFirst;
        if (skips_the_condition && untyped[static_cast<std::size_t>(n.output)] && out.type == ElemType::Unknown) {
            out.type = g.values[static_cast<std::size_t>(n.inputs[0])].type;
        }
        adapt_literal_operands(g, n, rules, out.type, untyped, learned_something);
    }
    return !learned_something || sweep_to_fixpoint(g, declared, untyped, axes, asked_the_solver, sweep_limit, why);
}

// The sweep by the old rules, then the numbers it left untyped.
constexpr bool settle(Graph& g, const Declared& declared, ExtentSolver& axes, bool& asked_the_solver, std::size_t sweep_limit,
                      Diagnostic& why) {
    return sweep_to_fixpoint(g, declared, nullptr, axes, asked_the_solver, sweep_limit, why) &&
           type_the_numbers(g, declared, axes, asked_the_solver, sweep_limit, why);
}

// Everything must have come out known. A value still unresolved here is a graph that says less than
// it needs to, and saying which value is the difference between a usable message and a puzzle. A
// `where` is named as the user wrote it: neither its values nor its condition had a type.
constexpr Diagnostic everything_is_known(const Graph& g) {
    for (const Node& n : g.nodes) {
        if (rules_of(n.op).type == TypeRule::FromOperandsExceptFirst &&
            g.values[static_cast<std::size_t>(n.output)].type == ElemType::Unknown) {
            MessageWriter m;
            m << std::string_view{"heinzel: "};
            write_op(m, n.op);
            m << std::string_view{": its element type could not be worked out; type one of its values"};
            return m.problem();
        }
    }
    for (std::size_t i = 0; i < g.values.size(); ++i) {
        if (g.values[i].type == ElemType::Unknown) {
            MessageWriter m;
            m << std::string_view{"the element type of value "} << static_cast<std::int32_t>(i)
              << std::string_view{" could not be worked out"};
            return m.problem();
        }
    }
    return Diagnostic{};
}

// May this graph's default precision seed a resolution? Not when it is no level at all, and not when
// it changed after an earlier analysis: a resolved level is written where a pin is read, so a second
// analysis sees every value pinned — which is what makes analysis idempotent, and also what would let
// a default that arrives late change nothing without saying so. It is refused instead.
constexpr bool default_precision_is_usable(const Graph& g, Diagnostic& why) {
    using numerics::Precision;
    if (g.default_precision == Precision::Inherit) {
        why = (MessageWriter{} << std::string_view{"a graph's default precision must be a level, not Inherit"}).problem();
        return false;
    }
    if (g.precision_resolved_under != Precision::Inherit && g.precision_resolved_under != g.default_precision) {
        why = (MessageWriter{} << std::string_view{"the default precision changed after the graph was analysed; set it before infer()"}).problem();
        return false;
    }
    return true;
}

// ---- numeric precision, resolved by data flow ----------------------------------------------------
// A value pinned by an explicit .precision() mark keeps it; an unpinned value (Inherit) takes the
// higher of the levels of the values its producing node consumes, seeded at the graph's default level
// (`Graph::default_precision`, Normal unless the graph says otherwise). The recurrence hand-off closes
// feedback cycles, as in propagate_recurrence above. The join is monotone on the finite lattice
// Fast < Normal < High, so a bounded sweep converges. The resolved level is written onto each
// `Value::precision`, which the planner, the lowering and the gradient rules read.
constexpr bool resolve_precision(Graph& g, Diagnostic& why) {
    using numerics::Precision;
    const std::size_t n = g.values.size();

    // Remember the pins before seeding overwrites them: pinned iff the original mark is not Inherit.
    numerics::Precision pin[kMaxValues]{};
    for (std::size_t i = 0; i < n; ++i) {
        pin[i] = g.values[i].precision;
    }
    auto pinned = [&](std::int32_t v) { return pin[static_cast<std::size_t>(v)] != Precision::Inherit; };

    if (!default_precision_is_usable(g, why)) {
        return false;
    }
    const Precision seed = g.default_precision;
    g.precision_resolved_under = seed;

    // Seed: a pin keeps its level; everything else starts at the graph's default.
    for (std::size_t i = 0; i < n; ++i) {
        g.values[i].precision = pinned(static_cast<std::int32_t>(i)) ? pin[i] : seed;
    }

    const std::size_t bound = n + 2;
    for (std::size_t sweep = 0; sweep < bound; ++sweep) {
        bool changed = false;
        for (const Node& nd : g.nodes) {
            const std::uint8_t arity = rules_of(nd.op).arity;
            Precision in = seed;
            for (std::uint8_t k = 0; k < arity; ++k) {
                const std::int32_t v = nd.inputs[k];
                if (v != kNoValue) {
                    in = numerics::join(in, g.values[static_cast<std::size_t>(v)].precision);
                }
            }
            const std::int32_t out = nd.output;
            if (out != kNoValue && !pinned(out) && g.values[static_cast<std::size_t>(out)].precision != in) {
                g.values[static_cast<std::size_t>(out)].precision = in;
                changed = true;
            }
            if (nd.attr.kind == AttrKind::Recurrence && nd.attr.recurrence.defines != kNoValue) {
                const std::int32_t var = nd.attr.recurrence.defines;
                if (!pinned(var)) {
                    const Precision next = g.values[static_cast<std::size_t>(out)].precision;
                    if (g.values[static_cast<std::size_t>(var)].precision != next) {
                        g.values[static_cast<std::size_t>(var)].precision = next;
                        changed = true;
                    }
                }
            }
        }
        if (!changed) {
            return true;
        }
    }
    fail("heinzel::infer: numeric precision did not reach a fixpoint within the bound");
}

// An operation that rounds as the machine rounds (`NumericContract::MachineRounded`) takes a freedom only a precision
// that grants reordering gives: at `High` its bits would follow the build's instruction set. The rewrite layer emits
// one only where granted; a graph that builds one by hand elsewhere is refused.
constexpr Diagnostic machine_rounding_is_granted(const Graph& g) {
    for (const Node& nd : g.nodes) {
        if (rules_of(nd.op).numeric == NumericContract::MachineRounded &&
            !numerics::grants(g.values[static_cast<std::size_t>(nd.output)].precision).reorder) {
            return refusal(nd.op, " rounds as the machine rounds, which only a precision that grants reordering allows");
        }
    }
    return {};
}

} // namespace detail

// ---- the pass ---------------------------------------------------------------------------------------
//
// In order: refuse a graph the frontend found over a capacity, in its words (`Graph::over`); seed the solver from
// the bindings, give a recurrence no signal drives the graph's one length, sweep shapes and element types to a
// fixpoint, type the numbers left untyped (and, where a variable that took the length is
// still untyped, make it f32 and do both again), hold the declared variables to their declarations, check which values
// have time, resolve precision (and that a rounding the machine chooses is granted), check what each operation
// requires of its attribute and its operands, and check that everything came out known.
constexpr Diagnostic infer(Graph& g, const Bindings& sizes = Bindings{}) {
    if (g.over != Capacity::None) {
        return (MessageWriter{} << words_of(g.over)).problem();
    }
    const std::size_t sweep_limit = g.values.size() + 2;
    ExtentSolver axes{};
    Diagnostic why{};

    // Whether this graph ever gave the solver anything to decide. A graph whose extents are all
    // literal never does.
    bool asked_the_solver = false;

    if (!detail::seed_solver(axes, sizes, asked_the_solver, why)) {
        return why;
    }
    detail::Declared declared = detail::declared_variables(g);
    if (const Diagnostic taken = detail::undriven_take_the_length(g, declared); !taken.ok) {
        return taken;
    }
    if (!detail::settle(g, declared, axes, asked_the_solver, sweep_limit, why) ||
        (detail::undriven_default_to_f32(g, declared) && !detail::settle(g, declared, axes, asked_the_solver, sweep_limit, why))) {
        return why;
    }
    if (const Diagnostic held = detail::declared_variables_hold(g, declared); !held.ok) {
        return held;
    }
    // Which values have time, and what that forbids, before any other check words a refusal (time_checks.hpp).
    if (const Diagnostic time = check_time(g, declared); !time.ok) {
        return time;
    }

    // Independent of shapes and element types, so it runs after they settle.
    if (!detail::resolve_precision(g, why)) {
        return why;
    }

    if (const Diagnostic granted = detail::machine_rounding_is_granted(g); !granted.ok) {
        return granted;
    }
    const Diagnostic attributes = detail::check_attributes(g);
    if (!attributes.ok) {
        return attributes;
    }
    return detail::everything_is_known(g);
}

} // namespace heinzel
