// The image_pipeline benchmark's `vblur` family, built on the OLD tree at c4012b8, run, and its samples
// printed in the same order as `vblur_here.cpp`.
//
// This is `benchmarks/image_pipeline_bench.cpp`'s `g_vblur_fused(H,W,K)`: a pointwise square producer
// p = x*x feeding a K-tap VERTICAL (row-axis) windowed reduce, y = sum(unfold_rows(p,K,1)*w, Axes{2}).
// As an S6 CORRECTNESS corpus the ported content is that fused square-then-vertical-conv; the bench's
// point (that the model fuses the producer into the conv rather than materializing p) is a scheduling
// property, numerically neutral. The old tree's headers come from the shared-object database at
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

constexpr int H = 8; // small correctness corpus, not the bench's large image
constexpr int W = 12;
constexpr int K = 5;
constexpr int OH = H - K + 1;

constexpr TargetInfo kTarget{
    .logical_cores = 1,
    .l1_cache_bytes = 16 * 1024,
    .simd_width_bytes = 16,
};

// `g_vblur_fused` verbatim: p = x*x, then a K-tap vertical (row) windowed reduce.
constexpr Graph build() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input x{g, "x", {H, W}};
    Input w{g, "w", {K}};
    Output y{g, "y", {OH, W}};
    Expr p = x * x;
    y = sum(unfold_rows(p, K, 1) * w, Axes{2}); // [H-K+1, W]
    return g;
}

inline constexpr auto kVblur = compile<build(), kTarget>();

} // namespace

int main() {
    std::array<float, H * W> x{};
    for (int i = 0; i < H * W; ++i) {
        x[static_cast<std::size_t>(i)] = 0.13f * static_cast<float>(i % 23) - 1.0f; // varied
    }
    std::array<float, K> w{};
    for (int k = 0; k < K; ++k) {
        w[static_cast<std::size_t>(k)] = 0.1f * static_cast<float>(k + 1); // non-uniform taps
    }
    std::array<float, OH * W> y{};

    Executor<kVblur, scalar_backend> ex;
    ex.input("x") = x.data();
    ex.input("w") = w.data();
    ex.output("y") = y.data();
    ex.run();

    for (int e = 0; e < OH * W; ++e) {
        std::printf("%.9e\n", static_cast<double>(y[static_cast<std::size_t>(e)]));
    }
    return 0;
}
