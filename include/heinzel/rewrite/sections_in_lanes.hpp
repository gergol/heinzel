#pragma once

// Sections in lanes (`docs/sections-in-lanes.md`; P5's design, Plan/23-audio-scheduler/05): a cascade of like
// sections, each a feedback cycle reading the one before at this sample, waits every sample on each section in turn.
// Laid along a section axis and linked (`u = _chain(x, V)`: element 0 of `u` is the cascade's input `x`, element j
// section j - 1's value, both at this sample), the k sections are one cycle over [k, time], which a stage can run
// skewed in lanes, section j a sample behind section j - 1. The merge changes no number: each section
// computes what it computed, in its own order. It is taken where the cost model prices it below today's cascade by more
// than it resolves (the price P): on zen2 two RBJ sections run 1.3-1.5x faster as a cascade, four 1.1-1.2x faster in
// lanes, eight 2-3x (the lanes' probe). `rewritten` runs it after the parallel form.
//
// Eligibility: at a precision that grants reordering, on a machine with lanes, a maximal run of k >= 2 like feedback
// cycles, each reading the one before at the present sample, reading from outside only timeless values, the first
// reading one outside value of one voice, read outside only at its end, in no larger cycle, its past all held in
// registers (each lag one of a run 1, 2, … of its value's lags, a lag of a lag only of the cycle's own state), and
// within `fits_a_block(k, the machine's double lanes)`, becomes one cycle over a section axis, linked
// (`like_sections.hpp`).
//
// What the linked cycle reads from outside: one value every section reads, or one number every section's
// constant holds, stays as it is; the sections' constants become one [k, 1] constant tensor, rounded to their type as
// every constant tensor is (a scalar constant the walk reads as the double it holds, the program as its type's: a
// difference the merge's test names); anything else is gathered by a chain of `where` over one-hot [k, 1] constants,
// the masks shared. Names no operation: it reads the feedback facts, the rule table and view kinds.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/loops/graph/cycle_chains.hpp"
#include "heinzel/loops/graph/staging.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/rewrite/double_state.hpp"
#include "heinzel/rewrite/feedback_sums.hpp"
#include "heinzel/rewrite/like_sections.hpp"
#include "heinzel/rewrite/priced.hpp"
#include "heinzel/schedule/machine.hpp"
#include "heinzel/sema/analyze.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel::rewrite {
namespace detail {

// The row of the rule table `holds` picks: the table names the operation, the rule does not.
template <class Holds>
constexpr OpCode row_where(Holds holds) {
    for (std::size_t i = 0; i < kOpCount; ++i) {
        if (holds(kOpTable[i].rules)) {
            return OpCode{static_cast<std::uint8_t>(i)};
        }
    }
    fail("heinzel::rewrite: the rule table has no row for a node sections in lanes emit");
    return OpCode{};
}
constexpr OpCode link_row() {
    return detail::row_where([](const OpRules& r) { return r.time == TimeRule::ReadsTheElementBefore; });
}
constexpr OpCode choice_row() {
    return detail::row_where([](const OpRules& r) { return r.type == TypeRule::FromOperandsExceptFirst; });
}
constexpr OpCode pool_row() {
    return detail::row_where([](const OpRules& r) { return r.attr == AttrKind::Constant; });
}
constexpr OpCode recurrence_row() {
    return detail::row_where([](const OpRules& r) { return r.attr == AttrKind::Recurrence; });
}
// A view of one operand. No column tells those rows apart, and every pass reads a view's kind, not its row (the
// frontend's squeeze is the reshape's row with a squeeze's kind): the slice and the reshape both take the first.
constexpr OpCode view_row() {
    return detail::row_where([](const OpRules& r) { return r.attr == AttrKind::View && r.arity == 1; });
}

// The graph being rewritten: `g` with its pool grown, its values and those made, the nodes each run puts after its
// last node, which of `g`'s nodes go, what reads a run's result reads instead (per value as read), and the values whose
// range marks go: a section's, since one mark would then name every section (the proof runs before the rewrites).
struct Linking {
    Graph h;
    std::vector<Value> values;
    std::vector<std::vector<Node>> after;
    std::vector<char> gone;
    std::vector<std::int32_t> renamed;
    std::vector<char> unmarked;
};

// What a reader of value `v` reads now: a run's result, sliced out; anything else itself.
constexpr std::int32_t instead(const Linking& l, const Reading& r, std::int32_t v) {
    const std::int32_t to = l.renamed[static_cast<std::size_t>(as_read(r, v))];
    return to == kNoValue ? v : to;
}

// Node `n`, put into `block`, and its result `made`: the value's number.
constexpr std::int32_t emitted(Linking& l, std::vector<Node>& block, Node n, const Value& made) {
    l.values.push_back(made);
    n.output = static_cast<std::int32_t>(l.values.size() - 1);
    block.push_back(n);
    return n.output;
}

// A constant tensor of one number a section, [k, 1], `numbers` rounded to `type` as the pool holds every constant's.
constexpr std::int32_t pooled(Linking& l, std::vector<Node>& block, const std::vector<double>& numbers, ElemType type,
                              numerics::Precision p) {
    const Shape column{literal_extent(static_cast<std::int32_t>(numbers.size())), literal_extent(1)};
    Node n{.op = detail::pool_row()};
    n.attr.kind = AttrKind::Constant;
    n.attr.generator = GeneratorAttr{column, type, static_cast<std::int32_t>(l.h.constants.size())};
    for (const double number : numbers) {
        push_constant(l.h, type == ElemType::F32 ? static_cast<double>(static_cast<float>(number)) : number);
    }
    return detail::emitted(l, block, n, Value{type, column, ValueCategory::Internal, 0, p, true});
}

// What the linked cycle reads where section 0's node `o.at` reads from outside through operand `o.operand`, made into
// `block`: the one value all sections read; their constants as a constant tensor; or their values gathered:
// `acc = c_0`, then `acc = where(e_j, c_j, acc)` for j = 1 ... k - 1, `e_j` one at row j, made once a run (`masks`).
constexpr std::int32_t merged(Linking& l, std::vector<Node>& block, const Graph& g, const Run& run, const Outside& o,
                              std::vector<std::int32_t>& masks) {
    const std::vector<std::int32_t> values = detail::outside_values(g, run, o.at, o.operand);
    const std::size_t k = values.size();
    const Value& first = g.values[static_cast<std::size_t>(values[0])];
    numerics::Precision p = first.precision;
    std::vector<double> numbers{};
    for (const std::int32_t v : values) {
        p = numerics::join(p, g.values[static_cast<std::size_t>(v)].precision);
        numbers.push_back(g.values[static_cast<std::size_t>(v)].constant);
    }
    if (o.how != Merge::Gathered) {
        return o.how == Merge::Shared ? values[0] : detail::pooled(l, block, numbers, first.type, p);
    }
    const numerics::Precision sections = g.values[static_cast<std::size_t>(g.nodes[static_cast<std::size_t>(run.sections[0][run.out])].output)].precision;
    for (std::size_t j = masks.size() + 1; j < k; ++j) {
        std::vector<double> one_hot(k, 0.0);
        one_hot[j] = 1.0;
        masks.push_back(detail::pooled(l, block, one_hot, ElemType::F64, sections));
    }
    std::int32_t acc = values[0];
    for (std::size_t j = 1; j < k; ++j) {
        Node choice{.op = detail::choice_row()};
        choice.inputs[0] = masks[j - 1];
        choice.inputs[1] = values[j];
        choice.inputs[2] = acc;
        const numerics::Precision chosen = numerics::join(sections, numerics::join(g.values[static_cast<std::size_t>(values[j])].precision,
                                                                                   l.values[static_cast<std::size_t>(acc)].precision));
        const Shape column{literal_extent(static_cast<std::int32_t>(k)), literal_extent(1)};
        acc = detail::emitted(l, block, choice, Value{first.type, column, ValueCategory::Internal, 0, chosen, false});
    }
    return acc;
}

// A section's shape with the sections' axis before it: [k, time] for [time].
constexpr Shape over_sections(const Shape& one, std::size_t k) {
    Shape s{};
    s.rank = one.rank + 1;
    set_extent(s, 0, literal_extent(static_cast<std::int32_t>(k)));
    for (std::int32_t a = 0; a < one.rank; ++a) {
        set_extent(s, a + 1, extent_at(one, a));
    }
    return s;
}

// Section 0's nodes over all sections, into `block`: each read of the run's input (or a lag of it) now of the link `u`
// (or the same lag of `u`), each operand from outside the merged one (`merged_at`, by node and operand), their values
// and the variables they define over [k, time].
constexpr void first_over_all(Linking& l, std::vector<Node>& block, const Reading& r, const Graph& g, const Run& run, std::int32_t u,
                              const std::vector<std::int32_t>& merged_at) {
    const std::size_t k = run.cycles.size();
    for (std::size_t t = 0; t < run.sections[0].size(); ++t) {
        Node n = g.nodes[static_cast<std::size_t>(run.sections[0][t])];
        for (std::size_t operand = 0; operand < rules_of(n.op).arity; ++operand) {
            const std::int32_t maker = made_by(r, n.inputs[operand]);
            if (maker >= 0 && detail::holds(r, run, 0, maker)) {
                continue;
            }
            n.inputs[operand] = as_read(r, n.inputs[operand]) == run.input ? u : merged_at[t * kMaxNodeInputs + operand];
        }
        Value& made = l.values[static_cast<std::size_t>(n.output)];
        made.shape = detail::over_sections(made.shape, k);
        if (n.attr.kind == AttrKind::Recurrence) {
            Value& defined = l.values[static_cast<std::size_t>(n.attr.recurrence.defines)];
            defined.shape = detail::over_sections(defined.shape, k);
        }
        block.push_back(n);
    }
}

// Section k - 1 of `v`, a value over the sections, as the value `one` it was: `reshape(slice(v, axis 0, k - 1, 1))`.
constexpr std::int32_t last_section(Linking& l, std::vector<Node>& block, std::int32_t v, std::size_t k, const Value& one) {
    Node slice{.op = detail::view_row()};
    slice.inputs[0] = v;
    slice.attr.kind = AttrKind::View;
    slice.attr.view.kind = ViewKind::Slice;
    slice.attr.view.axis = 0;
    slice.attr.view.offset = static_cast<std::int32_t>(k) - 1;
    slice.attr.view.length = 1;
    slice.attr.view.step = 1;
    Value part = one;
    part.category = ValueCategory::Internal;
    part.shape = detail::over_sections(one.shape, 1);
    const std::int32_t sliced = detail::emitted(l, block, slice, part);
    Node reshape{.op = detail::view_row()};
    reshape.inputs[0] = sliced;
    reshape.attr.kind = AttrKind::View;
    reshape.attr.view.kind = ViewKind::Reshape;
    reshape.attr.view.shape = one.shape;
    Value back = one;
    back.category = ValueCategory::Internal;
    return detail::emitted(l, block, reshape, back);
}

// `run` as one cycle over its sections, put after its last node: the operands from outside merged, the link
// `u = _chain(x, V)`, section 0's nodes over [k, time], `V` defined where the value the next section reads is no
// variable's definition (a TPT stage's output: a variable of its own), and the last section's value sliced out, which
// what read it reads instead. The slice reads V's definition, the same number: the proof of ranges reads a variable at
// this sample as any number, its definition as what it holds (a tanh's (-1, 1)). Every section's nodes go.
constexpr void link_run(Linking& l, const Reading& r, const Graph& g, const Run& run) {
    const std::size_t k = run.cycles.size();
    const std::vector<std::int32_t>& first = run.sections[0];
    std::vector<Node> block{};
    std::vector<std::int32_t> masks{};
    std::vector<std::int32_t> merged_at(first.size() * kMaxNodeInputs, kNoValue);
    for (const Outside& o : run.outside) {
        merged_at[o.at * kMaxNodeInputs + o.operand] = detail::merged(l, block, g, run, o, masks);
    }
    const Node& result = g.nodes[static_cast<std::size_t>(first[run.out])];
    const Value one = g.values[static_cast<std::size_t>(result.output)];
    const bool defines = result.attr.kind == AttrKind::Recurrence;
    Value over = one;
    over.shape = detail::over_sections(one.shape, k);
    over.category = ValueCategory::Variable;
    if (!defines) {
        l.values.push_back(over);
    }
    const std::int32_t v = defines ? result.attr.recurrence.defines : static_cast<std::int32_t>(l.values.size() - 1);
    Node link{.op = detail::link_row()};
    link.inputs[0] = detail::instead(l, r, run.input);
    link.inputs[1] = v;
    const ElemType type = heinzel::detail::wider(g.values[static_cast<std::size_t>(run.input)].type, one.type);
    const std::int32_t u = detail::emitted(l, block, link, Value{type, over.shape, ValueCategory::Internal, 0, one.precision, false});
    detail::first_over_all(l, block, r, g, run, u, merged_at);
    std::int32_t definition = result.output;
    if (!defines) {
        Node settle{.op = detail::recurrence_row()};
        settle.inputs[0] = result.output;
        settle.attr.kind = AttrKind::Recurrence;
        settle.attr.recurrence.defines = v;
        over.category = ValueCategory::Internal;
        definition = detail::emitted(l, block, settle, over);
    }
    const std::int32_t back = detail::last_section(l, block, definition, k, one);
    l.renamed[static_cast<std::size_t>(as_read(r, g.nodes[static_cast<std::size_t>(run.sections[k - 1][run.out])].output))] = back;
    for (const std::vector<std::int32_t>& section : run.sections) {
        for (const std::int32_t i : section) {
            const Node& n = g.nodes[static_cast<std::size_t>(i)];
            l.gone[static_cast<std::size_t>(i)] = 1;
            l.unmarked[static_cast<std::size_t>(n.output)] = 1;
            l.unmarked[static_cast<std::size_t>(n.attr.kind == AttrKind::Recurrence ? n.attr.recurrence.defines : n.output)] = 1;
        }
    }
    std::vector<Node>& after = l.after[static_cast<std::size_t>(detail::end_of(run))];
    after.insert(after.end(), block.begin(), block.end());
}

// `g`'s range marks for `out`, which `with` made of `nodes` and `l.h`'s inputs and outputs: each renumbered as `with`
// numbered its value, read off what it made; a mark on a value it dropped, or on a section's, goes.
constexpr void carry_ranges(const Linking& l, const std::vector<Node>& nodes, Graph& out) {
    std::vector<std::int32_t> to(l.values.size(), kNoValue);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        for (std::size_t k = 0; k < rules_of(nodes[i].op).arity; ++k) {
            to[static_cast<std::size_t>(nodes[i].inputs[k])] = out.nodes[i].inputs[k];
        }
        to[static_cast<std::size_t>(nodes[i].output)] = out.nodes[i].output;
        if (nodes[i].attr.kind == AttrKind::Recurrence) {
            to[static_cast<std::size_t>(nodes[i].attr.recurrence.defines)] = out.nodes[i].attr.recurrence.defines;
        }
    }
    for (std::size_t i = 0; i < l.h.inputs.size(); ++i) {
        to[static_cast<std::size_t>(l.h.inputs[i].value)] = out.inputs[i].value;
    }
    out.ranges.clear();
    for (const RangeMark& m : l.h.ranges) {
        const auto v = static_cast<std::size_t>(m.value);
        if (to[v] != kNoValue && l.unmarked[v] == 0) {
            out.ranges.push_back(RangeMark{to[v], m.range, m.promised});
        }
    }
}

