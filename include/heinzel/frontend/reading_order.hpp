#pragma once

// Statements in any order (Plan/28-dsl-proposals/03 B4, Decision 76). A variable's present may be read before the
// statement that defines it, `line[n] = x + fb * hp` before `hp[n] = …`: the read is made while `hp` is still the
// variable itself, so it stands before the recurrence that defines it, and every pass after finishing reads the nodes
// in the order they stand. Finishing puts such a graph in reading order, each node after what it waits for, and a read
// so moved reads the definition, as a read written after it does; then a lag of a variable standing before its
// definition, where no loop reads it, after that definition. A graph that needs neither is left exactly as built: the
// loop tree refused every graph that does, so no graph accepted before moves.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/timing.hpp"

namespace heinzel::detail {

// Per value, the node that makes it; for a variable, the recurrence that defines it (-1: an input, a constant, a
// variable never defined).
constexpr std::vector<std::int32_t> makers_of(const Graph& g) {
    std::vector<std::int32_t> made_by(g.values.size(), -1);
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        const Node& n = g.nodes[i];
        made_by[static_cast<std::size_t>(n.output)] = static_cast<std::int32_t>(i);
        if (n.attr.kind == AttrKind::Recurrence && n.attr.recurrence.defines != kNoValue) {
            made_by[static_cast<std::size_t>(n.attr.recurrence.defines)] = static_cast<std::int32_t>(i);
        }
    }
    return made_by;
}

// A lag: it reads the past and nothing of this sample.
constexpr bool is_a_lag(const Node& n) {
    for (std::size_t k = 0; k < kMaxNodeInputs; ++k) {
        if (n.inputs[k] != kNoValue && !operand_comes_from_the_past(n, k)) {
            return false;
        }
    }
    return reads_the_past(n);
}

// Where node `i`, a lag of a variable, closes a loop: the recurrence defining that variable, standing after it; else -1.
constexpr std::int32_t closes_at(const Graph& g, const std::vector<std::int32_t>& made_by, std::size_t i) {
    const Node& n = g.nodes[i];
    if (!is_a_lag(n) || g.values[static_cast<std::size_t>(n.inputs[0])].category != ValueCategory::Variable) {
        return -1;
    }
    const std::int32_t defined = made_by[static_cast<std::size_t>(n.inputs[0])];
    return defined > static_cast<std::int32_t>(i) ? defined : -1;
}

// Per node, a lag that closes a loop which nothing in it reads: in each stretch the lags' loops span, as the loop tree
// reads them (`loops/graph/feedback.hpp`, which refuses such a lag where it stands), what the recurrences they close
// read, followed back. Nothing needs the lag before its recurrence, so it waits for it, as one written after it. For a
// graph in reading order: there a read reaches only back, into its stretch, so waiting closes no loop.
constexpr std::vector<char> late_lags(const Graph& g, const std::vector<std::int32_t>& made_by) {
    const std::size_t count = g.nodes.size();
    std::vector<char> late(count, 0);
    std::vector<char> read(count, 0);
    for (std::size_t from = 0; from < count;) {
        std::size_t until = from;
        for (std::size_t j = from; j <= until; ++j) {
            const std::int32_t closes = closes_at(g, made_by, j);
            if (closes >= 0) {
                late[j] = 1;
                read[static_cast<std::size_t>(closes)] = 1;
                until = closes > static_cast<std::int32_t>(until) ? static_cast<std::size_t>(closes) : until;
            }
        }
        for (std::size_t j = until + 1; j-- > from;) {
            for (std::size_t k = 0; read[j] != 0 && k < kMaxNodeInputs; ++k) {
                const std::int32_t v = g.nodes[j].inputs[k];
                const std::int32_t m = v != kNoValue ? made_by[static_cast<std::size_t>(v)] : -1;
                if (m >= static_cast<std::int32_t>(from) && m <= static_cast<std::int32_t>(until)) {
                    read[static_cast<std::size_t>(m)] = 1;
                }
            }
            late[j] = late[j] != 0 && read[j] == 0 ? 1 : 0;
        }
        from = until + 1;
    }
    return late;
}

// What the nodes wait for.
struct Waits {
    std::vector<std::int32_t> made_by;
    std::vector<char> late; // `late_lags`, once the graph is in reading order
    bool past_of_a_value{true};
};

// The node operand `k` of `n`, the node built `i`-th, waits for: the one that makes what it reads. A variable's past
// waits for nothing, its recurrence having made it a sample ago: that is feedback; a late lag's waits for that
// recurrence. A computed value's past waits for its maker, as the loop tree and the proof of ranges read it, but where
// that leaves no order (`reorder`).
constexpr std::int32_t waits_for(const Graph& g, const Waits& w, const Node& n, std::size_t i, std::size_t k) {
    const std::int32_t v = n.inputs[k];
    if (v == kNoValue) {
        return -1;
    }
    const bool variable = g.values[static_cast<std::size_t>(v)].category == ValueCategory::Variable;
    const bool feedback = variable ? w.late[i] == 0 : !w.past_of_a_value;
    return feedback && operand_comes_from_the_past(n, k) ? -1 : w.made_by[static_cast<std::size_t>(v)];
}

