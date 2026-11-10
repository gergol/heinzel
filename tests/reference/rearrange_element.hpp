#pragma once

// Which element of its operand one element of a rearrangement's result is: the `ElementKind::Rearrange`
// meaning (`_xpose`, `_slice`, `_unfold`, `_unfld2d`, `_flip`, and `_reshape`'s three). It reads the
// `View` attribute and dispatches to the index maps in `elements.hpp`, naming no operation. A
// rearrangement reads exactly one operand element per result element, so the walk only copies and rounds
// nothing: the number is already stored at the type it will be stored at again.

#include <cstdint>

#include "heinzel/ir/ir.hpp"
#include "heinzel/support/fail.hpp"

#include "elements.hpp"

namespace heinzel {

// `_reshape`'s three cases keep the element sequence, so the element read is `e` itself; a transpose,
// slice, unfold or flip moves where an element is read from and answers another index.
constexpr std::int32_t rearranged_element(const Node& n, const Elements& to, const Elements& from,
                                          std::int32_t e) {
    if (n.attr.kind != AttrKind::View) {
        fail("heinzel::rearranged_element: this rearranges axes and carries no attribute saying how");
    }
    switch (n.attr.view.kind) {
    case ViewKind::Reshape:
    case ViewKind::Squeeze:
    case ViewKind::Unsqueeze:
        return same_elements_in_order(to, from, e);
    case ViewKind::Transpose:
        return permuted_element(to, from, n.attr.view.order, e);
    case ViewKind::Slice:
        return sliced_element(to, from, n.attr.view.axis, n.attr.view.offset, n.attr.view.step, e);
    case ViewKind::Unfold:
        return unfolded_element(to, from, n.attr.view.axis, n.attr.view.window, n.attr.view.hop, e);
    case ViewKind::Unfold2d:
        return unfolded2d_element(to, from, n.attr.view.axis, n.attr.view.window, n.attr.view.hop,
                                  n.attr.view.axis2, n.attr.view.window2, n.attr.view.hop2, e);
    case ViewKind::Flip:
        return flipped_element(to, from, n.attr.view.axis, e);
    case ViewKind::Pad:
        fail("heinzel::rearranged_element: a pad is a guarded read, not a plain rearrangement");
    case ViewKind::Concat:
        fail("heinzel::rearranged_element: a join reads two operands, not a plain rearrangement");
    }
    fail("heinzel::rearranged_element: a rearrangement with no element meaning written");
}

} // namespace heinzel
