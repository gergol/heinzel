// The apps benchmark's `softmax` family, built on the OLD tree at c4012b8, run, and its samples printed
// in the same order as `softmax_here.cpp`.
//
// This is `benchmarks/apps_bench.cpp`'s `g_softmax`: a numerically-stable row softmax
//     e = exp(x - max_c(x)) ; y = e / sum_c(e)
// over the columns of an [M, K] matrix, the max/sum taken with keepdims so they broadcast back. As an
// S6 CORRECTNESS corpus the ported content is that transcendental-plus-reduction pipeline. The old
// tree's headers come from the shared-object database at configure time (pinned to `main`); exact tier
// (`set_fast_math(false)`) pins the deterministic fold.
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

// `g_softmax` verbatim: row softmax over the K columns (axis 1), max/sum with keepdims.
constexpr Graph build() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input x{g, "x", {M, K}};
    Output y{g, "y", {M, K}};
    Expr e = exp(x - max(x, Axes{1}, true));
    y = e / sum(e, Axes{1}, true);
    return g;
}

inline constexpr auto kSoftmax = compile<build(), kTarget>();

} // namespace

int main() {
    std::array<float, M * K> x{};
    for (int i = 0; i < M * K; ++i) {
        x[static_cast<std::size_t>(i)] = 0.13f * static_cast<float>(i % 23) - 1.0f; // varied
    }
    std::array<float, M * K> y{};

    Executor<kSoftmax, scalar_backend> ex;
    ex.input("x") = x.data();
    ex.output("y") = y.data();
    ex.run();

    for (int e = 0; e < M * K; ++e) {
        std::printf("%.9e\n", static_cast<double>(y[static_cast<std::size_t>(e)]));
    }
    return 0;
}
