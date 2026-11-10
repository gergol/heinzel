// The `mlp_train` example — a 2-layer ReLU MLP trained by SGD through the compiled forward+backward
// graph — built on the OLD tree at c4012b8, run, and its loss trajectory printed in the same order as
// `mlp_train_here.cpp`.
//
// This is the old example's own graph and 600-step training loop verbatim (examples/mlp_train.cpp):
// `matmul(x,w1)+b1` → relu → `matmul(h,w2)+b2`, an MSE loss, and `gradients(loss, {w1,b1,w2,b2})`
// appending the whole backward pass; the host runs SGD between `run()` calls. The only change from the
// example is the OUTPUT: the parity harness compares numeric lines (`compare_samples` reads with `%lf`
// and stops at the first non-number), so this prints ONLY the loss at the six sampled steps the
// example itself reports (0,120,…,600) and none of the "step N loss …"/PASS text.
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
#include "heinzel/runtime/compile.hpp"
#include "heinzel/runtime/executor.hpp"

using namespace heinzel;

namespace {
constexpr int B = 16;
constexpr int IN = 4;
constexpr int H = 8;
constexpr int OUT = 2;

constexpr Graph build_mlp() {
    GraphBuilder g;
    Input x{g, "x", {B, IN}};
    Input w1{g, "w1", {IN, H}};
    Input b1{g, "b1", {H}};
    Input w2{g, "w2", {H, OUT}};
    Input b2{g, "b2", {OUT}};
    Input t{g, "t", {B, OUT}};

    auto h = max(matmul(x, w1) + b1, 0.0f); // relu hidden layer
    auto y = matmul(h, w2) + b2;
    auto loss = mean(abs2(y - t), Axes{0, 1});
    loss.as_output("loss");

    auto gs = gradients(loss, {w1, b1, w2, b2}); // one reverse sweep, four gradients
    gs[0].as_output("dw1");
    gs[1].as_output("db1");
    gs[2].as_output("dw2");
    gs[3].as_output("db2");
    return g;
}

// deterministic quasi-random init/data (no <random> — reproducible across runs and platforms)
float det(float seed, int i) { return std::sin(seed * static_cast<float>(i + 1) * 12.9898f) * 0.5f; }

// The example's probe x, its smooth nonlinear target t, and the two weight matrices (biases stay zero).
void init(std::array<float, B * IN>& x, std::array<float, B * OUT>& t, std::array<float, IN * H>& w1,
          std::array<float, H * OUT>& w2) {
    for (int i = 0; i < B * IN; ++i) {
        x[static_cast<std::size_t>(i)] = det(0.7f, i);
    }
    for (int b = 0; b < B; ++b) {
        float s = 0;
        for (int i = 0; i < IN; ++i) {
            s += x[static_cast<std::size_t>(b * IN + i)];
        }
        t[static_cast<std::size_t>(b * OUT + 0)] = std::tanh(s);
        t[static_cast<std::size_t>(b * OUT + 1)] = 0.5f * s * s - 0.2f;
    }
    for (int i = 0; i < IN * H; ++i) {
        w1[static_cast<std::size_t>(i)] = det(1.3f, i);
    }
    for (int i = 0; i < H * OUT; ++i) {
        w2[static_cast<std::size_t>(i)] = det(2.1f, i);
    }
}

} // namespace

int main() {
    executor<build_mlp()> ex;

    std::array<float, B * IN> x{};
    std::array<float, B * OUT> t{};
    std::array<float, IN * H> w1{};
    std::array<float, H> b1{};
    std::array<float, H * OUT> w2{};
    std::array<float, OUT> b2{};
    init(x, t, w1, w2);

    float loss = 0;
    std::array<float, IN * H> dw1{};
    std::array<float, H> db1{};
    std::array<float, H * OUT> dw2{};
    std::array<float, OUT> db2{};

    ex.input("x") = x.data();
    ex.input("t") = t.data();
    ex.input("w1") = w1.data();
    ex.input("b1") = b1.data();
    ex.input("w2") = w2.data();
    ex.input("b2") = b2.data();
    ex.output("loss") = &loss;
    ex.output("dw1") = dw1.data();
    ex.output("db1") = db1.data();
    ex.output("dw2") = dw2.data();
    ex.output("db2") = db2.data();

    constexpr float kLr = 0.1f;
    const auto sgd = [&](auto& p, const auto& dp) {
        for (std::size_t i = 0; i < p.size(); ++i) {
            p[i] -= kLr * dp[i];
        }
    };

    for (int step = 0; step <= 600; ++step) {
        ex.run();
        if (step % 120 == 0) { // the example's own sampling: 0,120,240,360,480,600
            std::printf("%.9e\n", static_cast<double>(loss));
        }
        sgd(w1, dw1);
        sgd(b1, db1);
        sgd(w2, dw2);
        sgd(b2, db2);
    }
    return 0;
}
