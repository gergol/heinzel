// M7 S7 (M7.5 — excluded adjoints fail only on the differentiated path, at a named point). An
// operation whose adjoint this tree does not carry (a recurrence, a delay, an order statistic, a table
// read — the rule table's `AdjointClass::Excluded` rows) is a NAMED refusal when a cotangent actually reaches it, and
// nothing at all when the same operation sits in the graph off the path being differentiated. Shown
// both ways, with the excluded operation pointed at a differentiated input and observed to object.
//
// THE OPERATION here is `_order` (a sort along an element axis — board row 19, "autodiff through an
// order statistic"). It is chosen over a delay because it acts on an ELEMENT axis, so a static
// hand-built graph places it on or off the differentiated path without the streamed-axis framing a
// delay would drag in; the mechanism it exercises is the same for every `Excluded` row (the driver
// skips a node whose output carries no cotangent, so `adjoint_of` only meets an excluded operation that
// is genuinely active). The refusal NAMES the operation, so an active recurrence would name `_recur`
// rather than a generic class — the "message carrying the decision" the condition asks for.
//
// ON the path: loss = mean(sort(x)^2) differentiated wrt x — the sort is varied (built from x) and
// useful (feeds the loss), so a cotangent reaches it and gradients() throws naming `_order`. OFF the
// path: loss = mean((x - sort(s))^2) wrt x, where s is a SECOND input, not a target — sort(s) is not
// varied, receives no cotangent, and the graph differentiates fine, its gradient the exact 2(x-sort(s))/N.
#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>

#include "heinzel/autodiff/gradients.hpp"
#include "heinzel/sema/analyze.hpp"
#include "heinzel/support/fail.hpp"

#include "filters.hpp"
#include "hand_built_graph.hpp"
#include "interpret.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t kN = 4;
constexpr std::size_t kBlock = 1;
constexpr std::array<double, 4> kXvals = {0.5, -1.0, 2.0, 0.25};
constexpr std::array<double, 4> kSvals = {0.3, -0.7, 1.1, -0.2};

constexpr OpAttr sort_attr(std::int32_t axis) {
    OpAttr a{};
    a.kind = AttrKind::Order;
    a.order.op = OrderOp::Sort;
    a.order.axis = axis;
    return a;
}

constexpr std::int32_t mean_axis0(Graph& g, std::int32_t v) {
    OpAttr mean{};
    mean.kind = AttrKind::Reduce;
    mean.reduce.op = ReduceOp::Mean;
    mean.reduce.axes = Axes{0};
    return add_node(g, op::Reduce, v, kNoValue, kNoValue, mean);
}

// loss = mean(sort(x)^2), wrt x. The sort is ON the differentiated path — a cotangent reaches it.
bool on_path_rejects(std::string& message) {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{kN, 1}, ValueCategory::Input);
    g.inputs.push_back(NamedValue{Name{"x"}, x});
    const auto o = add_node(g, op::Order, x, kNoValue, kNoValue, sort_attr(0));
    const auto loss = mean_axis0(g, add_node(g, op::Mul, o, o));
    g.outputs.push_back(NamedValue{Name{"loss"}, loss});
    try {
        autodiff::gradients(g, loss, {x});
    } catch (const error& e) {
        message = std::string(e.message()); // the view dies with the exception
        return true;
    }
    return false;
}

// An active RECURRENCE names itself, not its feedback delay. The biquad y[n] = x[n] + a·y[n-1] +
// b·y[n-2] is `… _fdelay _mul _add _fdelay _mul _add _recur`: the `_recur` is the OUTPUT, so the reverse
// walk reaches it FIRST and fails there — the E1 (`_recur`), never its feedback E2 (`_fdelay`) taps,
// which sit upstream in the recurrence body and are never reached. The attribution the plan's "two-pass
// active-path detection" asks for holds by the reverse-walk order, needing no extra pass. Returns the
// refusal message.
bool recurrence_names_recur(std::string& message) {
    Graph g = biquad(literal_extent(8));
    const std::int32_t x = g.inputs[0].value;
    const std::int32_t loss = mean_axis0(g, g.outputs[0].value); // a scalar loss over the streamed axis
    try {
        autodiff::gradients(g, loss, {x});
    } catch (const error& e) {
        message = std::string(e.message()); // the view dies with the exception
        return true;
    }
    return false;
}

// A matrix-product einsum (`_matmul`) on the path: its refusal says which einsum, since an elementwise one
// (`"ik,ik->i"`, a multiply and a sum) differentiates. loss = sum(a·b) wrt a. Returns the refusal message.
bool matmul_is_named_as_its_einsum(std::string& message) {
    Graph g{};
    const auto a = add_value(g, ElemType::F64, Shape{2, 3, 1}, ValueCategory::Input);
    const auto b = add_value(g, ElemType::F64, Shape{3, 2, 1}, ValueCategory::Input);
    g.inputs.push_back(NamedValue{Name{"a"}, a});
    g.inputs.push_back(NamedValue{Name{"b"}, b});
    OpAttr mm{};
    mm.kind = AttrKind::MatMul;
    mm.matmul.a_contract = 1;
    mm.matmul.b_contract = 0;
    const auto product = add_node(g, op::MatMul, a, b, kNoValue, mm); // [2, 2, 1]
    OpAttr sum{};
    sum.kind = AttrKind::Reduce;
    sum.reduce.axes = Axes{0, 1};
    const auto loss = add_node(g, op::Reduce, product, kNoValue, kNoValue, sum);
    g.outputs.push_back(NamedValue{Name{"loss"}, loss});
    try {
        autodiff::gradients(g, loss, {a});
    } catch (const error& e) {
        message = std::string(e.message());
        return true;
    }
    return false;
}

