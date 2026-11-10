// What the analysis pass makes of an ORDER STATISTIC — its sort/topk/quantile shapes, its refusals, and the
// ranks it reads (milestone M5, S5).
//
// `_order` is order statistics along ONE static element axis, realized by a fixed compare-exchange NETWORK
// (`support/order_net.hpp`), NOT a monoid fold: data-dependent VALUES with a data-independent SCHEDULE, so
// every runner runs the same comparisons in the same order and they agree bit-exact. Sort
// keeps the axis ascending, TopK resizes it to the k largest (descending), Quantile drops it to one
// interpolated value. This file checks the three shapes, the refusals its attribute and operand owe
// SHOWN REJECTING deliberately broken inputs, the run-side ranks read by a hand-filled walk, and the batch
// indexing. The loop tree's order statistics are `nest_symbolic_stream_test.cpp`'s.

#include <array>
#include <string_view>

#include "heinzel/ir/ir.hpp"
#include "heinzel/sema/analyze.hpp"
#include "heinzel/support/order_net.hpp"

#include "hand_built_graph.hpp"
#include "interpret.hpp"
#include "order_element.hpp"

using namespace heinzel;

// A node carrying an order statistic of one operand. Its own helper so each case below states only what it
// varies — the statistic, the axis, and the selection its kind reads (k for TopK, q for Quantile).
namespace {
constexpr OpAttr order_attr(OrderOp op, std::int32_t axis, std::int32_t k, double q) {
    OpAttr a{};
    a.kind = AttrKind::Order;
    a.order.op = op;
    a.order.axis = axis;
    a.order.k = k;
    a.order.q = q;
    return a;
}
} // namespace

// ---- the three shapes (milestone M5, S5) ------------------------------------------------------------
//
// A SORT keeps the ordered axis at its extent. `{4, 8}` — four values along axis 0, a streamed axis of 8 —
// sorted along axis 0 stays `{4, 8}`.
static_assert([] {
    Graph g{};
    const auto in = add_value(g, ElemType::F32, Shape{4, 8}, ValueCategory::Input);
    const auto out = add_node(g, op::Order, in, kNoValue, kNoValue, order_attr(OrderOp::Sort, 0, 0, 0.0));
    return infer(g).ok && g.values[static_cast<std::size_t>(out)].shape == Shape{4, 8};
}(), "a sort keeps the ordered axis at its full extent");

// A TOP-K resizes the ordered axis to k. `{4, 8}` top-2 along axis 0 becomes `{2, 8}`.
static_assert([] {
    Graph g{};
    const auto in = add_value(g, ElemType::F32, Shape{4, 8}, ValueCategory::Input);
    const auto out = add_node(g, op::Order, in, kNoValue, kNoValue, order_attr(OrderOp::TopK, 0, 2, 0.0));
    return infer(g).ok && g.values[static_cast<std::size_t>(out)].shape == Shape{2, 8};
}(), "a top-k resizes the ordered axis to k");

// A QUANTILE drops the ordered axis. `{4, 8}` quantiled along axis 0 becomes `{8}` — one number per batch.
static_assert([] {
    Graph g{};
    const auto in = add_value(g, ElemType::F32, Shape{4, 8}, ValueCategory::Input);
    const auto out = add_node(g, op::Order, in, kNoValue, kNoValue, order_attr(OrderOp::Quantile, 0, 0, 0.5));
    return infer(g).ok && g.values[static_cast<std::size_t>(out)].shape == Shape{8};
}(), "a quantile drops the ordered axis");

// An OUTER axis is kept: `{2, 4, 8}` sorted along the MIDDLE axis (axis 1) stays `{2, 4, 8}`, the outer
// batch axis and the streamed axis passing through.
static_assert([] {
    Graph g{};
    const auto in = add_value(g, ElemType::F32, Shape{2, 4, 8}, ValueCategory::Input);
    const auto out = add_node(g, op::Order, in, kNoValue, kNoValue, order_attr(OrderOp::Sort, 1, 0, 0.0));
    return infer(g).ok && g.values[static_cast<std::size_t>(out)].shape == Shape{2, 4, 8};
}(), "an order keeps every batch axis, ordering only the axis it names");

