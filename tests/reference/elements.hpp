#pragma once

// A value's elements, and which of an operand's elements one of a result's comes from, for the walk.
// A value is `numbers_per_sample(shape)` numbers per sample, its width. This is the indexing around the
// arithmetic: `compute()` answers for one element and knows nothing about shapes. The alignment is the
// analysis pass's (`ShapeRule::Elementwise` lines axes up from the innermost outwards and an extent of
// one repeats), so a shape the analysis accepted is one this can index.

#include <cstdint>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/vocab.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel {

// The axes of a value outside the streamed one, in the shape's own order, plus their product. The
// streamed axis is left out because it is the sample loop: the walk is already standing inside it.
struct Elements {
    std::int32_t extent[kMaxRank]{};
    std::int32_t rank{0};  // the shape's rank less its streamed axis
    std::int32_t count{1}; // the product of the extents above — `numbers_per_sample`

    // The value's own rank, streamed axis included, because axes are named against the full shape.
    // Carried rather than derived as `rank + 1`, which would be right for every reachable case only by
    // coincidence.
    std::int32_t full_rank{0};

    constexpr bool operator==(const Elements&) const = default;
};

constexpr Elements elements_of(const Shape& s) {
    Elements e{};
    e.full_rank = s.rank;
    e.count = numbers_per_sample(s);
    if (e.count == kWidthNotKnown) {
        fail("heinzel::elements_of: an axis outside the streamed one has no size, so its width "
             "is unknown");
    }
    // Not `e.extent[e.rank++] = ...`: gcc 15.2 rejects that in a constant expression with a false
    // bounds error when the array and the counter are members of the same named local object (clang
    // accepts it). The rank is set directly instead: the width axes are the shape's axes less the
    // streamed one.
    for (std::int32_t k = 0; k + 1 < s.rank; ++k) {
        e.extent[k] = literal_of(extent_at(s, k));
    }
    e.rank = s.rank > 0 ? s.rank - 1 : 0;
    return e;
}

// Which element of `from` the element `e` of `to` reads.
//
// Both are walked from the innermost width axis outwards, which is the alignment the shape column
// already uses: an axis the operand does not have repeats, and so does one it has at extent one.
// Anything else is a pair of shapes the analysis pass would not have accepted, and it stops here
// rather than reading a neighbouring element — a wrong number that looks like an answer is worse
// than a run that halts.
constexpr std::int32_t element_read_by(const Elements& to, const Elements& from, std::int32_t e) {
    if (from.rank > to.rank) {
        fail("heinzel::element_read_by: an operand has more axes than the result it feeds");
    }
    std::int32_t rest = e;
    std::int32_t index = 0;
    std::int32_t stride = 1;
    for (std::int32_t k = 0; k < to.rank; ++k) {
        const std::int32_t here = to.extent[to.rank - 1 - k];
        const std::int32_t index_here = rest % here;
        rest /= here;
        const std::int32_t from_axis = from.rank - 1 - k;
        if (from_axis < 0) {
            continue; // the operand does not have this axis at all, so it repeats
        }
        const std::int32_t there = from.extent[from_axis];
        if (there != 1 && there != here) {
            fail("heinzel::element_read_by: an operand's axis is neither one nor the result's");
        }
        index += (there == 1 ? 0 : index_here) * stride;
        stride *= there;
    }
    return index;
}

// The element layout of each of a node's operands, an operand it does not take staying at the
// default — one number, no width axes — which is what `element_read_by` reads a missing operand as.
//
// The layouts do not depend on which element of the result is wanted, so a caller works them out once
// per node and not once per element: the cost is then linear in the elements rather than in elements
// times axes.
constexpr void operand_elements(const Graph& g, const Node& node, Elements (&out)[kMaxNodeInputs]) {
    for (std::size_t k = 0; k < kMaxNodeInputs; ++k) {
        if (node.inputs[k] != kNoValue) {
            out[k] = elements_of(g.values[static_cast<std::size_t>(node.inputs[k])].shape);
        }
    }
}