// `g` with each of `runs` one linked cycle; `g` itself where the result would outgrow a capacity, or where `infer` would
// change a value the rewrite made or the feedback facts would not take it (a cycle past `kMaxGroupStmts`).
constexpr Graph linked(const Graph& g, const Reading& r, const std::vector<Run>& runs) {
    Linking l{g, std::vector<Value>(g.values.begin(), g.values.end()), std::vector<std::vector<Node>>(g.nodes.size()),
              std::vector<char>(g.nodes.size(), 0), std::vector<std::int32_t>(g.values.size(), kNoValue), std::vector<char>(g.values.size(), 0)};
    for (const Run& run : runs) {
        detail::link_run(l, r, g, run);
    }
    std::vector<Node> nodes{};
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        if (l.gone[i] == 0) {
            Node n = g.nodes[i];
            for (std::size_t k = 0; k < rules_of(n.op).arity; ++k) {
                n.inputs[k] = detail::instead(l, r, n.inputs[k]);
            }
            nodes.push_back(n);
        }
        nodes.insert(nodes.end(), l.after[i].begin(), l.after[i].end());
    }
    for (NamedValue& io : l.h.outputs) {
        io.value = detail::instead(l, r, io.value);
    }
    std::optional<Graph> out = detail::with(l.h, nodes, l.values);
    if (!out) {
        return g;
    }
    detail::carry_ranges(l, nodes, *out);
    Graph checked = *out;
    const loops::graph::Facts facts = loops::graph::facts_of(*out);
    const bool held = infer(checked).ok && checked == *out && facts.why_not.empty() && loops::graph::find_feedback(*out, facts).why_not.empty();
    return held ? *out : g;
}

