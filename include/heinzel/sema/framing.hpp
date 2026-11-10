#pragma once

// The shape a windowed view (`_unfold`, `_unfld2d`) makes, its refusals, and the derived-extent relations it records:
// how a sliding window turns one source extent into a frame count and a window axis, literal now or derived from a
// symbol. Keyed on `ViewKind`, never on an operation code; a gate holds it to that.

#include <cstdint>

#include "heinzel/ir/ir.hpp"
#include "heinzel/sema/refusal.hpp"
#include "heinzel/sema/solver.hpp"

namespace heinzel {

namespace detail {

// The frame extent one windowed axis produces; the 1-D and 2-D windows both use it, so they apply the same rule. A
// literal source of extent L becomes the literal frame count `N_out = (L - window)/hop + 1`. A symbolic source has
// no literal to write, so the frame axis becomes the symbol the author declared (`frames`), and
// `record_derived_extent` records how it follows from the source; with no declared count there is nothing to fit the
// window against. The caller has checked `window`/`hop >= 1` and the axis.
//
// A window wider than its axis is refused, but the sweep makes a shape until the count truncates below one: up to
// `hop - 1` too wide, `(L - window)/hop` truncates to zero.
constexpr bool frame_axis(OpCode op, Extent along, std::int32_t axis, std::int32_t window, std::int32_t hop,
                          std::int32_t frames, Extent& out, Diagnostic* why) {
    if (is_symbol(along)) {
        if (frames < 0) {
            return report(why, op, " unfolds axis ", axis,
                          ", whose size is not known yet; bind it or declare its frame count");
        }
        out = Extent{Dim{frames}};
        return true;
    }
    const std::int32_t count = (literal_of(along) - window) / hop + 1;
    if (window > literal_of(along) && (why != nullptr || count < 1)) {
        return report(why, op, " windows ", window, " of an axis of extent ", literal_of(along));
    }
    out = literal_extent(count);
    return true;
}

// A sliding window `{axis, window, hop}`: axis `axis` of extent L becomes the frame count `(L - window)/hop + 1`, and a
// window axis of `window` elements appears at `axis + 1`, so the rank grows by one. A window or hop below one is not
// a frame or a stride.
//
// Unlike the other views, an unfold may take the streamed axis (`docs/decisions.md`, Decision 44): framing it changes
// the rate, and the layout is the reverse. The window becomes a static element axis at the old streamed position, and
// the frame count W becomes the new innermost extent, the block the graph now runs at, streaming at the hop rate: so
// `[.., N]` frames to `[.., window, W]`. Transposing, slicing or flipping the stream stay refused: they move or select
// samples rather than window them.
constexpr Shape unfold_shape(const Shape& in, OpCode op, const ViewAttr& v, Diagnostic* why = nullptr) {
    if (v.window < 1 || v.hop < 1) {
        return refused(in, why, op, " windows ", v.window, " every ", v.hop, ": a window and a hop are each at least one");
    }
    if (!has_axis(op, "unfolds", v.axis, in.rank, why)) {
        return in;
    }
    if (in.rank + 1 > kMaxRank) {
        return refused(in, why, op, " unfolds a value of rank ", in.rank, "; a window axis would exceed the rank ceiling");
    }
    Extent frame_extent{};
    if (!frame_axis(op, extent_at(in, v.axis), v.axis, v.window, v.hop, v.frames, frame_extent, why)) {
        return in;
    }
    Shape shape{};
    if (v.axis == in.rank - 1) {
        for (std::int32_t k = 0; k < in.rank - 1; ++k) {
            set_extent(shape, shape.rank++, extent_at(in, k));
        }
        set_extent(shape, shape.rank++, literal_extent(v.window)); // window: a static element axis, OUTER
        set_extent(shape, shape.rank++, frame_extent);             // W: the new innermost/block length
        return shape;
    }
    for (std::int32_t k = 0; k < in.rank; ++k) {
        set_extent(shape, shape.rank++, k == v.axis ? frame_extent : extent_at(in, k));
        if (k == v.axis) {
            set_extent(shape, shape.rank++, literal_extent(v.window));
        }
    }
    return shape;
}

// A 2-D sliding window: two distinct outer axes `axis < axis2` are each framed as `unfold_shape` frames its one, so
// axis `axis` of extent L1 becomes `N1 = (L1 - K1)/H1 + 1` frames with a window axis of `K1` beside it, and likewise
// `axis2`. The rank grows by two, and each window axis sits right after its frame axis:
// `[.., N1, K1, .., N2, K2, .., streamed]`. A literal and a symbolic axis may be mixed (a symbolic one takes the frame
// symbol `frames`/`frames2` declares). A 2-D unfold does not frame the stream.
constexpr Shape unfold2d_shape(const Shape& in, OpCode op, const ViewAttr& v, Diagnostic* why = nullptr) {
    if (v.window < 1 || v.hop < 1 || v.window2 < 1 || v.hop2 < 1) {
        return refused(in, why, op, " windows ", v.window, "/", v.window2,
                       ": a window and a hop are each at least one on both axes");
    }
    if (v.axis < 0 || v.axis >= v.axis2 || !is_element_axis(v.axis2, in.rank)) {
        return refused(in, why, op, " unfolds axes ", v.axis, " and ", v.axis2,
                       ": each an outer axis, and the second further out than the first");
    }
    if (in.rank + 2 > kMaxRank) {
        return refused(in, why, op, " unfolds a value of rank ", in.rank, "; two window axes would exceed the rank ceiling");
    }
    Extent f1{};
    Extent f2{};
    if (!frame_axis(op, extent_at(in, v.axis), v.axis, v.window, v.hop, v.frames, f1, why) ||
        !frame_axis(op, extent_at(in, v.axis2), v.axis2, v.window2, v.hop2, v.frames2, f2, why)) {
        return in;
    }
    Shape shape{};
    for (std::int32_t k = 0; k < in.rank; ++k) {
        set_extent(shape, shape.rank++, k == v.axis ? f1 : k == v.axis2 ? f2 : extent_at(in, k));
        if (k == v.axis) {
            set_extent(shape, shape.rank++, literal_extent(v.window));
        }
        if (k == v.axis2) {
            set_extent(shape, shape.rank++, literal_extent(v.window2));
        }
    }
    return shape;
}

// Record how one symbolic framed axis's frame count follows from its source:
// `N_out = (1*L + (hop - window)) / hop`, a floor. A length that does not divide evenly yields fewer
// frames, so no divisibility obligation is recorded. A literal axis, or a symbolic one with no declared
// frame count, records nothing. Returns false only when the relation contradicts one already recorded.
constexpr bool relate_framed_axis(const Extent& along, std::int32_t window, std::int32_t hop,
                                  std::int32_t frames, OpName at, ExtentSolver& axes,
                                  bool& asked_the_solver, Diagnostic& why) {
    if (frames < 0 || !is_symbol(along)) {
        return true;
    }
    asked_the_solver = true;
    return axes.relate(frames, symbol_of(along), 1, hop - window, hop, /*exact=*/false, at, why);
}

// A window over a symbolic axis records how its frame count is derived from that axis, so the count resolves the
// moment the axis is bound. It does so exactly when the window made its shape: then its rank grew, and a frame axis
// was placed. A window that could not is left for `check_attributes` to refuse by name.
constexpr bool record_derived_extent(const Shape& src, const Shape& shape, const ViewAttr& v, OpName at,
                                     ExtentSolver& axes, bool& asked_the_solver, Diagnostic& why) {
    if ((v.kind != ViewKind::Unfold && v.kind != ViewKind::Unfold2d) || shape.rank == src.rank) {
        return true;
    }
    if (!relate_framed_axis(extent_at(src, v.axis), v.window, v.hop, v.frames, at, axes, asked_the_solver, why)) {
        return false;
    }
    return v.kind == ViewKind::Unfold ||
           relate_framed_axis(extent_at(src, v.axis2), v.window2, v.hop2, v.frames2, at, axes, asked_the_solver, why);
}

} // namespace detail

} // namespace heinzel
