// The apps benchmark's `rmsnorm` family, built on the OLD tree at c4012b8, run, and its samples printed
// in the same order as `rmsnorm_here.cpp`.
//
// This is `benchmarks/apps_bench.cpp`'s `g_rmsnorm`: y = x / sqrt(mean_c(x^2) + eps) over the columns of
// an [M, K] matrix, the mean taken with keepdims so it broadcasts back. As an S6 CORRECTNESS corpus the
// ported content is that RMS normalization — the sqrt transcendental over a row mean of squares, a
// different core from softmax's exp. The old tree's headers come from the shared-object database at
// configure time (pinned to `main`); exact tier (`set_fast_math(false)`) pins the deterministic fold.
#include <array>
#include <cstddef>
#include <cstdio>

#include "heinzel/backends/scalar_backend.hpp"
#include "heinzel/frontend/builder.hpp"
#include "heinzel/runtime/compile.hpp"
#include "heinzel/runtime/executor.hpp"

using namespace heinzel;

namespace {

constexpr int M = 4; // small correctness corpus, not the bench's 1024x256 sweep
constexpr int K = 6;

constexpr TargetInfo kTarget{
    .logical_cores = 1,
    .l1_cache_bytes = 16 * 1024,
    .simd_width_bytes = 16,
};

// `g_rmsnorm` verbatim: y = x / sqrt(mean_c(x^2) + eps), eps=1e-6, over the K columns (axis 1).
constexpr Graph build() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input x{g, "x", {M, K}};
    Output y{g, "y", {M, K}};
    y = x / sqrt(mean(x * x, Axes{1}, true) + 1e-6f);
    return g;
}

inline constexpr auto kRms = compile<build(), kTarget>();

} // namespace

int main() {
    std::array<float, M * K> x{};
    for (int i = 0; i < M * K; ++i) {
        x[static_cast<std::size_t>(i)] = 0.13f * static_cast<float>(i % 23) - 1.0f; // varied
    }
    std::array<float, M * K> y{};

    Executor<kRms, scalar_backend> ex;
    ex.input("x") = x.data();
    ex.output("y") = y.data();
    ex.run();

    for (int e = 0; e < M * K; ++e) {
        std::printf("%.9e\n", static_cast<double>(y[static_cast<std::size_t>(e)]));
    }
    return 0;
}
