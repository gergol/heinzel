#pragma once

// What the parallel form (`parallel_form.hpp`) recognises: a second-order section, a feedback cycle whose definition is
// linear in one signal before it (now, one and two samples back) and in its own past (one and two back), every
// coefficient a constant. A value's linear form is read off the graph by the rule table's `Algebra` column: sums,
// differences, negations, a constant times a signal; a lag of a signal is that signal some samples back. The signal a
// section reads is the one its lags read (its own aside): the form descends through every linear step down to it, so
// `0.99 x` is a term of `x`, and a section that reads its input only now opens its sum down to what no linear step
// makes. Names no operation.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel::rewrite {
namespace detail {

// ---- what a value is, linearly ---------------------------------------------------------------------------------

// Per value: the node making it (-1: none), the signal it is (a recurrence's result: the variable it defines); per
// variable, its recurrence's result; per value, the nodes reading it, once per operand.
struct Lookup {
    std::vector<std::int32_t> producer;
    std::vector<std::int32_t> signal;
    std::vector<std::int32_t> present;
    std::vector<std::vector<std::int32_t>> readers;
};

constexpr Lookup lookup_of(const Graph& g) {
    Lookup at{};
    const std::size_t values = g.values.size();
    at.producer.assign(values, -1), at.present.assign(values, kNoValue), at.readers.assign(values, {});
    for (std::size_t v = 0; v < values; ++v) {
        at.signal.push_back(static_cast<std::int32_t>(v));
    }
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        const Node& n = g.nodes[i];
        const auto out = static_cast<std::size_t>(n.output);
        at.producer[out] = static_cast<std::int32_t>(i);
        for (std::size_t k = 0; k < rules_of(n.op).arity; ++k) {
            at.readers[static_cast<std::size_t>(n.inputs[k])].push_back(static_cast<std::int32_t>(i));
        }
        if (n.attr.kind == AttrKind::Recurrence) {
            at.signal[out] = n.attr.recurrence.defines;
            at.present[static_cast<std::size_t>(n.attr.recurrence.defines)] = n.output;
        }
    }
    return at;
}

// Is value `v` a step a linear form descends through: a sum, a difference, a negation or a product, read by one node?
// A value read twice is a signal of its own, so each node is opened once and a form is found in time linear in the
// graph (`t = t + t` repeated doubles the paths through it at every step, as `feedback_sums`' `take` knows).
constexpr bool opens(const Graph& g, const Lookup& at, std::int32_t v) {
    const std::int32_t p = at.producer[static_cast<std::size_t>(v)];
    if (p < 0 || at.readers[static_cast<std::size_t>(v)].size() != 1) {
        return false;
    }
    const Algebra role = rules_of(g.nodes[static_cast<std::size_t>(p)].op).algebra;
    return role == Algebra::Add || role == Algebra::Subtract || role == Algebra::Negate || role == Algebra::Product;
}

// A number far inside double's range, so that the product of two such cannot overflow: an infinity met later as
// `inf - inf` or `0 * inf` is a NaN, which constant evaluation refuses, failing a compile that has no filter to fail.
constexpr bool tame(double x) { return x > -1e100 && x < 1e100; }

// The signal a lag among value `v`'s linear steps reads, signal `own` aside (kNoValue: none).
constexpr std::int32_t lagged_input(const Graph& g, const Lookup& at, std::int32_t v, std::int32_t own, int depth) {
    const std::int32_t p = at.producer[static_cast<std::size_t>(v)];
    if (p < 0 || depth > 64) {
        return kNoValue;
    }
    const Node& n = g.nodes[static_cast<std::size_t>(p)];
    if (n.attr.kind == AttrKind::Delay) {
        const std::int32_t s = at.signal[static_cast<std::size_t>(n.inputs[0])];
        return s == own ? kNoValue : s;
    }
    for (std::size_t k = 0; k < rules_of(n.op).arity && opens(g, at, v); ++k) {
        if (const std::int32_t s = lagged_input(g, at, n.inputs[k], own, depth + 1); s != kNoValue) {
            return s;
        }
    }
    return kNoValue;
}

// A value as a sum of constants times signals, each `lag` samples back; `nodes` gathers the nodes it opened.
struct Linear {
    struct Term {
        std::int32_t signal{kNoValue};
        std::int32_t lag{0};
        std::int32_t read{kNoValue};  // `read`: the value read, where the lag is 0
        double coefficient{0};
    };
    std::vector<Term> terms{};
    double constant{0};
    bool linear{true};
};

