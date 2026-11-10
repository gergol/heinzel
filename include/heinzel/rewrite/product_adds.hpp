#pragma once

// The rewrite layer's rule for what runs outside feedback cycles and the sums they read: where the machine has the
// multiply-add, an add or a subtract of a product nothing else reads becomes one (`_madd`, rounded as the build rounds;
// `x - a*b` as `_madd(-a, b, x)`, `a*b - x` as `_madd(a, b, -x)`, where what it negates is a constant or made in the
// sum's own loop, never apart as a Param is: `detail::negated_at`), as the feedback rule fuses a sum's last product
// (`feedback_sums.hpp`). A processor whose floating-point pipes each multiply or add — the M1 Pro's four — issues one
// operation for two: `clamp(1.2x + 0.05, 0, 1)` 1.2x faster there, level on zen2, whose adds have pipes of their own
// (`Plan/probes/map-unroll/maps.cpp`); `clamp(0.95 - 1.2x, 0, 1)` 1.29x, crv's wrap `x - floor(…) 2π` 41.4 -> 38.2 µs
// (`maps_subtract.cpp`, `Plan/probes/crv`). It changes rounding, so it fires only where the sum's precision grants
// reordering. It names no operation: what a node is to a sum is the rule table's `Algebra` column.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/rewrite/feedback_sums.hpp"
#include "heinzel/schedule/machine.hpp"

