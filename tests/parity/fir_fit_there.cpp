// The `fir_fit` example — differentiable FIR coefficient fit — built on the OLD tree at c4012b8, run,
// and its RESULT printed in the same order as `fir_fit_here.cpp`.
//
// This is the old example's own graph and training loop verbatim: unfold→einsum FIR, an MSE loss, and
// `gradients(loss, {w, gain})` appending the backward pass, all compiled into one schedule; the host
// runs SGD between `run()` calls. The only change from `examples/fir_fit.cpp` is the OUTPUT: the parity
// harness compares numeric lines (`compare_samples` reads with `%lf` and stops at the first non-number),
// so this prints ONLY the K effective taps `gain*w[k]` — the quantity the loss constrains and the fixed
// point both trees converge to — and none of the step trajectory or PASS text.
//
// The old headers come from the shared-object database pinned to `main`, as every other `_there` does.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>

#include "heinzel/backends/scalar_backend.hpp"
#include "heinzel/frontend/autodiff.hpp"
#include "heinzel/frontend/builder.hpp"
#include "heinzel/frontend/einsum.hpp"
#include "heinzel/runtime/executor.hpp"

using namespace heinzel;

namespace {
constexpr int L = 64; // signal length
constexpr int K = 5;  // taps
constexpr int F = L - K + 1; // valid frames

constexpr Graph build_fit() {
    GraphBuilder g;
    Input x{g, "x", {L}};  // the probe signal
    Input w{g, "w", {K}};  // the trainable FIR taps (an input the host mutates)
    Input t{g, "t", {F}};  // the target response
    Param gain{g, "gain"}; // a trainable scalar knob

    auto y = einsum("fk,k->f", x.unfold(K, 1), w) * gain; // frames · taps
    auto loss = mean(abs2(y - t), Axes{0});
    loss.as_output("loss");

    auto gs = gradients(loss, {w, gain}); // ONE reverse sweep for both targets
    gs[0].as_output("dw");
    gs[1].as_output("dgain");
    return g;
}

} // namespace

int main() {
    executor<build_fit()> ex; // the whole compiler (forward + adjoint) ran at compile time

    // probe signal + target = reference kernel applied at gain 1.5. The probe is full-band pseudo-noise
    // (a K-tap FIR has K degrees of freedom; a narrow-band probe leaves the fit rank-deficient).
    constexpr std::array<float, K> kRef = {0.20f, -0.35f, 0.55f, -0.15f, 0.40f};
    std::array<float, L> x{};
    for (int i = 0; i < L; ++i) {
        const float v = std::sin(static_cast<float>(i + 1)) * 43758.5453f;
        x[static_cast<std::size_t>(i)] = (v - std::floor(v)) - 0.5f;
    }
    std::array<float, F> t{};
    for (int f = 0; f < F; ++f) {
        float acc = 0;
        for (int k = 0; k < K; ++k) {
            acc += x[static_cast<std::size_t>(f + k)] * kRef[static_cast<std::size_t>(k)];
        }
        t[static_cast<std::size_t>(f)] = 1.5f * acc;
    }

    std::array<float, K> w{}; // zero-initialized taps
    float gain = 1.0f;
    float loss = 0;
    std::array<float, K> dw{};
    float dgain = 0;

    ex.input("x") = x.data();
    ex.input("w") = w.data();
    ex.input("t") = t.data();
    ex.output("loss") = &loss;
    ex.output("dw") = dw.data();
    ex.output("dgain") = &dgain;

    constexpr float kLr = 0.2f;
    for (int step = 0; step <= 400; ++step) {
        ex.param("gain") = gain;
        ex.run();
        for (int k = 0; k < K; ++k) {
            w[static_cast<std::size_t>(k)] -= kLr * dw[static_cast<std::size_t>(k)];
        }
        gain -= kLr * dgain;
    }

    // gain·w is what the loss constrains — print the effective kernel, one tap per line, nothing else.
    for (int k = 0; k < K; ++k) {
        std::printf("%.9e\n", static_cast<double>(gain * w[static_cast<std::size_t>(k)]));
    }
    return 0;
}