// Analysed graph `g` with every run of like sections that qualifies on machine `m` one cycle over its sections, linked
// (a run whose merge would pass the pool stays as written: `qualifies`); `g` itself where none does, or where the
// result would outgrow the nodes or values. A linked cycle is no section, so a second pass finds nothing. Unpriced: the
// merge alone, as its tests take it.
constexpr Graph linked_runs(const Graph& g, const Machine& m) {
    Reading r = detail::reading_of(g);
    if (!r.facts.why_not.empty() || !r.feedback.why_not.empty()) {
        return g; // (the planner refuses it, by name)
    }
    const std::vector<Run> runs = detail::runs_of(r, g, m);
    if (runs.empty()) {
        return g;
    }
    return detail::linked(g, r, runs);
}

// What a sample of `g`'s feedback stages costs as the planner will be given them (`priced.hpp`).
constexpr double feedback_priced(const Graph& g, const Machine& m) {
    return loops::graph::feedback_cycles(rewrite::sums_and_state(g, m), m);
}

} // namespace detail

// Analysed graph `g` with its runs of like sections linked (`detail::linked_runs`) where the cost model prices its
// feedback stages, as the planner will be given them, below today's by more than the model resolves (`kTie`); `g`
// itself otherwise, so the cascade wins a tie (the price P). The price is a sample's, so two costs a run stay out of
// it, named stand-ins: the lanes' drain (k - 1 steps a run), and the gather, which runs in a stage of its own before
// the loops, priced apart. The runs are priced together, taken or kept as one. The merge has returned before the
// price runs the rules, so their frames do not stack on `linked`'s three Graphs.
constexpr Graph sections_in_lanes(const Graph& g, const Machine& m) {
    const Graph merged = detail::linked_runs(g, m);
    if (merged == g) {
        return g;
    }
    return detail::feedback_priced(merged, m) * detail::kTie < detail::feedback_priced(g, m) ? merged : g;
}

// Sections in lanes (`docs/sections-in-lanes.md`, "Switch"): at 0 no link is made, every file of one program agreeing.
#ifndef HEINZEL_LANES
#define HEINZEL_LANES 1
#endif

// `sums_and_state(sections_in_lanes(g, m), m)`, what `rewritten` runs after the parallel form: the side the price takes
// is handed back as the price ran the rules on it, so they run twice, not three times. `kept` is built in the caller's
// frame, so this frame holds two Graphs, as `sections_in_lanes` and its price did.
constexpr Graph lanes_sums_and_state(const Graph& g, const Machine& m) {
    if constexpr (HEINZEL_LANES == 0) {
        return rewrite::sums_and_state(g, m);
    }
    const Graph merged = detail::linked_runs(g, m);
    if (merged == g) {
        return rewrite::sums_and_state(g, m);
    }
    Graph kept = rewrite::sums_and_state(g, m);
    const Graph lanes = rewrite::sums_and_state(merged, m);
    if (loops::graph::feedback_cycles(lanes, m) * detail::kTie < loops::graph::feedback_cycles(kept, m)) {
        kept = lanes;
    }
    return kept;
}

} // namespace heinzel::rewrite
