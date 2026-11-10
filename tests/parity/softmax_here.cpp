// The apps benchmark's `softmax` family, built in THIS tree's language, run on `Executor`, and printed.
//
// `benchmarks/apps_bench.cpp`'s `g_softmax` is a numerically-stable row softmax over the columns of an
// [M, K] matrix:
//     e = exp(x − max_c(x)) ; y = e / Σ_c(e)
// the max and sum taken over the column axis with keepdims so they broadcast back across the row. As an
// S6 CORRECTNESS corpus the ported content is that transcendental-plus-reduction pipeline — the exp
// (M4 `op::Exp`, the core tutorial ch03's softmax uses), a `_reduce` Max and a `_reduce` Sum with
// keepdims (M5, the broadcast-back the analysis pass already reads), and elementwise subtract/divide
// (M4). No new op; no existing parity test covers this exp+row-reduce combination.
//
// The matrix is a tensor, x{M, K}. The softmax runs over the COLUMN axis (1). `_reduce` Max over axis 1 with
// keepdims → {M, 1}, which broadcasts back against x for the stable shift; `exp`; `_reduce` Sum over axis 1
// with keepdims → {M, 1}; divide. The old tree spells the same reductions with `max(x, Axes{1}, true)` /
// `sum(e, Axes{1}, true)`; here they are `x.max(1, keepdims)` / `e.sum(1, keepdims)`. Output {M, K}, same
// shape and row-major order as the there side.
//
// THE NUMERIC DIFFERENCE. The max-shift and the division are exact; the accounted difference is that the
// two trees evaluate `exp` with different cores (this tree's op::Exp vs the old tree's scalar exp) and
// fold the length-K sum in a different order — so any gap is a transcendental-core + reduction-order
// rounding, and the parity ALLOWED is its measured max. Inputs are carried through float so their bits
// match the old tree's f32; they span a range so the softmax is a genuine distribution, not near-uniform.
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

// x(i) through float so its bits match the old tree's f32 input: 0.13f*(i%23) - 1, i the flat index.
constexpr double x_value(std::int32_t i) { return static_cast<double>(0.13f * static_cast<float>(i % 23) - 1.0f); }

// Row softmax over the column axis (1), max/sum with keepdims. `g_softmax`.
constexpr Graph build() {
    Input x{"x", {M, K}};
    Expr m = x.max(1, keepdims); // {M,1}
    Expr e = exp(x - m);                                                     // {M,K}
    Expr s = e.sum(1, keepdims); // {M,1}
    Expr y = e / s;                                                          // {M,K}
    return finish(Output{"y"} = y);
}

} // namespace

int main() {
    std::array<double, kN> x_buf{};
    for (std::int32_t i = 0; i < kN; ++i) {
        x_buf[static_cast<std::size_t>(i)] = x_value(i);
    }

    const testing::Outputs y = testing::executed<build>({{"x", x_buf.data(), kN}});

    // y over (row r, col c) row-major — element r*K + c — the same order the there side prints its
    // [M, K] output buffer.
    for (std::int32_t e = 0; e < kN; ++e) {
        std::printf("%.9e\n", y[0][static_cast<std::size_t>(e)]);
    }
    return 0;
}
