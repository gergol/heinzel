// The apps benchmark's `rmsnorm` family, built in THIS tree's language, run on `Executor`, and printed.
//
// `benchmarks/apps_bench.cpp`'s `g_rmsnorm` is an RMS normalization over the columns of an [M, K] matrix:
//     y = x / sqrt(mean_c(x^2) + eps)          (eps = 1e-6)
// the mean of squares taken over the column axis with keepdims so it broadcasts back across the row. As
// an S6 CORRECTNESS corpus the ported content is that sqrt-over-a-row-mean pipeline — a different
// transcendental core from softmax's exp (M4 `op::Sqrt`), a `_reduce` Mean with keepdims (M5), and
// elementwise square/add/divide (M4). No new op.
//
// The matrix is a tensor, x{M, K}. The
// norm runs over the COLUMN axis (1): `_reduce` Mean of x*x over axis 1 with keepdims → {M, 1}, add
// eps, `sqrt`, then divide x by it (the denominator broadcasts back across the row). The old tree spells
// the reduction `mean(x*x, Axes{1}, true)`; here it is `(x * x).mean(1, keepdims)`.
// Output {M, K}, same shape and row-major order as the there side.
//
// THE NUMERIC DIFFERENCE. The square, the eps add and the divide are exact; the accounted difference is
// that the two trees evaluate `sqrt` with different cores and fold the length-K mean in a different
// order — so any gap is a transcendental-core + reduction-order rounding, and the parity ALLOWED is its
// measured max. Inputs are carried through float so their bits match the old tree's f32.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "heinzel/frontend/graph_builder.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t M = 4;
constexpr std::int32_t K = 6;
constexpr std::int32_t kN = M * K;
constexpr double kEps = static_cast<double>(1e-6f);

// x(i) through float so its bits match the old tree's f32 input: 0.13f*(i%23) - 1, i the flat index.
constexpr double x_value(std::int32_t i) { return static_cast<double>(0.13f * static_cast<float>(i % 23) - 1.0f); }

// RMS norm over the column axis (1), mean with keepdims. `g_rmsnorm`.
constexpr Graph build() {
    Input x{"x", {M, K}};
    Expr ms = (x * x).mean(1, keepdims); // {M,1}
    Expr y = x / sqrt(ms + kEps);                                                  // {M,K}
    return finish(Output{"y"} = y);
}

} // namespace

int main() {
    std::array<double, kN> x_buf{};
    for (std::int32_t i = 0; i < kN; ++i) {
        x_buf[static_cast<std::size_t>(i)] = x_value(i);
    }

    const testing::Outputs y = testing::executed<build>({{"x", x_buf.data(), kN}});

    for (std::int32_t e = 0; e < kN; ++e) {
        std::printf("%.9e\n", y[0][static_cast<std::size_t>(e)]);
    }
    return 0;
}
