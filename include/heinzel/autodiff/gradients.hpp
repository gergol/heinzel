#pragma once

// `gradients(g, loss, {x, …})`: reverse-mode automatic differentiation as a Graph→Graph transform. It
// appends the nodes that compute the gradient of `loss` with respect to each target and returns their
// value ids. The result is an ordinary graph, compiled and run like a hand-written one, and the ids are
// ordinary values to keep building with (an optimizer step `w ← w − lr·dw`). The adjoint nodes it
// appends are not themselves differentiated. See `docs/decisions.md`, Decision 14.

#include <cstdint>
#include <initializer_list>
#include <string_view>

#include "heinzel/autodiff/activity.hpp"
#include "heinzel/autodiff/emit.hpp"
#include "heinzel/autodiff/engine.hpp"
#include "heinzel/autodiff/rules_contract.hpp"
#include "heinzel/autodiff/rules_elementwise.hpp"
#include "heinzel/autodiff/rules_reduce.hpp"
#include "heinzel/autodiff/rules_structural.hpp"
#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/ir/vocab.hpp"
#include "heinzel/sema/analyze.hpp"
#include "heinzel/support/fail.hpp"
#include "heinzel/support/fixed_vector.hpp"

namespace heinzel::autodiff {

// The graph can be differentiated. The loss must be one number per sample (otherwise its gradient is
// ambiguous), and every target must be an `Input`, which is what a learnable value is. Each failure names
// the mistake.
constexpr void validate_gradients(const Graph& g, std::int32_t loss, std::initializer_list<std::int32_t> wrt) {
    if (numbers_per_sample(g.values[static_cast<std::size_t>(loss)].shape) != 1) {
        fail("heinzel::gradients: the loss must be one number per sample — its gradient is otherwise "
             "ambiguous about which output number is meant");
    }
    for (const std::int32_t w : wrt) {
        if (g.values[static_cast<std::size_t>(w)].category != ValueCategory::Input) {
            fail("heinzel::gradients: a differentiation target must be an Input");
        }
    }
}

// The refusal for an excluded operation reached on the differentiated path, naming the operation. It
// fires only where a cotangent has reached the operation: the driver skips a node whose output carries
// none, so the same operation upstream and off the path differentiates fine. `_matmul` is said as the
// einsum it was written as: one whose operands are not lined up label for label (`"ij,j->i"`, `"ik,kj->ij"`).
// An einsum whose operands carry the same labels in the same order is elementwise, a product and a sum, and
// differentiates.
[[noreturn]] constexpr void fail_excluded(OpCode code) {
    if (code == op::MatMul) {
        fail("heinzel::gradients: a matrix-product einsum (not elementwise) has no adjoint (excluded)");
    }
    MessageWriter m;
    m << std::string_view{"heinzel::gradients: "};
    write_op(m, code);
    m << std::string_view{" on the differentiated path has no adjoint (excluded)"};
    fail(m.problem().message.view());
}

// One primal node's adjoint, routed by its class and, for a differentiable one, by its attribute kind:
// `None` and `Cast` are elementwise (a cast's adjoint casts the cotangent back), `Reduce` folds, `View`
// rearranges, `Contract` sums a product, `OverlapAdd` re-frames its scatter into an unfold. The spectral
// adjoint is not written, since `_fft` has no arithmetic yet; reaching one fails rather than giving a
// wrong gradient.
constexpr void adjoint_of(AdState& s, const Node& n, std::int32_t dY) {
    switch (rules_of(n.op).adjoint) {
    case AdjointClass::ZeroDerivative:
        return; // the derivative is genuinely zero — the cotangent stops here, correctly
    case AdjointClass::Unclassified: // (no row is: the table's coverage check)
    case AdjointClass::Excluded:
        fail_excluded(n.op);
    case AdjointClass::Differentiable:
        break;
    }
    if (n.attr.kind == AttrKind::None || n.attr.kind == AttrKind::Cast) {
        adjoint_elementwise(s, n, dY);
    } else if (n.attr.kind == AttrKind::Reduce) {
        adjoint_reduce(s, n, dY);
    } else if (n.attr.kind == AttrKind::View) {
        adjoint_structural(s, n, dY);
    } else if (n.attr.kind == AttrKind::Contract) {
        adjoint_contract(s, n, dY);
    } else if (n.attr.kind == AttrKind::OverlapAdd) {
        adjoint_scatter(s, n, dY);
    } else {
        fail("heinzel::gradients: the spectral adjoint (fft) is not written yet; nothing computes an fft yet");
    }
}

constexpr fixed_vector<std::int32_t, kMaxIO> gradients(Graph& g, std::int32_t loss,
                                                       std::initializer_list<std::int32_t> wrt) {
    if (const Diagnostic d = infer(g); !d.ok) {
        fail("heinzel::gradients: the graph did not infer before differentiation");
    }
    validate_gradients(g, loss, wrt);

    AdState s(g, analyze_activity(g, loss, wrt.begin(), static_cast<std::int32_t>(wrt.size())));
    const Value& loss_v = g.values[static_cast<std::size_t>(loss)];
    s.cot[static_cast<std::size_t>(loss)] = emit_ones(g, loss_v.shape, loss_v.type);

    // The primal nodes only — the adjoint nodes appended below are not differentiated.
    for (std::int32_t i = static_cast<std::int32_t>(g.nodes.size()) - 1; i >= 0; --i) {
        const Node node = g.nodes[static_cast<std::size_t>(i)];
        const std::int32_t dY = s.cot[static_cast<std::size_t>(node.output)];
        if (dY == kNoValue) {
            continue; // no cotangent reached this node — it does not affect the loss
        }
        adjoint_of(s, node, dY);
    }

    fixed_vector<std::int32_t, kMaxIO> grads{};
    for (const std::int32_t w : wrt) {
        if (s.cot[static_cast<std::size_t>(w)] == kNoValue) {
            // The loss does not depend on this target: its gradient is a materialised zero of its
            // own shape and type, not a missing value.
            const Value& v = g.values[static_cast<std::size_t>(w)];
            s.cot[static_cast<std::size_t>(w)] = push_value(g, Value{v.type, v.shape, ValueCategory::Const, 0.0});
        }
        grads.push_back(s.cot[static_cast<std::size_t>(w)]);
    }

    if (const Diagnostic d = infer(g); !d.ok) {
        MessageWriter m;
        m << std::string_view{"heinzel::gradients augmented-graph infer: "};
        m << d.message.view();
        fail(m.problem().message.view());
    }
    return grads;
}

} // namespace heinzel::autodiff