// The element layout the two operands of a node line up into: their join. An operation over two
// operands of different widths indexes its result against the layout they broadcast into; a contraction
// sums over the joined axes (`ContractAttr` in `ir/ir.hpp`). This is the analysis pass's `broadcast`
// (`sema/analyze.hpp`) over resolved extents, so a pair the analysis accepted is a pair this can join;
// any other pair stops here rather than picking one.
constexpr Elements joined_elements(const Elements& a, const Elements& b) {
    Elements out{};
    out.rank = a.rank > b.rank ? a.rank : b.rank;
    out.full_rank = a.full_rank > b.full_rank ? a.full_rank : b.full_rank;
    std::int64_t so_far = 1;
    for (std::int32_t k = 0; k < out.rank; ++k) {
        const std::int32_t ia = a.rank - out.rank + k; // negative ⇒ this operand has no such axis
        const std::int32_t ib = b.rank - out.rank + k;
        const std::int32_t ea = ia >= 0 ? a.extent[ia] : 1;
        const std::int32_t eb = ib >= 0 ? b.extent[ib] : 1;
        if (ea != eb && ea != 1 && eb != 1) {
            fail("heinzel::joined_elements: two operands' axes are neither equal nor one of them one");
        }
        out.extent[k] = ea > eb ? ea : eb;
        so_far *= out.extent[k];
        // The two operands' own widths are each inside an int32 and their join can still leave it:
        // six axes may multiply, and a width is what storage is then sized from.
        if (so_far > 2147483647) {
            fail("heinzel::joined_elements: more numbers per sample in the join than an int32 holds");
        }
    }
    out.count = static_cast<std::int32_t>(so_far);
    return out;
}

// ---- the element ONE element of a rearrangement's result comes from -----------------------------
//
// The indexing only: which rearrangement a node is, read off its attribute, is `rearrange_element.hpp`.
//
// A reshape, a squeeze and an unsqueeze (`ViewKind::Reshape`, `Squeeze`, `Unsqueeze`) keep the element
// sequence and change only the extents it is read with: dropping an axis of extent one, inserting one, or
// re-cutting the same numbers into different extents leaves every element where it was in the flat order.
// So the element of the operand is the element of the result.
//
// The refusal is why this is a function rather than `return e`. The elements are the axes outside the
// streamed one, so a rearrangement that changes how many numbers a sample has re-cuts time: `Shape{4, 8}`
// to `Shape{32}` turns four numbers per sample for eight samples into one number per sample for
// thirty-two. That is a reframing, which is what `_unfold` and `_ovadd` are for and a view is not. It
// stops here rather than reading past the end of an operand's storage.
constexpr std::int32_t same_elements_in_order(const Elements& to, const Elements& from, std::int32_t e) {
    // Short because a message carries at most `kMaxMessageLength` characters.
    if (to.count != from.count) {
        fail("heinzel::same_elements_in_order: a rearrangement changed how many numbers a sample has");
    }
    if (e < 0 || e >= to.count) {
        fail("heinzel::same_elements_in_order: an element outside the result's own elements");
    }
    return e;
}

// Which element of `from` the element `e` of `to` is, when the axes have been REORDERED.
//
// `order[k]` is the source axis that output axis k reads, over the value's full rank, the streamed axis
// included, as the attribute spells it.
//
// Read from the innermost outwards, as everything here is: the element's index along output axis k is
// its index along source axis `order[k]`, and the flat index is those indices recomposed against the
// source's extents. A scatter of the same indices rather than arithmetic on the flat one, so it is right
// for any number of axes.
//
// Two refusals, for two different mistakes. A permutation that moves the streamed axis asks for samples
// that do not exist yet as though they were elements. An `order` that is not a permutation reads one
// extent twice and loses another. The analysis pass refuses both by name; the walk refuses them again
// because a hand-built graph may never have gone through the pass.
constexpr std::int32_t permuted_element(const Elements& to, const Elements& from, const Axes& order,
                                        std::int32_t e) {
    if (order.rank != to.full_rank || to.rank != from.rank) {
        fail("heinzel::permuted_element: an order that is not over this value's own axes");
    }
    if (to.full_rank > 0 && order.axes[to.full_rank - 1] != to.full_rank - 1) {
        fail("heinzel::permuted_element: this moves the streamed axis, which is time");
    }
    std::int32_t index[kMaxRank]{}; // by SOURCE axis
    std::int32_t rest = e;
    for (std::int32_t k = to.rank; k > 0; --k) {
        const std::int32_t here = to.extent[k - 1];
        const std::int32_t source_axis = order.axes[k - 1];
        if (source_axis < 0 || source_axis >= from.rank) {
            fail("heinzel::permuted_element: an order naming an axis the operand does not have");
        }
        if (from.extent[source_axis] != here) {
            fail("heinzel::permuted_element: an axis whose extent is not the one it was permuted from");
        }
        index[source_axis] = rest % here;
        rest /= here;
    }
    std::int32_t flat = 0;
    for (std::int32_t k = 0; k < from.rank; ++k) {
        flat = flat * from.extent[k] + index[k];
    }
    return flat;
}

