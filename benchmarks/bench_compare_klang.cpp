// Third-party comparison benchmark: Heinzel vs klang (nashaudio/klang) — a single-header C++
// audio-DSP dialect that markets itself as expressive and optimised.
//
// This file carries the klang arms; Heinzel's (`biquad3/heinzel`) are in `bench_compare_klang_heinzel.cpp`, the
// same target, since klang.h's macros break Heinzel's headers. Both read `klang_compare_common.hpp`: the coefficients,
// the input and the library-independent references. `main` also had `sine/hz` and `eq/hz` arms here
// (`build/main-tree/benchmarks/klang_compare_bench.cpp`), not ported yet.
//
// Arms:
//   eq       — the klang EQ example (examples/Filtering/EQ.k): two PARALLEL biquads (LPF@200, HPF@4000)
//              split the signal into low/mid/high bands (mids = in − (lows + highs)) recombined with
//              per-band gains (0.8 / 0.4 / 0.6, distinct so the mix can't fold to a scalar·in).
//   biquad3  — a serial 3-section LPF/HPF cascade (klang's Biquad has no peaking/shelf, so main
//              matches coefficients on LPF/HPF sections only — HPF@80 → LPF@8000 → LPF@3000).
//   sine     — a constant-pitch oscillator: klang's per-sample Sine vs the analytic reference.
//
// klang is driven per sample through its concrete type (in = x[i]; process(); y[i] = out), and with
// the static type known the per-sample process() inlines (klang with no forced vtable dispatch).
//
// The coefficient math (RBJ Audio-EQ-Cookbook lowpass/highpass) is reproduced here in plain float
// arithmetic — main's version (heinzel::dsp::coeffs/biquad_design.hpp) is pure constexpr math with no
// graph dependency, so this is a faithful copy modulo dropping the constexpr requirement (no graph
// reads these as compile-time constants here). klang and the reference both realise the RBJ transfer
// function as transposed Direct-Form-II, so outputs differ only in float rounding — validated against
// a plain transposed-DF-II hand reference with the same loose tolerance main used.
//
// Built only when HEINZEL_BENCH_COMPARE_KLANG=ON, and only under Clang (GCC rejects klang.h). This is
// a Release/-O3 timing bench — do NOT build it under ASan: klang's Text<32>::operator= memcpy's a
// fixed 32 bytes from any C-string, so constructing a Dial with a short name over-reads the string
// literal — benign in klang's own code, but ASan aborts on it.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

// klang_shim.h declares `namespace hz = heinzel;`; heinzel itself is not used in this file (Heinzel's arm is in
// `bench_compare_klang_heinzel.cpp`), so just open the namespace rather than pull in any Heinzel header.
namespace heinzel {}
#include "klang/klang_shim.h"

#include "klang_compare_common.hpp"

#include "page_vector.hpp"

using heinzel_bench::OutputVector;

namespace {

using namespace klangcmp;

// ---- klang DSP (idiomatic mini-plugins; short names contained here) --------------------------------
namespace klang_arms {
using namespace klang::optimised;
// libc++ leaks the C library's ::signal into the global namespace, where the using-directive's
// klang::signal also lands for lookup; this using-declaration sits in a nearer scope and wins.
using klang::signal;

// The EQ example, verbatim (examples/Filtering/EQ.k), with the distinct band gains as Dial defaults.
struct EQ : Effect {
    LPF lpf;
    HPF hpf;

