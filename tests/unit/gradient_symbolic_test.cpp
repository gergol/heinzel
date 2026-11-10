// M7 S6 (M7.3 — symbolic extents on the differentiated path, the E7 lift). `gradients()` runs on a
// graph whose extent is still a SYMBOL: the reverse walk reads symbolic shapes, and where the old tree
// rejected the whole graph, this one differentiates it. The condition names one case in particular —
// R4-2, "a reduction whose mean needs a scalar computed from an extent at run time" — because a mean's
// adjoint scales by 1/N, and when N is a symbol that reciprocal cannot be a compile-time constant. On
// the symbolic branch `rules_reduce.hpp` builds N as a run-time value (a ones buffer of the operand's
// shape, summed over the reduced axes) and divides by it; after a binding substitutes the symbol, that
// value resolves to the literal count, so the symbolic gradient equals its literal twin's.
//
// THE ORACLE, in O7's spirit: a symbol bound to N is a SUBSTITUTION, so a bound symbolic graph must
// AGREE with its literal-N twin. Here the differentiated graphs are not required to be structurally
// equal (the symbolic branch legitimately records the ones-sum reciprocal the literal branch folds to
// a constant), so agreement is checked where O7's execution half checks it: on the numbers. Graph one
// is the R4-2 reduction, checked against an independent analytic gradient AND its literal twin. Graph
// two is S3's MLP with its BATCH extent symbolic, differentiated and bound, checked against the SAME
// numpy golden the literal MLP meets (`gen_grad_golden.py`) — a symbolic golden, green.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>

#include "heinzel/autodiff/gradients.hpp"
#include "heinzel/sema/analyze.hpp"

#include "../goldens/grad_golden.hpp"
#include "hand_built_graph.hpp"
#include "interpret.hpp"

using namespace heinzel;
namespace gg = heinzel_golden::grad;