// And a top-k over an outer axis, the batch axis outside it: `{2, 4, 8}` top-3 along axis 1 becomes
// `{2, 3, 8}` — the ordered axis resized, the outer axis kept and the streamed axis kept.
static_assert([] {
    Graph g{};
    const auto in = add_value(g, ElemType::F32, Shape{2, 4, 8}, ValueCategory::Input);
    const auto out = add_node(g, op::Order, in, kNoValue, kNoValue, order_attr(OrderOp::TopK, 1, 3, 0.0));
    return infer(g).ok && g.values[static_cast<std::size_t>(out)].shape == Shape{2, 3, 8};
}(), "a top-k over an outer axis keeps the batch axis and resizes only the ordered one");

// ---- the refusals, SHOWN REJECTING deliberately broken inputs ---------------------------------------

// A SYMBOLIC ordered extent has no static network to build, refused before the rest of the attribute check —
// board row 22, EXCLUDED (parity with the old tree). Every element axis must be known now.
static_assert([] {
    Graph g{};
    const auto in = add_value(g, ElemType::F32, Shape{Dim{0}, 8}, ValueCategory::Input);
    add_node(g, op::Order, in, kNoValue, kNoValue, order_attr(OrderOp::Sort, 0, 0, 0.0));
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() == std::string_view{"_order needs every element extent known now, but was given [?0, 8]"};
}(), "SHOWN REJECTING: a symbolic ordered extent is refused");
static_assert([] {
    Graph g{};
    const auto in = add_value(g, ElemType::F32, Shape{Dim{1}, 4, Dim{0}}, ValueCategory::Input);
    add_node(g, op::Order, in, kNoValue, kNoValue, order_attr(OrderOp::Sort, 1, 0, 0.0));
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() == std::string_view{"_order needs every element extent known now, but was given [?1, 4, ?0]"};
}(), "SHOWN REJECTING: a symbolic element extent is refused, though another axis is ordered");

// A symbolic element axis that reaches a view first is refused by the view, node by node: here a pad of it, which has
// no literal to grow, before the order would refuse it as not known now. No declaration spells a symbolic element
// axis, so only a hand-built graph can do this.
static_assert([] {
    Graph g{};
    const auto in = add_value(g, ElemType::F32, Shape{Dim{0}, 8}, ValueCategory::Input);
    OpAttr pad{};
    pad.kind = AttrKind::View;
    pad.view.kind = ViewKind::Pad;
    pad.view.axis = 0;
    pad.view.pad_before = 1;
    pad.view.pad_after = 1;
    const auto padded = add_node(g, op::Pad, in, kNoValue, kNoValue, pad);
    add_node(g, op::Order, padded, kNoValue, kNoValue, order_attr(OrderOp::Quantile, 0, 0, 0.5));
    return infer(g).message.view() == std::string_view{"_padc pads axis 0, whose size is not known yet; bind it or declare its extent"};
}(), "SHOWN REJECTING: a symbolic element axis padded before an order is the pad's refusal");

// ...but the STREAM's length may come later: a graph is compiled once for every signal length. Only the
// element axes are asked.
static_assert([] {
    Graph g{};
    const auto in = add_value(g, ElemType::F32, Shape{5, Dim{0}}, ValueCategory::Input);
    const auto out = add_node(g, op::Order, in, kNoValue, kNoValue, order_attr(OrderOp::Quantile, 0, 0, 0.5));
    return infer(g).ok && g.values[static_cast<std::size_t>(out)].shape == Shape{Dim{0}};
}(), "a median over an element axis of a symbolic-length stream analyses");

// The STREAMED axis is time — a run stands inside the sample loop, so ordering it would order samples that do
// not all exist yet. `{4, 8}` has its streamed axis at index 1; ordering it is refused by name.
static_assert([] {
    Graph g{};
    const auto in = add_value(g, ElemType::F32, Shape{4, 8}, ValueCategory::Input);
    add_node(g, op::Order, in, kNoValue, kNoValue, order_attr(OrderOp::Sort, 1, 0, 0.0));
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"_order orders the streamed axis, which is time; order an element axis"};
}(), "SHOWN REJECTING: ordering the streamed axis is refused");

