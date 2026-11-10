#pragma once

// The reverse sweep's running state, and the one way a cotangent (the derivative of the loss with
// respect to a value) is accumulated into a value. Each node's adjoint rule reads the cotangent of its
// output and pushes a contribution to each operand; a value fed to several operations receives their
// SUM, an `_add` chain seeded by the first contribution.

#include <cstdint>

#include "heinzel/autodiff/activity.hpp"
#include "heinzel/autodiff/emit.hpp"
#include "heinzel/ir/ir.hpp"
#include "heinzel/support/fixed_vector.hpp"

namespace heinzel::autodiff {

// The sweep's state: the graph being augmented, each value's running cotangent, and the activity that
// says which values may receive one. `cot[v]` is the value id of `v`'s cotangent so far, or `kNoValue`
// before its first contribution: the augmented graph holds the adjoint nodes, and this only remembers
// where each value's running sum lives.
struct AdState {
    Graph& g;
    Activity act;
    fixed_vector<std::int32_t, kMaxValues> cot{};

    constexpr explicit AdState(Graph& graph, Activity a) : g(graph), act(a) {
        for (std::size_t i = 0; i < graph.values.size(); ++i) {
            cot.push_back(kNoValue);
        }
    }
};

// Add one contribution to a value's running cotangent. The accumulation `_add` carries no numeric
// tier of its own — it follows the graph default — because it may join contributions that came from
// primal nodes of different tiers, and a sum of them belongs to none.
constexpr void accum(AdState& s, std::int32_t v, std::int32_t contrib) {
    std::int32_t& slot = s.cot[static_cast<std::size_t>(v)];
    slot = (slot == kNoValue) ? contrib : emit_add(s.g, ::heinzel::numerics::Precision::Inherit, slot, contrib);
}

// An elementwise contribution: it has the primal output's shape and is unbroadcast to the operand's
// before being summed in. Skipped when the operand does not exist or does not vary (a constant has no
// gradient to receive).
constexpr void accum_ew(AdState& s, const Node& primal, std::int32_t operand, std::int32_t contrib) {
    if (operand == kNoValue || !s.act.varied[static_cast<std::size_t>(operand)]) {
        return;
    }
    const Shape& os = s.g.values[static_cast<std::size_t>(primal.output)].shape;
    const Shape& ts = s.g.values[static_cast<std::size_t>(operand)].shape;
    accum(s, operand, unbroadcast(s.g, s.g.values[static_cast<std::size_t>(primal.output)].precision, contrib, os, ts));
}

// A contribution already in the operand's shape (a structural or reduce rule built it that way).
constexpr void accum_same(AdState& s, std::int32_t operand, std::int32_t contrib) {
    if (operand == kNoValue || !s.act.varied[static_cast<std::size_t>(operand)]) {
        return;
    }
    accum(s, operand, contrib);
}

} // namespace heinzel::autodiff
