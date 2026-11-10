#pragma once

// What the proof of ranges may tell the program, not only the graph (Decision 78, after the close: the phase in one
// turn). The proof's ranges are of the real numbers (Decision 74): a program computes in its values' types, so it may
// round past an end the proof computed in double. This walk keeps of the proof only what no rounding moves past 0 or
// 1: every type holds both, and a rounding, being monotone, leaves a number on its side of each. So a value it keeps
// in [0, 1] lies there at every precision, at `High` in double and at `Normal` and `Fast` in float, or is a NaN,
// which lies in no range. It trusts:
//
// - a constant's number: rounded to any type, it stays on its side of 0 and of 1;
// - a rule that makes no number: a view, a lag, a recurrence or a conversion hands its operand on (a conversion
//   rounds it, monotonely), a selection, `min` and `max` pick one, `abs` clears its sign, a comparison answers 0 or
//   1, a join hands on its parts, and a constant tensor and a ramp hold their own numbers. Every other rule computes
//   one: any number here;
// - silence, the 0 a read of the past reads before the first run and after a reset;
// - a fact, the range a value of the frontend's always has (`abs`, `frac`), which holds of its numbers as computed:
//   `abs(x)` is a number at least 0 or a NaN, and `x - floor(x)` lies in [0, 1] for every finite x of either type
//   (just below 0 it rounds up to 1), a NaN otherwise. The subtraction reads x twice, so no rewrite takes
//   x apart into a sum; one may swap its two terms or compute it in double, which keeps it in [0, 1].
//
// Nothing a graph's caller states, which release checks nowhere: no port's range (an input's, a Param's) and no
// promise. Linear, as the proof is: one pass over the marks and the recurrences, then one in node order.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/ranges/interval.hpp"
#include "heinzel/ranges/proven.hpp"

namespace heinzel::ranges {

namespace detail {

// A rule whose result is one of its operands' numbers, or a number every type holds.
constexpr bool makes_no_number(RangeRule r) {
    switch (r) {
    case RangeRule::Same:
    case RangeRule::EachIncreasing:
    case RangeRule::Magnitude:
    case RangeRule::Choice:
    case RangeRule::Truth:
    case RangeRule::Union:
    case RangeRule::Generated: return true;
    default: return false;
    }
}

// What a walk at every precision knows of a graph before it reaches a node: each value's range so far, each value's
// fact (its first mark that is no promise and no port's: -1 for none), each variable's definition (-1 for none).
struct Rounded {
    const Graph& g;
    std::vector<Range> held;
    std::vector<std::int32_t> fact;
    std::vector<std::int32_t> past;
};

constexpr Range with_fact(const Rounded& w, std::int32_t v, const Range& r) {
    const std::int32_t mark = w.fact[at(v)];
    return mark < 0 ? r : narrowed_or_kept(r, w.g.ranges[at(mark)].range);
}

// Operand `k` as its row reads it (the proof's rule 3): a read of the past reads silence too, and a variable's past is
// its definition's, which only its fact keeps here.
constexpr Range operand_in_every_rounding(const Rounded& w, const Node& n, std::size_t k, const OpRules& rules) {
    const std::int32_t v = n.inputs[k];
    if (k != 0 || !reaches_into_the_past(rules.time)) {
        return w.held[at(v)];
    }
    const bool variable = w.g.values[at(v)].category == ValueCategory::Variable;
    const std::int32_t definition = variable ? w.past[at(v)] : kNoValue;
    const Range read = !variable ? w.held[at(v)] : (definition < 0 ? whole() : with_fact(w, definition, whole()));
    return unite(read, point(0.0));
}

} // namespace detail

// Each value's range at every precision (above), by value, of a graph the proof has passed.
constexpr std::vector<Range> in_every_rounding(const Graph& g) {
    const std::size_t values = g.values.size();
    detail::Rounded w{g, std::vector<Range>(values, whole()), std::vector<std::int32_t>(values, -1),
                      std::vector<std::int32_t>(values, -1)};
    // The facts, taken as holding in every rounding at every precision, as `abs`'s and `frac`'s do, not only of the
    // real numbers; a false one (a graph built by hand may state one) makes a read in one turn a wrong number, never
    // one outside its table, whose unwrapped tap reads a place below 0 at the first cell (`table_tap`).
    for (std::size_t i = 0; i < g.ranges.size(); ++i) {
        const RangeMark& m = g.ranges[i];
        const bool port = g.values[detail::at(m.value)].category == ValueCategory::Input;
        std::int32_t& mark = w.fact[detail::at(m.value)];
        mark = m.promised || port || mark >= 0 ? mark : static_cast<std::int32_t>(i);
    }
    for (const Node& n : g.nodes) {
        if (n.attr.kind == AttrKind::Recurrence && n.attr.recurrence.defines != kNoValue) {
            w.past[detail::at(n.attr.recurrence.defines)] = n.inputs[0];
        }
    }
    for (std::size_t v = 0; v < values; ++v) {
        w.held[v] = g.values[v].category == ValueCategory::Const ? point(g.values[v].constant) : w.held[v];
    }
    for (const Node& n : g.nodes) {
        const OpRules rules = rules_of(n.op);
        Range in[kMaxNodeInputs]{};
        for (std::size_t k = 0; k < rules.arity; ++k) {
            in[k] = detail::operand_in_every_rounding(w, n, k, rules);
        }
        const Range made = detail::makes_no_number(rules.range) ? detail::by_rule(g, n, rules, in) : whole();
        w.held[detail::at(n.output)] = detail::with_fact(w, n.output, made);
    }
    return w.held;
}

} // namespace heinzel::ranges
