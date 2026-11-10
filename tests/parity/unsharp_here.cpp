// The image_pipeline benchmark's `unsharp` family, built in THIS tree's language, run on `Executor`, and
// printed.
//
// `benchmarks/image_pipeline_bench.cpp`'s `g_unsharp(H,W,K)` is a K-tap vertical blur followed by an
// unsharp-mask combine:
//     b[i,j] = Σ_k x[i+k, j]·w[k]                 (there: unfold_rows the row axis, sum the window)
//     y[i,j] = (1+alpha)·x[i+K/2, j] − alpha·b[i,j]
// The second term reads x at the K/2 row that ALIGNS with the "valid" blur output b — a center crop.
// As an S6 CORRECTNESS corpus the ported content is that blur-plus-combine, and it exercises the crop
// ALIGNMENT: a wrong offset moves y everywhere. No new op: M5 `_unfold`/`_reduce` for the blur, M5
// `_slice` for the center crop (the view `x.slice(…)` on the row axis), and M4 elementwise mul/sub.
//
// The image is a tensor, x{H,W}, recorded with a length-1 stream, x{H,W,1}, as the shapes below are. The
// blur `_unfold`s the ROW axis (0), window K — {H-K+1, K, W, 1} — weights by w{1,K,1,1} and `_reduce`s
// Sum over axis 1 → b{H-K+1, W, 1}. The center crop is `_slice` of x on the row axis: offset K/2,
// length H-K+1 → xc{H-K+1, W, 1}, which lines up with b row-for-row. The old tree spells the same crop
// as a flat slice + reshape; here the axis-aware `_slice` names the row band directly. y = xc·(1+alpha)
// − b·alpha, elementwise. Both {H-K+1, W, 1}, same output shape and order as the there side.
//
// THE NUMERIC DIFFERENCE. The blur `b` is a weighted sum folded in a different ORDER than the old tree's
// ascending-tap f32 sum, and the whole graph is evaluated in f64 here vs f32 there — so any gap is a
// reduction-order / f32-vs-f64 rounding, and the parity ALLOWED is its measured max. Taps sum to 1 so
// the center and blur terms are comparable and neither dominates.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "heinzel/frontend/graph_builder.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t H = 8;
constexpr std::int32_t W = 8;
constexpr std::int32_t K = 5;
constexpr std::int32_t OH = H - K + 1;
constexpr std::int32_t kIn = H * W;
constexpr std::int32_t kOut = OH * W;
constexpr double kAlpha = static_cast<double>(0.5f);

// x(i) through float so its bits match the old tree's f32 input: 0.13f*(i%23) - 1, i the flat index.
constexpr double x_value(std::int32_t i) { return static_cast<double>(0.13f * static_cast<float>(i % 23) - 1.0f); }

// The unsharp: vertical blur b, center crop xc, then y = (1+alpha)*xc - alpha*b. `g_unsharp`.
constexpr Graph build() {
    Input x{"x", {H, W}};
    Expr w = Input{"w", {1, K, 1}}; // K aligned to the vertical window axis (1)
    Expr windowed = x.unfold(/*axis*/ 0, /*window*/ K, /*hop*/ 1); // {H-K+1, K, W, 1}
    Expr b = (windowed * w).sum(1);          // {H-K+1, W, 1}
    Expr xc = x.slice(/*axis*/ 0, /*offset*/ K / 2, /*length*/ OH); // {H-K+1, W, 1}
    Expr y = xc * (1.0 + kAlpha) - b * kAlpha;
    return finish(Output{"y"} = y);
}

} // namespace

int main() {
    std::array<double, kIn> x_buf{};
    for (std::int32_t i = 0; i < kIn; ++i) {
        x_buf[static_cast<std::size_t>(i)] = x_value(i);
    }
    // The same blur taps as unsharp_there, summing to 1.
    std::array<double, K> w_buf{static_cast<double>(0.15f), static_cast<double>(0.2f), static_cast<double>(0.3f),
                                static_cast<double>(0.2f), static_cast<double>(0.15f)};

    const testing::Outputs y = testing::executed<build>({{"x", x_buf.data(), kIn}, {"w", w_buf.data(), K}});

    for (std::int32_t e = 0; e < kOut; ++e) {
        std::printf("%.9e\n", y[0][static_cast<std::size_t>(e)]);
    }
    return 0;
}