// Which element of `from` the element `e` of `to` is, when ONE axis has been sliced to a strided
// sub-range. The output keeps every axis; the sliced one is shorter. `axis` is the sliced axis
// against the value's element axes (the streamed axis is never one of them), `offset` its first
// source index and `step` its stride; its length is the output axis's own extent, read from `to`.
//
// Read from the innermost outwards, as everything here is: the output index along every axis is the
// element's index there, and along the SLICED axis the source index is `offset + i*step`. Those
// indices are recomposed against the SOURCE's extents, so it is a scatter of shifted indices rather
// than an arithmetic on the flat one — right for any number of axes, and the sliced axis need not be
// the innermost element axis.
//
// The refusals are the point: each is a mistake a hand-built node can carry that the analysis pass
// would have refused by name. The walk can be reached without the pass, so it refuses again rather than
// reading a plausible wrong element:
//   * a stride below one is a reversal, which is `_flip`, not a slice (the pass names `_flip` too).
//   * an offset before the first element, or a window that reads past the source axis, is a read
//     outside the operand's storage.
//   * a sliced axis that is not one of this value's element axes — the streamed axis, or none at all.
constexpr std::int32_t sliced_element(const Elements& to, const Elements& from, std::int32_t axis,
                                      std::int32_t offset, std::int32_t step, std::int32_t e) {
    if (to.rank != from.rank || to.full_rank != from.full_rank) {
        fail("heinzel::sliced_element: a slice changed the rank of its value");
    }
    if (step < 1) {
        fail("heinzel::sliced_element: a slice steps by less than one; a reversal is _flip");
    }
    if (offset < 0) {
        fail("heinzel::sliced_element: a slice starts before the first element");
    }
    if (axis < 0 || axis >= to.rank) {
        fail("heinzel::sliced_element: a slice along an axis that is not an element axis here");
    }
    if (e < 0 || e >= to.count) {
        fail("heinzel::sliced_element: an element outside the result's own elements");
    }
    std::int32_t index[kMaxRank]{};
    std::int32_t rest = e;
    for (std::int32_t k = to.rank; k > 0; --k) {
        const std::int32_t here = to.extent[k - 1];
        std::int32_t i = rest % here;
        rest /= here;
        if (k - 1 == axis) {
            i = offset + i * step;
        }
        if (i < 0 || i >= from.extent[k - 1]) {
            fail("heinzel::sliced_element: a slice reads past the source axis");
        }
        index[k - 1] = i;
    }
    std::int32_t flat = 0;
    for (std::int32_t k = 0; k < from.rank; ++k) {
        flat = flat * from.extent[k] + index[k];
    }
    return flat;
}

// Which element of `from` the element `e` of `to` is, when ONE axis has been REVERSED. The shape does
// not change — `_flip` reads the same axes at the same extents — so for every axis but `axis` the output
// index is the source index, and along `axis` output i reads source (extent - 1 - i). It is the
// materialising reversal the negative-stride `_slice` exclusion names (`docs/limitations.md`): one
// element in for one element out, nothing folded and nothing read twice.
//
// The refusals are the point: each is a mistake a hand-built node can carry that the analysis pass would
// have refused by name. The walk can be reached without the pass, so it refuses again rather than reading
// a plausible wrong element:
//   * a flip that changed the rank of its value — a reversal keeps every axis.
//   * a flipped axis that is not one of this value's element axes — the streamed axis, or none at all.
//   * a source index that reads past the axis — the only way a shape-preserving reversal can, which is a
//     `to`/`from` that disagree on an extent (a malformed node the pass would never have produced).
constexpr std::int32_t flipped_element(const Elements& to, const Elements& from, std::int32_t axis,
                                       std::int32_t e) {
    if (to.rank != from.rank || to.full_rank != from.full_rank) {
        fail("heinzel::flipped_element: a flip changed the rank of its value");
    }
    if (axis < 0 || axis >= to.rank) {
        fail("heinzel::flipped_element: a flip along an axis that is not an element axis here");
    }
    if (e < 0 || e >= to.count) {
        fail("heinzel::flipped_element: an element outside the result's own elements");
    }
    std::int32_t index[kMaxRank]{};
    std::int32_t rest = e;
    for (std::int32_t k = to.rank; k > 0; --k) {
        const std::int32_t here = to.extent[k - 1];
        std::int32_t i = rest % here;
        rest /= here;
        if (k - 1 == axis) {
            i = from.extent[k - 1] - 1 - i;
        }
        if (i < 0 || i >= from.extent[k - 1]) {
            fail("heinzel::flipped_element: a flip reads past the source axis");
        }
        index[k - 1] = i;
    }
    std::int32_t flat = 0;
    for (std::int32_t k = 0; k < from.rank; ++k) {
        flat = flat * from.extent[k] + index[k];
    }
    return flat;
}

