#pragma once

// Building a graph: the leaves, the operations over them, a value defined in terms of its own past,
// and finishing. They build into the arena of `arena.hpp`. No leaf takes a graph argument; what depends
// on where a graph is written is said where it applies: a subgraph's names where it is applied
// (`subgraph.hpp`), the sample rate and the default precision on the finished graph.

#include <cstdint>
#include <numbers>
#include <string_view>
#include <utility>
#include <vector>

#include "heinzel/frontend/arena.hpp"
#include "heinzel/frontend/dynamic_extent.hpp"
#include "heinzel/frontend/ops.hpp"
#include "heinzel/frontend/reading_order.hpp"
#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"

namespace heinzel {

// ---- leaves: no graph argument anywhere -------------------------------------------------------------

namespace detail {

// A name as a graph records it, refused when it would not fit. What a name is made of is the caller's
// business: the declarations pass the user's name, `subgraph` its piece's.
constexpr Name name_of(std::string_view name) {
    if (name.size() + 1 > kMaxNameLength) {
        fail("heinzel: a name is longer than kMaxNameLength");
    }
    Name out{};
    for (std::size_t i = 0; i < name.size(); ++i) {
        out.data_[i] = name[i];
    }
    return out;
}

// An output's name, by whichever path it enters a graph (`Output`, `finish`, `gradients`): one starting with `#` names
// an output a debug build adds, to check a promise (Plan/26-value-ranges/04).
constexpr Name output_name(std::string_view name) {
    if (name.starts_with('#')) {
        fail("heinzel: an output's name starting with # is kept for the checks of promises");
    }
    return name_of(name);
}

// A declared input, by its finished name.
// The optional `declared` is a runtime-resolved dimension this input's shape carries — the `DimDecl`
// whose symbol was placed on the shape's innermost axis. It rides to the graph so that `prepare()` can
// check the bound size against it. Left at its default (`kNoDimension`), nothing is declared.
constexpr Expr input(std::string_view name, Shape shape, ElemType type = ElemType::F32, DimDecl declared = {}) {
    BuildNode n{};
    n.category = ValueCategory::Input;
    n.shape = shape;
    n.type = type;
    n.name = name_of(name);
    n.declared = declared;
    return one_node(n);
}

} // namespace detail

constexpr Expr constant(double value) {
    BuildNode n{};
    n.category = ValueCategory::Const;
    n.constant = value;
    // Deliberately left Unknown: a literal adapts to what it is used with rather than forcing a type
    // on it. Resolving that is the analysis pass's job, and it is the reason `0.5 * w` does not make
    // an expression double precision.
    return detail::one_node(n);
}

// ---- composing --------------------------------------------------------------------------------------

// The rule table already knows how many operands each operation takes, so asking it here turns
// "built a node with the wrong number of operands" into a message at the line that did it, instead
// of a puzzle for whichever pass reads the node first.
constexpr void expect_arity(OpCode code, std::uint8_t given) {
    if (rules_of(code).arity != given) {
        fail("heinzel: this operation was given the wrong number of operands");
    }
}

constexpr Expr apply(OpCode code, Expr a) {
    expect_arity(code, 1);
    a.catch_up();
    BuildNode n{};
    n.op = code;
    n.inputs[0] = a.position();
    a.arena()->nodes.push_back(n);
    return Expr{a.arena(), static_cast<std::int32_t>(a.arena()->nodes.size() - 1)};
}

constexpr Expr apply(OpCode code, Expr a, Expr b) {
    expect_arity(code, 2);
    Arena* arena = bring_together(a, b);
    BuildNode n{};
    n.op = code;
    n.inputs[0] = a.position();
    n.inputs[1] = b.position();
    arena->nodes.push_back(n);
    return Expr{arena, static_cast<std::int32_t>(arena->nodes.size() - 1)};
}

constexpr Expr apply(OpCode code, Expr a, Expr b, Expr c) {
    expect_arity(code, 3);
    Arena* arena = bring_together(a, b);
    Expr first{arena, a.position()};
    bring_together(first, c);
    // Both earlier handles may have moved when `c` joined, so ask them again.
    a.catch_up();
    b.catch_up();
    c.catch_up();
    BuildNode n{};
    n.op = code;
    n.inputs[0] = a.position();
    n.inputs[1] = b.position();
    n.inputs[2] = c.position();
    Arena* live = a.arena();
    live->nodes.push_back(n);
    return Expr{live, static_cast<std::int32_t>(live->nodes.size() - 1)};
}

// Ordinary arithmetic mentions no context, which is the point: `a * b + c` is the same expression
// wherever it is written. Only the things that genuinely depend on where they were written ask.
constexpr Expr operator+(Expr a, Expr b) { return apply(op::Add, a, b); }
constexpr Expr operator-(Expr a, Expr b) { return apply(op::Sub, a, b); }
constexpr Expr operator*(Expr a, Expr b) { return apply(op::Mul, a, b); }
constexpr Expr operator/(Expr a, Expr b) { return apply(op::Div, a, b); }
constexpr Expr operator-(Expr a) { return apply(op::Neg, a); }

// The transcendentals and roundings spelled as free functions, found by ADL on `Expr`. Each names an
// operation that already exists and adds none; a helper such as `log10` or `cospi` (below) is an
// ordinary `constexpr` function over them, so the vocabulary grows by composition, not registration. A
// spelling is added when an example needs it, never in anticipation. The core a result runs on is chosen
// from the value's resolved precision, not from any per-call run-time tier. `tan`, `exp2` and `pow` have no
// float core yet: a float runs their double core one number at a time, slower than the others.
constexpr Expr log(Expr a) { return apply(op::Log, a); }
constexpr Expr tanh(Expr a) { return apply(op::Tanh, a); }
constexpr Expr sqrt(Expr a) { return apply(op::Sqrt, a); }
constexpr Expr exp(Expr a) { return apply(op::Exp, a); }
constexpr Expr exp2(Expr a) { return apply(op::Exp2, a); }
constexpr Expr sin(Expr a) { return apply(op::Sin, a); }
constexpr Expr cos(Expr a) { return apply(op::Cos, a); }
constexpr Expr tan(Expr a) { return apply(op::Tan, a); }

// x to the y, for a base the proof of ranges keeps at or above 0, and above 0 where y may be negative: a negative
// base has an answer only at a whole exponent, so a base it cannot keep there is refused ("heinzel: pow may see a
// negative base: the base is ..."). Square a signed value as `x * x`.
constexpr Expr pow(Expr x, Expr y) { return apply(op::Pow, x, y); }
constexpr Expr pow(Expr x, double y) {
    Expr exponent = constant(y);
    return pow(x, exponent);
}
constexpr Expr pow(double x, Expr y) {
    Expr base = constant(x);
    return pow(base, y);
}

// sin(pi*x), the half-turns form: an oscillator renders its waveform as `sinpi(2*phase)` with the phase
// in cycles, because the argument reduction is exact for the interesting phases.
constexpr Expr sinpi(Expr a) { return apply(op::SinPi, a); }

// Round toward negative infinity: exact, so it carries no accuracy tier.
constexpr Expr floor(Expr a) { return apply(op::Floor, a); }

namespace detail {

// A composition's result, marked with the range it always has: a fact, which the proof of ranges trusts as it does a
// port's range, and no promise, so a debug build checks nothing (Plan/26-value-ranges). The proof takes each operand's
// range alone, so `x - floor(x)` alone would be any number; and a variable's past is read by its definition's fact,
// not by the rule that made it, so `abs`, whose rule gives it, states it too. A table read skips its wrap on a fact too
// (`ranges/in_every_rounding.hpp`), so a fact must hold of the numbers as computed, in every rounding at every
// precision, as `abs`'s and `frac`'s do, not only of the real numbers.
constexpr Expr with_fact(Expr e, Interval always) {
    e.catch_up();
    BuildNode& node = e.arena()->nodes[static_cast<std::size_t>(e.position())];
    node.range = Range{{always}, 1};
    node.ranged = true;
    return e;
}

} // namespace detail

// The fractional part `x - floor(x)`, in [0, 1]: a composed helper rather than an operation. The subtraction is exact
// whenever `floor(x)` and `x` share an exponent, which is the point of keeping a phase accumulator in [0, 1). Its fact
// is closed: for a tiny negative x, x - floor(x) = x + 1 rounds to 1, in double and in float.
constexpr Expr frac(Expr a) {
    return detail::with_fact(a - floor(a), Interval{.lo = 0, .hi = 1, .lo_open = false, .hi_open = false});
}

// One over the square root (`x * rsqrt(mean(x*x) + eps)` normalizes), computed as `1 / sqrt(x)`
// (`runtime/root_cores.hpp`).
constexpr Expr rsqrt(Expr a) { return apply(op::Rsqrt, a); }

// The elementwise choose, compare and clamp spellings. Each names an exact operation that already
// exists (`op::Max`, `op::Min`, `op::Lt`, `op::Le`, `op::Select`); none adds an operation or a rounding.
constexpr Expr max(Expr a, Expr b) { return apply(op::Max, a, b); }
constexpr Expr min(Expr a, Expr b) { return apply(op::Min, a, b); }
constexpr Expr max(Expr a, double v) { return max(a, constant(v)); }
constexpr Expr max(double v, Expr a) { return max(constant(v), a); }
constexpr Expr min(Expr a, double v) { return min(a, constant(v)); }
constexpr Expr min(double v, Expr a) { return min(constant(v), a); }

// |x|, the sign bit cleared: abs(-0) is +0. At least 0, a fact for a variable defined by it (`with_fact`).
constexpr Expr abs(Expr a) { return detail::with_fact(apply(op::Abs, a), Interval{.lo = 0, .lo_open = false}); }

// clamp(x, lo, hi) = min(max(x, lo), hi): a composed helper with no rounding of its own. Two numbers the wrong way
// round are refused; a bound that is an expression is not known while the graph is built, and lo above hi answers hi.
constexpr Expr clamp(Expr x, Expr lo, Expr hi) { return min(max(x, lo), hi); }
constexpr Expr clamp(Expr x, Expr lo, double hi) { return min(max(x, lo), hi); }
constexpr Expr clamp(Expr x, double lo, Expr hi) { return min(max(x, lo), hi); }
constexpr Expr clamp(Expr x, double lo, double hi) {
    if (lo > hi) {
        fail("heinzel: clamp(x, lo, hi) has lo above hi");
    }
    return min(max(x, lo), hi);
}

// A comparison is a mask: `op::Lt(p, q)` is `p < q ? 1 : 0` and `op::Le(p, q)` is `p <= q ? 1 : 0`, so `>` and `>=`
// read their operands in the other order. A NaN on either side gives 0, as in C++. A mask is 1.0 or 0.0, exactly
// representable, so nothing here rounds. `==` and `!=` are not spelled: on floats they invite mistakes, and a typo
// `y == x` would build a mask nothing reads.
constexpr Expr operator<(Expr a, Expr b) { return apply(op::Lt, a, b); }
constexpr Expr operator>(Expr a, Expr b) { return apply(op::Lt, b, a); }
constexpr Expr operator<=(Expr a, Expr b) { return apply(op::Le, a, b); }
constexpr Expr operator>=(Expr a, Expr b) { return apply(op::Le, b, a); }
constexpr Expr operator<(Expr a, double v) { return a < constant(v); }
constexpr Expr operator<(double v, Expr a) { return constant(v) < a; }
constexpr Expr operator>(Expr a, double v) { return a > constant(v); }
constexpr Expr operator>(double v, Expr a) { return constant(v) > a; }
constexpr Expr operator<=(Expr a, double v) { return a <= constant(v); }
constexpr Expr operator<=(double v, Expr a) { return constant(v) <= a; }
constexpr Expr operator>=(Expr a, double v) { return a >= constant(v); }
constexpr Expr operator>=(double v, Expr a) { return constant(v) >= a; }

// `where(cond, t, f)` is `cond != 0 ? t : f` — `op::Select` reads its condition first, then the
// taken and the not-taken value (runtime/compute.hpp). It is a true blend: both branches are values,
// neither is control flow, which is what lets it fuse into the elementwise nest. Two numbers take the
// element type of what reads the `where`, or else its condition's (Decision 76).
constexpr Expr where(Expr cond, Expr t, Expr f) { return apply(op::Select, cond, t, f); }
constexpr Expr where(Expr cond, Expr t, double f) { return where(cond, t, constant(f)); }
constexpr Expr where(Expr cond, double t, Expr f) { return where(cond, constant(t), f); }
constexpr Expr where(Expr cond, double t, double f) {
    Expr taken = constant(t); // named, so the two numbers are made in the order written
    Expr not_taken = constant(f);
    return where(cond, taken, not_taken);
}

constexpr Expr operator+(Expr a, double v) { return a + constant(v); }
constexpr Expr operator+(double v, Expr a) { return constant(v) + a; }
constexpr Expr operator-(Expr a, double v) { return a - constant(v); }
constexpr Expr operator-(double v, Expr a) { return constant(v) - a; }
constexpr Expr operator*(Expr a, double v) { return a * constant(v); }
constexpr Expr operator*(double v, Expr a) { return constant(v) * a; }
constexpr Expr operator/(Expr a, double v) { return a / constant(v); }
constexpr Expr operator/(double v, Expr a) { return constant(v) / a; }

// The base-ten logarithm, composed: log(x) * log10(e). `std::numbers::log10e` is 1/ln 10 correctly rounded
// (`1.0 / std::numbers::ln10` is one ulp low). Measured over every positive float: at most 2.03 ulp at Normal and 1.37
// at High. No composed form reaches 1 at High: the log rounded to f32 first costs up to 0.87 ulp of the result, the
// f32 constant 0.34, the product 0.5. Its domain is log's, so an operand the proof of ranges cannot keep above 0 is
// refused in log's words ("heinzel: log may see a number <= 0: ..."). A mark on its result pins the product only, as
// precision flows forward: `log10(x).precision(High)` takes the log at the graph's level; a High log10 is marked on
// its operand where that is made, or finished `finish(High, ...)`.
constexpr Expr log10(Expr a) { return log(a) * std::numbers::log10e; }

// cos(pi x), the half-turns form: sinpi(1/2 - r) with r = |x| mod 2. cos is even, so the reduction takes |x|: for
// a >= 0, a - 2 floor(a/2) is exact at every float (a itself below 2, Sterbenz's lemma from 2 up), where x + 2k rounds
// for a negative x (reducing x answered -0 at -0.49999997, whose cosine is 9.4e-8). 1/2 - r is exact for r >= 1/4
// and rounds only below, where the answer is above 0.7. Exact at the integers and the halves, where r = 3/2 answers
// -0 (equal to 0; IEEE's cosPi gives +0). Measured over every float: at most 1.70 ulp at Normal and 1.06 at High.
constexpr Expr cospi(Expr x) {
    Expr a = abs(x);
    Expr r = a - 2.0 * floor(a * 0.5);
    return sinpi(0.5 - r);
}

// A table's N cells (an `Input` with a shape, a `constant(…)`) as one cycle of a wave, read at `phase` in turns: 0 is
// the first cell and so is every whole turn, a negative phase too; between two cells the two are blended linearly, the
// last with the first. A NaN or an infinite phase reads a NaN.
constexpr Expr wavetable(Expr table, Expr phase) {
    const std::int32_t cells = detail::cells_of(table);
    return detail::table_read(phase, table, cells);
}

// A table's N cells as a function at N evenly spaced points from lo to hi, both ends included, read at x: between two
// points the two cells blended linearly, at and below lo the first, at and above hi the last, a NaN at a NaN. A
// wavetable read whose phase is clamped below 1 by a cell, so that the last cell never blends into the first; the
// bounds go first, as `max` and `min` answer their second operand when either is a NaN, so a NaN x stays one. At least
// two cells, lo below hi, both finite (`detail::lookup_scale`).
constexpr Expr lookup(Expr table, Expr x, double lo, double hi) {
    const std::int32_t cells = detail::cells_of(table);
    const double last = detail::last_cell(cells);
    const double scale = detail::lookup_scale(cells, lo, hi);
    Expr along = (x - lo) * scale;
    Expr phase = min(last, max(0.0, along));
    return wavetable(table, phase);
}

namespace detail {

// Reading a value some number of samples ago: `x[n - k]`.
constexpr Expr delayed(Expr a, std::int32_t samples) {
    a.catch_up();
    BuildNode n{};
    n.op = op::FixedDelay;
    n.inputs[0] = a.position();
    n.attr.kind = AttrKind::Delay;
    n.attr.delay.samples = samples;
    a.arena()->nodes.push_back(n);
    return Expr{a.arena(), static_cast<std::int32_t>(a.arena()->nodes.size() - 1)};
}

struct variable_access;

} // namespace detail

constexpr Expr Expr::operator[](Past p) const { return detail::delayed(*this, p.samples); }
constexpr Expr Expr::operator[](const detail::VaryingPast& p) const { return detail::delay_line(*this, p.samples, p.most); }

// ---- a value defined in terms of its own past ---------------------------------------------------------
//
// This is the case the whole forwarding design exists for. A filter says `w[n] = x[n] - a1 * w[n - 1]`:
// the past of `w` is read before `w` has been defined. So `w` starts as a one-node graph of its own,
// `w[n - 1]` taps it, and the definition arrives later — by which time arbitrary merges may have moved
// that node somewhere else entirely. It still works, because the handle forwards rather than dangling.
//
// A variable is defined once, so it cannot be copied: two copies would share one node and could
// each define it. It takes `Expr`'s tensor members, `v.sum(0)`, read as `Expr(v)` (`ops.hpp`).
struct Signal; // declarations.hpp
class Output;   // declarations.hpp

class Variable : public forwarding::members<Variable> {
public:
    constexpr Variable() {
        BuildNode n{};
        // A value, not an operation: nothing produces it, and the recurrence that defines it will
        // name it. Giving it a category of its own is what stops it being mistaken for a node whose
        // operands went missing.
        n.category = ValueCategory::Variable;
        node_ = detail::one_node(n);
    }
    // A declared length (`Variable phase{Signal{N}}`): the shape and type its definition must come out at, for a
    // recurrence that no signal drives. Defined in `declarations.hpp`, beside `Signal`.
    constexpr explicit Variable(Signal declared, ElemType type = ElemType::F32);
    Variable(const Variable&) = delete;
    Variable& operator=(const Variable&) = delete;

