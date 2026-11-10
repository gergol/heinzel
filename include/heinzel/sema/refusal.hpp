#pragma once

// How the analysis words a refusal. A family's shape function (`view_shape.hpp`, `framing.hpp`, `order.hpp`,
// `spectral.hpp`, `overlap_add.hpp`) tests its conditions once: during the sweep it is called without a
// `Diagnostic` and hands its operand's shape back where it cannot make one, and `check_attributes` calls it once more
// with one, to word the first condition that fails. A condition the shape does not need is asked only then.

#include <cstdint>
#include <string_view>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel {

namespace detail {

// One part of a refusal: text, a number, an extent or a shape.
template <class Part>
constexpr void write_part(MessageWriter& out, const Part& part) {
    out << part;
}
constexpr void write_part(MessageWriter& out, Extent e) { write_extent(out, e); }
constexpr void write_part(MessageWriter& out, const Shape& s) { write_shape(out, s); }

// The operation as the user wrote it, then the parts in order. It truncates where `MessageWriter` does.
template <class... Parts>
constexpr Diagnostic refusal(OpCode op, Parts... parts) {
    MessageWriter m;
    write_op(m, op);
    (write_part(m, parts), ...);
    return m.problem();
}

// A family's refusal: worded into `why` when the caller asked for one; the operand's shape comes back either way.
template <class... Parts>
constexpr Shape refused(const Shape& in, Diagnostic* why, OpCode op, Parts... parts) {
    if (why != nullptr) {
        *why = refusal(op, parts...);
    }
    return in;
}

// The same, for a condition that answers whether it held.
template <class... Parts>
constexpr bool report(Diagnostic* why, OpCode op, Parts... parts) {
    if (why != nullptr) {
        *why = refusal(op, parts...);
    }
    return false;
}

// An element axis: one of a value's own axes, and not the streamed one, which is its last and is time.
constexpr bool is_element_axis(std::int32_t axis, std::int32_t rank) { return axis >= 0 && axis < rank - 1; }

// The axis a view names has to be one of the value's axes: there is no shape to make otherwise, so it stops the
// sweep too. `verb` says what the view does to it ("slices", "unfolds").
constexpr bool has_axis(OpCode op, const char* verb, std::int32_t axis, std::int32_t rank, Diagnostic* why) {
    if (axis >= 0 && axis < rank) {
        return true;
    }
    return report(why, op, " ", verb, " axis ", axis, ", which this value of rank ", rank, " does not have");
}

// And for slice, flip, pad and join, an outer one: they select, reverse, grow or splice elements, which time cannot
// be. The streamed axis still has a shape, so the sweep makes it, and only the report refuses it.
constexpr bool is_an_outer_axis(OpCode op, const char* verb, std::int32_t axis, std::int32_t rank, Diagnostic* why) {
    if (!has_axis(op, verb, axis, rank, why)) {
        return false;
    }
    if (why == nullptr || axis != rank - 1) {
        return true;
    }
    return report(why, op, " ", verb, " axis ", axis, "; that is the streamed axis, which is time and stays whole");
}

} // namespace detail

} // namespace heinzel
