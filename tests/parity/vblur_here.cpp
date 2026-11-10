// The image_pipeline benchmark's `vblur` family, built in THIS tree's language, run on `Executor`, and
// printed.
//
// `benchmarks/image_pipeline_bench.cpp`'s `g_vblur_fused(H,W,K)` is a pointwise square producer feeding
// a K-tap VERTICAL (row-axis) windowed reduce:
//     p = x*x ; y[i,j] = Σ_k p[i+k, j]·w[k]     (there: unfold_rows the row axis, sum the window)
// As an S6 CORRECTNESS corpus the ported content is that fused square-then-vertical-conv. The bench's
// headline — that the model FUSES the square producer into the conv rather than materializing p — is a
// scheduling property (schedule/cost model), numerically neutral, so this program builds the graph and
// runs it. No new op: `op::Mul` (M4) for the square, `_unfold` on the row axis and `_reduce` Sum (M5) —
// the same structural pair `stencil_unfold`/`fusion`/`cost_model_flip` exercise, here with a squared
// producer and general weights.
//
// The image is a tensor, x{H,W}, recorded with a length-1 stream, x{H,W,1}, as the shapes below are.
// `p = x*x` is elementwise. `_unfold` the ROW axis (0), window K — inserts the window at axis 1 giving
// {H-K+1, K, W, 1}; weight by w{1,K,1,1} and `_reduce` Sum over axis 1 → y{H-K+1, W, 1}. The old tree
// appends the window innermost (Axes{2}); this tree inserts it next to the unfolded axis. The layouts
// differ, the OUTPUT shape and numbers do not — each output is the same set of K weighted products, and
// only the reduction ORDER can differ, so any gap is a reduction-order rounding (the CMake `BECAUSE`).
//
// The image and taps are varied and non-uniform so y genuinely varies frame to frame.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "heinzel/frontend/graph_builder.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t H = 8;
constexpr std::int32_t W = 12;
constexpr std::int32_t K = 5;
constexpr std::int32_t OH = H - K + 1;
constexpr std::int32_t kIn = H * W;
constexpr std::int32_t kOut = OH * W;

// x(i) through float so its bits match the old tree's f32 input: 0.13f*(i%23) - 1, i the flat index.
constexpr double x_value(std::int32_t i) { return static_cast<double>(0.13f * static_cast<float>(i % 23) - 1.0f); }

// The vblur: p = x*x, then unfold the row axis, weight, and sum the window. `g_vblur_fused`.
constexpr Graph build() {
    Input x{"x", {H, W}};
    Expr w = Input{"w", {1, K, 1}}; // K aligned to the vertical window axis (1)
    Expr p = x * x;
    Expr windowed = p.unfold(/*axis*/ 0, /*window*/ K, /*hop*/ 1); // {H-K+1, K, W, 1}
    Expr y = (windowed * w).sum(1);          // {H-K+1, W, 1}
    return finish(Output{"y"} = y);
}

} // namespace

int main() {
    std::array<double, kIn> x_buf{};
    for (std::int32_t i = 0; i < kIn; ++i) {
        x_buf[static_cast<std::size_t>(i)] = x_value(i);
    }
    std::array<double, K> w_buf{};
    for (std::int32_t k = 0; k < K; ++k) {
        w_buf[static_cast<std::size_t>(k)] = static_cast<double>(0.1f * static_cast<float>(k + 1));
    }

    const testing::Outputs y = testing::executed<build>({{"x", x_buf.data(), kIn}, {"w", w_buf.data(), K}});

    // y over (frame i, col j) row-major — element i*W + j — the same order the there side prints its
    // [OH, W] output buffer.
    for (std::int32_t e = 0; e < kOut; ++e) {
        std::printf("%.9e\n", y[0][static_cast<std::size_t>(e)]);
    }
    return 0;
}
