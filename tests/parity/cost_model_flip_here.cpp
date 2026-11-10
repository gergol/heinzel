// The cost_model_flip benchmark's separable blur, built in THIS tree's language, run on `Executor`, and
// printed.
//
// `benchmarks/cost_model_flip_bench.cpp`'s `g_sep_blur` is a separable 2-D blur: a Kp-tap HORIZONTAL
// windowed reduce feeding a Kc-tap VERTICAL windowed reduce.
//     bx[i,j] = Σ_p x[i, j+p]·wh[p]        (there: unfold the innermost/W axis, sum the window)
//     y[i,j]  = Σ_c bx[i+c, j]·wv[c]       (there: unfold_rows the row axis, sum the window)
// The bench's headline is that the machine model's roofline FLIPS this graph's schedule (fuse vs
// materialize blur_x) between a compute-bound and a memory-rich target — a SCHEDULING property, carried
// by this tree's schedule/cost model and its gates. As an S6 CORRECTNESS corpus the ported content is
// the numeric blur OUTPUT, which is schedule-invariant (the fold is numerically neutral), so this
// program builds the blur once and runs it. No new op: `_unfold` (M5) on two axes and `_reduce` Sum
// (M5) — the same structural pair `stencil_unfold` and `fusion` already exercise.
//
// The image is a tensor, x{H, W}, recorded with a length-1 stream, x{H, W, 1}, as the shapes below are. The
// element axes are the row axis (0) and the column/W axis (1); the stream is axis 2.
//   * Horizontal: `_unfold` the W axis (1), window Kp — inserts the window at axis 2 giving
//     {H, W-Kp+1, Kp, 1}; weight by wh{1,1,Kp,1} and `_reduce` Sum over axis 2 → bx{H, W-Kp+1, 1}.
//   * Vertical: `_unfold` the row axis (0) of bx, window Kc — inserts the window at axis 1 giving
//     {H-Kc+1, Kc, W-Kp+1, 1}; weight by wv{1,Kc,1,1} and `_reduce` Sum over axis 1 → y{H-Kc+1, W-Kp+1, 1}.
// The old tree appends both windows innermost; this tree inserts each next to its unfolded axis. The
// layouts differ, the OUTPUT shape and numbers do not: each output is the same set of Kp·Kc weighted
// products, and only the reduction ORDER can differ — so any gap is a reduction-order rounding, and the
// parity ALLOWED is its measured max (see the CMake `BECAUSE`).
//
// The input image and both tap sets are varied and non-uniform (the bench's constant image + uniform
// weights would blur to a constant — vacuous), so bx and y genuinely vary frame to frame.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "heinzel/frontend/graph_builder.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t H = 6;
constexpr std::int32_t W = 8;
constexpr std::int32_t Kp = 3;
constexpr std::int32_t Kc = 3;
constexpr std::int32_t OH = H - Kc + 1;
constexpr std::int32_t OW = W - Kp + 1;
constexpr std::int32_t kIn = H * W;
constexpr std::int32_t kOut = OH * OW;

// x(h,w) through float so its bits match the old tree's f32 input: 0.13f*(p%23) - 1, p the flat index.
constexpr double x_value(std::int32_t p) { return static_cast<double>(0.13f * static_cast<float>(p % 23) - 1.0f); }

// The separable blur: unfold+weight+sum the W axis, then unfold+weight+sum the row axis. `g_sep_blur`.
constexpr Graph build() {
    Input x{"x", {H, W}};
    Expr wh = Input{"wh", {1, 1, Kp}}; // Kp aligned to the horizontal window axis (2)
    Expr wv = Input{"wv", {1, Kc, 1}}; // Kc aligned to the vertical window axis (1)
    Expr hz = x.unfold(/*axis*/ 1, /*window*/ Kp, /*hop*/ 1);   // {H, W-Kp+1, Kp, 1}
    Expr bx = (hz * wh).sum(2);           // {H, W-Kp+1, 1}
    Expr vt = bx.unfold(/*axis*/ 0, /*window*/ Kc, /*hop*/ 1);  // {H-Kc+1, Kc, W-Kp+1, 1}
    Expr y = (vt * wv).sum(1);            // {H-Kc+1, W-Kp+1, 1}
    return finish(Output{"y"} = y);
}

} // namespace

int main() {
    std::array<double, kIn> x_buf{};
    for (std::int32_t p = 0; p < kIn; ++p) {
        x_buf[static_cast<std::size_t>(p)] = x_value(p);
    }
    std::array<double, Kp> wh_buf{};
    for (std::int32_t p = 0; p < Kp; ++p) {
        wh_buf[static_cast<std::size_t>(p)] = static_cast<double>(0.2f + 0.1f * static_cast<float>(p));
    }
    std::array<double, Kc> wv_buf{};
    for (std::int32_t c = 0; c < Kc; ++c) {
        wv_buf[static_cast<std::size_t>(c)] = static_cast<double>(0.3f - 0.1f * static_cast<float>(c));
    }

    const testing::Outputs y =
        testing::executed<build>({{"x", x_buf.data(), kIn}, {"wh", wh_buf.data(), Kp}, {"wv", wv_buf.data(), Kc}});

    // y over (frame f, col j) row-major — element f*OW + j — the same order the there side prints its
    // [OH, OW] output buffer.
    for (std::int32_t e = 0; e < kOut; ++e) {
        std::printf("%.9e\n", y[0][static_cast<std::size_t>(e)]);
    }
    return 0;
}