// An axis the value does not have cannot be ordered. `{4, 8}` is rank 2; axis 5 is refused with both numbers.
static_assert([] {
    Graph g{};
    const auto in = add_value(g, ElemType::F32, Shape{4, 8}, ValueCategory::Input);
    add_node(g, op::Order, in, kNoValue, kNoValue, order_attr(OrderOp::Sort, 5, 0, 0.0));
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() == std::string_view{"_order orders axis 5 of a value of rank 2"};
}(), "SHOWN REJECTING: an axis the value does not have is refused");

// The ordered extent is bounded because the network is a fixed-capacity compile-time object; over the
// ceiling it is refused NAMING THE OVERRIDE (board row 23's workaround verbatim). `{257, 8}` exceeds 256.
static_assert([] {
    Graph g{};
    const auto in = add_value(g, ElemType::F32, Shape{257, 8}, ValueCategory::Input);
    add_node(g, op::Order, in, kNoValue, kNoValue, order_attr(OrderOp::Sort, 0, 0, 0.0));
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"_order orders an axis of 257; the ceiling is 256 (raise it with -D "
                                "HEINZEL_MAX_ORDER_EXTENT)"};
}(), "SHOWN REJECTING: an ordered extent over the ceiling is refused, naming the override");

// A TOP-K count outside `[1, L]` names a rank that does not exist. `{4, 8}` top-9 is refused with both.
static_assert([] {
    Graph g{};
    const auto in = add_value(g, ElemType::F32, Shape{4, 8}, ValueCategory::Input);
    add_node(g, op::Order, in, kNoValue, kNoValue, order_attr(OrderOp::TopK, 0, 9, 0.0));
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"_order keeps the top 9 of an axis of 4; a top-k count is between one and "
                                "the ordered extent"};
}(), "SHOWN REJECTING: a top-k count past the ordered extent is refused");

// A QUANTILE outside `[0, 1]` is not a quantile. `q = 1.5` is refused by name.
static_assert([] {
    Graph g{};
    const auto in = add_value(g, ElemType::F32, Shape{4, 8}, ValueCategory::Input);
    add_node(g, op::Order, in, kNoValue, kNoValue, order_attr(OrderOp::Quantile, 0, 0, 1.5));
    const Diagnostic d = infer(g);
    return !d.ok && d.message.view() == std::string_view{"_order takes a quantile outside [0, 1]"};
}(), "SHOWN REJECTING: a quantile outside [0, 1] is refused");

// ---- the run side: the ranks a hand-filled walk reads ------------------------------------------------
//
// A `{4, 8}` value ordered along axis 0, its four elements filled with a known permutation `3, 1, 4, 2` at
// one sample, walked one step. Sort reads them ascending; TopK the k largest descending; Quantile the
// interpolated rank. This exercises the whole walk — gather, network, write — over numbers that DIFFER, so a
// mapping that read the wrong element or wrote the wrong rank would move a number a check below names.
namespace {
constexpr Graph ordering(OrderOp op, std::int32_t axis, std::int32_t k, double q) {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{4, 8}, ValueCategory::Input);
    add_node(g, op::Order, x, kNoValue, kNoValue, order_attr(op, axis, k, q));
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the ordering graph did not resolve");
    }
    return g;
}

template <std::size_t Results>
constexpr std::array<double, Results> ordered(OrderOp op, std::int32_t k, double q) {
    const Graph g = ordering(op, 0, k, q);
    Signals<8> signals{};
    lay_out(g, signals);
    const double vals[4] = {3.0, 1.0, 4.0, 2.0};
    for (std::int32_t e = 0; e < 4; ++e) {
        signals.at[static_cast<std::size_t>(signals.first[0] + e)][0] = vals[e];
    }
    detail::walk_order_step(g, g.nodes[0], 0, signals);
    std::array<double, Results> out{};
    for (std::size_t i = 0; i < Results; ++i) {
        out[i] = sample_at(signals, g.nodes[0].output, static_cast<std::int32_t>(i), 0);
    }
    return out;
}
} // namespace

// Sort: `3, 1, 4, 2` ascending is `1, 2, 3, 4`.
static_assert(ordered<4>(OrderOp::Sort, 0, 0.0) == std::array<double, 4>{1.0, 2.0, 3.0, 4.0},
              "a sort reads the ordered axis ascending");

