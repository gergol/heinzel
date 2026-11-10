// The order-statistics benchmark's sliding median, built in THIS tree's language, run on `Executor`, and
// printed.
//
// `benchmarks/order_stats_bench.cpp`'s graph is a sliding median filter `median(unfold(x, W), -1)` — a
// pruned selection network, the M5 order-statistics op under pressure. As an S6 CORRECTNESS corpus its
// numeric content is that selection: each length-W window's middle value. No new op: `_unfold` (M5) and
// `_order` (M5, `OrderOp::Quantile` at q=0.5), the same order op `tests/unit/order_test.cpp` builds.
//
// The length-N signal is a tensor, x{N}, recorded with a length-1 stream, x{N,1}, as the shapes below are.
// `_unfold` on the DATA axis (axis 0) turns extent N into (N-W+1) frames and appends a window axis of W
// at axis 1 — {N-W+1, W, 1} — then `_order` Quantile over that window axis DROPS it to {N-W+1, 1}.
//
// WINDOW IS ODD (W=5), SO THE MEDIAN IS AN EXACT SELECTION, NOT AN AVERAGE. This tree resolves a
// quantile as numpy does: p=(L-1)·q, and at L=5, q=0.5, p=2.0 is integral — lo=hi=2, weight 0 — so the
// value returned is the sorted-middle element bit-for-bit, no arithmetic (`order_element.hpp`: "exact
// when the interpolation weight is zero"). The old tree's `median(unfold(x,W),-1)` picks the same middle
// element the same way, so the two selections are the same number and the parity ALLOWED is 0.0: a
// difference of ANY size is a real regression, not an accounted tolerance.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "heinzel/frontend/graph_builder.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t N = 48; // keeps the graph well under kMaxPlanes
constexpr std::int32_t W = 5;  // odd window
constexpr std::int32_t kFrames = N - W + 1;

// A varied, non-monotonic oracle so each window is a genuine selection rather than a trivial middle:
// x[i] = 0.31f*((i*7)%23) - 2.0f, through float so the bits match the old tree's f32 input.
constexpr double x_value(std::int32_t i) {
    return static_cast<double>(0.31f * static_cast<float>((i * 7) % 23) - 2.0f);
}

// y[f] = median over the window of x[f..f+W-1]. Unfold the data axis into (frame, window), then take the
// median (Quantile 0.5) over the window axis, which drops it.
constexpr Graph build() {
    Input x{"x", {N}};
    Expr windowed = x.unfold(0, W);  // {N-W+1, W, 1}
    Expr median = windowed.median(1); // {N-W+1, 1}
    return finish(Output{"y"} = median);
}

} // namespace

int main() {
    std::array<double, N> x_buf{};
    for (std::int32_t i = 0; i < N; ++i) {
        x_buf[static_cast<std::size_t>(i)] = x_value(i);
    }

    const testing::Outputs y = testing::executed<build>({{"x", x_buf.data(), N}});

    for (std::int32_t f = 0; f < kFrames; ++f) {
        std::printf("%.9e\n", y[0][static_cast<std::size_t>(f)]);
    }
    return 0;
}
