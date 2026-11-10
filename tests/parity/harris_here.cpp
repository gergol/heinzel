// The image_pipeline benchmark's `harris` family, built in THIS tree's language, run on `Executor`, and
// printed.
//
// `benchmarks/image_pipeline_bench.cpp`'s `g_harris(H,W)` is the wide-DAG corner detector:
//     dx = Σ_win unfold2d(x,3,3)·wx ; dy = Σ_win unfold2d(x,3,3)·wy    (two 3x3 gradient stencils)
//     ixx = dx·dx ; ixy = dx·dy ; iyy = dy·dy                          (the products stage)
//     sxx = Σ_win unfold2d(ixx,3,3) ; sxy,syy likewise                 (three 3x3 box sums)
//     y = sxx·syy − sxy·sxy − 0.04·(sxx+syy)^2                          (the Harris response)
// It is the one image_pipeline family that exercises the 2-D windowed view `_unfold2d` under numeric
// pressure — twice, nested — which no other parity test does. `op::Unfold2d` is an M5 view with a
// runtime (`tests/reference/rearrange_element.hpp`) and its own unit tests (`rearrange2d_test`,
// `view_inference2d_test`); its spelling is the member `x.unfold2d(axis, window, axis2, window2)`, which
// emits the node with the same `ViewAttr` those unit tests build, and the rest of the graph is ordinary
// frontend (mul, `_reduce` Sum, add/sub). No shared header changes; no new
// op meaning; every node lowers to an already-tested M4/M5 op.
//
// The image is a tensor, x{H,W}, recorded with a length-1 stream, x{H,W,1}, as the shapes below are. A 3x3
// `_unfold2d` over the two OUTER axes (rows 0, cols 1) frames each — extent L → (L-3)/1+1 — and inserts
// a window axis of 3 BESIDE each, giving {H-2, 3, W-2, 3, 1} (window axes at 1 and 3;
// `view_inference2d_test` pins this layout). The old tree appends both windows at the end
// ({H-2, W-2, 3, 3}, reduce Axes{2,3}); here they sit next to their frame axes, so the fold is over
// Axes{1,3}. The layout differs, the OUTPUT shape and numbers do not. The single `_unfold2d(x)` is built
// ONCE and reused by dx and dy (the old tree's two identical calls CSE to one), keeping the graph's peak
// live planes down.
//
// THE NUMERIC DIFFERENCE. Every stage is a windowed weighted sum folded in a different ORDER than the
// old tree's ascending f32 fma sum, and the whole graph runs in f64 here vs f32 there, so any gap is a
// reduction-order / f32-vs-f64 rounding — and the response's subtractions of near-equal products amplify
// it. The parity ALLOWED is the measured max (the CMake `BECAUSE`). A Sobel-like wx/wy pair makes dx and
// dy genuinely different, so the response is a real corner measure and not a symmetric degenerate.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "heinzel/frontend/graph_builder.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t H = 6;
constexpr std::int32_t W = 6;
constexpr std::int32_t OH = H - 4;
constexpr std::int32_t OW = W - 4;
constexpr std::int32_t kIn = H * W;
constexpr std::int32_t kOut = OH * OW;
constexpr double kK = static_cast<double>(0.04f); // the bench's Harris k

// x(i) through float so its bits match the old tree's f32 input: 0.13f*(i%23) - 1, i the flat index.
constexpr double x_value(std::int32_t i) { return static_cast<double>(0.13f * static_cast<float>(i % 23) - 1.0f); }

// The Harris response. `g_harris`, with the two window axes folded at {1,3} rather than {2,3}.
constexpr Graph build() {
    Input x{"x", {H, W}};
    // wx/wy 3x3 kernels aligned to the two window axes (1 and 3): {1,3,1,3,1}.
    Input wx{"wx", {1, 3, 1, 3}};
    Input wy{"wy", {1, 3, 1, 3}};

    Expr xwin = x.unfold2d(0, 3, 1, 3); // {H-2,3,W-2,3,1}, built ONCE
    Expr dx = (xwin * wx).sum({1, 3});          // {H-2,W-2,1}
    Expr dy = (xwin * wy).sum({1, 3});          // {H-2,W-2,1}

    Expr ixx = dx * dx;
    Expr ixy = dx * dy;
    Expr iyy = dy * dy;

    Expr sxx = ixx.unfold2d(0, 3, 1, 3).sum({1, 3}); // {H-4,W-4,1}
    Expr sxy = ixy.unfold2d(0, 3, 1, 3).sum({1, 3});
    Expr syy = iyy.unfold2d(0, 3, 1, 3).sum({1, 3});

    Expr tr = sxx + syy;
    Expr y = sxx * syy - sxy * sxy - kK * tr * tr; // (kK*tr)*tr, matching the bench's 0.04f*tr*tr association
    return finish(Output{"y"} = y);
}

} // namespace

int main() {
    std::array<double, kIn> x_buf{};
    for (std::int32_t i = 0; i < kIn; ++i) {
        x_buf[static_cast<std::size_t>(i)] = x_value(i);
    }
    // The same Sobel-like kernels as harris_there, row-major 3x3 (element a*3+b -> window (a,b)).
    std::array<double, 9> wx_buf{static_cast<double>(-1.0f), 0.0, static_cast<double>(1.0f),
                                 static_cast<double>(-2.0f), 0.0, static_cast<double>(2.0f),
                                 static_cast<double>(-1.0f), 0.0, static_cast<double>(1.0f)};
    std::array<double, 9> wy_buf{static_cast<double>(-1.0f), static_cast<double>(-2.0f), static_cast<double>(-1.0f),
                                 0.0, 0.0, 0.0,
                                 static_cast<double>(1.0f), static_cast<double>(2.0f), static_cast<double>(1.0f)};

    const testing::Outputs y =
        testing::executed<build>({{"x", x_buf.data(), kIn}, {"wx", wx_buf.data(), 9}, {"wy", wy_buf.data(), 9}});

    for (std::int32_t e = 0; e < kOut; ++e) {
        std::printf("%.9e\n", y[0][static_cast<std::size_t>(e)]);
    }
    return 0;
}
