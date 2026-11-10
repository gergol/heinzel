#pragma once

// The adjoint of every structural rearrangement. A view moves elements without arithmetic, so its
// adjoint moves the cotangent back by the inverse rearrangement: each contribution is already in the
// operand's shape and is summed in with `accum_same`, never unbroadcast. The views are grouped by the
// shape of their adjoint (reindex, resize, join, framing); what no group covers — a strided slice, a
// 2-D or non-innermost unfold, a symbolic extent — is a named failure, not a wrong gradient.

#include <cstdint>

#include "heinzel/autodiff/emit.hpp"
#include "heinzel/autodiff/engine.hpp"
#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/vocab.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel::autodiff {

// Transpose / Reshape / Squeeze / Unsqueeze / Flip — the reindexing views, whose adjoint is the
// inverse read. Every operand element maps to exactly one output element, so the inverse maps every
// output cotangent back to exactly one operand element: a bijection, conserving the cotangent.
constexpr bool adjoint_reindex(AdState& s, const Node& n, std::int32_t dY) {
    Graph& g = s.g;
    const ::heinzel::numerics::Precision ns = g.values[static_cast<std::size_t>(n.output)].precision;
    const std::int32_t ia = n.inputs[0];
    const ViewAttr& v = n.attr.view;
    if (n.op == op::Transpose) { // the inverse permutation: output axis order[k] came from k
        Axes inv{};
        inv.rank = v.order.rank;
        for (std::int32_t k = 0; k < v.order.rank; ++k) {
            inv.axes[v.order.axes[k]] = k;
        }
        accum_same(s, ia, emit_transpose(g, ns, dY, inv));
    } else if (n.op == op::Flip) { // self-adjoint: reverse the reversed cotangent
        accum_same(s, ia, emit_flip(g, ns, dY, v.axis));
    } else if (n.op == op::Reshape && v.kind == ViewKind::Reshape) { // re-cut back to the operand shape
        accum_same(s, ia, emit_reshape_to(g, ns, dY, g.values[static_cast<std::size_t>(ia)].shape));
    } else if (n.op == op::Reshape && v.kind == ViewKind::Squeeze) { // squeeze's adjoint reinserts it
        accum_same(s, ia, emit_unsqueeze(g, ns, dY, v.axis));
    } else if (n.op == op::Reshape && v.kind == ViewKind::Unsqueeze) { // unsqueeze's adjoint drops it
        accum_same(s, ia, emit_squeeze(g, ns, dY, v.axis));
    } else {
        return false;
    }
    return true;
}

// Slice / Pad — the resizing views, adjoint to each other. A slice keeps `length` elements of an axis
// of extent L; the elements it dropped have zero cotangent, so its adjoint PADS the cotangent back to
// L with a zero fill (`before = offset`, `after = L - offset - length`). A pad grows an axis of extent
// L by constant margins; the margins came from no operand element, so its adjoint SLICES the body back
// out (`offset = before`, `length = L`). L is the operand's extent along the axis; `literal_of`
// refuses a symbolic one by name rather than guessing.
constexpr bool adjoint_resize(AdState& s, const Node& n, std::int32_t dY) {
    Graph& g = s.g;
    const ::heinzel::numerics::Precision ns = g.values[static_cast<std::size_t>(n.output)].precision;
    const std::int32_t ia = n.inputs[0];
    const ViewAttr& v = n.attr.view;
    const std::int32_t axis = v.axis;
    const std::int32_t L = literal_of(extent_at(g.values[static_cast<std::size_t>(ia)].shape, axis));
    if (n.op == op::Slice) {
        if (v.step != 1) {
            fail("heinzel::gradients: a strided slice (step > 1) is excluded from differentiation "
                 "(exclusion E5); the flip workaround differentiates, a stride does not");
        }
        accum_same(s, ia, emit_pad(g, ns, dY, axis, v.offset, L - v.offset - v.length, 0.0));
    } else if (n.op == op::Pad) {
        accum_same(s, ia, emit_slice(g, ns, dY, axis, v.pad_before, L, 1));
    } else {
        return false;
    }
    return true;
}

// Concat — the joining view. `out = [a; b]` along `axis`, so `dY`'s lower `extent_a` rows are a's
// cotangent and the upper `extent_b` are b's: two slices at the split, each operand's own extent giving
// its slice length (equal operands, a planar pack, need nothing special).
constexpr bool adjoint_join(AdState& s, const Node& n, std::int32_t dY) {
    Graph& g = s.g;
    const ::heinzel::numerics::Precision ns = g.values[static_cast<std::size_t>(n.output)].precision;
    const std::int32_t ia = n.inputs[0];
    const std::int32_t ib = n.inputs[1];
    const ViewAttr& v = n.attr.view;
    const std::int32_t axis = v.axis;
    if (n.op != op::Concat) {
        return false;
    }
    const std::int32_t ea = literal_of(extent_at(g.values[static_cast<std::size_t>(ia)].shape, axis));
    const std::int32_t eb = literal_of(extent_at(g.values[static_cast<std::size_t>(ib)].shape, axis));
    accum_same(s, ia, emit_slice(g, ns, dY, axis, 0, ea, 1));
    accum_same(s, ib, emit_slice(g, ns, dY, axis, ea, eb, 1));
    return true;
}

