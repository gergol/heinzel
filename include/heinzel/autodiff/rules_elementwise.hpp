#pragma once

// The adjoint of every elementwise operation: the chain rule per element, `dY · ∂out/∂operand`, built as
// nodes and handed to `accum_ew`, which unbroadcasts it to the operand's shape. Where a rule needs the
// primal output it reads `n.output` rather than recomputing it. The rules are grouped by family; each
// family returns whether it handled the operation, and `adjoint_elementwise` tries them in turn.

#include <cstdint>

#include "heinzel/autodiff/emit.hpp"
#include "heinzel/autodiff/engine.hpp"
#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/ir/vocab.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel::autodiff {

// Add/Sub/Mul/Div/Neg/Abs/Fma/MultiplyAdd/Cast/Realize — the operations whose derivative is itself arithmetic.
constexpr bool adjoint_arithmetic(AdState& s, const Node& n, std::int32_t dY) {
    Graph& g = s.g;
    const ::heinzel::numerics::Precision ns = g.values[static_cast<std::size_t>(n.output)].precision;
    const std::int32_t ia = n.inputs[0];
    const std::int32_t ib = n.inputs[1];
    const std::int32_t ic = n.inputs[2];
    const std::int32_t out = n.output;
    if (n.op == op::Add) {
        accum_ew(s, n, ia, dY);
        accum_ew(s, n, ib, dY);
    } else if (n.op == op::Sub) {
        accum_ew(s, n, ia, dY);
        accum_ew(s, n, ib, emit_neg(g, ns, dY));
    } else if (n.op == op::Mul) {
        accum_ew(s, n, ia, emit_mul(g, ns, dY, ib));
        accum_ew(s, n, ib, emit_mul(g, ns, dY, ia));
    } else if (n.op == op::Div) { // d/db (a/b) = -a/b² = -(out/b)
        accum_ew(s, n, ia, emit_div(g, ns, dY, ib));
        accum_ew(s, n, ib, emit_neg(g, ns, emit_mul(g, ns, dY, emit_div(g, ns, out, ib))));
    } else if (n.op == op::Neg) {
        accum_ew(s, n, ia, emit_neg(g, ns, dY));
    } else if (n.op == op::Abs) { // dY · sign(x): 0 at 0, as sign(0) is (PyTorch's)
        accum_ew(s, n, ia, emit_mul(g, ns, dY, emit(g, op::Sign, ns, ia)));
    } else if (n.op == op::Fma || n.op == op::MultiplyAdd) { // a*b + c, rounded either way
        accum_ew(s, n, ia, emit_mul(g, ns, dY, ib));
        accum_ew(s, n, ib, emit_mul(g, ns, dY, ia));
        accum_ew(s, n, ic, dY);
    } else if (n.op == op::Cast) { // the cotangent casts back to the operand's type
        OpAttr a{};
        a.kind = AttrKind::Cast;
        a.cast.to = g.values[static_cast<std::size_t>(ia)].type;
        accum_ew(s, n, ia, emit(g, op::Cast, ns, dY, kNoValue, kNoValue, a));
    } else if (n.op == op::Realize) { // the identity: the cotangent passes straight through
        accum_same(s, ia, dY);
    } else {
        return false;
    }
    return true;
}

// Sqrt/Rsqrt/Exp/Exp2/Log/Log2/Log10 — roots, exponentials, logarithms.
constexpr bool adjoint_exp_log(AdState& s, const Node& n, std::int32_t dY) {
    Graph& g = s.g;
    const ::heinzel::numerics::Precision ns = g.values[static_cast<std::size_t>(n.output)].precision;
    const std::int32_t ia = n.inputs[0];
    const std::int32_t out = n.output;
    const ElemType t = g.values[static_cast<std::size_t>(out)].type;
    if (n.op == op::Sqrt) { // ½/√x = ½/out
        accum_ew(s, n, ia, emit_mul(g, ns, dY, emit_div(g, ns, emit_const(g, 0.5, t), out)));
    } else if (n.op == op::Rsqrt) { // -½ · out³
        const std::int32_t out3 = emit_mul(g, ns, out, emit_mul(g, ns, out, out));
        accum_ew(s, n, ia, emit_mul(g, ns, emit_mul(g, ns, dY, emit_const(g, -0.5, t)), out3));
    } else if (n.op == op::Exp) {
        accum_ew(s, n, ia, emit_mul(g, ns, dY, out));
    } else if (n.op == op::Exp2) { // 2^x · ln2
        accum_ew(s, n, ia, emit_mul(g, ns, emit_mul(g, ns, dY, out), emit_const(g, 0.6931471805599453, t)));
    } else if (n.op == op::Log) {
        accum_ew(s, n, ia, emit_div(g, ns, dY, ia));
    } else if (n.op == op::Log2) {
        accum_ew(s, n, ia, emit_div(g, ns, dY, emit_mul(g, ns, ia, emit_const(g, 0.6931471805599453, t))));
    } else if (n.op == op::Log10) {
        accum_ew(s, n, ia, emit_div(g, ns, dY, emit_mul(g, ns, ia, emit_const(g, 2.302585092994046, t))));
    } else {
        return false;
    }
    return true;
}