constexpr bool reads_ahead(const Graph& g, const Waits& w) {
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        for (std::size_t k = 0; k < kMaxNodeInputs; ++k) {
            if (waits_for(g, w, g.nodes[i], i, k) > static_cast<std::int32_t>(i)) {
                return true;
            }
        }
    }
    return false;
}

// A depth-first walk over what each node waits for, its path held here rather than on the stack: constant evaluation
// allows 512 frames of recursion, and a chain of nodes is longer.
struct Placing {
    std::vector<char> state;          // per node: 0 not reached, 1 on the path, 2 placed
    std::vector<std::uint8_t> next;   // per node on the path: the operand it looks at next, in two rounds
    std::vector<std::int32_t> path;
    std::vector<std::int32_t> order;  // the nodes placed, each after what it waits for
};

// Places `root` and what it waits for, depth first: of each node first what is no lag, then its lags, so that a lag
// stands just before its first reader, after whatever else that reader waits for. False where the walk meets a node
// on its own path: a value that waits for itself.
constexpr bool place(const Graph& g, const Waits& w, Placing& p, std::size_t root) {
    p.state[root] = 1;
    p.path.push_back(static_cast<std::int32_t>(root));
    while (!p.path.empty()) {
        const auto i = static_cast<std::size_t>(p.path.back());
        if (p.next[i] == 2 * kMaxNodeInputs) {
            p.state[i] = 2;
            p.order.push_back(static_cast<std::int32_t>(i));
            p.path.pop_back();
            continue;
        }
        const std::size_t step = p.next[i]++;
        const bool lags = step >= kMaxNodeInputs; // the second round
        const std::int32_t to = waits_for(g, w, g.nodes[i], i, step % kMaxNodeInputs);
        if (to < 0) {
            continue;
        }
        const auto at = static_cast<std::size_t>(to);
        if (p.state[at] == 2 || is_a_lag(g.nodes[at]) != lags) {
            continue;
        }
        if (p.state[at] == 1) {
            return false;
        }
        p.state[at] = 1;
        p.path.push_back(to);
    }
    return true;
}

// The nodes in reading order: each in the order it stands, after what it waits for, and a lag just before its first
// reader, not where it stood. Empty where a value waits for itself.
constexpr std::vector<std::int32_t> reading_order(const Graph& g, const Waits& w) {
    const std::size_t count = g.nodes.size();
    std::vector<char> waited_for(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t k = 0; k < kMaxNodeInputs; ++k) {
            const std::int32_t to = waits_for(g, w, g.nodes[i], i, k);
            if (to >= 0) {
                waited_for[static_cast<std::size_t>(to)] = 1;
            }
        }
    }
    Placing p{std::vector<char>(count, 0), std::vector<std::uint8_t>(count, 0), {}, {}};
    for (std::size_t i = 0; i < count; ++i) {
        const bool pulled = waited_for[i] != 0 && is_a_lag(g.nodes[i]);
        if (p.state[i] == 0 && !pulled && !place(g, w, p, i)) {
            return {};
        }
    }
    return p.order;
}

// The nodes put in reading order where some wait for a later one. A present read that stood before its maker (only a
// variable's can) reads the value made there: the definition's, as a read written after the definition does; a lag
// moved still reads the variable, as one written after it does. Where a value waits for itself through a computed
// value's past (`Expr r = x + 0.5 * v;` before `v[n] = 0.3 * r[n - 1]`), that past is taken for feedback, as a
// variable's: the lag then stands before what makes it, which the loop tree refuses by name, and the walk computes it.
// One that waits for itself in the present, an algebraic loop, keeps its order, so that the planner refuses it by name.
constexpr void reorder(Graph& g, Waits& w) {
    if (!reads_ahead(g, w)) {
        return;
    }
    std::vector<std::int32_t> order = reading_order(g, w);
    if (order.empty()) {
        w.past_of_a_value = false;
        order = reading_order(g, w);
    }
    if (order.size() != g.nodes.size()) {
        return;
    }
    const std::vector<Node> built(g.nodes.begin(), g.nodes.end());
    for (std::size_t j = 0; j < order.size(); ++j) {
        const auto i = static_cast<std::size_t>(order[j]);
        Node n = built[i];
        for (std::size_t k = 0; k < kMaxNodeInputs; ++k) {
            const std::int32_t to = waits_for(g, w, built[i], i, k);
            if (to > order[j] && !operand_comes_from_the_past(built[i], k)) {
                n.inputs[k] = built[static_cast<std::size_t>(to)].output;
            }
        }
        g.nodes[j] = n;
    }
}

// Finishing's last step: a variable's present read ahead, then, the graph in reading order, its late lags.
constexpr void in_reading_order(Graph& g) {
    Waits w{makers_of(g), std::vector<char>(g.nodes.size(), 0), true};
    reorder(g, w);
    w.made_by = makers_of(g);
    w.late = late_lags(g, w.made_by);
    reorder(g, w);
}

} // namespace heinzel::detail
