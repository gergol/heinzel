// The `mlp_train` example's FORWARD pass, built on the OLD tree at c4012b8, run, and its output printed
// in the same order as `mlp_forward_here.cpp`.
//
// The old tree's headers come from the shared-object database at configure time (pinned to `main`), the
// same as the other `_there` programs. This is the old example's own forward — `y = matmul(relu(matmul
// (x,w1)+b1), w2)+b2` — with its `gradients()`/loss/SGD training loop DROPPED: that half needs a
// frontend autodiff surface this tree does not have (F-M7-GRAPH2GRAPH), so there is nothing on the here
// side to compare a backward pass to. `set_fast_math(false)` is pinned so the contractions take the
// deterministic ascending-K accumulation.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>

#include "heinzel/backends/scalar_backend.hpp"
#include "heinzel/frontend/builder.hpp"
#include "heinzel/frontend/einsum.hpp"
#include "heinzel/runtime/compile.hpp"
#include "heinzel/runtime/executor.hpp"

using namespace heinzel;

namespace {

constexpr TargetInfo kTarget{.logical_cores = 1, .l1_cache_bytes = 32 * 1024, .simd_width_bytes = 32};
constexpr int B = 16;
constexpr int IN = 4;
constexpr int H = 8;
constexpr int OUT = 2;

constexpr Graph build_forward() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input x{g, "x", {B, IN}};
    Input w1{g, "w1", {IN, H}};
    Input b1{g, "b1", {H}};
    Input w2{g, "w2", {H, OUT}};
    Input b2{g, "b2", {OUT}};
    Output y{g, "y", {B, OUT}};
    auto h = max(matmul(x, w1) + b1, 0.0f); // relu hidden layer
    y = matmul(h, w2) + b2;
    return g;
}

inline constexpr auto kSched = compile<build_forward(), kTarget>();

float det(float seed, int i) { return std::sin(seed * static_cast<float>(i + 1) * 12.9898f) * 0.5f; }

} // namespace

int main() {
    std::array<float, B * IN> x{};
    std::array<float, IN * H> w1{};
    std::array<float, H> b1{}; // zero
    std::array<float, H * OUT> w2{};
    std::array<float, OUT> b2{}; // zero
    for (int i = 0; i < B * IN; ++i) {
        x[static_cast<std::size_t>(i)] = det(0.7f, i);
    }
    for (int i = 0; i < IN * H; ++i) {
        w1[static_cast<std::size_t>(i)] = det(1.3f, i);
    }
    for (int i = 0; i < H * OUT; ++i) {
        w2[static_cast<std::size_t>(i)] = det(2.1f, i);
    }

    std::array<float, B * OUT> y{};
    Executor<kSched, scalar_backend> engine;
    engine.input("x") = x.data();
    engine.input("w1") = w1.data();
    engine.input("b1") = b1.data();
    engine.input("w2") = w2.data();
    engine.input("b2") = b2.data();
    engine.output("y") = y.data();
    engine.run();

    for (int e = 0; e < B * OUT; ++e) {
        std::printf("%a\n", static_cast<double>(y[e]));
    }
    return 0;
}
