#pragma once

// The shape an `_order` makes, and its refusals. Sort keeps the ordered axis at its extent L, TopK resizes it to k,
// and Quantile drops it (one number per batch); every other axis passes through, symbol and all. The ordered extent is
// read straight off the axis, never carried by the attribute, so nothing can disagree with it.

#include <cstdint>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/vocab.hpp"
#include "heinzel/sema/refusal.hpp"
#include "heinzel/support/order_net.hpp"

namespace heinzel {

namespace detail {

// The network is sized from the ordered axis, so every element extent must be a number now; the stream's length may
// still come at run time. The ordered axis is an element axis: ordering the streamed axis would order samples that do
// not all exist yet (the attribute carries a single axis, so a multi-axis order statistic cannot be asked for). The
// ordered extent is bounded, because the compare-exchange network is a fixed-capacity compile-time object
// (`support/order_net.hpp`); the refusal names the macro that raises the ceiling. A TopK keeps between one and L; a
// Quantile reads a rank interpolated at q, in `[0, 1]`.
constexpr Shape order_shape(const Shape& in, OpCode op, const OrderAttr& v, Diagnostic* why = nullptr) {
    for (std::int32_t a = 0; why != nullptr && a + 1 < in.rank; ++a) {
        if (!is_literal(extent_at(in, a))) {
            return refused(in, why, op, " needs every element extent known now, but was given ", in);
        }
    }
    if (v.axis == in.rank - 1) {
        return refused(in, why, op, " orders the streamed axis, which is time; order an element axis");
    }
    if (!is_element_axis(v.axis, in.rank)) {
        return refused(in, why, op, " orders axis ", v.axis, " of a value of rank ", in.rank);
    }
    const Extent along = extent_at(in, v.axis);
    if (is_symbol(along)) {
        return in;
    }
    const std::int32_t length = literal_of(along);
    if (why != nullptr && length > order::kMaxOrderedLength) {
        return refused(in, why, op, " orders an axis of ", length, "; the ceiling is ", order::kMaxOrderedLength,
                       " (raise it with -D HEINZEL_MAX_ORDER_EXTENT)");
    }
    if (v.op == OrderOp::Sort) {
        return in; // ascending, shape-preserving
    }
    if (v.op == OrderOp::TopK && (v.k < 1 || v.k > length)) {
        return refused(in, why, op, " keeps the top ", v.k, " of an axis of ", length,
                       "; a top-k count is between one and the ordered extent");
    }
    if (why != nullptr && v.op == OrderOp::Quantile && !(v.q >= 0.0 && v.q <= 1.0)) {
        return refused(in, why, op, " takes a quantile outside [0, 1]");
    }
    Shape shape{};
    for (std::int32_t k = 0; k < in.rank; ++k) {
        if (order_keeps_axis(v.op)) {
            set_extent(shape, shape.rank++, k == v.axis ? literal_extent(v.k) : extent_at(in, k)); // TopK: k
        } else if (k != v.axis) {
            set_extent(shape, shape.rank++, extent_at(in, k)); // Quantile drops the ordered axis
        }
    }
    return shape;
}

} // namespace detail

} // namespace heinzel
