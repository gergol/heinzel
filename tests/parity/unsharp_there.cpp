// The image_pipeline benchmark's `unsharp` family, built on the OLD tree at c4012b8, run, and its
// samples printed in the same order as `unsharp_here.cpp`.
//
// This is `benchmarks/image_pipeline_bench.cpp`'s `g_unsharp(H,W,K)`: a K-tap vertical blur of x, then
// an unsharp-mask combine y = (1+alpha)*center(x) - alpha*b, where center(x) is x cropped to the K/2
// row band that aligns with the "valid" blur output. As an S6 CORRECTNESS corpus the ported content is
// that blur-plus-combine, including the center-crop ALIGNMENT (a wrong crop offset would move y). The
// old tree's headers come from the shared-object database at configure time (pinned to `main`); exact
// tier (`set_fast_math(false)`) pins the deterministic fold.
#include <array>
#include <cstddef>
#include <cstdio>

#include "heinzel/backends/scalar_backend.hpp"
#include "heinzel/frontend/builder.hpp"
#include "heinzel/runtime/compile.hpp"
#include "heinzel/runtime/executor.hpp"

using namespace heinzel;

namespace {

constexpr int H = 8; // small correctness corpus (under kMaxPlanes), not the bench's large image
constexpr int W = 8;
constexpr int K = 5;
constexpr int OH = H - K + 1;
constexpr float kAlpha = 0.5f; // the bench's alpha

constexpr TargetInfo kTarget{
    .logical_cores = 1,
    .l1_cache_bytes = 16 * 1024,
    .simd_width_bytes = 16,
};

// `g_unsharp` verbatim: vertical blur, then y = (1+alpha)*center(x) - alpha*b. center(x) is the K/2 row
// band, a flat slice + reshape (views are addressing only).
constexpr Graph build() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input x{g, "x", {H, W}};
    Input w{g, "w", {K}};
    Output y{g, "y", {OH, W}};
    Expr b = sum(unfold_rows(x, K, 1) * w, Axes{2}); // [H-K+1, W]
    Expr xc = reshape(slice(reshape(x, Shape{H * W}), (K / 2) * W, OH * W), Shape{OH, W});
    y = xc * (1.0f + kAlpha) - b * kAlpha;
    return g;
}

inline constexpr auto kUnsharp = compile<build(), kTarget>();

} // namespace

int main() {
    std::array<float, H * W> x{};
    for (int i = 0; i < H * W; ++i) {
        x[static_cast<std::size_t>(i)] = 0.13f * static_cast<float>(i % 23) - 1.0f; // varied
    }
    // Blur taps summing to 1 (a real blur) so the center and blur terms are comparable in magnitude and
    // the crop alignment is genuinely exercised — not a vacuous large-b combine.
    std::array<float, K> w{0.15f, 0.2f, 0.3f, 0.2f, 0.15f};
    std::array<float, OH * W> y{};

    Executor<kUnsharp, scalar_backend> ex;
    ex.input("x") = x.data();
    ex.input("w") = w.data();
    ex.output("y") = y.data();
    ex.run();

    for (int e = 0; e < OH * W; ++e) {
        std::printf("%.9e\n", static_cast<double>(y[static_cast<std::size_t>(e)]));
    }
    return 0;
}
