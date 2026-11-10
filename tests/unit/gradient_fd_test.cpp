// gradients() over a small elementwise+reduce graph, its analytic gradient checked against finite
// differences — the tree can RUN both, so the check is numbers against numbers.
//
// The graph is `loss = Σ_c exp(x · w_c)`, a sum over three channels of an exponential of the input
// scaled by a per-channel constant. Differentiating it exercises exactly S1's machinery: the reduce
// (Sum) adjoint spreads the loss's cotangent back over the collapsed channel axis, the exp adjoint
// multiplies by its own output, and the mul adjoint unbroadcasts the channel-wide contribution back
// down to the input's one-number-per-sample shape. gradients() appends all of that as ordinary nodes,
// so the same `run` that computes the loss computes the gradient value beside it.
//
// The finite-difference check is a genuine one: perturb the input, run the loss again, and compare
// (loss(x+ε) − loss(x))/ε to the gradient the adjoint nodes computed. Each sample is independent (the
// graph has no delay), so the per-sample forward difference is the per-sample gradient.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>

#include "heinzel/autodiff/gradients.hpp"
#include "heinzel/sema/analyze.hpp"

#include "hand_built_graph.hpp"
#include "interpret.hpp"

using namespace heinzel;

namespace {

constexpr std::size_t kBlock = 64;
constexpr std::int32_t kChannels = 3;
constexpr double kW = 0.5; // the per-channel scale, one constant standing for the whole {3,64} value

struct Built {
    Graph g{};
    std::int32_t loss{kNoValue};
    std::int32_t grad{kNoValue};
};

// x:{64} (one number per sample) → broadcast against w:{3,64} → exp → sum over the channel axis.
constexpr Built build() {
    Built b{};
    Graph& g = b.g;
    const auto x = add_value(g, ElemType::F64, Shape{kBlock}, ValueCategory::Input);
    const auto w = add_value(g, ElemType::F64, Shape{kChannels, kBlock}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(w)].constant = kW;

    const auto scaled = add_node(g, op::Mul, x, w);
    const auto expd = add_node(g, op::Exp, scaled);

    OpAttr collapse{};
    collapse.kind = AttrKind::Reduce;
    collapse.reduce.op = ReduceOp::Sum;
    collapse.reduce.axes = Axes{0};
    b.loss = add_node(g, op::Reduce, expd, kNoValue, kNoValue, collapse);

    g.inputs.push_back(NamedValue{Name{"x"}, x});
    g.outputs.push_back(NamedValue{Name{"loss"}, b.loss});

    const auto grads = autodiff::gradients(g, b.loss, {x});
    b.grad = grads[0];
    return b;
}

// Built at compile time: an adjoint rule that produced an ill-shaped node would fail the build here,
// not at run time (gradients() infers the augmented graph and would have refused).
constexpr Built kBuilt = build();

// Read the one loss number at sample n from a completed run.
template <std::size_t N>
double loss_at(const Signals<N>& sig, std::size_t n) {
    return sample_at(sig, kBuilt.loss, 0, n);
}

} // namespace

int main() {
    std::array<double, kBlock> x{};
    for (std::size_t n = 0; n < kBlock; ++n) {
        x[n] = 0.31 * static_cast<double>((n * 5) % 23) - 2.0; // the parity oracle's spread of magnitudes
    }

    Signals<kBlock> base{};
    run(kBuilt.g, x, base);

    // The gradient the adjoint nodes computed, one number per sample.
    std::array<double, kBlock> analytic{};
    for (std::size_t n = 0; n < kBlock; ++n) {
        analytic[n] = sample_at(base, kBuilt.grad, 0, n);
    }

    // Finite differences: perturb every sample by the same ε (each is independent) and re-run.
    const double eps = 1e-6;
    std::array<double, kBlock> perturbed_input{};
    for (std::size_t n = 0; n < kBlock; ++n) {
        perturbed_input[n] = x[n] + eps;
    }
    Signals<kBlock> bumped{};
    run(kBuilt.g, perturbed_input, bumped);

    int failures = 0;
    for (std::size_t n = 0; n < kBlock; ++n) {
        const double fd = (loss_at(bumped, n) - loss_at(base, n)) / eps;
        const double closed = analytic[n];
        // The independent cross-check: Σ_c w·exp(x·w) = 3·w·exp(w·x).
        const double expected = kChannels * kW * std::exp(kW * x[n]);
        const double tol = 1e-4 * (1.0 + std::fabs(closed));
        if (std::fabs(fd - closed) > tol || std::fabs(closed - expected) > tol) {
            std::printf("sample %zu: analytic %.9e  fd %.9e  expected %.9e\n", n, closed, fd, expected);
            ++failures;
        }
    }
    if (failures != 0) {
        std::printf("gradient FD check: %d samples disagreed\n", failures);
        return 1;
    }
    return 0;
}