// Which element of `from` the element `e` of `to` is, when ONE axis has been UNFOLDED into a sliding
// window. The output has one axis MORE than the source: axis `axis` of the source, of extent L, becomes
// a frame count `N_out = (L - window)/hop + 1` at `axis`, and a window axis of `window` elements appears
// at `axis + 1`. `axis` is against the value's own element axes (the streamed axis is never one of them,
// which is why an unfold windows an OUTER axis, exactly as `_slice` slices one).
//
// Read from the innermost outwards, as everything here is: the output index along the frame axis is the
// frame f and along the window axis is the tap k, and the source index along `axis` is `f*hop + k`;
// every other axis is the identity. Those indices are recomposed against the SOURCE's extents — a
// scatter of shifted indices, right for any number of axes. Overlapping windows READ the same source
// element more than once, which is what a sliding window is; nothing is written, so nothing is summed.
//
// The refusals are the point: each is a mistake a hand-built node can carry that the analysis pass would
// have refused by name. The walk can be reached without the pass, so it refuses again rather than reading
// a plausible wrong element:
//   * a hop or window below one is not a stride or a frame.
//   * a windowed axis that is not one of the source's own element axes — the streamed axis, or none.
//   * a window axis whose extent disagrees with `window`, or a source index that reads past the axis.
constexpr std::int32_t unfolded_element(const Elements& to, const Elements& from, std::int32_t axis,
                                        std::int32_t window, std::int32_t hop, std::int32_t e) {
    if (to.rank != from.rank + 1 || to.full_rank != from.full_rank + 1) {
        fail("heinzel::unfolded_element: an unfold does not add exactly one axis to its value");
    }
    if (hop < 1 || window < 1) {
        fail("heinzel::unfolded_element: an unfold with a hop or window below one");
    }
    if (axis < 0 || axis >= from.rank) {
        fail("heinzel::unfolded_element: an unfold along an axis that is not a source element axis");
    }
    if (to.extent[axis + 1] != window) {
        fail("heinzel::unfolded_element: the window axis disagrees with the declared window");
    }
    if (e < 0 || e >= to.count) {
        fail("heinzel::unfolded_element: an element outside the result's own elements");
    }
    std::int32_t to_index[kMaxRank]{};
    std::int32_t rest = e;
    for (std::int32_t k = to.rank; k > 0; --k) {
        to_index[k - 1] = rest % to.extent[k - 1];
        rest /= to.extent[k - 1];
    }
    std::int32_t flat = 0;
    for (std::int32_t a = 0; a < from.rank; ++a) {
        const std::int32_t i = a < axis    ? to_index[a]
                               : a == axis ? to_index[axis] * hop + to_index[axis + 1]
                                           : to_index[a + 1];
        if (i < 0 || i >= from.extent[a]) {
            fail("heinzel::unfolded_element: an unfold reads past the source axis");
        }
        flat = flat * from.extent[a] + i;
    }
    return flat;
}

