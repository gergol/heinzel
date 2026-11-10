// The order-statistics members (`frontend/ops.hpp`) — the user-facing way to spell sort/topk/quantile
// as an expression, milestone MO (frontend growth, item b). It appends exactly the `op::Order` node the
// hand-built `OrderAttr` path builds (`tests/unit/order_test.cpp`, `tests/parity/order_stats_here.cpp`),
// so it adds NO operation and NO meaning: the graph it produces is the same graph, checked by the same
// M5 analysis pass. This file pins that equivalence — the three shapes come out right, the carried
// attribute matches the hand one, and the axis refusals the op owes are shown rejecting THROUGH the
// builder.

#include <string_view>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"

using namespace heinzel;

namespace {

// The resolved shape of the single output of a one-order graph built with `make` over x{4,8} (element
// axis 0 of extent 4, streamed axis 1 of extent 8). `ok` reports whether it resolved at all.
template <typename Make>
constexpr Shape ordered_output_shape(Make make, bool& ok) {
    Input x{"x", Signal{4, 8}};
    Graph g = finish(Output{"y"} = make(x));
    ok = infer(g).ok;
    return ok ? g.values[static_cast<std::size_t>(g.outputs[0].value)].shape : Shape{};
}

} // namespace

// ---- the three shapes, built through the members, match `_order`'s M5 shape rule ----------------

// Sort keeps the ordered axis at its full extent: {4,8} sorted along axis 0 stays {4,8}.
static_assert([] {
    bool ok = false;
    const Shape s = ordered_output_shape([](Expr x) { return x.sort(0); }, ok);
    return ok && s == Shape{4, 8};
}(), "sort keeps the ordered axis at its extent");

// TopK resizes the ordered axis to k: {4,8} top-2 along axis 0 becomes {2,8}.
static_assert([] {
    bool ok = false;
    const Shape s = ordered_output_shape([](Expr x) { return x.topk(0, 2); }, ok);
    return ok && s == Shape{2, 8};
}(), "topk resizes the ordered axis to k");

// Quantile drops the ordered axis: {4,8} quantile along axis 0 becomes {8}.
static_assert([] {
    bool ok = false;
    const Shape s = ordered_output_shape([](Expr x) { return x.quantile(0, 0.5); }, ok);
    return ok && s == Shape{8};
}(), "quantile drops the ordered axis");

// median is the q=0.5 quantile: same drop, same shape.
static_assert([] {
    bool ok = false;
    const Shape s = ordered_output_shape([](Expr x) { return x.median(0); }, ok);
    return ok && s == Shape{8};
}(), "median is the q=0.5 quantile");

// ---- the carried attribute is exactly the hand-built one (no meaning added) -----------------------

// The OrderAttr the last (order) node of a one-order graph carries, built through `make`.
template <typename Make>
constexpr OrderAttr built_attr(Make make) {
    Input x{"x", Signal{4, 8}};
    Graph g = finish(Output{"y"} = make(x));
    const Node& n = g.nodes[g.nodes.size() - 1];
    // A caller reads .order only after checking these; the assertions below do.
    return (n.op == op::Order && n.attr.kind == AttrKind::Order) ? n.attr.order : OrderAttr{};
}

// TopK carries exactly the hand-built TopK{axis 0, k 3} — `tests/unit/order_test.cpp` spells the same.
static_assert(built_attr([](Expr x) { return x.topk(0, 3); })
                  == OrderAttr{OrderOp::TopK, /*axis*/ 0, /*k*/ 3, /*q*/ 0.0},
              "the topk builder carries exactly the hand-built OrderAttr");

// Sort carries k == 0, the value the hand path writes for a non-TopK op — NOT the struct default k == 1.
static_assert(built_attr([](Expr x) { return x.sort(0); })
                  == OrderAttr{OrderOp::Sort, /*axis*/ 0, /*k*/ 0, /*q*/ 0.0},
              "the sort builder carries the hand-built OrderAttr (k == 0, not the struct default 1)");

// Quantile (and median, its q=0.5 case) carry k == 0 and the quantile in q, exactly as the hand path.
static_assert(built_attr([](Expr x) { return x.quantile(0, 0.25); })
                  == OrderAttr{OrderOp::Quantile, /*axis*/ 0, /*k*/ 0, /*q*/ 0.25},
              "the quantile builder carries exactly the hand-built OrderAttr");
static_assert(built_attr([](Expr x) { return x.median(0); })
                  == OrderAttr{OrderOp::Quantile, /*axis*/ 0, /*k*/ 0, /*q*/ 0.5},
              "median is the q=0.5 quantile, carried exactly as the hand path");

// ---- the refusals, shown rejecting THROUGH the builder --------------------------------------------

// Ordering the streamed (innermost) axis is refused — it is time, and a run stands inside the sample loop.
static_assert([] {
    bool ok = true;
    ordered_output_shape([](Expr x) { return x.sort(1); }, ok);  // axis 1 of a signal {4, 8} is its time
    return !ok;
}(), "ordering the streamed axis is refused through the builder");

// An axis the value does not have is refused.
static_assert([] {
    bool ok = true;
    ordered_output_shape([](Expr x) { return x.sort(5); }, ok);  // rank is 2
    return !ok;
}(), "an out-of-range order axis is refused through the builder");

int main() { return 0; }
