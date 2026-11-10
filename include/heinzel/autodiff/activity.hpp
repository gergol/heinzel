#pragma once

// Which values matter to a gradient, so the reverse sweep touches only those.

#include <cstdint>

#include "heinzel/ir/ir.hpp"
#include "heinzel/support/fixed_vector.hpp"

namespace heinzel::autodiff {

// The two activity masks, one bit per value id:
//
//   VARIED   the value depends on one of the `wrt` targets — it is the target itself, or something an
//            operation computed from a varied operand. A value that is not varied has a zero gradient
//            with respect to every target (nothing it is made of moves), so no cotangent is ever
//            pushed to it. This is what keeps the adjoint of `x * c` from trying to differentiate the
//            constant `c`.
//
//   USEFUL   the value affects the loss — it is the loss, or an operand of an operation whose output is
//            useful. Nothing outside this file reads it: the sweep skips any node whose output received
//            no cotangent, which keeps the same dead branches out of the gradient.
//
// Each is one pass, with no fixpoint: in the node list a node's operands always come before its output,
// so a forward pass settles `varied` and a backward pass settles `useful`.
struct Activity {
    fixed_vector<bool, kMaxValues> varied{};
    fixed_vector<bool, kMaxValues> useful{};
};

// `wrt` is the set of value ids the gradient is taken with respect to. `loss` is the value the
// gradient is of. Both masks are sized to the graph's current values (the primal ones — this runs
// before any adjoint node is appended).
constexpr Activity analyze_activity(const Graph& g, std::int32_t loss, const std::int32_t* wrt,
                                    std::int32_t wrt_count) {
    Activity a{};
    const auto n_values = static_cast<std::int32_t>(g.values.size());
    for (std::int32_t v = 0; v < n_values; ++v) {
        a.varied.push_back(false);
        a.useful.push_back(false);
    }

    for (std::int32_t k = 0; k < wrt_count; ++k) {
        a.varied[static_cast<std::size_t>(wrt[k])] = true;
    }
    // Forward: an output is varied if any operand is. Operands precede outputs, so one pass settles it.
    for (const Node& node : g.nodes) {
        bool any = false;
        for (const std::int32_t in : node.inputs) {
            if (in != kNoValue && a.varied[static_cast<std::size_t>(in)]) {
                any = true;
            }
        }
        if (any) {
            a.varied[static_cast<std::size_t>(node.output)] = true;
        }
    }

    a.useful[static_cast<std::size_t>(loss)] = true;
    // Backward: an operand is useful if the output it feeds is. Outputs follow operands, so walking
    // the nodes in reverse settles it in one pass.
    for (std::int32_t i = static_cast<std::int32_t>(g.nodes.size()) - 1; i >= 0; --i) {
        const Node& node = g.nodes[static_cast<std::size_t>(i)];
        if (!a.useful[static_cast<std::size_t>(node.output)]) {
            continue;
        }
        for (const std::int32_t in : node.inputs) {
            if (in != kNoValue) {
                a.useful[static_cast<std::size_t>(in)] = true;
            }
        }
    }
    return a;
}

} // namespace heinzel::autodiff