// Which element of `from` the element `e` of `to` is, when TWO axes have been UNFOLDED into sliding
// windows at once. The output has two axes MORE than the source: axis `axis1` of the source
// becomes a frame count with a window axis of `window1` beside it, and likewise `axis2`, INTERLEAVED —
// the 1-D `unfolded_element` applied per axis. `axis1 < axis2` are the two windowed source element axes
// (the streamed axis is never one of them); every other axis is the identity.
//
// Read from the innermost outwards, as everything here is. The output element indices are decomposed
// against `to`'s extents, then walked in LOCKSTEP with the source axes: a windowed source axis consumes
// TWO output positions — the frame f at `op` and the tap k at `op + 1` — and reads `f*hop + k`; every
// other axis consumes one. Recomposed against the SOURCE extents, so it is a scatter of shifted indices,
// right for any rank rather than for two.
//
// The refusals are the point: each is a mistake a hand-built node can carry that the analysis pass would
// have refused by name. The walk can be reached without the pass, so it refuses again rather than reading
// a plausible wrong element:
//   * a hop or window below one, on either axis.
//   * axes that are not two distinct ordered source element axes.
//   * a window axis whose extent disagrees with its declared window, or a source index past the axis.
constexpr std::int32_t unfolded2d_element(const Elements& to, const Elements& from, std::int32_t axis1,
                                          std::int32_t window1, std::int32_t hop1, std::int32_t axis2,
                                          std::int32_t window2, std::int32_t hop2, std::int32_t e) {
    if (to.rank != from.rank + 2 || to.full_rank != from.full_rank + 2) {
        fail("heinzel::unfolded2d_element: a 2-D unfold does not add exactly two axes to its value");
    }
    if (hop1 < 1 || window1 < 1 || hop2 < 1 || window2 < 1) {
        fail("heinzel::unfolded2d_element: a 2-D unfold with a hop or window below one");
    }
    if (axis1 < 0 || axis1 >= axis2 || axis2 >= from.rank) {
        fail("heinzel::unfolded2d_element: a 2-D unfold along axes that are not two distinct source axes");
    }
    if (e < 0 || e >= to.count) {
        fail("heinzel::unfolded2d_element: an element outside the result's own elements");
    }
    std::int32_t to_index[kMaxRank]{};
    std::int32_t rest = e;
    for (std::int32_t k = to.rank; k > 0; --k) {
        to_index[k - 1] = rest % to.extent[k - 1];
        rest /= to.extent[k - 1];
    }
    std::int32_t flat = 0;
    std::int32_t op = 0; // position in `to_index`, walked in lockstep with the source axes
    for (std::int32_t a = 0; a < from.rank; ++a) {
        std::int32_t i = 0;
        if (a == axis1 || a == axis2) {
            const std::int32_t window = a == axis1 ? window1 : window2;
            const std::int32_t hop = a == axis1 ? hop1 : hop2;
            if (to.extent[op + 1] != window) {
                fail("heinzel::unfolded2d_element: a window axis disagrees with its declared window");
            }
            i = to_index[op] * hop + to_index[op + 1];
            op += 2;
        } else {
            i = to_index[op];
            op += 1;
        }
        if (i < 0 || i >= from.extent[a]) {
            fail("heinzel::unfolded2d_element: a 2-D unfold reads past a source axis");
        }
        flat = flat * from.extent[a] + i;
    }
    return flat;
}

// Which element of `from` the element `e` of `to` is, when ONE axis has been CONSTANT-PADDED — or
// whether `e` is in a MARGIN and reads no source element at all. The first meaning here that is not
// one-source-in for one-out: axis `axis` of extent L becomes `L + before + after`, the body reads source
// `(i_out - before)` and the `before` elements in front and `after` behind read a constant that lives in
// the attribute, not here. So it answers a PAIR — the source index, and whether there is one — and the
// walk writes the constant when there is not. Read innermost outwards as everything here is; the
// in-source indices are recomposed against the SOURCE's extents.
//
// The refusals are the point: the walk can be reached without the analysis pass, so it refuses a
// malformed node again rather than reading a wrong element or a wrong margin. A rank change (a pad keeps
// every axis); a padded axis that is not an element axis (the streamed axis, or none); a negative amount
// (a pad never shrinks); a padded axis whose extent is not `source + before + after`, or a non-padded
// axis whose extent disagrees — the only ways a pad's own shape can be wrong.
struct PadRead {
    std::int32_t index{0};  // the source flat index, when `in_source`; unused otherwise
    bool in_source{false};  // false ⇒ the element is in a margin and reads the constant fill
    constexpr bool operator==(const PadRead&) const = default;
};

