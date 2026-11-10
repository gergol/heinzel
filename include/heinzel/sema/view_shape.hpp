#pragma once

// The shape a view makes, and its refusals: the rearrangements `_reshape` carries and the views that resize one axis
// (slice, pad, join). `_reshape` is one operation carrying several rearrangements with different shape rules, so the
// rule comes from which case of the attribute it is (`ViewKind`), never from the operation alone. Each family is one
// function, which hands its operand's shape back where it cannot make one and words the refusal when asked
// (`sema/refusal.hpp`). `shape_of_node` in `sema/analyze.hpp` routes `ShapeRule::ByViewKind` here.

#include <bit>
#include <cstdint>
#include <string_view>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/vocab.hpp"
#include "heinzel/sema/framing.hpp"
#include "heinzel/sema/refusal.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel {

namespace detail {

// Whether an `order` really is a permutation of `[0, rank)`: every axis named, none named twice, none out of range.
// Whether it moves the streamed axis is a different mistake with its own refusal, so it is not asked here.
constexpr bool is_a_permutation_of_the_rank(const Axes& order, std::int32_t rank) {
    if (order.rank != rank) {
        return false;
    }
    bool seen[kMaxRank]{};
    for (std::int32_t k = 0; k < rank; ++k) {
        const std::int32_t axis = order.axes[k];
        if (axis < 0 || axis >= rank || seen[axis]) {
            return false;
        }
        seen[axis] = true;
    }
    return true;
}

// A transpose: output axis k has source axis `order[k]`'s extent. An order that is not a permutation cannot make a
// shape. One that moves the streamed axis can, but the innermost axis is time: moved, it would read samples that do
// not exist yet as though they were elements. One still marked (see `as_read`) waits for its operand's rank.
constexpr Shape transpose_shape(const Shape& in, OpCode op, const ViewAttr& v, Diagnostic* why = nullptr) {
    if (!is_a_permutation_of_the_rank(v.order, in.rank)) {
        return refused(in, why, op, " over rank ", in.rank, ": an order must name every axis of it exactly once");
    }
    if (why != nullptr && in.rank > 0 && v.order.axes[in.rank - 1] != in.rank - 1) {
        return refused(in, why, op, " moves the streamed axis to ", v.order.axes[in.rank - 1],
                       "; the innermost axis is time and stays where it is");
    }
    if (v.elements_only) {
        return in;
    }
    Shape shape{};
    for (std::int32_t k = 0; k < in.rank; ++k) {
        set_extent(shape, shape.rank++, extent_at(in, v.order.axes[k]));
    }
    return shape;
}

// A slice `{axis, offset, length, step}`: one outer axis shortens to `length`; every other axis is left as it was,
// symbol and all. A stride below one is a reversal, which is `_flip`: everything downstream is written for
// `step >= 1` (`docs/limitations.md`). A length below one or an offset before the first element is the attribute
// contradicting itself. The window fits when the last index read, `offset + (length - 1)*step`, is inside the axis;
// an axis still symbolic was never bound, so there is nothing to fit it against.
constexpr Shape slice_shape(const Shape& in, OpCode op, const ViewAttr& v, Diagnostic* why = nullptr) {
    if (why != nullptr && v.step < 1) {
        return refused(in, why, op, " steps by ", v.step, "; a stride below one is a reversal, which is _flip");
    }
    if (why != nullptr && (v.length < 1 || v.offset < 0)) {
        return refused(in, why, op, " takes ", v.length, " from ", v.offset,
                       ": a slice keeps at least one, starting at zero or more");
    }
    if (!is_an_outer_axis(op, "slices", v.axis, in.rank, why)) {
        return in;
    }
    if (why != nullptr) {
        const Extent along = extent_at(in, v.axis);
        if (is_symbol(along)) {
            return refused(in, why, op, " slices axis ", v.axis, ", whose size is not known yet; bind it before slicing");
        }
        const std::int32_t last = v.offset + (v.length - 1) * v.step;
        if (last > literal_of(along) - 1) {
            return refused(in, why, op, " reads index ", last, " of an axis of extent ", literal_of(along));
        }
    }
    Shape shape{};
    for (std::int32_t k = 0; k < in.rank; ++k) {
        set_extent(shape, shape.rank++, k == v.axis ? literal_extent(v.length) : extent_at(in, k));
    }
    return shape;
}

// A constant pad: one outer axis grows to the literal `L + before + after`; every other axis is left as it was,
// symbol and all. A negative amount would shrink it, which a pad never does. A symbolic padded axis has no literal
// `L` to grow.
constexpr Shape pad_shape(const Shape& in, OpCode op, const ViewAttr& v, Diagnostic* why = nullptr) {
    if (why != nullptr && (v.pad_before < 0 || v.pad_after < 0)) {
        return refused(in, why, op, " pads axis ", v.axis, " by ", v.pad_before, " and ", v.pad_after,
                       ": a pad amount is at least zero");
    }
    if (!is_an_outer_axis(op, "pads", v.axis, in.rank, why)) {
        return in;
    }
    if (is_symbol(extent_at(in, v.axis))) {
        return refused(in, why, op, " pads axis ", v.axis, ", whose size is not known yet; bind it or declare its extent");
    }
    Shape shape{};
    for (std::int32_t k = 0; k < in.rank; ++k) {
        set_extent(shape, shape.rank++,
                   k == v.axis ? literal_extent(literal_of(extent_at(in, k)) + v.pad_before + v.pad_after)
                               : extent_at(in, k));
    }
    return shape;
}

// A join of two operands along one outer axis, whose extent becomes the literal sum of theirs. They need the same
// rank, and every other axis equal (no broadcast, as in numpy); a symbolic one passes through. A symbolic joined axis
// is a sum of two, outside the derived-extent algebra, so there is no literal to write.
constexpr Shape concat_shape(const Shape& a, const Shape& b, OpCode op, const ViewAttr& v, Diagnostic* why = nullptr) {
    if (!is_an_outer_axis(op, "joins", v.axis, a.rank, why)) {
        return a;
    }
    if (a.rank != b.rank) {
        return refused(a, why, op, " joins operands of rank ", a.rank, " and ", b.rank, "; a join needs both of the same rank");
    }
    if (is_symbol(extent_at(a, v.axis)) || is_symbol(extent_at(b, v.axis))) {
        return refused(a, why, op, " joins axis ", v.axis,
                       ", whose size is a sum of two operand extents, not known yet; bind both");
    }
    for (std::int32_t k = 0; k < a.rank; ++k) {
        if (k != v.axis && !same_extent(extent_at(a, k), extent_at(b, k))) {
            return refused(a, why, op, " joins axis ", v.axis, " but its operands disagree on axis ", k);
        }
    }
    Shape shape{};
    for (std::int32_t k = 0; k < a.rank; ++k) {
        set_extent(shape, shape.rank++,
                   k == v.axis ? literal_extent(literal_of(extent_at(a, k)) + literal_of(extent_at(b, k)))
                               : extent_at(a, k));
    }
    return shape;
}

// A flip changes no extent, but its axis is an outer one: a reversal of the streamed axis would reverse time.
constexpr Shape flip_shape(const Shape& in, OpCode op, const ViewAttr& v, Diagnostic* why = nullptr) {
    (void)is_an_outer_axis(op, "flips", v.axis, in.rank, why);
    return in;
}

// The shape of a `_squeeze`: axis `axis` gone. An axis the value does not have hands the shape back;
// `time_checks.hpp` refuses it, and an axis of extent other than 1, by name.
constexpr Shape dropped_axis_shape(const Shape& in, const ViewAttr& v) {
    if (v.axis < 0 || v.axis >= in.rank) {
        return in;
    }
    Shape shape{};
    for (std::int32_t k = 0; k < in.rank; ++k) {
        if (k != v.axis) {
            set_extent(shape, shape.rank++, extent_at(in, k));
        }
    }
    return shape;
}

// The shape of an `_unsqueeze`: an axis of extent 1 at `axis`. A place the value has no room for — past its
// end, or a value already of `kMaxRank` axes — hands the shape back rather than writing past the storage.
constexpr Shape inserted_axis_shape(const Shape& in, const ViewAttr& v) {
    if (v.axis < 0 || v.axis > in.rank || in.rank + 1 > static_cast<std::int32_t>(kMaxRank)) {
        return in;
    }
    Shape shape{};
    for (std::int32_t k = 0; k < in.rank + 1; ++k) {
        set_extent(shape, shape.rank++, k == v.axis ? literal_extent(1) : extent_at(in, k > v.axis ? k - 1 : k));
    }
    return shape;
}

// A regroup's refusal, in the words `rearrange` used while it worked the regroup out as the graph was built: only
// `rearrange` writes one (`ViewAttr::operand_groups`).
constexpr bool regroup_refused(Diagnostic* why, std::string_view text) {
    if (why != nullptr) {
        *why = (MessageWriter{} << text).problem();
    }
    return false;
}

// Where the group that starts at label `first` ends: at the next label that starts one, or after the last.
constexpr std::int32_t group_end(LabelMask starts, std::int32_t first, std::int32_t labels) {
    std::int32_t end = first + 1;
    while (end < labels && ((starts >> end) & 1u) == 0) {
        ++end;
    }
    return end;
}

// A split of an axis of extent `along` into the labels [first, end): the sizes given stand, and the one left out is the
// extent over their product, which must divide it. A symbol has no literal to split.
constexpr bool split_factors(Extent along, std::int32_t first, std::int32_t end, Shape& ext, Diagnostic* why) {
    if (is_symbol(along)) {
        return regroup_refused(why, "heinzel::rearrange: a split needs the operand's extent known now, not a symbol");
    }
    std::int32_t known = 1;
    std::int32_t unknown = -1;
    for (std::int32_t k = first; k < end; ++k) {
        const Extent size = extent_at(ext, k);
        if (!is_no_extent(size)) {
            known *= literal_of(size);
        } else if (unknown >= 0) {
            return regroup_refused(why, "heinzel::rearrange: a split leaves more than one axis unknown; size all but one");
        } else {
            unknown = k;
        }
    }
    if (unknown < 0) {
        return known == literal_of(along) ||
               regroup_refused(why, "heinzel::rearrange: the split sizes do not multiply to the axis extent");
    }
    if (known <= 0 || literal_of(along) % known != 0) {
        return regroup_refused(why, "heinzel::rearrange: the split sizes do not divide the axis extent");
    }
    set_extent(ext, unknown, literal_extent(literal_of(along) / known));
    return true;
}

// Each label's extent, the operand's side read group by group, one group to an axis: a group of one takes its axis's
// extent, a symbol too, and a group of several splits it.
constexpr bool label_extents(const Shape& in, const ViewAttr& v, Shape& ext, Diagnostic* why) {
    if (std::popcount(v.operand_groups) != in.rank) {
        return regroup_refused(why, "heinzel::rearrange: the left side names a different number of axes than the operand");
    }
    ext = v.shape;
    std::int32_t axis = 0;
    for (std::int32_t first = 0; first < v.shape.rank; ++axis) {
        const std::int32_t end = group_end(v.operand_groups, first, v.shape.rank);
        const Extent along = extent_at(in, axis);
        if (end - first == 1) {
            set_extent(ext, first, along);
        } else if (!split_factors(along, first, end, ext, why)) {
            return false;
        }
        first = end;
    }
    return true;
}

// The target the labels' extents make, the result's side read group by group, one group to an axis: a group of one is
// its label's extent, a symbol too, and a group of several merges into the product of its labels', which are literal.
constexpr bool merged_target(const Shape& ext, LabelMask starts, Shape& target, Diagnostic* why) {
    target = Shape{};
    for (std::int32_t first = 0; first < ext.rank;) {
        const std::int32_t end = group_end(starts, first, ext.rank);
        Extent merged = extent_at(ext, first);
        if (end - first > 1) {
            std::int32_t product = 1;
            for (std::int32_t k = first; k < end; ++k) {
                if (is_symbol(extent_at(ext, k))) {
                    return regroup_refused(why, "heinzel::rearrange: a merge needs its extents known now, not a symbol");
                }
                product *= literal_of(extent_at(ext, k));
            }
            merged = literal_extent(product);
        }
        set_extent(target, target.rank++, merged);
        first = end;
    }
    return true;
}

// A regroup `rearrange` wrote, over an operand of shape `in`: its whole target into `target`, or false where it cannot
// make one, and with `why` the refusal. The two sides lay the same labels out in the same order, grouped differently.
constexpr bool regroup_target(const Shape& in, const ViewAttr& v, Shape& target, Diagnostic* why = nullptr) {
    Shape ext{};
    return label_extents(in, v, ext, why) && merged_target(ext, v.result_groups, target, why);
}

// A view as the analysis reads it over an operand of shape `in`. A member such as `x.reshape({…})` names
// the element axes only (`ViewAttr::elements_only`), so the streamed axis is added here: a reshape target
// gets the operand's last extent appended, and a transpose order that is exactly one short gets the last
// axis, kept last. A regroup `rearrange` wrote gets its whole target (`regroup_target`). Some views cannot
// be completed yet: the operand may still have rank 0 (a recurrence's own past, in the first sweep), the
// target would pass `kMaxRank`, the order or the groups do not fit a rank that is still growing
// (`w * y[n - 1]` has `w`'s rank first), or a split meets a symbol the solver binds only after the sweep.
// Those come back still marked, `viewed_shape` hands on what it can (see there), and `time_checks.hpp`
// refuses whatever is still marked once shapes settle. An unmarked view is read as written.
constexpr ViewAttr as_read(const Shape& in, ViewAttr v) {
    if (v.operand_groups != 0) {
        Shape target{};
        if (regroup_target(in, v, target)) {
            v.shape = target;
            v.operand_groups = 0;
            v.result_groups = 0;
        }
        return v;
    }
    if (!v.elements_only || in.rank == 0) {
        return v;
    }
    if (v.kind == ViewKind::Reshape && v.shape.rank < static_cast<std::int32_t>(kMaxRank)) {
        set_extent(v.shape, v.shape.rank++, extent_at(in, in.rank - 1));
        v.elements_only = false;
    }
    if (v.kind == ViewKind::Transpose && v.order.rank == in.rank - 1) {
        v.order.axes[static_cast<std::size_t>(v.order.rank++)] = in.rank - 1;
        v.elements_only = false;
    }
    return v;
}

// What a view of one operand makes of its shape, by which case of the attribute it is, and with `why`, the first
// refusal it owes. A squeeze, an unsqueeze, and a target or order still marked are refused in `time_checks.hpp`;
// a reshape that does not conserve its elements in `check_reshape_attribute`, which reads the resolved output.
// A regroup still marked hands on no shape yet, as a recurrence's past does in the first sweep: its operand's has
// other axes, which a reader would disagree with, or tell the solver of, though they are not the regroup's.
constexpr Shape viewed_shape(const Shape& in, OpCode op, const ViewAttr& v, Diagnostic* why = nullptr) {
    Shape shape{};
    switch (v.kind) {
    case ViewKind::Reshape:
        shape = v.elements_only ? in : v.operand_groups != 0 ? Shape{} : v.shape;
        break;
    case ViewKind::Squeeze:
        shape = dropped_axis_shape(in, v);
        break;
    case ViewKind::Unsqueeze:
        shape = inserted_axis_shape(in, v);
        break;
    case ViewKind::Transpose:
        shape = transpose_shape(in, op, v, why);
        break;
    case ViewKind::Slice:
        shape = slice_shape(in, op, v, why);
        break;
    case ViewKind::Unfold:
        shape = unfold_shape(in, op, v, why);
        break;
    case ViewKind::Unfold2d:
        shape = unfold2d_shape(in, op, v, why);
        break;
    case ViewKind::Flip:
        shape = flip_shape(in, op, v, why);
        break;
    case ViewKind::Pad:
        shape = pad_shape(in, op, v, why);
        break;
    case ViewKind::Concat:
        // A join is shaped from two operands, by `concat_shape`. Reaching this is a routing mistake, not a graph a
        // user can write.
        fail("heinzel::infer: a join is shaped from two operands in shape_of_node, not viewed_shape");
    }
    return shape;
}

} // namespace detail

} // namespace heinzel
