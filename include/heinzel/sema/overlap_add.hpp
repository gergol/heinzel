#pragma once

// The shape an `_ovadd` makes, the inverse of an `_unfold`'s, and its refusals: the two innermost element axes
// `[.., F, K]` (F frames of K taps) become one axis of length `(F-1)*hop + window`, the streamed axis kept innermost.
// The length is derived, never carried by the attribute, so nothing can disagree with it. Unlike `_fft`'s, the
// refusals run on a possibly symbolic operand: a frame count from an `_unfold` resolves through the solver, so one that
// never resolved is still symbolic and is refused by name, as an unfold refuses an unbound axis.

#include <cstdint>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/vocab.hpp"
#include "heinzel/sema/refusal.hpp"

namespace heinzel {

// The output length an overlap-add reconstructs from F frames of `window` taps hopped by `hop`:
// `(F-1)*hop + window`, the last frame's far edge.
constexpr std::int32_t overlap_add_length(std::int32_t frames, std::int32_t window, std::int32_t hop) {
    return (frames - 1) * hop + window;
}

namespace detail {

// Axes `[.., F, K, streamed]` become `[.., out_len, streamed]`, every outer axis and the streamed axis passing through.
// A window and a hop below one are not a frame and a stride. The operand needs the three axes it folds and keeps.
// The window axis (just inside the streamed one) is the tap count and must equal `window`; the frame axis (just
// outside it) supplies the output length, so a symbolic one that never resolved has no literal to derive from.
constexpr Shape overlap_add_shape(const Shape& in, OpCode op, const OverlapAddAttr& v, Diagnostic* why = nullptr) {
    if (v.window < 1 || v.hop < 1) {
        return refused(in, why, op, " windows ", v.window, " every ", v.hop, ": a window and a hop are each at least one");
    }
    if (in.rank < 3) {
        return refused(in, why, op, " folds a value of rank ", in.rank,
                       "; it needs a frame axis, a window axis and the streamed axis");
    }
    const Extent taps = extent_at(in, in.rank - 2);   // K, the innermost element axis
    const Extent frames = extent_at(in, in.rank - 3); // F, the axis just outside it
    if (is_symbol(taps)) {
        return refused(in, why, op, " window axis size is not known yet; bind it before the overlap-add");
    }
    if (literal_of(taps) != v.window) {
        return refused(in, why, op, " declares window ", v.window, " but its innermost axis is ", literal_of(taps));
    }
    if (is_symbol(frames)) {
        return refused(in, why, op, " frame count is not known yet; bind it or derive it from an _unfold");
    }
    Shape shape{};
    for (std::int32_t k = 0; k < in.rank - 3; ++k) { // the outer element axes, unchanged
        set_extent(shape, shape.rank++, extent_at(in, k));
    }
    set_extent(shape, shape.rank++, literal_extent(overlap_add_length(literal_of(frames), v.window, v.hop)));
    set_extent(shape, shape.rank++, extent_at(in, in.rank - 1)); // the streamed axis stays innermost
    return shape;
}

} // namespace detail

} // namespace heinzel