    EQ() {
        controls = {
            Dial("Low", 0.0, 1.0, kGLow),
            Dial("Mid", 0.0, 1.0, kGMid),
            Dial("High", 0.0, 1.0, kGHigh),
        };
    }
    void prepare() override {
        lpf.set(200);
        hpf.set(4000);
    }
    void process() override {
        signal lows = in >> lpf;
        signal highs = in >> hpf;
        signal mids = in - (lows + highs);
        lows * sqr(controls[0]) + mids * sqr(controls[1]) + highs * sqr(controls[2]) >> out;
    }
};

// A serial 3-section cascade using the same LPF/HPF sections (and Q) as kBq3.
struct Biquad3 : Effect {
    HPF s0;
    LPF s1;
    LPF s2;
    void prepare() override {
        s0.set(80, kQ);
        s1.set(8000, kQ);
        s2.set(3000, kQ);
    }
    void process() override { in >> s0 >> s1 >> s2 >> out; }
};

// Drive a klang Effect per sample: read x[i] into its input, process, write its output to y[i].
template <class FX>
void run_effect(FX& fx, const float* x, float* y, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        fx.in = x[i];
        fx.process();
        y[i] = fx.out;
    }
}

void run_sine(float* y, std::size_t n) {
    Sine osc;
    osc.set(kFreq);
    for (std::size_t i = 0; i < n; ++i) {
        osc.process();
        y[i] = kAmp * float(osc.out);
    }
}

} // namespace klang_arms

// klang keeps the sample rate in a global (default 44.1 kHz); set it to match the references before
// anything klang computes (oscillator increment, filter coefficients). Runs before any benchmark.
const bool g_klang_fs = [] {
    klang::fs = klang::SampleRate(kFs);
    return true;
}();

// ---- benches (each checks its klang arm against the library-independent reference once, via
// SkipWithError on mismatch, before timing) ----------------------------------------------------------
void bench_klang_sine(benchmark::State& state) {
    constexpr std::size_t M = 1024;
    OutputVector<float> yk(M);
    std::vector<float> ref(M);
    klang_arms::run_sine(yk.data(), M);
    for (std::size_t i = 0; i < M; ++i) {
        ref[i] = kAmp * std::sin(kTwoPi * kFreq * static_cast<float>(i) / kFs);
    }
    if (!max_abs_diff_ok(yk.data(), ref.data(), M, 2e-3f)) {
        state.SkipWithError("sine/klang: mismatch vs analytic reference");
        return;
    }
    OutputVector<float> y(N);
    for (auto _ : state) {
        klang_arms::run_sine(y.data(), N);
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * N);
}

template <class FX>
bool check_effect(std::string& err);
template <>
bool check_effect<klang_arms::Biquad3>(std::string& err) {
    constexpr std::size_t M = 1024;
    OutputVector<float> yk(M);
    std::vector<float> ref(M);
    klang_arms::Biquad3 fx;
    fx.prepare();
    klang_arms::run_effect(fx, g_in.data(), yk.data(), M);
    hand_biquad3(g_in.data(), ref.data(), M);
    if (!max_abs_diff_ok(yk.data(), ref.data(), M, 1e-3f)) {
        err = "biquad3/klang: mismatch vs hand DF-II reference";
        return false;
    }
    return true;
}
template <>
bool check_effect<klang_arms::EQ>(std::string& err) {
    constexpr std::size_t M = 1024;
    OutputVector<float> yk(M);
    std::vector<float> ref(M);
    klang_arms::EQ fx;
    fx.prepare();
    klang_arms::run_effect(fx, g_in.data(), yk.data(), M);
    hand_eq(g_in.data(), ref.data(), M);
    if (!max_abs_diff_ok(yk.data(), ref.data(), M, 1e-3f)) {
        err = "eq/klang: mismatch vs hand reference";
        return false;
    }
    return true;
}

template <class FX>
void bench_klang_effect(benchmark::State& state) {
    std::string err;
    if (!check_effect<FX>(err)) {
        state.SkipWithError(err.c_str());
        return;
    }
    OutputVector<float> y(N);
    FX fx;
    fx.prepare();
    for (auto _ : state) {
        klang_arms::run_effect(fx, g_in.data(), y.data(), N);
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * N);
}

BENCHMARK(bench_klang_sine)->Name("sine/klang")->UseRealTime();
BENCHMARK(bench_klang_effect<klang_arms::Biquad3>)->Name("biquad3/klang")->UseRealTime();
BENCHMARK(bench_klang_effect<klang_arms::EQ>)->Name("eq/klang")->UseRealTime();

} // namespace

BENCHMARK_MAIN();
