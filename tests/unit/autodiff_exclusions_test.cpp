// M7 S9 (M7.6 — the residual autodiff exclusions, enumerated and asserted as a whole). Extends oracle
// O9. What `gradients()` does NOT differentiate after M7 is written down here, each row SHOWN FAILING:
// a graph is built that puts the excluded thing on the differentiated path, and gradients() is required
// to OBJECT carrying the row's marker. The board is asserted whole — if any refusal silently stops
// firing (the most dangerous parity break, because nothing else notices a rejection that quietly starts
// accepting), that row shows NO THROW and this test fails.
//
// The rows, and where each refusal lives:
//   E5  a strided slice (step > 1)                 rules_structural.hpp — the flip workaround differentiates
//   E6  a non-innermost / 2-D unfold               rules_structural.hpp — only innermost 1-D framing has an adjoint
//   fft the spectral adjoint (_fft)                gradients.hpp — this tree's fft runs no numbers until M13 (F-M7-FFT-NOTRUN)
//   order  an order statistic on the path          the rule table's Excluded — old board row 19, now live
//   recur  a recurrence on the path                the rule table's Excluded — old board row 20, now live
//
// These EXTEND the M5.9 board (`exclusion_board_test.cpp`): its rows 19 ("autodiff through an order
// statistic") and 20 ("autodiff through stateful / dynamic paths"), NotApplicable "M7" until now, are
// re-dispositioned Excluded there and shown failing HERE. E5/E6/fft are this tree's own autodiff
// residuals — refusals the old tree did not have in this form (its fft ran, so it lifted the fft
// exclusion; here that reason is absent, F-M7-FFT-NOTRUN). The symbolic-operated-axis structural case
// is NOT here: a slice/pad/unfold over a symbolic axis is refused at the FORWARD infer (M5.7), so
// gradients() never reaches its adjoint — it is a core exclusion, not an autodiff residual.
#include <cstdio>
#include <string_view>

#include "heinzel/autodiff/gradients.hpp"
#include "heinzel/sema/analyze.hpp"

#include "filters.hpp"
#include "hand_built_graph.hpp"

using namespace heinzel;

namespace {

struct Built {
    Graph g{};
    std::int32_t loss{kNoValue};
    std::int32_t wrt{kNoValue};
};

constexpr std::int32_t mean_all(Graph& g, std::int32_t v, Axes axes) {
    OpAttr m{};
    m.kind = AttrKind::Reduce;
    m.reduce.op = ReduceOp::Mean;
    m.reduce.axes = axes;
    return add_node(g, op::Reduce, v, kNoValue, kNoValue, m);
}

// A one-operand graph loss = mean(f(x)^2), wrt x, where f is the excluded view the caller appended.
Built square_loss(Graph g, std::int32_t x, std::int32_t f, Axes axes) {
    Built b{};
    b.g = g;
    b.wrt = x;
    b.loss = mean_all(b.g, add_node(b.g, op::Mul, f, f), axes);
    b.g.outputs.push_back(NamedValue{Name{"l"}, b.loss});
    return b;
}

Built build_e5_strided_slice() {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{8, 1}, ValueCategory::Input);
    g.inputs.push_back(NamedValue{Name{"x"}, x});
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Slice;
    k.view.axis = 0;
    k.view.offset = 0;
    k.view.length = 4;
    k.view.step = 2; // strided — forward-legal, adjoint E5
    const auto sl = add_node(g, op::Slice, x, kNoValue, kNoValue, k);
    return square_loss(g, x, sl, Axes{0});
}

Built build_e6_noninnermost_unfold() {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{8, 8, 1}, ValueCategory::Input);
    g.inputs.push_back(NamedValue{Name{"x"}, x});
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Unfold;
    k.view.axis = 0; // not the innermost element axis (R-2 == 1) — E6
    k.view.window = 3;
    k.view.hop = 1;
    const auto u = add_node(g, op::Unfold, x, kNoValue, kNoValue, k);
    return square_loss(g, x, u, Axes{0, 1, 2});
}

Built build_fft() {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{8}, ValueCategory::Input);
    g.inputs.push_back(NamedValue{Name{"x"}, x});
    OpAttr k{};
    k.kind = AttrKind::Fft;
    k.fft.n = 8;
    const auto f = add_node(g, op::Fft, x, kNoValue, kNoValue, k);
    return square_loss(g, x, f, Axes{0});
}

Built build_order() {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{4, 1}, ValueCategory::Input);
    g.inputs.push_back(NamedValue{Name{"x"}, x});
    OpAttr k{};
    k.kind = AttrKind::Order;
    k.order.op = OrderOp::Sort;
    k.order.axis = 0;
    const auto o = add_node(g, op::Order, x, kNoValue, kNoValue, k);
    return square_loss(g, x, o, Axes{0});
}

Built build_recurrence() {
    Built b{};
    b.g = biquad(literal_extent(8));
    b.wrt = b.g.inputs[0].value;
    b.loss = mean_all(b.g, b.g.outputs[0].value, Axes{0});
    return b;
}

// One row: build the excluded graph, differentiate, require a throw carrying the marker.
int refuses(const char* name, const char* marker, Built (*build)()) {
    Built b = build();
    try {
        autodiff::gradients(b.g, b.loss, {b.wrt});
    } catch (const error& e) {
        if (e.message().find(marker) == std::string_view::npos) {
            std::printf("%s: refused, but the message does not carry '%s': %.*s\n", name, marker,
                        static_cast<int>(e.message().size()), e.message().data());
            return 1;
        }
        std::printf("%s: refused — %.*s\n", name, static_cast<int>(e.message().size()), e.message().data());
        return 0;
    }
    std::printf("%s: NOT REFUSED — a residual exclusion silently started accepting\n", name);
    return 1;
}

} // namespace

int main() {
    int failures = 0;
    failures += refuses("E5 strided slice", "E5", build_e5_strided_slice);
    failures += refuses("E6 non-innermost unfold", "E6", build_e6_noninnermost_unfold);
    failures += refuses("fft spectral adjoint", "fft", build_fft);
    failures += refuses("order statistic (board row 19)", "_order", build_order);
    failures += refuses("recurrence (board row 20)", "_recur", build_recurrence);

    if (failures != 0) {
        std::printf("autodiff residual board: %d rows are not shown failing\n", failures);
        return 1;
    }
    std::printf("autodiff residual board: all six residual exclusions shown failing\n");
    return 0;
}
