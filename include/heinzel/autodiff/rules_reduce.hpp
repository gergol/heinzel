#pragma once

// The adjoint of a reduction: a reduction collapses some axes of its operand into one value each, so
// its adjoint spreads each output cotangent back over the axis positions that folded into it.

#include <cstdint>

#include "heinzel/autodiff/emit.hpp"
#include "heinzel/autodiff/engine.hpp"
#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/vocab.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel::autodiff {

// How many operand elements folded into each reduced element — the product of the reduced axes'
// extents. Called only when every reduced axis is literal (see `reduced_extents_literal`); a symbolic
// one takes the run-time path in `mean_scaled` instead, so `literal_of` here never meets a symbol.
constexpr double reduced_count(const ReduceAttr& ra, const Shape& operand_shape) {
    std::int64_t n = 1;
    for (std::int32_t k = 0; k < ra.axes.rank; ++k) {
        const std::int32_t a = ra.axes.axes[k];
        const std::int32_t axis = a < 0 ? a + operand_shape.rank : a;
        n *= literal_of(extent_at(operand_shape, axis));
    }
    return static_cast<double>(n);
}

// Whether every reduced axis has a literal extent — then 1/N is known now, a compile-time constant.
// If any is symbolic, N is not known until the symbol is bound.
constexpr bool reduced_extents_literal(const ReduceAttr& ra, const Shape& operand_shape) {
    for (std::int32_t k = 0; k < ra.axes.rank; ++k) {
        const std::int32_t a = ra.axes.axes[k];
        const std::int32_t axis = a < 0 ? a + operand_shape.rank : a;
        if (is_symbol(extent_at(operand_shape, axis))) {
            return false;
        }
    }
    return true;
}

// A mean's cotangent scaled by 1/N. LITERAL N — a multiply by the constant reciprocal. SYMBOLIC N — a
// run-time scalar: sum a ones buffer of the operand's shape over the SAME reduced axes with the SAME
// `keep_axes`, giving a value of the cotangent's own shape whose every element is N, and divide the
// cotangent by it. Once the symbol is bound, the sum is exactly N, so the two branches agree.
constexpr std::int32_t mean_scaled(Graph& g, ::heinzel::numerics::Precision ns, std::int32_t dY, const ReduceAttr& ra,
                                   const Shape& in_s, ElemType type) {
    if (reduced_extents_literal(ra, in_s)) {
        return emit_mul(g, ns, dY, emit_const(g, 1.0 / reduced_count(ra, in_s), type));
    }
    OpAttr sum{};
    sum.kind = AttrKind::Reduce;
    sum.reduce.op = ReduceOp::Sum;
    sum.reduce.axes = ra.axes;
    sum.reduce.keep_axes = ra.keep_axes;
    const std::int32_t n_of = emit(g, op::Reduce, ns, emit_ones(g, in_s, type), kNoValue, kNoValue, sum);
    return emit_div(g, ns, dY, n_of);
}

// SUM.  Every operand element contributed its own value once, so every one receives the same
// cotangent: `bcast_back` re-inserts the reduced axes and expands the cotangent to the operand's
// shape. MEAN.  A mean is a sum divided by the number of elements reduced, so its adjoint is the sum's
// scaled by `1/n` before it is spread.
// MAX / MIN.  Only the operand elements equal to the reduced result contributed to it, so only they
// receive the cotangent — the equal-mask subgradient, with ties receiving it duplicated (numpy
// autograd's convention). The mask is `operand == result`, the result broadcast back over the reduced
// axes; where it holds, the spread cotangent flows, elsewhere zero. PROD.  Each element's derivative
// is the product of the others, i.e. `result / element`, so the adjoint is `bcast(dY·result) /
// operand` — NaN/Inf where an operand element is zero (a known caveat).
constexpr void adjoint_reduce(AdState& s, const Node& n, std::int32_t dY) {
    const std::int32_t ia = n.inputs[0];
    if (!s.act.varied[static_cast<std::size_t>(ia)]) {
        return;
    }
    Graph& g = s.g;
    const ::heinzel::numerics::Precision ns = g.values[static_cast<std::size_t>(n.output)].precision;
    const ReduceAttr& ra = n.attr.reduce;
    const Value& in = g.values[static_cast<std::size_t>(ia)];
    const Shape& in_s = in.shape;
    if (ra.op == ReduceOp::Sum) {
        accum_same(s, ia, bcast_back(g, ns, dY, ra, in_s, in.type));
    } else if (ra.op == ReduceOp::Mean) {
        accum_same(s, ia, bcast_back(g, ns, mean_scaled(g, ns, dY, ra, in_s, in.type), ra, in_s, in.type));
    } else if (ra.op == ReduceOp::Max || ra.op == ReduceOp::Min) {
        const std::int32_t out = n.output;
        const std::int32_t mask = emit_eq(g, ns, ia, bcast_back(g, ns, out, ra, in_s, in.type));
        const std::int32_t spread = bcast_back(g, ns, dY, ra, in_s, in.type);
        accum_same(s, ia, emit_select(g, ns, mask, spread, emit_const(g, 0.0, in.type)));
    } else if (ra.op == ReduceOp::Prod) {
        const std::int32_t out = n.output;
        const std::int32_t t = bcast_back(g, ns, emit_mul(g, ns, dY, out), ra, in_s, in.type);
        accum_same(s, ia, emit_div(g, ns, t, ia));
    } else {
        fail("heinzel::gradients: unhandled reduce op in the adjoint dispatch (bug)");
    }
}

} // namespace heinzel::autodiff
