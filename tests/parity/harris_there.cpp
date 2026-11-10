// The image_pipeline benchmark's `harris` family, built on the OLD tree at c4012b8, run, and its
// samples printed in the same order as `harris_here.cpp`.
//
// This is `benchmarks/image_pipeline_bench.cpp`'s `g_harris(H,W)` — the wide-DAG corner detector:
//     dx = sum(unfold2d(x,3,3)*wx, {2,3}) ; dy = sum(unfold2d(x,3,3)*wy, {2,3})   (3x3 gradient stencils)
//     ixx = dx*dx ; ixy = dx*dy ; iyy = dy*dy                                      (the products stage)
//     sxx = sum(unfold2d(ixx,3,3), {2,3}) ; sxy,syy likewise                       (3x3 box sums)
//     y = sxx*syy - sxy*sxy - 0.04*(sxx+syy)^2                                      (the Harris response)
// As an S6 CORRECTNESS corpus the ported content is that whole multi-stage response, and it is the one
// image_pipeline family that exercises the 2-D windowed view `unfold2d` under numeric pressure (twice,
// nested). The old tree's headers come from the shared-object database at configure time (pinned to
// `main`); exact tier (`set_fast_math(false)`) pins the deterministic fold.
#include <array>
#include <cstddef>
#include <cstdio>

#include "heinzel/backends/scalar_backend.hpp"
#include "heinzel/frontend/builder.hpp"
#include "heinzel/runtime/compile.hpp"
#include "heinzel/runtime/executor.hpp"

using namespace heinzel;

namespace {

constexpr int H = 6; // small correctness corpus (output (H-4)x(W-4)), not the bench's large image
constexpr int W = 6;
constexpr int OH = H - 4;
constexpr int OW = W - 4;

constexpr TargetInfo kTarget{
    .logical_cores = 1,
    .l1_cache_bytes = 16 * 1024,
    .simd_width_bytes = 16,
};

// `g_harris` verbatim.
constexpr Graph build() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input x{g, "x", {H, W}};
    Input wx{g, "wx", {3, 3}};
    Input wy{g, "wy", {3, 3}};
    Output y{g, "y", {OH, OW}};
    Expr dx = sum(unfold2d(x, 3, 3) * wx, Axes{2, 3});
    Expr dy = sum(unfold2d(x, 3, 3) * wy, Axes{2, 3});
    Expr ixx = dx * dx;
    Expr ixy = dx * dy;
    Expr iyy = dy * dy;
    Expr sxx = sum(unfold2d(ixx, 3, 3), Axes{2, 3});
    Expr sxy = sum(unfold2d(ixy, 3, 3), Axes{2, 3});
    Expr syy = sum(unfold2d(iyy, 3, 3), Axes{2, 3});
    Expr tr = sxx + syy;
    y = sxx * syy - sxy * sxy - 0.04f * tr * tr;
    return g;
}

inline constexpr auto kHarris = compile<build(), kTarget>();

} // namespace

int main() {
    std::array<float, H * W> x{};
    for (int i = 0; i < H * W; ++i) {
        x[static_cast<std::size_t>(i)] = 0.13f * static_cast<float>(i % 23) - 1.0f; // varied
    }
    // A Sobel-like pair so dx/dy genuinely differ (not a symmetric response), row-major 3x3.
    std::array<float, 9> wx{-1.0f, 0.0f, 1.0f, -2.0f, 0.0f, 2.0f, -1.0f, 0.0f, 1.0f};
    std::array<float, 9> wy{-1.0f, -2.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 2.0f, 1.0f};
    std::array<float, OH * OW> y{};

    Executor<kHarris, scalar_backend> ex;
    ex.input("x") = x.data();
    ex.input("wx") = wx.data();
    ex.input("wy") = wy.data();
    ex.output("y") = y.data();
    ex.run();

    for (int e = 0; e < OH * OW; ++e) {
        std::printf("%.9e\n", static_cast<double>(y[static_cast<std::size_t>(e)]));
    }
    return 0;
}
