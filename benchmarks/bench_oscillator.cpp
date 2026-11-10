// Oscillator benchmark — the rewrite arm, re-expressed against the NEW API.
//
// Ported from ctg3 benchmarks/oscillator_bench.cpp. The old bench has two Heinzel arms:
//   - FM / recurrence:  phase[n] = frac(phase[n-1] + inc); out = sinpi(2·phase)·amp — a serial scan.
//   - affine:           phase = frac(iota·inc), a loop-invariant-pitch elementwise ramp.
//
// Only the FM/recurrence arm is ported here. The affine arm is DEFERRED (a finding, not faked): in the
// rewrite `dsp::affine_oscillator(shape, inc, amp)` is a width-materialized `iota(shape)` generator
// (see tests/parity/oscillator_affine_here.cpp — "the N-sample buffer is a tensor of N elements"), so N
// lives on the WIDTH axis and is a COMPILE-TIME extent bounded by the constexpr plane
// budget (kMaxPlanes; the affine parity test sits at N=96). The bench's N = 1<<22 ≈ 4M samples cannot
// be a compile-time width, so the rewrite cannot express the affine arm at the bench's scale. Recorded
// as deferred; the FM arm below IS the genuine recurrence workload and streams to any N.
//
// MAPPING to the rewrite (pattern verbatim from tests/unit/runtime_recurrence_test.cpp +
// tests/parity/oscillator_here.cpp): `dsp::oscillator(inc, amp)` builds the phase-accumulator
// recurrence over the STREAMED axis. To reach N = 1<<22 the streamed extent is runtime-resolved
// (a `Length`), the program compiled once symbolic in N.
//
// `heinzel` is the nest path: the graph in F32, planned for this machine. Only the phase accumulator is
// feedback — four statements walking time a sample at a time — and the waveform after it (`sinpi`, the
// amplitude) carries nothing, so it runs in lanes along time on the float `sinpi` core. `hand` is ctg3's
// phase-accumulator `hand_sine`, ported verbatim (float). Both trees run the full N. inc is a fixed
// pitch (440 Hz @ 48 kHz), fed as a width-1 per-sample stream; amp = 0.5 folded as a constant, exactly
// as ctg3's bench.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>

#include "page_vector.hpp"
#include "parallel_arm.hpp"

#include "heinzel/dsp/oscillator.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"

using heinzel_bench::PageVector;
using heinzel_bench::OutputVector;

namespace hz = heinzel;

namespace {

constexpr std::size_t N = 1u << 22;   // ~4M samples ≈ 87 s @ 48 kHz — the old bench's length
constexpr float kFs = 48000.0f;
constexpr float kFreq = 440.0f;
constexpr float kAmp = 0.5f;
constexpr float kTwoPi = 6.283185307179586f;
constexpr std::int32_t kLengthId = hz::kStreamLength; // the symbol a Length declares
constexpr hz::Length kFrames{.multiple_of = 1, .min = 1, .expect = 512};

// hand: ctg3's phase-accumulator oscillator, ported verbatim (float).
void hand_sine(float* y, std::size_t n, float f, float fs, float amp) {
    const float inc = f / fs;
    float ph = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = amp * std::sin(kTwoPi * ph);
        ph += inc;
        ph -= std::floor(ph);
    }
}

void bench_hand(benchmark::State& state) {
    OutputVector<float> y(N);
    hand_sine(y.data(), N, kFreq, kFs, kAmp);
    for (const std::size_t i : {std::size_t{0}, N / 2, N - 1}) {
        // Reference: y[i] uses phase = frac(i * inc) — i increments applied before sample i is read.
        const double inc = static_cast<double>(kFreq / kFs);
        const double ph = static_cast<double>(i) * inc - std::floor(static_cast<double>(i) * inc);
        const double want = static_cast<double>(kAmp) * std::sin(6.283185307179586 * ph);
        // Generous: float32 phase accumulation drifts by millions of samples (i=N-1) — this is a
        // sanity check against a broken port, not a precision bound.
        if (!(std::fabs(y[i] - want) <= 0.1)) {
            state.SkipWithError("bench_oscillator: hand disagrees with the reference");
            return;
        }
    }
    for (auto _ : state) {
        hand_sine(y.data(), N, kFreq, kFs, kAmp);
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(N));
}

// ---- the nest arm: the oscillator in F32, planned for this machine ------------------------------------
// inc is a width-1 per-sample input; amp is a folded constant. Symbolic in N — compiled once, run at
// N = 1<<22.
constexpr hz::Graph build_osc_f32() {
    hz::Input inc{"inc", hz::Signal{kFrames}};
    hz::Graph g = hz::finish(hz::Output{"y"} = hz::dsp::oscillator(inc, hz::constant(static_cast<double>(kAmp))));
    if (!hz::infer(g).ok) {
        hz::fail("bench_oscillator: the F32 FM oscillator graph did not infer");
    }
    return g;
}
constexpr hz::Graph kOscF32 = build_osc_f32();
constexpr const hz::loops::Program& kOscNest = heinzel_bench::kProgramOf<kOscF32, heinzel_bench::kMachine>;

void bench_fm_nest(benchmark::State& state) {
    const PageVector<float> inc(N, kFreq / kFs);
    hz::loops::Sizes sizes{};
    sizes[kLengthId] = static_cast<std::int64_t>(N);
    hz::loops::Workspace<kOscNest> work(sizes);
    const hz::loops::Feeds feeds{inc.data()};
    heinzel_bench::run<kOscNest, false>(feeds, work);
    const float* y = work.data<float>(static_cast<std::size_t>(kOscNest.outputs[0]));
    // The phase is exact arithmetic (an add, a floor, a subtraction), accumulated in double at `Normal` (a one-voice
    // cycle, Decision 66), so accumulated so by hand it is the graph's phase bit for bit, read as float; what is
    // measured is the float `sinpi` core against the platform's sine in double, relative to the amplitude.
    double max_rel = 0.0;
    double phase = 0.0;
    for (std::size_t i = 0; i < N; ++i) {
        const double sum = phase + static_cast<double>(inc[i]);
        phase = sum - std::floor(sum);
        const float ph = static_cast<float>(phase);
        const double want = std::sin(3.141592653589793 * static_cast<double>(ph * 2.0f)) * static_cast<double>(kAmp);
        const double err = std::fabs(static_cast<double>(y[i]) - want) / static_cast<double>(kAmp);
        max_rel = err > max_rel ? err : max_rel;
    }
    if (!(max_rel <= 1e-6)) {
        state.SkipWithError("bench_oscillator: the nest oscillator is outside the float sinpi core's bound");
        return;
    }
    state.counters["max_rel_err"] = max_rel;
    for (auto _ : state) {
        heinzel_bench::run<kOscNest, false>(feeds, work);
        benchmark::DoNotOptimize(y);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(N));
}

} // namespace

BENCHMARK(bench_fm_nest)->Name("osc_sine/fm/heinzel")->UseRealTime();
BENCHMARK(bench_hand)->Name("osc_sine/fm/hand")->UseRealTime();

BENCHMARK_MAIN();