// Top-2: the two LARGEST, sorted descending — `4, 3`, with `out[0]` the maximum.
static_assert(ordered<2>(OrderOp::TopK, 2, 0.0) == std::array<double, 2>{4.0, 3.0},
              "a top-k reads the k largest, descending, the max first");

// Quantile at 0.5: the median of `1, 2, 3, 4` is the lerp of the two middles, `(2 + 3)/2 = 2.5`.
static_assert(ordered<1>(OrderOp::Quantile, 0, 0.5) == std::array<double, 1>{2.5},
              "a quantile at 0.5 is the median — the lerp of the two middle ranks for even L");

// Quantile at 0 is the minimum (an exact rank, no interpolation); at 1 the maximum.
static_assert(ordered<1>(OrderOp::Quantile, 0, 0.0) == std::array<double, 1>{1.0},
              "a quantile at 0 is the minimum, an exact rank");
static_assert(ordered<1>(OrderOp::Quantile, 0, 1.0) == std::array<double, 1>{4.0},
              "a quantile at 1 is the maximum, an exact rank");

// Two GROUPS, sorted INDEPENDENTLY: a `{2, 4, 8}` value sorted along the MIDDLE axis (axis 1) is two
// batches of four, each network run on its own window. Group 0's elements `3, 1, 4, 2` and group 1's
// `8, 6, 7, 5` must come out `1, 2, 3, 4` and `5, 6, 7, 8` — a batch mis-decomposition would mix the two.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{2, 4, 8}, ValueCategory::Input);
    add_node(g, op::Order, x, kNoValue, kNoValue, order_attr(OrderOp::Sort, 1, 0, 0.0));
    if (!infer(g).ok) {
        fail("the two-group ordering graph did not resolve");
    }
    Signals<8> signals{};
    lay_out(g, signals);
    const double vals[8] = {3.0, 1.0, 4.0, 2.0, 8.0, 6.0, 7.0, 5.0}; // group 0 = 0..3, group 1 = 4..7
    for (std::int32_t e = 0; e < 8; ++e) {
        signals.at[static_cast<std::size_t>(signals.first[0] + e)][0] = vals[e];
    }
    detail::walk_order_step(g, g.nodes[0], 0, signals);
    const double expect[8] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0};
    for (std::int32_t e = 0; e < 8; ++e) {
        if (sample_at(signals, g.nodes[0].output, e, 0) != expect[e]) {
            return false;
        }
    }
    return true;
}(), "two groups are ordered independently, each network run on its own window");

// ---- the batch indexing of the reference walk's order ------------------------------------------------
//
// The seam decomposes a group across the NON-ordered axes and recomposes against the operand's extents, so a
// value with a batch axis OUTSIDE the ordered one indexes the right group. Operand `{2, 4}` element axes, the
// ordered axis 1 (extent 4): there are two groups. Group 1's third value (t = 2) is element `1*4 + 2 = 6`.
static_assert([] {
    const Elements from = elements_of(Shape{2, 4, 8}); // element axes {2, 4}, streamed 8
    return from.rank == 2 && from.count == 8 &&
           order_group_input(from, 1, 1, 2) == 6 && order_group_input(from, 1, 0, 0) == 0;
}(), "an order gathers each group's values along the ordered axis, batched over the others");

// And the kept output places rank r at the ordered axis while the batch axis passes through: a Sort of
// `{2, 4}` keeps `{2, 4}`, so group 1's rank 3 writes element `1*4 + 3 = 7`. A dropped (Quantile) output of
// `{2}` writes group 1 to element 1 — the batch position, the ordered axis gone.
static_assert([] {
    const Elements from = elements_of(Shape{2, 4, 8});
    const Elements kept = elements_of(Shape{2, 4, 8});     // Sort keeps the shape
    const Elements dropped = elements_of(Shape{2, 8});     // Quantile drops the ordered axis
    return order_group_output(kept, from, 1, true, 1, 3) == 7 &&
           order_group_output(dropped, from, 1, false, 1, 0) == 1;
}(), "a kept output places the rank at the ordered axis; a dropped output writes the batch position");

int main() { return 0; }
