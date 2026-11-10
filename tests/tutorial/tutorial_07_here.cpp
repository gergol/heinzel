// Tutorial 07 — autodiff — built in THIS tree's language, run, and its result printed.
//
// The chapter (tutorial/code/07_autodiff.cpp) fits K FIR taps to a hidden reference kernel by gradient
// descent through the compiled graph: loss = mean((conv(x,w) - t)^2), the whole backward appended at
// capture time, the host running SGD between run() calls. It is fir_fit's sibling — the same
// differentiable FIR, without the redundant gain, so the taps recover the reference DIRECTLY. It is
// portable for the same reason (MG): `gradients(loss, {…})` composes the frontend graph with the
// closed M7 autodiff, and names loss + dw together.
//
// The FIR is the old dsp::fir lowering — reduce(unfold(x,K,1) * w, Sum, tap axis) — not an einsum:
// einsum("fk,k->f") lowers to the general _matmul (MB) whose adjoint is unwritten, whereas the
// weight-and-reduce lowers to _unfold (View), * and _reduce (Sum), every op the M7 autodiff already
// differentiates. No new op, no new numeric meaning. The fit has no time: x{L}, w{1,K} (broadcast over
// the frame axis) and t{O} are tensors. It runs on `Executor` at `High`, where the loop tree computes as
// the reference walk does, the choice fir_fit made, and every run is checked against the walk on the same
// taps, bit for bit. Both trees descend to the SAME reference kernel wref with the host update in float;
// the residual gap is measured and accounted for in tests/CMakeLists.txt (BECAUSE).
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "heinzel/frontend/graph_builder.hpp"

#include "executed.hpp"
#include "gradients_capture.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t L = 64;
constexpr std::int32_t K = 8;
constexpr std::int32_t O = L - K + 1;

// loss = mean_f (reduce(unfold(x,K,1) * w, Sum, tap axis) - t)^2, with dloss/dw.
constexpr Graph build() {
    Input x{"x", {L}};
    Input w{"w", {1, K}}; // taps, broadcast over the frame axis
    Input t{"t", {O}};

    Expr frames = x.unfold(0, K);  // {O, K, 1}
    Expr y = (frames * w).sum(1);   // {O, 1}
    Expr d = y - t;                 // {O, 1}
    Output loss{"loss"};
    loss = (d * d).mean(0);         // {1}
    return gradients(loss, {Grad{"dw", w}}, numerics::Precision::High);
}

} // namespace

int main() {
    constexpr std::array<float, K> wref{0.02f, -0.11f, 0.25f, 0.48f, 0.31f, -0.05f, 0.08f, -0.01f};
    std::array<float, L> x{};
    for (int i = 0; i < L; ++i) {
        const float v = std::sin(static_cast<float>(i + 1)) * 43758.5453f;
        x[static_cast<std::size_t>(i)] = (v - std::floor(v)) - 0.5f;
    }
    std::array<float, O> t{};
    for (int o = 0; o < O; ++o) {
        float acc = 0;
        for (int k = 0; k < K; ++k) {
            acc += x[static_cast<std::size_t>(o + k)] * wref[static_cast<std::size_t>(k)];
        }
        t[static_cast<std::size_t>(o)] = acc;
    }

    std::array<float, K> w{}; // start from zero, mutated in place across runs
    std::array<float, K> dw{};

    Executor<build> ex;
    ex.input("x") = x.data();
    ex.input("w") = w.data();
    ex.input("t") = t.data();
    ex.output("dw") = dw.data();

    // The update runs in float, as the old side's host loop does.
    constexpr float kLr = 0.04f;
    for (int step = 0; step < 4000; ++step) {
        ex.run(); // recomputes from the current w
        const testing::Outputs walk =
            testing::walked<build>({{"x", x.data(), L}, {"w", w.data(), K}, {"t", t.data(), O}});
        testing::require_walked("dw", dw, walk[1]); // the outputs: loss, dw
        for (int k = 0; k < K; ++k) {
            float& wk = w[static_cast<std::size_t>(k)];
            wk = wk - kLr * dw[static_cast<std::size_t>(k)];
        }
    }

    for (int k = 0; k < K; ++k) {
        std::printf("%.9e\n", static_cast<double>(w[static_cast<std::size_t>(k)]));
    }
    return 0;
}