// Sin/Cos/Tan/Atan/Asin/Acos/Tanh/SinPi — the one-operand circular and hyperbolic functions.
constexpr bool adjoint_trig(AdState& s, const Node& n, std::int32_t dY) {
    Graph& g = s.g;
    const ::heinzel::numerics::Precision ns = g.values[static_cast<std::size_t>(n.output)].precision;
    const std::int32_t ia = n.inputs[0];
    const std::int32_t out = n.output;
    const ElemType t = g.values[static_cast<std::size_t>(out)].type;
    const auto one = [&] { return emit_const(g, 1.0, t); };
    const auto asq = [&] { return emit_mul(g, ns, ia, ia); };
    if (n.op == op::Sin) {
        accum_ew(s, n, ia, emit_mul(g, ns, dY, emit(g, op::Cos, ns, ia)));
    } else if (n.op == op::Cos) {
        accum_ew(s, n, ia, emit_neg(g, ns, emit_mul(g, ns, dY, emit(g, op::Sin, ns, ia))));
    } else if (n.op == op::Tan) { // 1 + tan²x = 1 + out²
        accum_ew(s, n, ia, emit_mul(g, ns, dY, emit_add(g, ns, one(), emit_mul(g, ns, out, out))));
    } else if (n.op == op::Atan) { // 1/(1 + x²)
        accum_ew(s, n, ia, emit_div(g, ns, dY, emit_add(g, ns, one(), asq())));
    } else if (n.op == op::Asin) { // 1/√(1 - x²)
        accum_ew(s, n, ia, emit_mul(g, ns, dY, emit(g, op::Rsqrt, ns, emit_sub(g, ns, one(), asq()))));
    } else if (n.op == op::Acos) { // -1/√(1 - x²)
        accum_ew(s, n, ia, emit_neg(g, ns, emit_mul(g, ns, dY, emit(g, op::Rsqrt, ns, emit_sub(g, ns, one(), asq())))));
    } else if (n.op == op::Tanh) { // 1 - tanh²x = 1 - out²
        accum_ew(s, n, ia, emit_mul(g, ns, dY, emit_sub(g, ns, one(), emit_mul(g, ns, out, out))));
    } else if (n.op == op::SinPi) { // π·cos(πx) = π·sinpi(x + ½), there being no cospi op
        const std::int32_t sp = emit(g, op::SinPi, ns, emit_add(g, ns, ia, emit_const(g, 0.5, t)));
        accum_ew(s, n, ia, emit_mul(g, ns, emit_mul(g, ns, dY, emit_const(g, 3.141592653589793, t)), sp));
    } else {
        return false;
    }
    return true;
}

