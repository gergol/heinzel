#pragma once

// Which width axes a fold collapses, read off the node's attribute kind. It names no operation; the index
// maps it feeds are `elements.hpp`.

#include "heinzel/ir/ir.hpp"
#include "heinzel/runtime/compute.hpp"
#include "heinzel/support/fail.hpp"

#include "elements.hpp"

namespace heinzel {

// Which width axes this node collapses, read off whichever attribute carries them.
//
// `in` is the layout the axes are NAMED against, which for an operation over two operands is their
// JOIN rather than either of them — a contraction sums over axes of the joined shape (`ContractAttr`
// in `ir/ir.hpp`), and the shape rule builds its result the same way: broadcast, then collapse. So
// every caller hands `joined_elements` of the operands here, which for a one-operand fold is that
// operand's own layout and asks nothing extra of the caller.
constexpr Collapse collapse_of(const Node& n, const Elements& in) {
    if (n.attr.kind == AttrKind::Reduce) {
        return collapse_over(n.attr.reduce.axes, n.attr.reduce.keep_axes, in);
    }
    if (n.attr.kind == AttrKind::Contract) {
        // A contraction's axes disappear into the sum rather than staying at extent one, which is
        // exactly what `ShapeRule::ContractAxes` passes `collapsed_shape` — so the two cannot take
        // different views of the result's shape.
        return collapse_over(n.attr.contract.axes, false, in);
    }
    fail("heinzel::collapse_of: this collapses axes and carries no attribute saying which");
}

} // namespace heinzel
