// The order-statistics benchmark's sliding median, built on the OLD tree at c4012b8, run, and its
// frames printed in the same order as `order_stats_here.cpp`.
//
// The old tree's headers are extracted from the shared-object database at configure time (pinned to the
// commit `main` names) — same as the other `*_there.cpp`.
//
// This is `benchmarks/order_stats_bench.cpp`'s graph via the old tree's own helper:
// `median_filter(x, W)` = `median(unfold(x, W), -1)` (builder.hpp), a length-W sliding median giving
// N-W+1 outputs. W is odd, so the median is the sorted-middle element — an exact selection, no average —
// which is why the here side and this side agree bit-for-bit and the parity ALLOWED is 0.0.
// `set_fast_math(false)` pins the deterministic path; a selection network does no reassociable
// arithmetic anyway, so the two sides return the identical middle value.
#include <array>
#include <cstddef>
#include <cstdio>

#include "heinzel/backends/scalar_backend.hpp"
#include "heinzel/frontend/builder.hpp"
#include "heinzel/runtime/compile.hpp"
#include "heinzel/runtime/executor.hpp"

using namespace heinzel;

namespace {

constexpr TargetInfo kTarget{.logical_cores = 1, .l1_cache_bytes = 32 * 1024, .simd_width_bytes = 32};
// N,W match order_stats_here.cpp (a correctness corpus, not the bench's 1<<20 sweep). See the here side.
constexpr int N = 48;
constexpr int W = 5;

constexpr Graph build() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input x{g, "x", {N}};
    median_filter(x, W).as_output("y"); // median(unfold(x, W), -1) — the bench's g_median_filter body
    return g;
}

inline constexpr auto kSched = compile<build(), kTarget>();

} // namespace

int main() {
    std::array<float, N> x{};
    for (int i = 0; i < N; ++i) {
        x[static_cast<std::size_t>(i)] = 0.31f * static_cast<float>((i * 7) % 23) - 2.0f;
    }
    std::array<float, N - W + 1> y{};

    Executor<kSched, scalar_backend> ex;
    ex.input("x") = x.data();
    ex.output("y") = y.data();
    ex.run();

    for (int f = 0; f < N - W + 1; ++f) {
        std::printf("%.9e\n", static_cast<double>(y[static_cast<std::size_t>(f)]));
    }
    return 0;
}