namespace {

constexpr std::size_t kBlock = 1;
constexpr std::int32_t kSymBatch = 5;         // the reduction's symbolic row count, bound at run time
constexpr std::array<double, 5> kX = {0.5, -1.5, 2.0, 0.25, -0.75};

// loss = mean(x*x) over the (symbolic) row axis; d loss/dx = 2x/N. Built as a function of the row
// extent so the same code produces the literal twin and the symbolic graph. gradients() infers with
// NO bindings, so the symbol survives into the reverse walk and the mean adjoint takes its run-time
// path. `x` is [rows, 1]: rows an element axis, the trailing 1 the single streamed step.
struct Reduction {
    Graph g{};
    std::int32_t x{kNoValue};
    std::int32_t loss{kNoValue};
    std::int32_t grad{kNoValue};
};

constexpr Reduction build_reduction(Extent rows) {
    Reduction b{};
    Graph& g = b.g;
    b.x = add_value(g, ElemType::F64, Shape{rows, 1}, ValueCategory::Input);
    g.inputs.push_back(NamedValue{Name{"x"}, b.x});
    const auto sq = add_node(g, op::Mul, b.x, b.x);
    OpAttr mean{};
    mean.kind = AttrKind::Reduce;
    mean.reduce.op = ReduceOp::Mean;
    mean.reduce.axes = Axes{0}; // collapse the row axis — the symbolic extent
    b.loss = add_node(g, op::Reduce, sq, kNoValue, kNoValue, mean);
    g.outputs.push_back(NamedValue{Name{"loss"}, b.loss});
    b.grad = autodiff::gradients(g, b.loss, {b.x})[0];
    return b;
}

constexpr Reduction kReductionLiteral = build_reduction(literal_extent(kSymBatch));
constexpr Reduction kReductionSymbolic = build_reduction(Extent{Dim{0}});

// The symbol really did survive gradients(): unbound, the gradient value's row axis is still a symbol.
static_assert(is_symbol(extent_at(kReductionSymbolic.g.values[static_cast<std::size_t>(kReductionSymbolic.grad)].shape, 0)),
              "the gradient of a symbolic-extent input keeps the symbol until it is bound");

template <std::size_t K>
void fill_input(Signals<kBlock>& out, std::int32_t value, const std::array<double, K>& buf) {
    for (std::int32_t e = 0; e < static_cast<std::int32_t>(K); ++e) {
        detail::write(out, value, e, 0, buf[static_cast<std::size_t>(e)]);
    }
}

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

// Bind the graph (substituting any symbol), lay it out, fill x, step every node once. Returns nothing;
// the caller reads gradients out of `out`.
void run_bound(Graph g, std::int32_t x, const Bindings& sizes, Signals<kBlock>& out) {
    if (const Diagnostic d = infer(g, sizes); !d.ok) {
        std::printf("binding the symbolic gradient graph failed to infer\n");
        return;
    }
    lay_out(g, out);
    fill_consts(g, out);
    fill_input(out, x, kX);
    for (const Node& node : g.nodes) {
        detail::walk_step(g, node, 0, out);
    }
}

int check_reduction() {
    int failures = 0;

    Signals<kBlock> lit{};
    run_bound(kReductionLiteral.g, kReductionLiteral.x, Bindings{}, lit);

    Signals<kBlock> sym{};
    run_bound(kReductionSymbolic.g, kReductionSymbolic.x, Bindings{}.bind(Dim{0}, kSymBatch), sym);

    for (std::int32_t e = 0; e < kSymBatch; ++e) {
        const double analytic = 2.0 * kX[static_cast<std::size_t>(e)] / static_cast<double>(kSymBatch);
        const double g_lit = sample_at(lit, kReductionLiteral.grad, e, 0);
        const double g_sym = sample_at(sym, kReductionSymbolic.grad, e, 0);
        if (std::fabs(g_sym - analytic) > 1e-9 * (1.0 + std::fabs(analytic))) {
            std::printf("reduction dX[%d]: symbolic %.12e  analytic %.12e\n", e, g_sym, analytic);
            ++failures;
        }
        if (std::fabs(g_sym - g_lit) > 1e-12 * (1.0 + std::fabs(g_lit))) {
            std::printf("reduction dX[%d]: symbolic %.12e disagrees with literal twin %.12e\n", e, g_sym, g_lit);
            ++failures;
        }
    }
    return failures;
}

// S3's MLP layer with its BATCH extent symbolic — loss = mean((relu(x@w+b) − t)^2), differentiated wrt
// {x, w, b}. The symbol flows through the contraction (a batch axis it carries, not the K it collapses)
// and through the mean over [B,N] (whose reciprocal is now run-time, B being symbolic). Bound to gg::B
// it must reproduce the SAME numpy golden the literal MLP meets — a symbolic golden, green.
struct MlpSym {
    Graph g{};
    std::int32_t x{kNoValue};
    std::int32_t w{kNoValue};
    std::int32_t bias{kNoValue};
    std::int32_t t{kNoValue};
    std::int32_t loss{kNoValue};
    std::int32_t dX{kNoValue};
    std::int32_t dW{kNoValue};
    std::int32_t dB{kNoValue};
};

constexpr std::int32_t unsqueeze(Graph& g, std::int32_t v, std::int32_t axis) {
    OpAttr a{};
    a.kind = AttrKind::View;
    a.view.kind = ViewKind::Unsqueeze;
    a.view.axis = axis;
    return add_node(g, op::Reshape, v, kNoValue, kNoValue, a);
}

constexpr std::int32_t contract_k(Graph& g, std::int32_t a, std::int32_t b) {
    OpAttr s{};
    s.kind = AttrKind::Contract;
    s.contract.axes = Axes{1};
    return add_node(g, op::Contract, a, b, kNoValue, s);
}

constexpr MlpSym build_mlp_sym() {
    MlpSym b{};
    Graph& g = b.g;
    b.x = add_value(g, ElemType::F64, Shape{Dim{0}, gg::K, 1}, ValueCategory::Input); // batch is the symbol
    b.w = add_value(g, ElemType::F64, Shape{gg::K, gg::N, 1}, ValueCategory::Input);
    b.bias = add_value(g, ElemType::F64, Shape{gg::N, 1}, ValueCategory::Input);
    b.t = add_value(g, ElemType::F64, Shape{Dim{0}, gg::N, 1}, ValueCategory::Input);
    g.inputs.push_back(NamedValue{Name{"x"}, b.x});
    g.inputs.push_back(NamedValue{Name{"w"}, b.w});
    g.inputs.push_back(NamedValue{Name{"b"}, b.bias});
    g.inputs.push_back(NamedValue{Name{"t"}, b.t});

    const auto z = contract_k(g, unsqueeze(g, b.x, 2), unsqueeze(g, b.w, 0)); // [B,N,1]
    const auto zb = add_node(g, op::Add, z, b.bias);
    const auto zero = add_value(g, ElemType::F64, Shape{}, ValueCategory::Const);
    const auto y = add_node(g, op::Max, zb, zero);
    const auto diff = add_node(g, op::Sub, y, b.t);
    const auto sq = add_node(g, op::Mul, diff, diff);
    OpAttr mean{};
    mean.kind = AttrKind::Reduce;
    mean.reduce.op = ReduceOp::Mean;
    mean.reduce.axes = Axes{0, 1};
    b.loss = add_node(g, op::Reduce, sq, kNoValue, kNoValue, mean);
    g.outputs.push_back(NamedValue{Name{"loss"}, b.loss});

    const auto grads = autodiff::gradients(g, b.loss, {b.x, b.w, b.bias});
    b.dX = grads[0];
    b.dW = grads[1];
    b.dB = grads[2];
    return b;
}

constexpr MlpSym kMlpSym = build_mlp_sym();

template <std::size_t K>
void fill_golden(Signals<kBlock>& out, std::int32_t value, const std::array<float, K>& buf) {
    for (std::int32_t e = 0; e < static_cast<std::int32_t>(K); ++e) {
        detail::write(out, value, e, 0, static_cast<double>(buf[static_cast<std::size_t>(e)]));
    }
}

template <std::size_t K>
int check_grad(const char* name, const Signals<kBlock>& sig, std::int32_t grad, const std::array<float, K>& want) {
    int failures = 0;
    for (std::int32_t e = 0; e < static_cast<std::int32_t>(K); ++e) {
        const double got = sample_at(sig, grad, e, 0);
        const double w = static_cast<double>(want[static_cast<std::size_t>(e)]);
        if (std::fabs(got - w) > 5e-3 * (1.0 + std::fabs(w))) {
            std::printf("%s[%d]: symbolic-MLP %.9e  golden %.9e\n", name, e, got, w);
            ++failures;
        }
    }
    return failures;
}

int check_mlp_sym() {
    Graph g = kMlpSym.g;
    if (const Diagnostic d = infer(g, Bindings{}.bind(Dim{0}, gg::B)); !d.ok) {
        std::printf("binding the symbolic MLP gradient graph failed to infer\n");
        return 1;
    }
    Signals<kBlock> sig{};
    lay_out(g, sig);
    fill_consts(g, sig);
    fill_golden(sig, kMlpSym.x, gg::X);
    fill_golden(sig, kMlpSym.w, gg::W);
    fill_golden(sig, kMlpSym.bias, gg::BIAS);
    fill_golden(sig, kMlpSym.t, gg::T);
    for (const Node& node : g.nodes) {
        detail::walk_step(g, node, 0, sig);
    }

    int failures = 0;
    const double loss = sample_at(sig, kMlpSym.loss, 0, 0);
    if (std::fabs(loss - gg::loss_mlp) > 1e-4 * (1.0 + std::fabs(gg::loss_mlp))) {
        std::printf("symbolic-MLP loss: %.9e  golden %.9e\n", loss, static_cast<double>(gg::loss_mlp));
        ++failures;
    }
    failures += check_grad("dW", sig, kMlpSym.dW, gg::dW);
    failures += check_grad("dB", sig, kMlpSym.dB, gg::dB);
    failures += check_grad("dX", sig, kMlpSym.dX, gg::dX);
    return failures;
}

} // namespace

int main() {
    int failures = check_reduction() + check_mlp_sym();
    if (failures != 0) {
        std::printf("symbolic-extent gradients: %d checks disagreed\n", failures);
        return 1;
    }
    std::printf("symbolic-extent gradients: R4-2 reduction + symbolic-batch MLP golden green\n");
    return 0;
}