    // `v[n] = …` defines it; read, `v[n]` is its value now, as is `v` itself. `v[n] = w[n]` needs its
    // own assignment: the implicit one would copy the proxy and define nothing.
    struct Now {
        Variable* v;
        constexpr void operator=(Expr definition) const { v->is(definition); }
        constexpr void operator=(const Now& other) const { v->is(Expr(other)); }
        constexpr operator Expr() const { return v->value(); }
        // A recurrence's promise goes on its definition, where the proof reads it (Plan/26-value-ranges/02).
        template <class... R>
        constexpr Expr promise(R...) const {
            static_assert(detail::kNeverCompiles<R...>, "heinzel: promise its definition: y[n] = (...).promise(...)");
            return v->value();
        }
    };
    constexpr Now operator[](Index) { return {this}; }
    constexpr Expr operator[](Index) const { return value(); }
    constexpr Expr operator[](Past p) const { return previous(p.samples); }
    constexpr Expr operator[](const detail::VaryingPast& p) const { return Expr(node_)[p]; }
    constexpr operator Expr() const { return value(); }

private:
    friend class Output;
    friend struct detail::variable_access;

    // The value this had `samples` ago.
    constexpr Expr previous(std::int32_t samples) const {
        Expr self = node_;
        return detail::delayed(self, samples);
    }

