#pragma once

// Whether a value has time, and what that forbids (`docs/decisions.md`, Decision 53). A value is a
// signal (its last axis is time) or a tensor (the same numbers at every sample, recorded with a last
// axis of extent 1). An input or a generator says which; a literal is neutral and takes what it meets;
// a computed value is the join of its operands (neutral < tensor < signal), and a `Variable` that of its
// recurrence's definition, or a signal where it declared its length or took the graph's (Decision 76).

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/sema/declared_variables.hpp"
#include "heinzel/sema/view_shape.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel {

namespace detail {

enum class TimeKind : std::uint8_t { Neutral, Tensor, Signal }; // a lattice, in this order

using TimeKinds = TimeKind[kMaxValues];

constexpr Diagnostic time_refusal(std::string_view text) { return (MessageWriter{} << text).problem(); }

// A marked reshape target, a short transpose order and a regroup, completed where they stand: the analysed graph is
// the one built with the full target and order, and a second analysis finds nothing to complete.
constexpr void complete_views(Graph& g) {
    for (Node& n : g.nodes) {
        if (n.attr.kind == AttrKind::View) {
            n.attr.view = as_read(g.values[static_cast<std::size_t>(n.inputs[0])].shape, n.attr.view);
        }
    }
}

constexpr bool raise(TimeKind& kind, TimeKind to) {
    const bool higher = kind < to;
    kind = higher ? to : kind;
    return higher;
}

// The seeds, then the join to a fixpoint: monotone on three levels, so `n + 2` sweeps bound it. A seed
// is only ever raised: a generator, which has no operand, keeps its own. The kinds are never written
// back, so a second analysis computes them again from the same seeds.
constexpr void time_kinds(const Graph& g, const Declared& variables, TimeKinds& kind) {
    for (std::size_t v = 0; v < g.values.size(); ++v) {
        const Value& value = g.values[v];
        if (variables.at[v] || variables.takes[v]) {
            kind[v] = TimeKind::Signal; // a variable that declared its length, or took the graph's
        } else if (value.category == ValueCategory::Input) {
            kind[v] = value.timeless ? TimeKind::Tensor : TimeKind::Signal;
        } else {
            kind[v] = TimeKind::Neutral;
        }
    }
    for (const Node& n : g.nodes) {
        if (rules_of(n.op).shape == ShapeRule::Declared) {
            kind[static_cast<std::size_t>(n.output)] = g.values[static_cast<std::size_t>(n.output)].timeless ? TimeKind::Tensor : TimeKind::Signal;
        }
    }
    for (std::size_t sweep = 0; sweep < g.values.size() + 2; ++sweep) {
        bool changed = false;
        for (const Node& n : g.nodes) {
            TimeKind& out = kind[static_cast<std::size_t>(n.output)];
            for (std::uint8_t k = 0; k < rules_of(n.op).arity; ++k) {
                changed = (n.inputs[k] != kNoValue && raise(out, kind[static_cast<std::size_t>(n.inputs[k])])) || changed;
            }
            if (n.attr.kind == AttrKind::Recurrence && n.attr.recurrence.defines != kNoValue) {
                changed = raise(kind[static_cast<std::size_t>(n.attr.recurrence.defines)], out) || changed;
            }
        }
        if (!changed) {
            return;
        }
    }
    fail("heinzel::infer: the time kinds did not reach a fixpoint within the bound");
}

// One time seed's length into the count: its last extent, told apart from the first by `same_extent` (every `Length`
// is the one symbol `kStreamLength`, so `Signal{Length{}}` and `Signal{2, Length{}}` are one length). Counted to two,
// already one too many to choose from.
constexpr void count_length(const Shape& seed, Extent& length, std::int32_t& lengths) {
    if (seed.rank == 0) {
        return; // an input built by hand with no axes has no length to give
    }
    const Extent last = extent_at(seed, seed.rank - 1);
    if (lengths == 0) {
        length = last;
        lengths = 1;
    } else if (!same_extent(last, length)) {
        lengths = 2;
    }
}

// The graph's lengths: those of the values `time_kinds` seeds as signals — a timed input, a timed generator
// (`iota(Signal{…})`), a declared variable or output — the first of them kept in `variables.length`.
constexpr std::int32_t graph_lengths(const Graph& g, Declared& variables) {
    std::int32_t lengths = 0;
    for (std::size_t v = 0; v < g.values.size(); ++v) {
        const Value& value = g.values[v];
        if (variables.at[v] || (value.category == ValueCategory::Input && !value.timeless)) {
            count_length(value.shape, variables.length, lengths);
        }
    }
    for (const Node& n : g.nodes) {
        const bool timed = !g.values[static_cast<std::size_t>(n.output)].timeless;
        if (rules_of(n.op).shape == ShapeRule::Declared && timed) {
            count_length(n.attr.generator.shape, variables.length, lengths);
        }
    }
    return lengths;
}

// A recurrence no signal drives takes the graph's length where the graph has exactly one (Decision 76): a length is
// inferred from a declared one, never time from operations. The kinds come from the declarations alone, before the
// sweep, as `check_time` works them out after it, so a graph accepted before has no recurrence that takes one. With
// no length it stays refused, by `recurrences_have_a_signal`; with two, here.
constexpr Diagnostic undriven_take_the_length(const Graph& g, Declared& variables) {
    TimeKinds kind{};
    time_kinds(g, variables, kind);
    std::int32_t lengths = -1; // counted at the first recurrence that needs one
    for (const Node& n : g.nodes) {
        if (n.attr.kind != AttrKind::Recurrence || n.attr.recurrence.defines == kNoValue) {
            continue;
        }
        const auto v = static_cast<std::size_t>(n.attr.recurrence.defines);
        if (variables.at[v] || kind[static_cast<std::size_t>(n.output)] == TimeKind::Signal) {
            continue;
        }
        lengths = lengths < 0 ? graph_lengths(g, variables) : lengths;
        if (lengths > 1) {
            return time_refusal("heinzel: a recurrence no signal drives takes the graph's length, but it has two; declare it");
        }
        variables.takes[v] = lengths == 1;
    }
    return Diagnostic{};
}

// Every recurrence first: `v[n] = w * v[n - 1]` with `w` a tensor reads this, not the lag refusal its delay,
// which comes before it, would give — the fix is a signal in the definition.
constexpr Diagnostic recurrences_have_a_signal(const Graph& g, const TimeKinds& kind) {
    for (const Node& n : g.nodes) {
        if (n.attr.kind == AttrKind::Recurrence && kind[static_cast<std::size_t>(n.output)] != TimeKind::Signal) {
            return time_refusal(kNeedsASignal);
        }
    }
    return Diagnostic{};
}

constexpr bool hidden(std::int32_t axis, std::int32_t rank) { return axis == kInnermostAxis || axis >= rank - 1; }

// Does this node name, on its first operand, the axis a tensor keeps for time? A fold, an order statistic, a
// slice, a window, or a permutation that puts it anywhere but last (or names an axis past it).
constexpr bool names_the_hidden_axis(const Node& n, const OpRules& rules, std::int32_t rank) {
    if (rules.shape == ShapeRule::ReduceAxes) {
        bool named = false;
        for (std::int32_t k = 0; k < n.attr.reduce.axes.rank; ++k) {
            named = named || hidden(n.attr.reduce.axes.axes[k], rank);
        }
        return named;
    }
    if (rules.shape == ShapeRule::OrderAxis) {
        return hidden(n.attr.order.axis, rank);
    }
    if (rules.shape != ShapeRule::ByViewKind) {
        return false;
    }
    const ViewAttr& v = n.attr.view;
    switch (v.kind) {
    case ViewKind::Slice:
    case ViewKind::Unfold:
        return hidden(v.axis, rank);
    case ViewKind::Unfold2d:
        return hidden(v.axis, rank) || hidden(v.axis2, rank);
    case ViewKind::Transpose: {
        bool named = rank > 0 && v.order.rank == rank && v.order.axes[rank - 1] != rank - 1;
        for (std::int32_t k = 0; k < v.order.rank; ++k) {
            named = named || (hidden(v.order.axes[k], rank) && k != rank - 1);
        }
        return named;
    }
    default:
        return false;
    }
}

// A squeeze drops an element axis of extent 1; an unsqueeze inserts one among the element axes, or right after
// the last of them (autodiff's `bcast_back`), and never past `kMaxRank` axes. For every value: time is the
// last axis of a tensor's record too. A view still marked is one `complete_views` could not complete.
constexpr Diagnostic axis_counts_hold(const Shape& in, const ViewAttr& v) {
    if (v.kind == ViewKind::Squeeze &&
        (v.axis < 0 || v.axis >= in.rank - 1 || !(is_literal(extent_at(in, v.axis)) && literal_of(extent_at(in, v.axis)) == 1))) {
        return time_refusal("heinzel: a squeeze drops an element axis of extent 1");
    }
    if (v.kind == ViewKind::Unsqueeze && (v.axis < 0 || v.axis > in.rank - 1 || in.rank >= static_cast<std::int32_t>(kMaxRank))) {
        return time_refusal("heinzel: an unsqueeze inserts an element axis, and a value has at most kMaxRank axes");
    }
    if (v.kind == ViewKind::Reshape && v.elements_only) { // what `complete_views` could not complete
        return time_refusal(in.rank == 0 ? "heinzel: a reshape of a rank-0 value; broadcast it instead"
                                         : "heinzel: a reshape target has at most kMaxRank - 1 axes");
    }
    if (v.operand_groups != 0) { // a regroup it could not complete, worded by its family
        Shape target{};
        Diagnostic why{};
        (void)regroup_target(in, v, target, &why);
        return why;
    }
    if (v.kind == ViewKind::Transpose && v.elements_only) { // a member's order that does not fit the value
        return time_refusal("heinzel: a transpose order names the element axes, each once");
    }
    return Diagnostic{};
}

constexpr Diagnostic node_keeps_time(const Graph& g, const Node& n, const TimeKinds& kind) {
    const OpRules rules = rules_of(n.op);
    if (rules.arity == 0) {
        return Diagnostic{};
    }
    const Shape& in = g.values[static_cast<std::size_t>(n.inputs[0])].shape;
    for (std::size_t k = 0; k < 2; ++k) { // the ranks a public spec named (`OpAttr::named_ranks`)
        if (n.attr.named_ranks[k] != 0 && n.inputs[k] != kNoValue &&
            g.values[static_cast<std::size_t>(n.inputs[k])].shape.rank != n.attr.named_ranks[k]) {
            return time_refusal("heinzel: the spec names a different number of axes than the value has");
        }
    }
    if (n.attr.kind == AttrKind::View) {
        const Diagnostic counts = axis_counts_hold(in, n.attr.view);
        if (!counts.ok) {
            return counts;
        }
    }
    const TimeKind first = kind[static_cast<std::size_t>(n.inputs[0])];
    const TimeKind second = rules.arity > 1 ? kind[static_cast<std::size_t>(n.inputs[1])] : TimeKind::Neutral;
    if (reaches_into_the_past(rules.time) && first == TimeKind::Tensor) {
        return time_refusal("heinzel: a lag reads a tensor — declare it as a Signal to read its past");
    }
    if (rules.shape == ShapeRule::SpectralAxis && first == TimeKind::Tensor) {
        return time_refusal("heinzel: an fft needs a Signal");
    }
    if (rules.shape == ShapeRule::ContractGeneral &&
        ((first == TimeKind::Tensor && second == TimeKind::Signal) || (first == TimeKind::Signal && second == TimeKind::Tensor))) {
        return time_refusal("heinzel: a tensor times a signal needs a multiply and sum, not a contraction");
    }
    if (first == TimeKind::Tensor && names_the_hidden_axis(n, rules, in.rank)) {
        return time_refusal("heinzel: that axis is past the tensor's last");
    }
    return Diagnostic{};
}

} // namespace detail

// Runs right after the shapes settle and before any other check, so that what a user reads about a
// tensor is worded for a tensor. It first completes the reshapes and transpose orders a member wrote
// over the element axes only, then works out whether each value is a signal or a tensor, then refuses
// what does not fit: every recurrence first, then each node in order, including the ranks a public spec
// named. Each refusal asks a rule column, never an operation code.
constexpr Diagnostic check_time(Graph& g, const detail::Declared& declared) {
    detail::complete_views(g);
    detail::TimeKinds kind{};
    detail::time_kinds(g, declared, kind);
    const Diagnostic recurrences = detail::recurrences_have_a_signal(g, kind);
    if (!recurrences.ok) {
        return recurrences;
    }
    for (const Node& n : g.nodes) {
        const Diagnostic d = detail::node_keeps_time(g, n, kind);
        if (!d.ok) {
            return d;
        }
    }
    // Cleared once checked, so an analysed graph equals the same graph built without a spec (`OpAttr`, `ir/ir.hpp`).
    for (Node& n : g.nodes) {
        n.attr.named_ranks[0] = n.attr.named_ranks[1] = 0;
    }
    return Diagnostic{};
}

} // namespace heinzel