// loss = mean((x - sort(s))^2), wrt x. sort(s) is OFF the path (s is an input, not a target, so it does
// not vary). Returns the built graph and the gradient value id.
struct OffPath {
    Graph g{};
    std::int32_t x{kNoValue};
    std::int32_t s{kNoValue};
    std::int32_t grad{kNoValue};
};

OffPath build_off_path() {
    OffPath b{};
    Graph& g = b.g;
    b.x = add_value(g, ElemType::F64, Shape{kN, 1}, ValueCategory::Input);
    b.s = add_value(g, ElemType::F64, Shape{kN, 1}, ValueCategory::Input); // second input, never a wrt
    g.inputs.push_back(NamedValue{Name{"x"}, b.x});
    g.inputs.push_back(NamedValue{Name{"s"}, b.s});
    const auto os = add_node(g, op::Order, b.s, kNoValue, kNoValue, sort_attr(0));
    const auto diff = add_node(g, op::Sub, b.x, os);
    const auto loss = mean_axis0(g, add_node(g, op::Mul, diff, diff));
    g.outputs.push_back(NamedValue{Name{"loss"}, loss});
    b.grad = autodiff::gradients(g, loss, {b.x})[0]; // must NOT throw — the sort is off the path
    return b;
}

void fill_input(Signals<kBlock>& out, std::int32_t value, const std::array<double, 4>& buf) {
    for (std::int32_t e = 0; e < kN; ++e) {
        detail::write(out, value, e, 0, buf[static_cast<std::size_t>(e)]);
    }
}

// Fill every Const value's planes — including the constants gradients() emitted (the loss cotangent's
// ones seed, the mean's 1/N factor, the bcast_back ones buffers). Without this the adjoint reads them
// as zero and every gradient collapses to zero.
void fill_consts(const Graph& g, Signals<kBlock>& out) {
    for (std::size_t i = 0; i < g.values.size(); ++i) {
        if (g.values[i].category != ValueCategory::Const) {
            continue;
        }
        for (std::int32_t e = 0; e < elements_of(g.values[i].shape).count; ++e) {
            detail::write(out, static_cast<std::int32_t>(i), e, 0, g.values[i].constant);
        }
    }
}

int check_off_path(const OffPath& b) {
    Graph g = b.g;
    if (const Diagnostic d = infer(g); !d.ok) {
        std::printf("off-path graph did not infer\n");
        return 1;
    }
    Signals<kBlock> sig{};
    lay_out(g, sig);
    fill_consts(g, sig);
    fill_input(sig, b.x, kXvals);
    fill_input(sig, b.s, kSvals);
    for (const Node& node : g.nodes) {
        detail::walk_step(g, node, 0, sig);
    }
    std::array<double, 4> sorted = kSvals; // OrderOp::Sort is ascending — the analytic oracle sorts too
    std::sort(sorted.begin(), sorted.end());

    int failures = 0;
    for (std::int32_t e = 0; e < kN; ++e) {
        const double want = 2.0 * (kXvals[static_cast<std::size_t>(e)] - sorted[static_cast<std::size_t>(e)]) /
                            static_cast<double>(kN);
        const double got = sample_at(sig, b.grad, e, 0);
        if (std::fabs(got - want) > 1e-9 * (1.0 + std::fabs(want))) {
            std::printf("off-path dX[%d]: tree %.12e  analytic %.12e\n", e, got, want);
            ++failures;
        }
    }
    return failures;
}

} // namespace

int main() {
    int failures = 0;

    std::string message;
    if (!on_path_rejects(message)) {
        std::printf("on-path: gradients() did NOT reject an _order on the differentiated path\n");
        ++failures;
    } else if (message.find("_order") == std::string::npos) {
        std::printf("on-path: the refusal does not name the excluded operation: '%.*s'\n",
                    static_cast<int>(message.size()), message.data());
        ++failures;
    } else {
        std::printf("on-path rejected, named: %.*s\n", static_cast<int>(message.size()), message.data());
    }

    std::string recur_msg;
    if (!recurrence_names_recur(recur_msg)) {
        std::printf("recurrence: gradients() did NOT reject an active recurrence\n");
        ++failures;
    } else if (recur_msg.find("_recur") == std::string::npos) {
        std::printf("recurrence: an active recurrence is not named _recur (E1 vs E2): '%.*s'\n",
                    static_cast<int>(recur_msg.size()), recur_msg.data());
        ++failures;
    } else {
        std::printf("recurrence named itself (E1, not the feedback E2): %.*s\n",
                    static_cast<int>(recur_msg.size()), recur_msg.data());
    }

    std::string matmul_msg;
    if (!matmul_is_named_as_its_einsum(matmul_msg) ||
        matmul_msg != "heinzel::gradients: a matrix-product einsum (not elementwise) has no adjoint (excluded)") {
        std::printf("matmul: the refusal is not the matrix-product einsum's: '%.*s'\n", static_cast<int>(matmul_msg.size()),
                    matmul_msg.data());
        ++failures;
    }

    failures += check_off_path(build_off_path());

    if (failures != 0) {
        std::printf("excluded-adjoint both-ways: %d checks disagreed\n", failures);
        return 1;
    }
    std::printf("excluded-adjoint both-ways: on-path named refusal + off-path clean gradient green\n");
    return 0;
}
