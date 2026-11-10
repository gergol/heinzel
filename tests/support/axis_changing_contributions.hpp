#pragma once

// The wide axis-changing contributions the both-ways corpus folds — one per structural operation
// (transpose, slice, unfold, 2-D unfold, flip, pad, concat, pack, overlap-add, order), milestone M5.
//
// Its own file, and not more functions in `both_ways.hpp`, for the amendment-06 reason that file
// reached its 600-line limit: assembling one wide contribution per operation is a concern of its own,
// separate from the base graph and the runnable per-sample entries. Each builds a wide value from the
// one named input, views it, and folds it back to one number per sample so the corpus entry stays
// runnable. `axis_changing_graph` in `both_ways.hpp` sums them.

#include <bit>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/vocab.hpp"

#include "hand_built_graph.hpp"

namespace heinzel::testing {

// Sixty-four samples: M4.1 asks every operation to be run "over at least 64 samples".
inline constexpr std::int32_t kBlock = 64;


// Two contributions to the structural entry, each its own named step — a function that assembles
// several of them is several steps, which is where operator amendment 06 asks the decomposition to be
// (`plan/M5-STEPS.md`, the amendment-06 note). Each builds a wide value from the one named input,
// views it, and folds it back to one number per sample so the entry stays runnable.

// A wide value with its two outer axes transposed, folded back — a `_xpose` contribution. That each
// element lands where the permutation says is `rearrange_test.cpp`'s check.
constexpr std::int32_t a_transposed_contribution(Graph& g, std::int32_t x) {
    const auto spread = add_value(g, ElemType::F64, Shape{2, 3, kBlock}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(spread)].constant = 0.5;
    const auto field = add_node(g, op::Mul, x, spread);
    OpAttr turn{};
    turn.kind = AttrKind::View;
    turn.view.kind = ViewKind::Transpose;
    turn.view.order = Axes{1, 0, 2}; // swap the two outer axes, leave the streamed axis where it is
    const auto turned = add_node(g, op::Transpose, field, kNoValue, kNoValue, turn);
    OpAttr collapse{};
    collapse.kind = AttrKind::Reduce;
    collapse.reduce.axes = Axes{0, 1};
    return add_node(g, op::Reduce, turned, kNoValue, kNoValue, collapse);
}

// A wide value with its outer axis sliced to two elements read at stride two — source indices zero and
// two — folded back: a `_slice` contribution. That each output element is read from `offset + i*step`
// is `rearrange_test.cpp`'s check.
constexpr std::int32_t a_sliced_contribution(Graph& g, std::int32_t x) {
    const auto strip = add_value(g, ElemType::F64, Shape{4, kBlock}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(strip)].constant = 0.5;
    const auto band = add_node(g, op::Mul, x, strip);
    OpAttr take{};
    take.kind = AttrKind::View;
    take.view.kind = ViewKind::Slice;
    take.view.axis = 0;
    take.view.offset = 0;
    take.view.length = 2;
    take.view.step = 2;
    const auto taken = add_node(g, op::Slice, band, kNoValue, kNoValue, take);
    OpAttr narrow{};
    narrow.kind = AttrKind::Reduce;
    narrow.reduce.axes = Axes{0};
    return add_node(g, op::Reduce, taken, kNoValue, kNoValue, narrow);
}

// A wide value with its outer axis unfolded into overlapping frames — an extent of six becomes
// N_out=(6-3)/1+1=4 frames of window three at hop one — then folded back: an `_unfold` contribution.
// The frames OVERLAP (hop below the window), so a source element is read into more than one frame,
// which is what a sliding window is. That each output element is read from `f*hop + k` of the source is
// `rearrange_test.cpp`'s check.
constexpr std::int32_t an_unfolded_contribution(Graph& g, std::int32_t x) {
    const auto strip = add_value(g, ElemType::F64, Shape{6, kBlock}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(strip)].constant = 0.5;
    const auto band = add_node(g, op::Mul, x, strip);
    OpAttr frame{};
    frame.kind = AttrKind::View;
    frame.view.kind = ViewKind::Unfold;
    frame.view.axis = 0;
    frame.view.window = 3;
    frame.view.hop = 1;
    const auto framed = add_node(g, op::Unfold, band, kNoValue, kNoValue, frame);
    OpAttr narrow{};
    narrow.kind = AttrKind::Reduce;
    narrow.reduce.axes = Axes{0, 1}; // fold both the frame axis and the window axis back
    return add_node(g, op::Reduce, framed, kNoValue, kNoValue, narrow);
}

// A wide value with its TWO outer axes each unfolded into frames at once, then folded back — an
// `_unfld2d` contribution. An outer axis of six becomes N1=(6-2)/2+1=3 frames of window two at hop two
// (NON-overlapping), and an outer axis of five becomes N2=(5-3)/1+1=3 frames of window three at hop one
// (overlapping). The two window axes are INTERLEAVED with their frame axes — [3, 2, 3, 3, kBlock] — the
// layout `rearrange_test.cpp` checks each output element of against `f*hop + k` per axis.
constexpr std::int32_t an_unfolded_2d_contribution(Graph& g, std::int32_t x) {
    const auto sheet = add_value(g, ElemType::F64, Shape{6, 5, kBlock}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(sheet)].constant = 0.5;
    const auto field = add_node(g, op::Mul, x, sheet);
    OpAttr frame{};
    frame.kind = AttrKind::View;
    frame.view.kind = ViewKind::Unfold2d;
    frame.view.axis = 0;
    frame.view.window = 2;
    frame.view.hop = 2;
    frame.view.axis2 = 1;
    frame.view.window2 = 3;
    frame.view.hop2 = 1;
    const auto framed = add_node(g, op::Unfold2d, field, kNoValue, kNoValue, frame);
    OpAttr narrow{};
    narrow.kind = AttrKind::Reduce;
    narrow.reduce.axes = Axes{0, 1, 2, 3}; // fold both frame axes and both window axes back
    return add_node(g, op::Reduce, framed, kNoValue, kNoValue, narrow);
}

// A wide value with its outer axis of four REVERSED, then folded back — a `_flip` contribution. The
// reversal reads the same four elements in the opposite order, so the fold's sum is unchanged and it is
// `rearrange_test.cpp` that checks each output element came from (extent-1-i) of the source.
constexpr std::int32_t a_flipped_contribution(Graph& g, std::int32_t x) {
    const auto strip = add_value(g, ElemType::F64, Shape{4, kBlock}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(strip)].constant = 0.5;
    const auto band = add_node(g, op::Mul, x, strip);
    OpAttr turn{};
    turn.kind = AttrKind::View;
    turn.view.kind = ViewKind::Flip;
    turn.view.axis = 0;
    const auto flipped = add_node(g, op::Flip, band, kNoValue, kNoValue, turn);
    OpAttr narrow{};
    narrow.kind = AttrKind::Reduce;
    narrow.reduce.axes = Axes{0};
    return add_node(g, op::Reduce, flipped, kNoValue, kNoValue, narrow);
}

// A wide value with its outer axis of three CONSTANT-PADDED — one element in front, one behind, both
// margins reading a NON-ZERO fill — then folded back: a `_padc` contribution. Unlike every rearrangement
// above, the fold now sums two elements that came from NO source element: the margins read the constant,
// the three body elements read `(i - before)` of the source. A non-zero fill (2.0) is deliberate — a
// margin silently dropped, or filled with zero where the attribute says otherwise, would change the sum,
// so the run is a real check of the guarded read. `rearrange_pad_test.cpp` checks the per-element
// mapping; here it runs end to end.
constexpr std::int32_t a_padded_contribution(Graph& g, std::int32_t x) {
    const auto strip = add_value(g, ElemType::F64, Shape{3, kBlock}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(strip)].constant = 0.5;
    const auto band = add_node(g, op::Mul, x, strip);
    OpAttr grow{};
    grow.kind = AttrKind::View;
    grow.view.kind = ViewKind::Pad;
    grow.view.axis = 0;
    grow.view.pad_before = 1;
    grow.view.pad_after = 1;
    grow.view.pad_bits = std::bit_cast<std::uint64_t>(2.0); // the margins read 2.0, not zero
    const auto padded = add_node(g, op::Pad, band, kNoValue, kNoValue, grow);
    OpAttr narrow{};
    narrow.kind = AttrKind::Reduce;
    narrow.reduce.axes = Axes{0};
    return add_node(g, op::Reduce, padded, kNoValue, kNoValue, narrow);
}

// TWO wide values, each DERIVED from the one named input, JOINED along their outer axis into a wider one,
// then folded back: a `_concat` contribution. It is the first corpus entry over TWO operands with an
// element meaning — the contraction above reads two but folds; this SELECTS one operand per output
// element, the lower two of the four from the first, the upper two from the second. The two gains differ
// (0.5 and 0.25) so an element taken from the wrong operand, or a join that dropped one, would change the
// fold's sum. `rearrange_concat_test.cpp` checks the per-element mapping; here it runs end to end.
constexpr std::int32_t a_concatenated_contribution(Graph& g, std::int32_t x) {
    const auto lo_gain = add_value(g, ElemType::F64, Shape{2, kBlock}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(lo_gain)].constant = 0.5;
    const auto lo = add_node(g, op::Mul, x, lo_gain);
    const auto hi_gain = add_value(g, ElemType::F64, Shape{2, kBlock}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(hi_gain)].constant = 0.25;
    const auto hi = add_node(g, op::Mul, x, hi_gain);
    OpAttr join{};
    join.kind = AttrKind::View;
    join.view.kind = ViewKind::Concat;
    join.view.axis = 0;
    const auto joined = add_node(g, op::Concat, lo, hi, kNoValue, join);
    OpAttr narrow{};
    narrow.kind = AttrKind::Reduce;
    narrow.reduce.axes = Axes{0};
    return add_node(g, op::Reduce, joined, kNoValue, kNoValue, narrow);
}

// TWO wide values of the SAME shape, each DERIVED from the one named input, joined along their outer axis
// into one twice as tall — `[2,B]+[2,B] -> [4,B]` — then folded back: a planar pack, the `_concat` of two
// equal operands (once `_pack` of its own). The two gains differ (0.5 and 0.25) so a plane dropped or the
// two swapped moves the fold's sum.
constexpr std::int32_t a_packed_contribution(Graph& g, std::int32_t x) {
    const auto lo_gain = add_value(g, ElemType::F64, Shape{2, kBlock}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(lo_gain)].constant = 0.5;
    const auto lo = add_node(g, op::Mul, x, lo_gain);
    const auto hi_gain = add_value(g, ElemType::F64, Shape{2, kBlock}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(hi_gain)].constant = 0.25;
    const auto hi = add_node(g, op::Mul, x, hi_gain);
    OpAttr join{};
    join.kind = AttrKind::View;
    join.view.kind = ViewKind::Concat;
    join.view.axis = 0;
    const auto packed = add_node(g, op::Concat, lo, hi, kNoValue, join);
    OpAttr narrow{};
    narrow.kind = AttrKind::Reduce;
    narrow.reduce.axes = Axes{0};
    return add_node(g, op::Reduce, packed, kNoValue, kNoValue, narrow);
}

// A wide value of three frames of window four — `[3, 4, kBlock]` — OVERLAP-ADDED back into one axis of
// `out_len = (3-1)*2 + 4 = 8` at hop two, then folded to one number per sample: an `_ovadd` contribution,
// the adjoint of the `_unfold` above. The frames OVERLAP (hop below the window), so where two meet the
// output is their SUM — the fifth element-meaning kind (`ElementKind::Scatter`), RUNNABLE so unlike `_fft`
// it joins the numeric fold set. `spectral_ovadd_test.cpp` checks the scatter; here it runs end to end.
constexpr std::int32_t an_overlap_added_contribution(Graph& g, std::int32_t x) {
    const auto frames = add_value(g, ElemType::F64, Shape{3, 4, kBlock}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(frames)].constant = 0.5;
    const auto framed = add_node(g, op::Mul, x, frames);
    OpAttr recombine{};
    recombine.kind = AttrKind::OverlapAdd;
    recombine.overlap.window = 4;
    recombine.overlap.hop = 2;
    const auto joined = add_node(g, op::OverlapAdd, framed, kNoValue, kNoValue, recombine);
    OpAttr narrow{};
    narrow.kind = AttrKind::Reduce;
    narrow.reduce.axes = Axes{0}; // fold the reconstructed axis back to one number per sample
    return add_node(g, op::Reduce, joined, kNoValue, kNoValue, narrow);
}

// TWO wide values of two DISTINCT gains, JOINED into a `[4, kBlock]` axis of four values that DIFFER, then
// reduced to one number per sample by a QUANTILE at 0.5 — the median: an `_order` contribution, the fifth
// RUNNABLE element-meaning kind (`ElementKind::Ordered`). The two gains (0.5 and 0.25) make the four ordered
// values two distinct levels, so the median is the lerp of the two middle ranks — the ONE arithmetic an
// order op does — and a wrong rank selection or a batch mis-decomposition would move it. `order_test.cpp`
// checks the ranks by hand.
constexpr std::int32_t an_ordered_contribution(Graph& g, std::int32_t x) {
    const auto lo_gain = add_value(g, ElemType::F64, Shape{2, kBlock}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(lo_gain)].constant = 0.5;
    const auto lo = add_node(g, op::Mul, x, lo_gain);
    const auto hi_gain = add_value(g, ElemType::F64, Shape{2, kBlock}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(hi_gain)].constant = 0.25;
    const auto hi = add_node(g, op::Mul, x, hi_gain);
    OpAttr join{};
    join.kind = AttrKind::View;
    join.view.kind = ViewKind::Concat;
    join.view.axis = 0;
    const auto stacked = add_node(g, op::Concat, lo, hi, kNoValue, join); // [4, kBlock], four values that differ
    OpAttr median{};
    median.kind = AttrKind::Order;
    median.order.op = OrderOp::Quantile;
    median.order.axis = 0;
    median.order.q = 0.5; // the median collapses the ordered axis to one number per sample
    return add_node(g, op::Order, stacked, kNoValue, kNoValue, median);
}

// A wavetable READ folded back — a `_tblread` contribution (milestone M5, S6). The table is an
// `[N, kBlock]` buffer built the way the ordered contribution builds its wide operand: two
// differently-scaled copies of `x` joined to four entries that differ (`N=4`). The phase is `x`, one
// number per sample — `frac(phase)*N` wraps any finite phase into the table, so no domain constraint is
// needed — and the read's output has the phase's shape, one number per sample, so it folds straight back.
// `tblread_test.cpp` checks the interpolation by hand and points the attribute check at a broken table.
constexpr std::int32_t a_table_read_contribution(Graph& g, std::int32_t x) {
    const auto lo_gain = add_value(g, ElemType::F64, Shape{2, kBlock}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(lo_gain)].constant = 0.5;
    const auto lo = add_node(g, op::Mul, x, lo_gain);
    const auto hi_gain = add_value(g, ElemType::F64, Shape{2, kBlock}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(hi_gain)].constant = 0.25;
    const auto hi = add_node(g, op::Mul, x, hi_gain);
    OpAttr join{};
    join.kind = AttrKind::View;
    join.view.kind = ViewKind::Concat;
    join.view.axis = 0;
    const auto table = add_node(g, op::Concat, lo, hi, kNoValue, join); // [4, kBlock] = the [N] buffer
    OpAttr read{};
    read.kind = AttrKind::Table;
    read.table.size = 4; // N; the four-entry table above
    return add_node(g, op::TableRead, x, table, kNoValue, read);
}

// An index RAMP of four elements — `[0, 1, 2, 3]` at every sample — reduced to one number per sample: an
// `_iota` contribution (milestone M5, S6). It is the ONLY corpus entry with no operand: a generator takes no
// input, so unlike every contribution above it does not derive its wide value from `x`, it declares one.
// Its element meaning is the sixth RUNNABLE kind (`ElementKind::Indexed`), so it joins the numeric fold set.
// The four indices differ, so a runner that wrote the wrong index — or the same one to every element — would
// move the fold's sum (0+1+2+3=6). `iota_test.cpp` checks the ramp by hand and the refusals.
constexpr std::int32_t an_iota_contribution(Graph& g) {
    OpAttr ramp{};
    ramp.kind = AttrKind::Iota;
    ramp.generator.shape = Shape{4, kBlock}; // four elements over the block; its value at element e is e
    ramp.generator.type = ElemType::F64;
    const auto counted = add_node(g, op::Iota, kNoValue, kNoValue, kNoValue, ramp);
    OpAttr narrow{};
    narrow.kind = AttrKind::Reduce;
    narrow.reduce.axes = Axes{0};
    return add_node(g, op::Reduce, counted, kNoValue, kNoValue, narrow);
}

// A CONSTANT of four numbers at every sample, reduced to one number per sample: a `_const` contribution, the other
// generator (Plan/19-constants), its numbers in the graph's pool. Its element meaning is `ElementKind::Pooled`. The
// numbers differ and are not 0 … 3, so reading the wrong one, or a ramp's, moves the sum.
constexpr std::int32_t a_constant_contribution(Graph& g) {
    OpAttr pooled{};
    pooled.kind = AttrKind::Constant;
    pooled.generator.shape = Shape{4, kBlock};
    pooled.generator.type = ElemType::F64;
    pooled.generator.first = static_cast<std::int32_t>(g.constants.size());
    for (const double number : {0.5, -1.25, 2.0, 3.5}) {
        push_constant(g, number);
    }
    const auto pooled_value = add_node(g, op::Constant, kNoValue, kNoValue, kNoValue, pooled);
    OpAttr narrow{};
    narrow.kind = AttrKind::Reduce;
    narrow.reduce.axes = Axes{0};
    return add_node(g, op::Reduce, pooled_value, kNoValue, kNoValue, narrow);
}

} // namespace heinzel::testing