    // Say what this value is. Everything the definition refers to is brought into one arena first,
    // so the recorded link is a position that is valid there.
    constexpr void is(Expr definition) {
        if (defined_.arena() != nullptr) {
            fail("heinzel::Variable: defined twice");
        }
        Expr self = node_;
        Arena* arena = bring_together(self, definition);
        BuildNode n{};
        n.op = op::Recurrence;
        n.inputs[0] = definition.position();
        n.attr.kind = AttrKind::Recurrence;
        n.attr.recurrence.defines = self.position();
        arena->nodes.push_back(n);
        defined_ = Expr{arena, static_cast<std::int32_t>(arena->nodes.size() - 1)};
    }

    // The value itself, usable in further expressions.
    constexpr Expr value() const { return defined_.arena() != nullptr ? defined_ : node_; }

    Expr node_{};
    Expr defined_{};
};

namespace detail {

// The steps `v[n - k]`, `v[n] = e` and `v` are spelled with, for a test that builds a graph one step at a time.
struct variable_access {
    static constexpr Expr previous(const Variable& v, std::int32_t samples) { return v.previous(samples); }
    static constexpr void is(Variable& v, Expr definition) { v.is(definition); }
    static constexpr Expr value(const Variable& v) { return v.value(); }
};

} // namespace detail

// ---- finishing --------------------------------------------------------------------------------------

namespace detail {

// The first capacity a graph emitted from `arena` would pass, or `None`: a value for each build node, a node for each
// operation, its constants' numbers, an input, a range stated. Counted before anything is emitted, so that a graph
// function over a capacity still returns, its graph carrying which (`Graph::over`).
constexpr Capacity capacity_passed(const Arena* arena) {
    std::size_t nodes = 0;
    std::size_t inputs = 0;
    std::size_t constants = 0;
    std::size_t ranges = 0;
    for (const BuildNode& b : arena->nodes) {
        const bool value_only = b.category == ValueCategory::Input || b.category == ValueCategory::Const || b.category == ValueCategory::Variable;
        nodes += value_only ? 0 : 1;
        inputs += b.category == ValueCategory::Input ? 1 : 0;
        constants += !value_only && b.attr.kind == AttrKind::Constant ? b.numbers.size() : 0;
        ranges += (b.ranged ? 1 : 0) + (b.promised ? 1 : 0);
    }
    if (arena->nodes.size() > kMaxValues) {
        return Capacity::Values;
    }
    if (nodes > kMaxNodes) {
        return Capacity::Nodes;
    }
    if (inputs > kMaxIO) {
        return Capacity::Inputs;
    }
    if (constants > kMaxConstants) {
        return Capacity::Constants;
    }
    return ranges > kMaxRanges ? Capacity::Ranges : Capacity::None;
}

} // namespace detail

// Copy an arena's build nodes into a graph, in emission order, registering the inputs and the ranges as it goes, then
// in reading order where a variable's present was read before its definition (`reading_order.hpp`); or, over a
// capacity, only which.
// Every `finish` shares it and differs only in which values it then marks as outputs. It leaves every
// produced value `Internal` (or Input/Const/Variable as built); naming an output is the caller's separate
// step, because a value may be read by later nodes AND named as an output.
constexpr void emit_arena_nodes(Graph& g, const Arena* arena) {
    g.over = detail::capacity_passed(arena);
    if (g.over != Capacity::None) {
        return;
    }
    for (const BuildNode& b : arena->nodes) {
        Value v{};
        v.type = b.type;
        v.shape = b.shape;
        v.category = b.category;
        v.constant = b.constant;
        v.precision = b.precision; // the .precision() mark rides the BuildNode to the Value
        v.timeless = b.timeless;   // on any node, not only an input: `iota({…})` is an op
        const std::int32_t id = push_value(g, v);
        detail::record_dim(g, b.declared); // a runtime-resolved dimension this node's shape carried, if any
        if (b.ranged) {
            push_range(g, RangeMark{id, b.range, false});
        }
        if (b.promised) {
            push_range(g, RangeMark{id, b.promise, true});
        }

        if (b.category == ValueCategory::Input) {
            g.inputs.push_back(NamedValue{b.name, id});
            continue; // an input is a value, not an operation
        }
        if (b.category == ValueCategory::Const || b.category == ValueCategory::Variable) {
            continue; // so is a constant, and so is a value a recurrence will define
        }

        Node n{};
        n.op = b.op;
        for (std::size_t k = 0; k < kMaxNodeInputs; ++k) {
            n.inputs[k] = b.inputs[k];
        }
        n.output = id;
        n.attr = b.attr;
        if (b.attr.kind == AttrKind::Constant) { // (a node built by hand may hold any count: each holds its own)
            if (static_cast<std::size_t>(numbers_per_sample(b.attr.generator.shape)) != b.numbers.size()) {
                fail("heinzel: a constant's numbers do not fill its shape");
            }
            n.attr.generator.first = static_cast<std::int32_t>(g.constants.size());
            for (const double number : b.numbers) {
                push_constant(g, number);
            }
        }
        push_node(g, n);
    }
    detail::in_reading_order(g);
}

namespace detail {

// The arena already is the flat list of nodes, so finishing is a copy in emission order rather than a
// walk of a structure; nothing in finishing chooses the order, but for a variable's present read before its
// definition, which `reading_order.hpp` moves after it.
//
// That order is the order the parts of the expression were EVALUATED in, which is weaker than the order
// they were written in: C++ leaves the evaluation order of `operator+`'s operands unspecified. gcc 15.2
// builds `x + c1*y[n - 1] + c2*y[n - 2]` in one node order at compile time and a permutation of it at run
// time (clang chooses consistently). The graphs are the same graph renumbered, so the numbers do not
// change (zero differing samples out of sixteen), but a graph used as a template argument could mangle
// differently in two translation units. Naming each subexpression as its own variable fixes the order.
// See `docs/decisions.md`, Decision 30.
//
// The declarations finish through `emit_arena_nodes` themselves; this one-output finish of an expression
// stays as the oracle `declarations_test` and `index_notation_test` compare them with.
constexpr Graph finish(std::string_view name, Expr root) {
    root.catch_up();
    Graph g{};
    emit_arena_nodes(g, root.arena());
    if (g.over != Capacity::None) {
        return g;
    }

    Value& out = g.values[static_cast<std::size_t>(root.position())];
    if (out.category != ValueCategory::Internal) {
        // Naming an input, a literal or a variable directly as the output would leave one value in
        // two roles at once. The declarations' `finish` inserts the copy for an input or a variable
        // (`declarations.hpp`); this one refuses.
        fail("heinzel::finish: the output must be a computed value, not an input, literal or variable");
    }
    g.outputs.push_back(NamedValue{output_name(name), root.position()});
    return g;
}

// An index ramp over `shape`'s element axes, the same at every sample: the `_iota` node, marked `timeless` for a
// tensor and carrying a signal's `Length` (`declarations.hpp`'s `iota`).
constexpr Expr iota_node(Shape shape, ElemType type, bool timeless, DimDecl declared) {
    BuildNode n{};
    n.op = op::Iota;
    n.attr.kind = AttrKind::Iota;
    n.attr.generator.shape = shape;
    n.attr.generator.type = type;
    n.shape = shape; // known while building, as an input's is
    n.timeless = timeless;
    n.declared = declared;
    return one_node(n);
}

// A constant tensor over `shape`, a recorded tensor's: the `_const` node, its numbers rounded to `type` here, so the
// pool holds exactly what every runner reads (`declarations.hpp`'s `constant`).
constexpr Expr constant_node(std::vector<double> numbers, Shape shape, ElemType type) {
    if (type != ElemType::F32 && type != ElemType::F64) {
        fail("heinzel: a constant is f32 or f64");
    }
    if (static_cast<std::size_t>(numbers_per_sample(shape)) != numbers.size()) {
        fail("heinzel: a constant's numbers do not fill its shape");
    }
    for (double& number : numbers) {
        number = type == ElemType::F32 ? static_cast<double>(static_cast<float>(number)) : number;
    }
    BuildNode n{};
    n.op = op::Constant;
    n.attr.kind = AttrKind::Constant;
    n.attr.generator.shape = shape;
    n.attr.generator.type = type;
    n.shape = shape; // known while building, as an input's is
    n.timeless = true;
    n.numbers = std::move(numbers);
    return one_node(n);
}

// The identity, as an operation: what an output that is an input or a variable is (`declarations.hpp`'s
// `finish`). `op::Realize` is a materialisation barrier no pass removes, and it gives the output a
// value of its own.
constexpr Expr identity(Expr e) { return apply(op::Realize, e); }

} // namespace detail

} // namespace heinzel

// The declarations (`Input`, `Output`, durations, …) build on everything above, so one include gives
// the whole frontend.
#include "heinzel/frontend/declarations.hpp"
