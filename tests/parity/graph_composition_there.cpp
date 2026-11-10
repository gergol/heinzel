// The `graph_composition` example, built on the OLD tree at c4012b8, run, and its samples printed in
// the same format as `graph_composition_here.cpp`.
//
// The fragments below are the old example's own code (builder + input handle -> output handle, with
// `instantiate` namespacing the parameters). The old tree's headers are extracted from the shared
// object database at configure time, not read out of the working directory — same reason as the other
// `_there` programs. The knob values are the same five the `_here` program feeds as constant streams.
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

constexpr std::size_t N = 32;

constexpr TargetInfo kTarget{
    .logical_cores = 1,
    .l1_cache_bytes = 16 * 1024,
    .simd_width_bytes = 16,
};

constexpr float kInGain = 1.5f;
constexpr float kA1 = 0.3f;
constexpr float kA2 = 0.5f;
constexpr float kDrive = 0.8f;
constexpr float kOutGain = 0.7f;

// ---- the old example's reusable fragments --------------------------------------------------------

constexpr Expr gain_stage(GraphBuilder& g, Expr x) {
    Param gain{g, "gain"};
    return x * gain;
}

constexpr Expr onepole_stage(GraphBuilder& g, Expr x) {
    Param a{g, "a"};
    Index n;
    Variable y;
    y[n] = (1.0f - a) * x[n] + a * y[n - 1];
    return y;
}

constexpr Expr twopole_stage(GraphBuilder& g, Expr x) {
    Expr s = instantiate(g, "p1", onepole_stage, x);
    return instantiate(g, "p2", onepole_stage, s);
}

constexpr Expr saturate_stage(GraphBuilder& g, Expr x) {
    Param drive{g, "drive"};
    return tanh(x * drive);
}

// The exact tier, so a difference cannot be the compiler reassociating arithmetic. (The old tree's
// own contract_fma still fuses each one-pole; the pair's BECAUSE accounts for it.)
constexpr Graph build_strip() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input x{g, "x", {N}};
    Output y{g, "y", {N}};

    Expr s = x;
    s = instantiate(g, "in_gain", gain_stage, s);
    s = instantiate(g, "lpf", twopole_stage, s);
    s = instantiate(g, "sat", saturate_stage, s);
    s = instantiate(g, "out_gain", gain_stage, s);
    y = s;
    return g;
}

inline constexpr auto kStrip = compile<build_strip(), kTarget>();

} // namespace

int main() {
    std::array<float, N> x{};
    std::array<float, N> y{};
    for (std::size_t n = 0; n < N; ++n) {
        x[n] = std::sin(0.4f * static_cast<float>(n));
    }

    Executor<kStrip, scalar_backend> ex;
    ex.input("x") = x.data();
    ex.param("in_gain.gain") = kInGain;
    ex.param("lpf.p1.a") = kA1;
    ex.param("lpf.p2.a") = kA2;
    ex.param("sat.drive") = kDrive;
    ex.param("out_gain.gain") = kOutGain;
    ex.output("y") = y.data();
    ex.run();

    for (std::size_t n = 0; n < N; ++n) {
        std::printf("%.9e\n", static_cast<double>(y[n]));
    }
    return 0;
}
