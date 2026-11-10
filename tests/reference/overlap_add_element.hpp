#pragma once

// The elements one element of an overlap-add's result is a sum of: the `ElementKind::Scatter` meaning of
// `_ovadd`, the adjoint of `_unfold`. It takes `[.., F, K]` back to `[.., out_len]` by placing frame f at
// offset `f*hop` and summing where windows overlap. Which frames reach output position t is a windowed,
// position-dependent set, not a collapse over named axes, so it answers a span of contributing frames and,
// per frame, the source element it reads.

#include <cstdint>

#include "heinzel/ir/ir.hpp"
#include "heinzel/support/fail.hpp"

#include "elements.hpp"

namespace heinzel {

// The frames that overlap one output element: the first of them and how many. `count` is zero where a hop
// larger than the window leaves a gap between frames — the output there is a sum of nothing, which is the
// zero an overlap-add reconstructs in a gap, not an error.
struct OverlapSpan {
    std::int32_t first_frame{0};
    std::int32_t count{0};
    constexpr bool operator==(const OverlapSpan&) const = default;
};

// Which frames overlap output element `e`. `to` is the overlap-add's element layout `[.., out_len]`,
// `from` the operand's `[.., F, K]` (F the frame axis just outside the streamed one, K the window axis
// innermost), `window`/`hop` the two fixed facts. Output position t along the reconstructed axis is
// covered by frame f exactly when `f*hop <= t <= f*hop + window - 1`, so the contributing frames are the
// integers in `[ceil((t-window+1)/hop), floor(t/hop)]` clamped to `[0, F)` — a run of at most
// `ceil(window/hop)` of them. A run visits only those, never all F, so the source elements read sum back
// to exactly the operand's F*K: linear in the numbers read rather than `out_len * F`.
//
// The refusals are the point: each is a mistake a hand-built node can carry that the analysis pass would
// have refused by name. The walk can be reached without the pass, so it refuses again rather than reading
// a plausible wrong element: a rank that did not fall by exactly one (an overlap-add collapses two axes
// into one); a window or hop below one; a result with no element axes; a window axis disagreeing with
// `window`.
constexpr OverlapSpan overlap_frames(const Elements& to, const Elements& from, std::int32_t window,
                                     std::int32_t hop, std::int32_t e) {
    if (from.rank != to.rank + 1 || from.full_rank != to.full_rank + 1) {
        fail("heinzel::overlap_frames: an overlap-add does not collapse exactly two axes into one");
    }
    if (window < 1 || hop < 1) {
        fail("heinzel::overlap_frames: an overlap-add with a window or hop below one");
    }
    if (to.rank < 1 || from.extent[from.rank - 1] != window) {
        fail("heinzel::overlap_frames: no reconstructed axis, or a window axis unlike the declared window");
    }
    if (e < 0 || e >= to.count) {
        fail("heinzel::overlap_frames: an element outside the result's own elements");
    }
    const std::int32_t t = e % to.extent[to.rank - 1]; // the index along the reconstructed axis
    const std::int32_t frames = from.extent[from.rank - 2];
    const std::int32_t need = t - window + 1;          // the first frame's offset must be at least this
    const std::int32_t lo = need <= 0 ? 0 : (need + hop - 1) / hop; // ceil(need/hop), need > 0
    std::int32_t hi = t / hop;                         // floor, since t >= 0 and hop >= 1
    if (hi > frames - 1) {
        hi = frames - 1;
    }
    return OverlapSpan{lo, hi >= lo ? hi - lo + 1 : 0};
}

// The source element the `j`-th contributing frame of output element `e` reads. Read from the innermost
// element axis outwards, as every element meaning here is: the output index along the reconstructed axis
// is t, its contributor from frame `first_frame + j` reads window tap `t - frame*hop`, and every OUTER
// axis is the identity. Those indices are recomposed against the SOURCE's extents — a scatter of shifted
// indices, right for any rank. It guards its own indices (the same rank relation `overlap_frames` checks,
// a tap inside the window, an index inside each source axis) so a direct caller cannot read out of bounds.
constexpr std::int32_t overlap_source_element(const Elements& to, const Elements& from, std::int32_t window,
                                              std::int32_t hop, const OverlapSpan& span, std::int32_t j,
                                              std::int32_t e) {
    if (from.rank != to.rank + 1) {
        fail("heinzel::overlap_source_element: an overlap-add does not collapse exactly two axes into one");
    }
    if (e < 0 || e >= to.count || j < 0 || j >= span.count) {
        fail("heinzel::overlap_source_element: a contributor outside the ones for this element");
    }
    std::int32_t to_index[kMaxRank]{};
    std::int32_t rest = e;
    for (std::int32_t k = to.rank; k > 0; --k) {
        to_index[k - 1] = rest % to.extent[k - 1];
        rest /= to.extent[k - 1];
    }
    const std::int32_t frame = span.first_frame + j;
    const std::int32_t tap = to_index[to.rank - 1] - frame * hop;
    if (tap < 0 || tap >= window) {
        fail("heinzel::overlap_source_element: a contributor whose tap falls outside the window");
    }
    std::int32_t flat = 0;
    for (std::int32_t a = 0; a < from.rank; ++a) {
        const std::int32_t i = a < to.rank - 1      ? to_index[a] // an outer axis, the identity
                               : a == from.rank - 2 ? frame       // the frame axis F
                                                    : tap;        // the window axis K, innermost
        if (i < 0 || i >= from.extent[a]) {
            fail("heinzel::overlap_source_element: an overlap-add reads past a source axis");
        }
        flat = flat * from.extent[a] + i;
    }
    return flat;
}

// The frames overlapping one output element, and the source element one of them reads, with the window
// and hop read off the node's `OverlapAdd` attribute (as `concat_read` and `padded_read` read theirs). The
// layouts are the caller's.
constexpr OverlapSpan overlap_span(const Node& n, const Elements& to, const Elements& from, std::int32_t e) {
    if (n.attr.kind != AttrKind::OverlapAdd) {
        fail("heinzel::overlap_span: this is an overlap-add and carries no overlap-add attribute");
    }
    return overlap_frames(to, from, n.attr.overlap.window, n.attr.overlap.hop, e);
}

constexpr std::int32_t overlap_read(const Node& n, const Elements& to, const Elements& from,
                                    const OverlapSpan& span, std::int32_t j, std::int32_t e) {
    if (n.attr.kind != AttrKind::OverlapAdd) {
        fail("heinzel::overlap_read: this is an overlap-add and carries no overlap-add attribute");
    }
    return overlap_source_element(to, from, n.attr.overlap.window, n.attr.overlap.hop, span, j, e);
}

} // namespace heinzel