constexpr PadRead padded_element(const Elements& to, const Elements& from, std::int32_t axis,
                                 std::int32_t before, std::int32_t after, std::int32_t e) {
    if (to.rank != from.rank || to.full_rank != from.full_rank) {
        fail("heinzel::padded_element: a pad changed the rank of its value");
    }
    if (axis < 0 || axis >= to.rank) {
        fail("heinzel::padded_element: a pad along an axis that is not an element axis here");
    }
    if (before < 0 || after < 0) {
        fail("heinzel::padded_element: a pad by a negative amount");
    }
    if (to.extent[axis] != from.extent[axis] + before + after) {
        fail("heinzel::padded_element: the padded axis extent is not source + before + after");
    }
    if (e < 0 || e >= to.count) {
        fail("heinzel::padded_element: an element outside the result's own elements");
    }
    std::int32_t index[kMaxRank]{};
    bool in_source = true;
    std::int32_t rest = e;
    for (std::int32_t k = to.rank; k > 0; --k) {
        const std::int32_t here = to.extent[k - 1];
        std::int32_t i = rest % here;
        rest /= here;
        if (k - 1 == axis) {
            i -= before; // the body reads the source shifted back by `before`
            if (i < 0 || i >= from.extent[k - 1]) {
                in_source = false; // a margin: no source element, so the constant fill
            }
        } else if (to.extent[k - 1] != from.extent[k - 1]) {
            fail("heinzel::padded_element: a pad changed a non-padded axis's extent");
        }
        index[k - 1] = i;
    }
    if (!in_source) {
        return PadRead{0, false};
    }
    std::int32_t flat = 0;
    for (std::int32_t k = 0; k < from.rank; ++k) {
        flat = flat * from.extent[k] + index[k];
    }
    return PadRead{flat, true};
}

// ---- the elements that fold into one, for an operation that collapses axes ----------------------
//
// The indexing only. Which axes a node collapses is `collapse_element.hpp`, and what the fold does to the
// numbers is `fold_of` (`compute_reference.hpp`) and `runtime/fold_arithmetic.hpp`. What is here names
// no operation and no attribute: it is handed the axes and answers where the numbers are.

// Which width axes of an operand collapse, and how many of its elements therefore fold into one of
// the result's.
struct Collapse {
    bool axis[kMaxRank]{}; // by width axis of the operand, in the operand's own order
    std::int32_t fan_in{1};
    bool kept{false}; // the collapsed axes stay at extent one instead of disappearing

    constexpr bool operator==(const Collapse&) const = default;
};

// Axes are named against the operand's full shape, the streamed one being the last of them, so this
// reads `in.full_rank` rather than being handed it.
//
// A collapse over the streamed axis is refused by name. The walk stands inside the sample loop, so
// folding over that axis would be folding over time, over operands that do not all exist yet.
constexpr Collapse collapse_over(const Axes& over, bool kept, const Elements& in) {
    Collapse c{};
    c.kept = kept;
    for (std::int32_t j = 0; j < over.rank; ++j) {
        const std::int32_t k = over.axes[j] == kInnermostAxis ? in.full_rank - 1 : over.axes[j];
        if (k == in.full_rank - 1) {
            fail("heinzel::collapse_over: this collapses the streamed axis, which a run here cannot do");
        }
        if (k < 0 || k >= in.rank) {
            fail("heinzel::collapse_over: this collapses an axis the operand does not have");
        }
        if (c.axis[k]) {
            fail("heinzel::collapse_over: the same axis is named twice among the ones that collapse");
        }
        c.axis[k] = true;
        c.fan_in *= in.extent[k];
    }
    return c;
}

// The `j`-th of the operand's elements that folds into element `e` of the result.
//
// Both indices are decomposed axis by axis and recomposed against the operand's axes: `e` supplies
// the index of every axis that SURVIVES, and `j` the index of every axis that collapses. It is not a
// stride, and writing it as one would be right for a single collapsed axis and quietly wrong for two.
constexpr std::int32_t folded_element(const Elements& in, const Collapse& c, const Elements& out,
                                      std::int32_t e, std::int32_t j) {
    std::int32_t index[kMaxRank]{};
    std::int32_t surviving = out.rank;
    std::int32_t rest_e = e;
    std::int32_t rest_j = j;
    // Innermost outwards, which is the order both flat indices are laid out in.
    for (std::int32_t k = in.rank; k > 0; --k) {
        if (c.axis[k - 1]) {
            index[k - 1] = rest_j % in.extent[k - 1];
            rest_j /= in.extent[k - 1];
            // A kept axis is still an axis of the result, at extent one, so it consumes one of the
            // result's positions even though its index is always zero.
            surviving -= c.kept ? 1 : 0;
        } else {
            --surviving;
            index[k - 1] = rest_e % out.extent[surviving];
            rest_e /= out.extent[surviving];
        }
    }
    std::int32_t flat = 0;
    for (std::int32_t k = 0; k < in.rank; ++k) {
        flat = flat * in.extent[k] + index[k];
    }
    return flat;
}

} // namespace heinzel