// Unfold — the framing view, adjoint to overlap-add. An unfold reads source position `f*hop + k` into
// frame f, tap k, so its adjoint scatters each frame's cotangent back to that position and SUMS where
// windows overlap: an `_ovadd` of the SAME window and hop. Overlap-add reconstructs the two INNERMOST
// element axes `[.., F, K]` into one, so this reverses only an unfold that framed the last element axis
// (`axis == R - 2`, with the streamed axis at `R - 1`); a non-innermost or a 2-D unfold lays its
// frames elsewhere and is refused by name. The overlap-add rebuilds `(F-1)*hop + window`; when the
// unfold's floor-division dropped a tail (that below the source extent L), those source elements were
// read by no frame and carry zero cotangent — a zero PAD restores them to L. `literal_of` refuses a
// symbolic L by name, as in `adjoint_resize`.
constexpr bool adjoint_frame(AdState& s, const Node& n, std::int32_t dY) {
    if (n.op != op::Unfold && n.op != op::Unfold2d) {
        return false;
    }
    Graph& g = s.g;
    const ::heinzel::numerics::Precision ns = g.values[static_cast<std::size_t>(n.output)].precision;
    const std::int32_t ia = n.inputs[0];
    const ViewAttr& v = n.attr.view;
    const Shape& in = g.values[static_cast<std::size_t>(ia)].shape;
    if (n.op == op::Unfold2d || v.axis != in.rank - 2) {
        // Kept under kMaxMessageLength so the exclusion code is not truncated away.
        fail("heinzel::gradients: only an innermost 1-D unfold differentiates (exclusion E6, framing)");
    }
    const std::int32_t L = literal_of(extent_at(in, v.axis));
    const std::int32_t frames = (L - v.window) / v.hop + 1; // the floor count `_unfold` itself framed
    const std::int32_t out_len = (frames - 1) * v.hop + v.window; // what overlap-add reconstructs
    std::int32_t d = emit_ovadd(g, ns, dY, v.window, v.hop);
    if (out_len < L) {
        d = emit_pad(g, ns, d, v.axis, 0, L - out_len, 0.0); // the dropped tail: zero cotangent
    }
    accum_same(s, ia, d);
    return true;
}

// Overlap-add — the scatter view, adjoint to unfold. An overlap-add sums frame f, tap k into output
// position `f*hop + k`, so its adjoint reads each of those output cotangents back into the frame it came
// from: an `_unfold` of the reconstructed axis with the SAME window and hop. That axis is the result's
// innermost element axis (`R - 2`, streamed at `R - 1`), and the framing is exact — the reconstructed
// length `(F-1)*hop + window` re-frames to F frames with no tail — so no pad mirrors the unfold adjoint's.
// A symbolic reconstructed extent is refused by name, as in the sibling rules.
constexpr void adjoint_scatter(AdState& s, const Node& n, std::int32_t dY) {
    Graph& g = s.g;
    const ::heinzel::numerics::Precision ns = g.values[static_cast<std::size_t>(n.output)].precision;
    const std::int32_t ia = n.inputs[0];
    const OverlapAddAttr& o = n.attr.overlap;
    const Shape& out = g.values[static_cast<std::size_t>(n.output)].shape;
    if (is_symbol(extent_at(out, out.rank - 2))) {
        fail("heinzel::gradients: an overlap-add over a symbolic output length is not differentiated yet");
    }
    accum_same(s, ia, emit_unfold(g, ns, dY, out.rank - 2, o.window, o.hop));
}

// The structural dispatch. Every operation reaching here carries `AttrKind::View` and is classified
// `Differentiable`; the four families cover the reindex, resize, join and framing views. (`_ovadd`
// carries `AttrKind::OverlapAdd`, not a view, so `adjoint_of` routes it to `adjoint_scatter` directly.)
constexpr void adjoint_structural(AdState& s, const Node& n, std::int32_t dY) {
    if (adjoint_reindex(s, n, dY) || adjoint_resize(s, n, dY) || adjoint_join(s, n, dY) ||
        adjoint_frame(s, n, dY)) {
        return;
    }
    fail("heinzel::gradients: this view op has no adjoint family — every view op should match one");
}

} // namespace heinzel::autodiff
