// Tutorial 07 — autodiff — built on the OLD tree at c4012b8, run, its result printed in the order
// `tutorial_07_here.cpp` prints (tutorial/code/07_autodiff.cpp).
//
// The chapter fits K FIR taps to a hidden reference kernel by gradient descent through the compiled
// graph: loss = mean((conv(x,w) - t)^2), grad(loss, w) appended at capture time, the host running SGD
// between run() calls. It recovers the reference exactly. The only change from the chapter program is
// the OUTPUT: the parity harness compares numeric lines (compare_samples reads with %lf and stops at
// the first non-number), so this prints ONLY the K recovered taps and none of the header/PASS text.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>

#include "heinzel/frontend/autodiff.hpp"
#include "heinzel/frontend/builder.hpp"
#include "heinzel/frontend/einsum.hpp"
#include "heinzel/runtime/executor.hpp"

using namespace heinzel;

namespace {

constexpr int L = 64;        // probe signal length
constexpr int K = 8;         // FIR taps to recover
constexpr int O = L - K + 1; // valid-convolution output length

constexpr Graph build_fit() {
    GraphBuilder g;
    Input x{g, {.name = "x", .shape = {L}}};
    Input w{g, {.name = "w", .shape = {K}}};
    Input t{g, {.name = "t", .shape = {O}}};
    auto y = einsum("fk,k->f", x.unfold(K, 1), w); // valid FIR convolution
    auto loss = mean(abs2(y - t), Axes{0});
    loss.as_output("loss");
    grad(loss, w).as_output("dw"); // the whole backward pass, appended here
    return g;
}

} // namespace

int main() {
    std::array<float, K> wref{0.02f, -0.11f, 0.25f, 0.48f, 0.31f, -0.05f, 0.08f, -0.01f};
    std::array<float, L> x{};
    std::array<float, O> t{};
    for (int i = 0; i < L; ++i) {
        const float v = std::sin(static_cast<float>(i + 1)) * 43758.5453f;
        x[static_cast<std::size_t>(i)] = (v - std::floor(v)) - 0.5f;
    }
    for (int o = 0; o < O; ++o) {
        t[static_cast<std::size_t>(o)] = 0;
        for (int k = 0; k < K; ++k) {
            t[static_cast<std::size_t>(o)] += x[static_cast<std::size_t>(o + k)] * wref[static_cast<std::size_t>(k)];
        }
    }

    std::array<float, K> w{}; // start from zero
    std::array<float, K> dw{};
    float loss = 0;

    executor<build_fit()> ex; // forward + backward, one compiled schedule
    ex.input("x") = x.data();
    ex.input("w") = w.data();
    ex.input("t") = t.data();
    ex.output("loss") = &loss;
    ex.output("dw") = dw.data();

    constexpr float kLr = 0.04f;
    for (int step = 0; step < 4000; ++step) {
        ex.run();
        for (int k = 0; k < K; ++k) {
            w[static_cast<std::size_t>(k)] -= kLr * dw[static_cast<std::size_t>(k)]; // host-side SGD
        }
    }

    for (int k = 0; k < K; ++k) {
        std::printf("%.9e\n", static_cast<double>(w[static_cast<std::size_t>(k)]));
    }
    return 0;
}
