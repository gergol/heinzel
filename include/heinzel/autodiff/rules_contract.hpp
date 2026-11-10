#pragma once

// The adjoint of a contraction (`_contr`). Its operands broadcast into a joined shape, their product is
// formed at every joined position and the named axes are summed away: `Y = reduce_sum(A ⊙ B, axes=S)`.
// Its adjoint is the composition of the broadcast's, the product's and the sum's, so this file writes no
// arithmetic of its own. A batched or transposing contraction is `_matmul`, a separate operation.

#include <cstdint>

#include "heinzel/autodiff/emit.hpp"
#include "heinzel/autodiff/engine.hpp"
#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/vocab.hpp"

namespace heinzel::autodiff {

// The shape the two operands broadcast into before the sum — numpy's rule, right-aligned, taking the
// non-one extent of each aligned pair. The primal already inferred, so the operands ARE broadcast
// compatible and this cannot disagree with the shape rule's own `broadcast`; it recomputes J from the
// primal operand shapes (adjoint nodes are shapeless until the final infer, so it reads no appended
// node). `adjoint_contract` refuses a symbolic summed extent, so every summed extent here is literal.
constexpr Shape joined_shape(const Shape& a, const Shape& b) {
    Shape out{};
    out.rank = a.rank > b.rank ? a.rank : b.rank;
    for (std::int32_t k = 0; k < out.rank; ++k) {
        const std::int32_t ia = a.rank - out.rank + k; // < 0 ⇒ this operand has no such axis (extent 1)
        const std::int32_t ib = b.rank - out.rank + k;
        const Extent ea = ia >= 0 ? extent_at(a, ia) : literal_extent(1);
        const Extent eb = ib >= 0 ? extent_at(b, ib) : literal_extent(1);
        set_extent(out, k, same_extent(ea, literal_extent(1)) ? eb : ea);
    }
    return out;
}

// One contraction node's adjoint:
//
//   let  J = broadcast(shape(A), shape(B))                 the shape the product lives in
//        P = A ⊙ B            (shape J)                    the primal product, summed to Y
//        Y = reduce_sum(P, S) (shape J with S removed)
//   then dP = the sum's adjoint of dY  = dY lifted back over S to J  (`bcast_back`)
//        dA = unbroadcast( dP ⊙ B , J → shape(A) )         the product's adjoint w.r.t. A
//        dB = unbroadcast( dP ⊙ A , J → shape(B) )                                    w.r.t. B
//
// For `x@w` built as a contraction (`x` as `[.,K,1]`, `w` as `[.,K,N]` broadcast, S the K axis), `dA`
// reduces to `dY·Wᵀ` and `dB` to `Xᵀ·dY`. An operand that is extent-1 along a summed axis is handled by
// the same two lines: `bcast_back` materialises `dP` at the full joined extent and the unbroadcast sums
// it back. A contraction always has two operands, so both inputs are present.
constexpr void adjoint_contract(AdState& s, const Node& n, std::int32_t dY) {
    Graph& g = s.g;
    const ::heinzel::numerics::Precision ns = g.values[static_cast<std::size_t>(n.output)].precision;
    const std::int32_t ia = n.inputs[0];
    const std::int32_t ib = n.inputs[1];
    const Shape sa = g.values[static_cast<std::size_t>(ia)].shape;
    const Shape sb = g.values[static_cast<std::size_t>(ib)].shape;
    const Shape joined = joined_shape(sa, sb);
    const ElemType type = g.values[static_cast<std::size_t>(n.output)].type;

    // A summed extent must be literal to expand `dP` back over it. A symbolic one is refused by name
    // rather than given a guessed size.
    for (std::int32_t k = 0; k < n.attr.contract.axes.rank; ++k) {
        if (is_symbol(extent_at(joined, n.attr.contract.axes.axes[k]))) {
            fail("heinzel::gradients: a contraction over a symbolic summed extent is not differentiated yet");
        }
    }

    // dP: the summed axes re-inserted and the cotangent expanded to the joined extent — a reduce-sum's
    // adjoint, reached with the contraction's own axes (keep_axes false: the sum dropped them).
    const ReduceAttr spread{ReduceOp::Sum, n.attr.contract.axes, false};
    const std::int32_t dP = bcast_back(g, ns, dY, spread, joined, type);

    if (s.act.varied[static_cast<std::size_t>(ia)]) {
        accum_same(s, ia, unbroadcast(g, ns, emit_mul(g, ns, dP, ib), joined, sa));
    }
    if (s.act.varied[static_cast<std::size_t>(ib)]) {
        accum_same(s, ib, unbroadcast(g, ns, emit_mul(g, ns, dP, ia), joined, sb));
    }
}

} // namespace heinzel::autodiff
