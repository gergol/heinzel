#pragma once

// The element one element of a join's result comes from, and from which of two operands: the
// `ElementKind::Joined` meaning of `_concat`. A join reads two operands and selects one per
// element: below the split along the joined axis it is operand 0's, at or above it operand 1's, shifted by
// operand 0's extent. So it answers a `ConcatRead` (which operand, which element) rather than a plain
// index, as a pad answers a `PadRead`.

#include <cstdint>

#include "heinzel/ir/ir.hpp"
#include "heinzel/support/fail.hpp"

#include "elements.hpp"

namespace heinzel {

// Which element of which operand the element `e` of a join's result is. `from_second` picks operand 1
// over operand 0; `index` is that operand's own flat element index.
struct ConcatRead {
    std::int32_t index{0};
    bool from_second{false};
    constexpr bool operator==(const ConcatRead&) const = default;
};

// `to` is the join's element layout, `a`/`b` the two operands', `axis` the joined element axis (an outer
// axis — the streamed innermost one is never joined). Read from the innermost width axis outwards, as
// every element meaning here is: the output index along each axis is the element's index there, and along
// the JOINED axis an index below `a`'s extent stays in operand 0 while one at or above it moves to operand
// 1, shifted down by `a`'s extent. Those indices are recomposed against the CHOSEN operand's extents, so
// it is a scatter of shifted indices rather than an arithmetic on the flat one — right for any rank.
//
// The refusals are the point: each is a mistake a hand-built node can carry that the analysis pass would
// have refused by name. The walk can be reached without the pass, so it refuses again rather than reading
// a plausible wrong element:
//   * operands of unequal rank, or a rank the join did not keep — a join keeps every axis of both.
//   * a joined axis that is not one of this value's element axes — the streamed axis, or none at all.
//   * a joined axis whose extent is not `a`'s plus `b`'s — a malformed node the shape pass never made.
//   * a NON-joined axis the two operands disagree on — numpy semantics forbid a broadcast across a join.
constexpr ConcatRead concatenated_element(const Elements& to, const Elements& a, const Elements& b,
                                          std::int32_t axis, std::int32_t e) {
    if (to.rank != a.rank || to.rank != b.rank || to.full_rank != a.full_rank ||
        to.full_rank != b.full_rank) {
        fail("heinzel::concatenated_element: a join changed the rank of its operands");
    }
    if (axis < 0 || axis >= to.rank) {
        fail("heinzel::concatenated_element: a join along an axis that is not an element axis here");
    }
    if (e < 0 || e >= to.count) {
        fail("heinzel::concatenated_element: an element outside the result's own elements");
    }
    if (to.extent[axis] != a.extent[axis] + b.extent[axis]) {
        fail("heinzel::concatenated_element: the joined axis extent is not the sum of the two operands'");
    }
    std::int32_t index[kMaxRank]{};
    bool from_second = false;
    std::int32_t rest = e;
    for (std::int32_t k = to.rank; k > 0; --k) {
        const std::int32_t here = to.extent[k - 1];
        std::int32_t i = rest % here;
        rest /= here;
        if (k - 1 == axis) {
            if (i >= a.extent[axis]) {
                from_second = true;
                i -= a.extent[axis];
            }
        } else if (a.extent[k - 1] != here || b.extent[k - 1] != here) {
            fail("heinzel::concatenated_element: a join's non-joined axis extents disagree");
        }
        index[k - 1] = i;
    }
    const Elements& src = from_second ? b : a;
    std::int32_t flat = 0;
    for (std::int32_t k = 0; k < src.rank; ++k) {
        flat = flat * src.extent[k] + index[k];
    }
    return ConcatRead{flat, from_second};
}

// Which element of which operand one element of a joined result is, the joined axis read off the node's
// `view.axis` (as `padded_read` reads a pad's attribute).
constexpr ConcatRead concat_read(const Node& n, const Elements& to, const Elements& a, const Elements& b,
                                 std::int32_t e) {
    if (n.attr.kind != AttrKind::View || n.attr.view.kind != ViewKind::Concat) {
        fail("heinzel::concat_read: this is a join and carries no join attribute saying which axis");
    }
    return concatenated_element(to, a, b, n.attr.view.axis, e);
}

} // namespace heinzel
