// The four-section biquad cascade, built on the OLD tree at c4012b8, run, and its samples printed in
// the same order as `cascade4_here.cpp`.
//
// The old tree's headers are extracted from the shared-object database at configure time (pinned to
// the commit `main` names) — same as the other `*_there.cpp`; nothing here opens /home/user/ctg3.
//
// This is `benchmarks/apps_bench.cpp`'s `g_cascade4` verbatim, sized to the shared kBlock: three
// internal recurrence states via `x.variable()` and the final section `y4` as the output, each a
// second-order feedback section reading the current sample of the section before it. `set_fast_math
// (false)` pins the deterministic tier so any difference is f32-vs-f64 rounding, not reassociation.
#include <array>
#include <cstddef>
#include <cstdio>

#include "heinzel/backends/scalar_backend.hpp"
#include "heinzel/frontend/builder.hpp"
#include "heinzel/runtime/compile.hpp"
#include "heinzel/runtime/executor.hpp"

using namespace heinzel;

namespace {

constexpr std::size_t kBlock = 64; // matches cascade4_here.cpp

constexpr TargetInfo kTarget{
    .logical_cores = 1,
    .l1_cache_bytes = 16 * 1024,
    .simd_width_bytes = 16,
};

constexpr Graph build() {
    GraphBuilder g;
    g.set_fast_math(false);
    auto x = g.input("x", {kBlock});
    auto y4 = g.output("y4", {kBlock});
    auto n = index();
    Out y1 = x.variable();
    Out y2 = x.variable();
    Out y3 = x.variable();
    y1[n] = 0.3f * x[n] + 0.2f * x[n - 1] - 0.4f * y1[n - 1] + 0.1f * y1[n - 2];
    y2[n] = 0.5f * Expr(y1) + 0.1f * y1[n - 1] + 0.3f * y2[n - 1] - 0.2f * y2[n - 2];
    y3[n] = 0.4f * Expr(y2) + 0.2f * y2[n - 1] - 0.1f * y3[n - 1] + 0.15f * y3[n - 2];
    y4[n] = 0.6f * Expr(y3) + 0.1f * y3[n - 1] + 0.2f * y4[n - 1] - 0.1f * y4[n - 2];
    return g;
}

inline constexpr auto kSched = compile<build(), kTarget>();

} // namespace

int main() {
    std::array<float, kBlock> x{};
    std::array<float, kBlock> y{};
    for (std::size_t n = 0; n < kBlock; ++n) {
        x[n] = 0.31f * static_cast<float>((n * 5) % 23) - 2.0f;
    }

    Executor<kSched, scalar_backend> ex;
    ex.input("x") = x.data();
    ex.output("y4") = y.data();
    ex.run();

    for (std::size_t n = 0; n < kBlock; ++n) {
        std::printf("%.9e\n", static_cast<double>(y[n]));
    }
    return 0;
}