// `from` times `scale`, added to `to`; a coefficient past `tame` makes the form no section's (nothing past it is
// multiplied again).
constexpr void scaled_into(Linear& to, const Linear& from, double scale) {
    to.linear = to.linear && from.linear && tame(scale);
    for (Linear::Term t : from.terms) {
        t.coefficient = to.linear ? t.coefficient * scale : 0;
        to.linear = to.linear && tame(t.coefficient);
        to.terms.push_back(t);
    }
    to.constant += to.linear ? from.constant * scale : 0;
    to.linear = to.linear && tame(to.constant);
}

// Value `v`'s linear form over signal `input` and the lags it reads: it stops at `input` (a term now), at a scalar
// constant, at a lag (a term at its lag), at a cycle's value and at whatever no linear step `opens` (a term now).
constexpr Linear linear_of(const Graph& g, const Lookup& at, std::int32_t v, std::int32_t input, std::vector<std::int32_t>& nodes, int depth) {
    Linear out{};
    const Value& value = g.values[static_cast<std::size_t>(v)];
    const std::int32_t p = at.producer[static_cast<std::size_t>(v)];
    if (value.category == ValueCategory::Const && value.shape.rank == 0) {
        out.constant = value.constant;
        out.linear = tame(value.constant);
        return out;
    }
    if (at.signal[static_cast<std::size_t>(v)] == input || p < 0 ||
        (g.nodes[static_cast<std::size_t>(p)].attr.kind != AttrKind::Delay && !opens(g, at, v))) {
        out.terms.push_back({at.signal[static_cast<std::size_t>(v)], 0, v, 1.0}); // (a signal, read now)
        return out;
    }
    const Node& n = g.nodes[static_cast<std::size_t>(p)];
    const Algebra role = rules_of(n.op).algebra;
    nodes.push_back(p);
    if (n.attr.kind == AttrKind::Delay) {
        out.terms.push_back({at.signal[static_cast<std::size_t>(n.inputs[0])], n.attr.delay.samples, kNoValue, 1.0});
        return out;
    }
    out.linear = depth < 64;
    if (!out.linear) {
        return out;
    }
    const Linear a = linear_of(g, at, n.inputs[0], input, nodes, depth + 1);
    if (role == Algebra::Negate) {
        scaled_into(out, a, -1.0);
        return out;
    }
    const Linear b = linear_of(g, at, n.inputs[1], input, nodes, depth + 1);
    if (role != Algebra::Product) {
        scaled_into(out, a, 1.0), scaled_into(out, b, role == Algebra::Subtract ? -1.0 : 1.0);
    } else if (a.terms.empty() || b.terms.empty()) {
        scaled_into(out, a.terms.empty() ? b : a, a.terms.empty() ? a.constant : b.constant);
        out.linear = out.linear && a.linear && b.linear;
    } else {
        out.linear = false; // (a signal times a signal)
    }
    return out;
}

// ---- a section, and a chain of them -----------------------------------------------------------------------------

// y = b0 u + b1 u[n-1] + b2 u[n-2] - a1 y[n-1] - a2 y[n-2], its `_recur` node, its variable, the signal `u` it reads
// and the value it reads `u` through now; `nodes`, every node of it.
struct Section {
    double b[3]{};
    double a[3]{1, 0, 0};
    std::int32_t recur{-1};
    std::int32_t variable{kNoValue};
    std::int32_t input{kNoValue};
    std::int32_t present{kNoValue};
    std::vector<std::int32_t> nodes{};
    bool valid{false};
};

constexpr Section section_of(const Graph& g, const Lookup& at, std::size_t r) {
    Section s{};
    s.recur = static_cast<std::int32_t>(r);
    s.variable = g.nodes[r].attr.recurrence.defines;
    s.nodes.push_back(s.recur);
    const std::int32_t definition = g.nodes[r].inputs[0];
    const Linear form = linear_of(g, at, definition, lagged_input(g, at, definition, s.variable, 0), s.nodes, 0);
    bool fits = form.linear && form.constant == 0;
    for (const Linear::Term& t : form.terms) {
        const bool own = t.signal == s.variable;
        if (!own && s.input == kNoValue) {
            s.input = t.signal;
        }
        fits = fits && (own ? t.lag >= 1 && t.lag <= 2 : t.signal == s.input && t.lag >= 0 && t.lag <= 2);
        if (fits && own) {
            s.a[t.lag] -= t.coefficient;
        } else if (fits) {
            s.b[t.lag] += t.coefficient;
            s.present = t.lag == 0 ? t.read : s.present;
        }
    }
    s.valid = fits && s.input != kNoValue && s.a[2] != 0;
    return s;
}

} // namespace detail
} // namespace heinzel::rewrite