namespace heinzel::rewrite {
namespace detail {

// How many read each value of `g` (an output counts).
constexpr std::vector<std::int32_t> readers_of(const Graph& g) {
    std::vector<std::int32_t> readers(g.values.size(), 0);
    for (const Node& n : g.nodes) {
        for (std::size_t k = 0; k < rules_of(n.op).arity; ++k) {
            ++readers[static_cast<std::size_t>(n.inputs[k])];
        }
    }
    for (const NamedValue& out : g.outputs) {
        ++readers[static_cast<std::size_t>(out.value)];
    }
    return readers;
}

// Per node: whether it is a term of a feedback sum — a value a cycle reads this sample, or that a sum node of such
// terms reads. Those are the feedback rule's, which orders them for the cycle's chain and fuses the last product only
// (Decision 66).
constexpr std::vector<char> in_feedback_sums(const Graph& g, const loops::graph::Facts& facts, const loops::graph::Feedback& feedback) {
    std::vector<char> into_a_cycle(g.nodes.size(), 0);
    for (std::size_t i = g.nodes.size(); i-- > 0;) {
        const Node& n = g.nodes[i];
        const Algebra role = algebra_of(n);
        const bool sum = feedback.cycle[i] >= 0 || (into_a_cycle[i] && (role == Algebra::Add || role == Algebra::Subtract ||
                                                                        role == Algebra::Negate || role == Algebra::ProductAdd));
        for (std::size_t k = 0; sum && k < rules_of(n.op).arity; ++k) {
            const std::int32_t p = facts.producer[static_cast<std::size_t>(n.inputs[k])];
            into_a_cycle[static_cast<std::size_t>(p < 0 ? 0 : p)] |= p >= 0 ? 1 : 0;
        }
    }
    return into_a_cycle;
}

// Where sum node `n` with its product `product` at operand `k` fuses, what the multiply-add negates: nothing (-1) for
// an add; of `x - a*b` a constant factor (0, 1), else one the sum's own loop makes (as timeless as the sum and of its
// shape: a signal's sample, a tensor's element); of `a*b - x` the addend (2) where it is a constant or made so. Never a
// value made apart, as a Param is (`timeless`, per value): the planner makes its `_neg` once a run and the loop reloads
// it from the workspace every vector (it cannot prove the output apart), and NEON's `fmla` overwrites its addend, so
// a shared `-k` costs a copy a vector (`maps_subtract.cpp` on the M1: `clamp(x*w - k, 0, 1)` 188 ns fused, 183 not;
// `clamp(x - k*w, 0, 1)` 168 negating `k`, 163 negating `w`, the add case's). Where no operand qualifies (`a*b - k`),
// it does not fuse.
constexpr std::optional<std::int32_t> negated_at(const Graph& g, const std::vector<char>& timeless, const Node& n, const Node& product,
                                                 std::size_t k) {
    const auto folds = [&](std::int32_t v) { return detail::is_constant(g, v); };
    const auto with_the_sum = [&](std::int32_t v) {
        const auto at = [](std::int32_t u) { return static_cast<std::size_t>(u); };
        return timeless[at(v)] == timeless[at(n.output)] && g.values[at(v)].shape == g.values[at(n.output)].shape;
    };
    const std::int32_t a = product.inputs[0];
    const std::int32_t b = product.inputs[1];
    const std::int32_t x = n.inputs[1 - k];
    if (detail::algebra_of(n) != Algebra::Subtract) {
        return -1;
    }
    if (k == 0) {
        return folds(x) || with_the_sum(x) ? std::optional<std::int32_t>{2} : std::nullopt;
    }
    if (folds(a) || folds(b)) {
        return folds(a) ? 0 : 1;
    }
    if (with_the_sum(a) || with_the_sum(b)) {
        return with_the_sum(a) ? 0 : 1;
    }
    return std::nullopt;
}

// Sum node `n` with its product `product` at operand `k` as one multiply-add, operand `minus` of it (`negated_at`)
// negated (`negated`: a constant as it is copied, anything else by a `_neg` put into `ahead`; exact, so the multiply-add
// rounds as the add case's). Clang folds the `_neg` of a factor into the fused instruction (`fmls`, `vfnmadd`), and of
// the addend on x86 (`vfmsub`); NEON has no vector instruction for `a*b - x`, so there it is `fneg` and `fmla`, as many
// as `fmul` and `fsub` were (level on the M1, `maps_subtract.cpp`).
constexpr Node product_added(const Graph& g, const Node& n, const Node& product, std::size_t k, std::int32_t minus, std::vector<Value>& values,
                             std::vector<Node>& ahead) {
    std::int32_t operands[3] = {product.inputs[0], product.inputs[1], n.inputs[1 - k]};
    if (minus >= 0) {
        operands[minus] = detail::negated(g, values, operands[minus], ahead);
    }
    Node fused{.output = n.output};
    fused.op = detail::op_with(Algebra::ProductAdd);
    fused.inputs[0] = operands[0], fused.inputs[1] = operands[1], fused.inputs[2] = operands[2];
    return fused;
}

} // namespace detail

// Every add or subtract of analysed graph `g` outside the feedback cycles and their sums whose operand is a product
// nothing else reads, laid out and rounded as the sum, fused into a multiply-add where machine `m` has one; `g` as it was
// where none is. A subtracted side is negated (`detail::product_added`), a `_neg` standing right before the multiply-add.
// In place, so that `rewritten` holds no second graph (an unoptimized build's frames hold every temporary).
constexpr void fuse_product_adds(Graph& g, const Machine& m) {
    const loops::graph::Facts facts = loops::graph::facts_of(g);
    const loops::graph::Feedback feedback = loops::graph::find_feedback(g, facts);
    if (!m.fma || !facts.why_not.empty() || !feedback.why_not.empty()) {
        return; // (where the graph is refused, the planner refuses it by name)
    }
    const std::vector<std::int32_t> readers = detail::readers_of(g);
    const std::vector<char> theirs = detail::in_feedback_sums(g, facts, feedback);
    const std::vector<char> timeless = loops::graph::timeless_of(g);
    // The sum's operand `k` as the product to fuse: its node, or -1.
    const auto product_at = [&](const Node& sum, std::size_t k) {
        const std::int32_t v = sum.inputs[k];
        const std::int32_t p = facts.producer[static_cast<std::size_t>(v)];
        const bool fuses = p >= 0 && feedback.cycle[static_cast<std::size_t>(p)] < 0 && readers[static_cast<std::size_t>(v)] == 1 &&
                           detail::algebra_of(g.nodes[static_cast<std::size_t>(p)]) == Algebra::Product && detail::alike(g, v, sum.output);
        return fuses ? p : -1;
    };
    std::vector<Value> values(g.values.begin(), g.values.end());
    std::vector<std::vector<Node>> ahead(g.nodes.size()); // per node, the negations put right before it
    std::vector<Node> nodes(g.nodes.begin(), g.nodes.end());
    std::vector<char> gone(g.nodes.size(), 0);
    bool any = false;
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        const Node& n = g.nodes[i];
        const Algebra role = detail::algebra_of(n);
        if (feedback.cycle[i] >= 0 || theirs[i] || (role != Algebra::Add && role != Algebra::Subtract) ||
            !numerics::grants(g.values[static_cast<std::size_t>(n.output)].precision).reorder) {
            continue;
        }
        const std::size_t k = product_at(n, 1) >= 0 ? 1 : 0;
        const std::int32_t p = product_at(n, k);
        const std::optional<std::int32_t> minus = p < 0 ? std::nullopt : detail::negated_at(g, timeless, n, g.nodes[static_cast<std::size_t>(p)], k);
        if (!minus) {
            continue;
        }
        nodes[i] = detail::product_added(g, n, g.nodes[static_cast<std::size_t>(p)], k, *minus, values, ahead[i]);
        gone[static_cast<std::size_t>(p)] = 1;
        any = true;
    }
    std::vector<Node> kept{};
    for (std::size_t i = 0; any && i < nodes.size(); ++i) {
        kept.insert(kept.end(), ahead[i].begin(), ahead[i].end());
        if (!gone[i]) {
            kept.push_back(nodes[i]);
        }
    }
    if (std::optional<Graph> fused = any ? detail::with(g, kept, values) : std::nullopt) {
        g = *fused;
    }
}

// The same, as a value.
constexpr Graph fused_product_adds(const Graph& g, const Machine& m) {
    Graph out = g;
    rewrite::fuse_product_adds(out, m);
    return out;
}

} // namespace heinzel::rewrite