// Pow/Atan2 — the two-operand transcendentals, each carrying a cotangent to both operands.
constexpr bool adjoint_binary_transc(AdState& s, const Node& n, std::int32_t dY) {
    Graph& g = s.g;
    const ::heinzel::numerics::Precision ns = g.values[static_cast<std::size_t>(n.output)].precision;
    const std::int32_t ia = n.inputs[0];
    const std::int32_t ib = n.inputs[1];
    const std::int32_t out = n.output;
    const ElemType t = g.values[static_cast<std::size_t>(out)].type;
    if (n.op == op::Pow) { // ∂a: b·a^(b-1);  ∂b: out·ln(a), only when b varies
        const std::int32_t p = emit(g, op::Pow, ns, ia, emit_sub(g, ns, ib, emit_const(g, 1.0, t)));
        accum_ew(s, n, ia, emit_mul(g, ns, emit_mul(g, ns, dY, ib), p));
        if (ib != kNoValue && s.act.varied[static_cast<std::size_t>(ib)]) {
            accum_ew(s, n, ib, emit_mul(g, ns, emit_mul(g, ns, dY, out), emit(g, op::Log, ns, ia)));
        }
    } else if (n.op == op::Atan2) { // atan2(y=a, x=b); den = a² + b²
        const std::int32_t den = emit_add(g, ns, emit_mul(g, ns, ia, ia), emit_mul(g, ns, ib, ib));
        accum_ew(s, n, ia, emit_mul(g, ns, dY, emit_div(g, ns, ib, den)));
        accum_ew(s, n, ib, emit_neg(g, ns, emit_mul(g, ns, dY, emit_div(g, ns, ia, den))));
    } else {
        return false;
    }
    return true;
}

// Min/Max/Select — the operations that route each element's cotangent to ONE operand, chosen by a
// per-element mask, not by arithmetic. The mask must send a tie's cotangent where the forward sends the
// tie's value: `runtime/compute.hpp` writes `min` as `a<b?a:b` and `max` as `a>b?a:b`, so a tie goes to
// the SECOND operand, and the "a wins" mask is strict (`a<b` for min, `b<a` for max). The two masks
// partition (exactly one side per element), so the cotangent is conserved. Select routes by its
// condition, whose derivative is zero: each branch keeps only the elements it produced.
constexpr bool adjoint_masked(AdState& s, const Node& n, std::int32_t dY) {
    Graph& g = s.g;
    const ::heinzel::numerics::Precision ns = g.values[static_cast<std::size_t>(n.output)].precision;
    const std::int32_t ia = n.inputs[0];
    const std::int32_t ib = n.inputs[1];
    const std::int32_t ic = n.inputs[2];
    const std::int32_t out = n.output;
    const ElemType t = g.values[static_cast<std::size_t>(out)].type;
    if (n.op == op::Min) { // a wins iff a < b; a tie (a==b) goes to b, matching the forward
        const std::int32_t m = emit_lt(g, ns, ia, ib);
        const std::int32_t z = emit_const(g, 0.0, t);
        accum_ew(s, n, ia, emit_select(g, ns, m, dY, z));
        accum_ew(s, n, ib, emit_select(g, ns, m, z, dY));
    } else if (n.op == op::Max) { // a wins iff a > b, i.e. b < a; a tie goes to b
        const std::int32_t m = emit_lt(g, ns, ib, ia);
        const std::int32_t z = emit_const(g, 0.0, t);
        accum_ew(s, n, ia, emit_select(g, ns, m, dY, z));
        accum_ew(s, n, ib, emit_select(g, ns, m, z, dY));
    } else if (n.op == op::Select) { // the condition (first operand) is zero-derivative
        const std::int32_t z = emit_const(g, 0.0, t);
        accum_ew(s, n, ib, emit_select(g, ns, ia, dY, z));
        accum_ew(s, n, ic, emit_select(g, ns, ia, z, dY));
    } else {
        return false;
    }
    return true;
}

// The elementwise dispatch. Every operation reaching here is `AttrKind::None` and classified
// `Differentiable`; the families cover the smooth ones and the mask-routed ones. A zero-derivative
// operation contributes nothing (and is normally filtered by the driver before it arrives). Anything
// left is a differentiable elementwise operation whose rule is not written yet — a named failure.
constexpr void adjoint_elementwise(AdState& s, const Node& n, std::int32_t dY) {
    if (adjoint_arithmetic(s, n, dY) || adjoint_exp_log(s, n, dY) || adjoint_trig(s, n, dY) ||
        adjoint_binary_transc(s, n, dY) || adjoint_masked(s, n, dY)) {
        return;
    }
    if (rules_of(n.op).adjoint == AdjointClass::ZeroDerivative) {
        return;
    }
    fail("heinzel::gradients: this elementwise operation's adjoint is not written yet");
}

} // namespace heinzel::autodiff
