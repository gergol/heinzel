// The channel-strip benchmark's saturating gain chain, built on the OLD tree at c4012b8, run, and its
// samples printed in the same order as `channel_strip_here.cpp`.
//
// This is `benchmarks/channel_strip_bench.cpp`'s `g_strip()` verbatim — input gain, three cascaded
// `tanh(d·s + b)` drive stages, output gain, and a hard clamp — built with the old tree's own builder.
// The old tree's headers are extracted from the shared-object database at configure time (pinned to the
// commit `main` names), same as the other `*_there.cpp`. The exact tier (`set_fast_math(false)`) pins
// the deterministic path, so a difference cannot be the compiler reassociating the gains.
// (The old tree's own contract_fma still fuses each d*s+b; the pair's BECAUSE accounts for it.)
// The input sweep matches the here side sample for sample.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>

#include "heinzel/backends/scalar_backend.hpp"
#include "heinzel/frontend/builder.hpp"
#include "heinzel/runtime/compile.hpp"
#include "heinzel/runtime/executor.hpp"

using namespace heinzel;

namespace {

constexpr std::size_t N = 64; // matches channel_strip_here.cpp's kBlock (a correctness corpus, not the bench's 1<<20 sweep)

constexpr TargetInfo kTarget{
    .logical_cores = 1,
    .l1_cache_bytes = 16 * 1024,
    .simd_width_bytes = 16,
};

// The bench's strip coefficients, verbatim.
constexpr float kGin = 2.0f;
constexpr float kGout = 0.7f;
constexpr float kD1 = 1.5f;
constexpr float kD2 = 1.3f;
constexpr float kD3 = 1.1f;
constexpr float kB1 = 0.10f;
constexpr float kB2 = -0.05f;
constexpr float kB3 = 0.0f;

// The bench's `saturate`: sat(s) = tanh(d·s + b).
constexpr Expr saturate(Expr s, float d, float b) { return tanh(d * s + b); }

// `g_strip()` verbatim.
constexpr Graph build() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input x{g, "x", {N}};
    Output y{g, "y", {N}};
    Expr s = kGin * x;
    s = saturate(s, kD1, kB1);
    s = saturate(s, kD2, kB2);
    s = saturate(s, kD3, kB3);
    s = kGout * s;
    y = max(min(s, 1.0f), -1.0f); // hard clamp to [-1, 1]
    return g;
}

inline constexpr auto kStrip = compile<build(), kTarget>();

} // namespace

int main() {
    std::array<float, N> x{};
    std::array<float, N> y{};
    for (std::size_t n = 0; n < N; ++n) {
        x[n] = 0.6f * std::sin(0.09f * static_cast<float>(n));
    }

    Executor<kStrip, scalar_backend> ex;
    ex.input("x") = x.data();
    ex.output("y") = y.data();
    ex.run();

    for (std::size_t n = 0; n < N; ++n) {
        std::printf("%.9e\n", static_cast<double>(y[n]));
    }
    return 0;
}
