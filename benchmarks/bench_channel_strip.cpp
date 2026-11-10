// Channel-strip benchmark — the rewrite arm, re-expressed against the NEW API.
//
// Ported from ctg3 benchmarks/channel_strip_bench.cpp. Workload: input gain → three cascaded
// tanh-saturation stages with gain staging → output gain → hard clamp, all elementwise over a
// ~1M-sample width-1 stream:
//     y = clamp( kGout · tanh(kD3·tanh(kD2·tanh(kD1·(kGin·x)+kB1)+kB2)+kB3), -1, 1 )
// `heinzel` is the NEST path (milestone V5): F32, the whole strip one fused stage walked in lanes of
// eight floats, `tanh` computed by the float core (`runtime/float_cores.hpp`) — the same accuracy class
// as ctg3's SIMD minimax cores, which is what makes the comparison like with like (Plan/archive/00 D-7).
// `hand-staged` and `hand-fused` are ctg3's `composed` / `fused_scalar` C references, ported verbatim —
// F32, and over the SAME generator and constants as the nest arm, so those three are one comparison.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>

#include "page_vector.hpp"
#include "parallel_arm.hpp"

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"

using heinzel_bench::PageVector;
using heinzel_bench::OutputVector;

namespace hz = heinzel;

namespace {

constexpr double kGin = 2.0;
constexpr double kGout = 0.7;
constexpr double kD1 = 1.5;
constexpr double kD2 = 1.3;
constexpr double kD3 = 1.1;
constexpr double kB1 = 0.10;
constexpr double kB2 = -0.05;
constexpr double kB3 = 0.0;
constexpr int kN = 1 << 20;

// sat(s) = tanh(d·s + b) — the transcendental is the point of the workload.
constexpr hz::Expr saturate(hz::Expr s, double d, double b) { return hz::tanh(hz::constant(d) * s + b); }
double saturate(double s, double d, double b) { return std::tanh(d * s + b); }

double strip_ref(double x) {
    double s = kGin * x;
    s = saturate(s, kD1, kB1);
    s = saturate(s, kD2, kB2);
    s = saturate(s, kD3, kB3);
    s = kGout * s;
    return std::fmin(std::fmax(s, -1.0), 1.0);
}

// ---- hand-staged / hand-fused: ctg3's C++ references, ported verbatim (float) --------------------
// Own constants/generator, matching ctg3 channel_strip_bench.cpp exactly (kGin=2.0f etc., a 0.6·sin
// tone), so hand-staged and hand-fused compute over identical data and agree bit-for-bit — the same
// invariant ctg3's own startup check establishes.
constexpr float kGinF = 2.0f;
constexpr float kGoutF = 0.7f;
constexpr float kD1F = 1.5f;
constexpr float kD2F = 1.3f;
constexpr float kD3F = 1.1f;
constexpr float kB1F = 0.10f;
constexpr float kB2F = -0.05f;
constexpr float kB3F = 0.0f;

inline float saturatef(float s, float d, float b) { return std::tanh(d * s + b); }

// Library-style separate stages (noinline models a real .process() call boundary — the signal
// round-trips RAM once per stage).
__attribute__((noinline)) void stage_gain(const float* x, float* y, int n, float g) {
    for (int i = 0; i < n; ++i) { y[i] = g * x[i]; }
}
__attribute__((noinline)) void stage_saturate(float* y, int n, float d, float b) {
    for (int i = 0; i < n; ++i) { y[i] = saturatef(y[i], d, b); }
}
__attribute__((noinline)) void stage_clamp(float* y, int n) {
    for (int i = 0; i < n; ++i) { y[i] = std::fmin(std::fmax(y[i], -1.0f), 1.0f); }
}
void composed(const float* x, float* y, int n) {
    stage_gain(x, y, n, kGinF);
    stage_saturate(y, n, kD1F, kB1F);
    stage_saturate(y, n, kD2F, kB2F);
    stage_saturate(y, n, kD3F, kB3F);
    stage_gain(y, y, n, kGoutF);
    stage_clamp(y, n);
}

// One pass, all stages in registers (scalar std::tanh).
void fused_scalar(const float* x, float* y, int n) {
    for (int i = 0; i < n; ++i) {
        float s = kGinF * x[i];
        s = saturatef(s, kD1F, kB1F);
        s = saturatef(s, kD2F, kB2F);
        s = saturatef(s, kD3F, kB3F);
        s = kGoutF * s;
        y[i] = std::fmin(std::fmax(s, -1.0f), 1.0f);
    }
}

PageVector<float> strip_input_f32() {
    PageVector<float> v(kN);
    for (int i = 0; i < kN; ++i) { v[static_cast<std::size_t>(i)] = 0.6f * std::sin(0.01f * static_cast<float>(i)); }
    return v;
}

float strip_ref_f32(float x) {
    float s = kGinF * x;
    s = saturatef(s, kD1F, kB1F);
    s = saturatef(s, kD2F, kB2F);
    s = saturatef(s, kD3F, kB3F);
    s = kGoutF * s;
    return std::fmin(std::fmax(s, -1.0f), 1.0f);
}

// ---- the nest arm: the same strip in F32, planned for this machine -------------------------------
// No kernel and no special case for a transcendental: the planner fuses the nine elementwise nodes
// into one stage because the rule table says a `tanh` is a map like the gains around it, and the
// runner reaches the float core through the one writing (`runtime/compute.hpp`).
constexpr hz::Graph strip_f32 = [] {
    hz::Expr s = hz::constant(kGin) * hz::Input{"x", hz::Signal{kN}};
    s = saturate(s, kD1, kB1);
    s = saturate(s, kD2, kB2);
    s = saturate(s, kD3, kB3);
    hz::Graph g = hz::finish(hz::Output{"y"} = hz::clamp(hz::constant(kGout) * s, -1.0, 1.0));
    if (!hz::infer(g).ok) {
        hz::fail("bench_channel_strip: the f32 strip graph did not infer");
    }
    return g;
}();
constexpr const hz::loops::Program& strip_nest = heinzel_bench::kProgramOf<strip_f32, heinzel_bench::kMachine>;

template <bool Par = false>
void bench_strip_nest(benchmark::State& state) {
    const PageVector<float> x = strip_input_f32();
    hz::loops::Workspace<strip_nest, heinzel_bench::copies<Par>()> work(hz::loops::Sizes{});
    const hz::loops::Feeds feeds{x.data()};
    heinzel_bench::run<strip_nest, Par>(feeds, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(strip_nest.outputs[0]));
    // Against the double chain, every sample. The error is reported against FULL SCALE rather than
    // against the sample: the strip is clamped to +-1 and crosses zero, so dividing by a result near
    // zero would report a number about the zero crossing and not about the arithmetic.
    double max_rel = 0.0;
    for (int i = 0; i < kN; ++i) {
        const double err = std::fabs(static_cast<double>(y[static_cast<std::size_t>(i)]) -
                                     strip_ref(static_cast<double>(x[static_cast<std::size_t>(i)])));
        if (!(err <= 1e-5)) {
            state.SkipWithError("bench_channel_strip: the nest output is outside the float cores' bound");
            return;
        }
        max_rel = std::max(max_rel, err);
    }
    state.counters["max_rel_err"] = max_rel;
    if (Par && !heinzel_bench::same_as_serial<strip_nest>(feeds, work)) {
        state.SkipWithError("bench_channel_strip: the split run differs from the serial run");
        return;
    }
    for (auto _ : state) {
        heinzel_bench::run<strip_nest, Par>(feeds, work);
        float first = y[0];
        benchmark::DoNotOptimize(first);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * kN);
}

void bench_strip_hand_staged(benchmark::State& state) {
    const PageVector<float> x = strip_input_f32();
    OutputVector<float> y(static_cast<std::size_t>(kN));
    composed(x.data(), y.data(), kN);
    for (const int i : {0, kN / 2, kN - 1}) {
        if (!(std::fabs(y[static_cast<std::size_t>(i)] - strip_ref_f32(x[static_cast<std::size_t>(i)])) <= 1e-5f)) {
            state.SkipWithError("bench_channel_strip: hand-staged disagrees with the reference");
            return;
        }
    }
    for (auto _ : state) {
        composed(x.data(), y.data(), kN);
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * kN);
}

void bench_strip_hand_fused(benchmark::State& state) {
    const PageVector<float> x = strip_input_f32();
    OutputVector<float> y(static_cast<std::size_t>(kN));
    fused_scalar(x.data(), y.data(), kN);
    for (const int i : {0, kN / 2, kN - 1}) {
        if (!(std::fabs(y[static_cast<std::size_t>(i)] - strip_ref_f32(x[static_cast<std::size_t>(i)])) <= 1e-5f)) {
            state.SkipWithError("bench_channel_strip: hand-fused disagrees with the reference");
            return;
        }
    }
    for (auto _ : state) {
        fused_scalar(x.data(), y.data(), kN);
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * kN);
}

} // namespace

BENCHMARK(bench_strip_nest)->Name("strip/heinzel")->UseRealTime();
BENCHMARK(bench_strip_nest<true>)->Name("strip/heinzel-par")->UseRealTime();
BENCHMARK(bench_strip_hand_staged)->Name("strip/hand-staged")->UseRealTime();
BENCHMARK(bench_strip_hand_fused)->Name("strip/hand-fused")->UseRealTime();

BENCHMARK_MAIN();
