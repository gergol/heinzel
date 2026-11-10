// The Heinzel and hand arms of the crv comparison (bench_compare_crv), compiled with Heinzel's flags; crv's arms are in
// `bench_compare_crv.cpp`, compiled with crv's. The workload is crv's headline benchmark: an additive synth of 64
// partials over one block of 1024 samples in double, each partial's amplitude and detune from a formant parabola.
// Plan/probes/crv/README.md has the history and the findings.
//
// additive — the sine crv's `sin_fast` (a parabola after a wrap to [-pi, pi], about 1e-3 off a sine):
//   heinzel         the synth as a graph, its constants folded and the block's first sample a Param, crv's sine
//                   spelled in the DSL
//   heinzel-params  the same, the five synth settings Params (crv's naive loop takes them at run time)
//   hand            the loop one writes: voice constants once, each partial added into every sample (Heinzel's flags,
//                   so no fused multiply-add); hand-fma the same with std::fma where Heinzel fuses
//   crv             crv's graph (`subgraph_sum<voices, samples>`) at its recorded best shape, col 4x4, crv's flags;
//                   crv-col-1x16, crv-row-2x8, crv-row-4x4 its other shapes
//   crv-naive       crv's own reference loop (blocked<4>), voice constants recomputed per sample
//   crv-unrolled-*  crv's graph built again with `-mllvm -unroll-threshold=4000`: on x86 clang 22 leaves the
//                   per-element loop of crv's `sin_op` rolled, the sine scalar; unrolled, it runs in lanes as on crv's
//                   home machine. Not crv's configuration.
// additive-sin — the same synth with an accurate sine, a different function:
//   heinzel         `sin` (double core, `Normal`); heinzel-fast at `Fast`
//   heinzel-f32     in float with `sin` (the float core, 2 ulp): what a Heinzel user would write
//   hand            the hand loop with std::sin
//
// Every additive arm is checked against crv's naive loop to crv's tolerance (1e-6 of the peak), every additive-sin arm
// against the synth with std::sin, before it is timed; `max_rel_err` is how far off it is, relative to the peak.

#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <vector>

#include <benchmark/benchmark.h>

#include "heinzel/dsl.hpp"

#include "bench_machine.hpp"
#include "page_vector.hpp"

using heinzel_bench::OutputVector;

namespace hz = heinzel;

extern "C" void crv_graph_col_1x16(double* out);
extern "C" void crv_graph_col_4x4(double* out);
extern "C" void crv_graph_row_2x8(double* out);
extern "C" void crv_graph_row_4x4(double* out);
extern "C" void crv_naive_blocked4(double* out);
extern "C" void crv_graph_col_1x16_unrolled(double* out);
extern "C" void crv_graph_col_4x4_unrolled(double* out);
extern "C" void crv_graph_row_2x8_unrolled(double* out);
extern "C" void crv_graph_row_4x4_unrolled(double* out);

namespace {

constexpr int kVoices = 64;
constexpr int kSamples = 1024;
constexpr double kRate = 44100.0;

// crv's test settings (additive_synth_common.hpp).
constexpr double kFundamental = 40.0;
constexpr double kFormantFreq = 1200.0;
constexpr double kFormantWidth = 2200.0;
constexpr double kFormantAmp = 1.0;
constexpr double kFormantDetune = 5.0;

constexpr double kLow = kFormantFreq - kFormantWidth / 2.0;
constexpr double kHigh = kFormantFreq + kFormantWidth / 2.0;
constexpr double kWidthSq = (kHigh - kLow) * (kHigh - kLow);
constexpr double kQuad = -4.0 / kWidthSq;
constexpr double kLinear = 4.0 * (kLow + kHigh) / kWidthSq;
constexpr double kConst = -4.0 * kLow * kHigh / kWidthSq;

// crv's sin_fast constants (crv/math.hpp): the wrap in float constants, the parabola in double ones.
constexpr double kPiF = static_cast<float>(std::numbers::pi);
constexpr double kTwoPiF = 2.0f * static_cast<float>(std::numbers::pi);
constexpr double kInvTwoPiF = 1.0f / (2.0f * static_cast<float>(std::numbers::pi));
constexpr double kB = 4.0 / std::numbers::pi;
constexpr double kC = -4.0 / (std::numbers::pi * std::numbers::pi);
constexpr double kP = 0.225;

constexpr hz::Expr parabolic_sine(hz::Expr x) {
    hz::Expr shifted = (x + kPiF) * kInvTwoPiF;
    hz::Expr wraps = hz::floor(shifted);
    hz::Expr r = x - wraps * kTwoPiF;
    hz::Expr y = kB * r + kC * r * hz::abs(r);
    return kP * (y * hz::abs(y) - y) + y;
}

enum class Sine { Parabola, Accurate };

template <Sine S>
constexpr hz::Expr sine_of(hz::Expr x) {
    if constexpr (S == Sine::Parabola) {
        return parabolic_sine(x);
    } else {
        return hz::sin(x);
    }
}

// Each voice's partial at every sample of the block that starts at `start`, summed over the voices.
template <Sine S, hz::ElemType T = hz::f64>
constexpr hz::Expr partials_summed(hz::Expr start, hz::Expr amp, hz::Expr detuned) {
    hz::Expr sample = start + hz::iota({kSamples}, T);
    hz::Expr time = sample / kRate;
    hz::Expr phase = detuned * time;
    hz::Expr partials = amp * sine_of<S>(phase);
    return partials.sum(0);
}

template <Sine S, hz::Precision P, hz::ElemType T = hz::f64>
constexpr hz::Graph additive() {
    hz::Param start{"start", T};
    hz::Expr voice = hz::iota({kVoices}, T).unsqueeze(1);
    hz::Expr freq = kFundamental * (1.0 + voice);
    hz::Expr curve = kQuad * freq * freq + kLinear * freq + kConst;
    hz::Expr amp = kFormantAmp * curve;
    hz::Expr detuned = freq + kFormantDetune * curve;
    hz::Output y{"y"};
    y = partials_summed<S, T>(start, amp, detuned);
    return hz::finish(P, y);
}

constexpr hz::Graph additive_params() {
    hz::Param start{"start", hz::f64};
    hz::Param fundamental{"fundamental", hz::f64};
    hz::Param formant_freq{"formant_freq", hz::f64};
    // A width of at least 1 Hz: the coefficients divide by its square, and Heinzel refuses a division it cannot
    // prove away from 0 (tour 12).
    hz::Param formant_width{"formant_width", hz::f64, hz::ge(1.0)};
    hz::Param formant_amp{"formant_amp", hz::f64};
    hz::Param formant_detune{"formant_detune", hz::f64};
    hz::Expr low = formant_freq - formant_width / 2.0;
    hz::Expr high = formant_freq + formant_width / 2.0;
    hz::Expr width_sq = formant_width * formant_width;
    hz::Expr quad = -4.0 / width_sq;
    hz::Expr linear = 4.0 * (low + high) / width_sq;
    hz::Expr offset = -4.0 * low * high / width_sq;
    hz::Expr voice = hz::iota({kVoices}, hz::f64).unsqueeze(1);
    hz::Expr freq = fundamental * (1.0 + voice);
    hz::Expr curve = quad * freq * freq + linear * freq + offset;
    hz::Expr amp = formant_amp * curve;
    hz::Expr detuned = freq + formant_detune * curve;
    hz::Output y{"y"};
    y = partials_summed<Sine::Parabola>(start, amp, detuned);
    return y;
}

constexpr auto kParabola = additive<Sine::Parabola, hz::Normal>;
constexpr auto kSin = additive<Sine::Accurate, hz::Normal>;
constexpr auto kSinFast = additive<Sine::Accurate, hz::Fast>;
constexpr auto kSinF32 = additive<Sine::Accurate, hz::Normal, hz::f32>;

double parabola(double x) {
    const double r = x - std::floor((x + kPiF) * kInvTwoPiF) * kTwoPiF;
    const double y = kB * r + kC * r * std::fabs(r);
    return kP * (y * std::fabs(y) - y) + y;
}

double parabola_fma(double x) {
    const double wraps = std::floor((x + kPiF) * kInvTwoPiF);
    const double r = std::fma(-wraps, kTwoPiF, x);
    const double y = std::fma(kC * r, std::fabs(r), kB * r);
    return std::fma(kP, y * std::fabs(y) - y, y);
}

double std_sin(double x) { return std::sin(x); }

// The loop one writes: the voices' constants once, then each partial added into every sample, so the loop over the
// samples runs in lanes without reordering a sum.
template <double (*Sine)(double), bool Fma = false>
void hand(double* y) {
    double amp[kVoices];
    double detuned[kVoices];
    for (int v = 0; v < kVoices; ++v) {
        const double freq = kFundamental * (1.0 + v);
        const double curve = kQuad * freq * freq + kLinear * freq + kConst;
        amp[v] = kFormantAmp * curve;
        detuned[v] = freq + kFormantDetune * curve;
    }
    alignas(64) double time[kSamples];
    for (int s = 0; s < kSamples; ++s) {
        time[s] = s / kRate;
        y[s] = 0.0;
    }
    for (int v = 0; v < kVoices; ++v) {
        for (int s = 0; s < kSamples; ++s) {
            const double partial = Sine(detuned[v] * time[s]);
            y[s] = Fma ? std::fma(amp[v], partial, y[s]) : y[s] + amp[v] * partial;
        }
    }
}

// The synth with std::sin, summed in order: what the additive-sin arms are held to.
const std::vector<double>& with_std_sin() {
    static const std::vector<double> y = [] {
        std::vector<double> r(kSamples, 0.0);
        for (int s = 0; s < kSamples; ++s) {
            for (int v = 0; v < kVoices; ++v) {
                const double freq = kFundamental * (1.0 + v);
                const double curve = kQuad * freq * freq + kLinear * freq + kConst;
                const double phase = (freq + kFormantDetune * curve) * (s / kRate);
                r[s] += kFormantAmp * curve * std::sin(phase);
            }
        }
        return r;
    }();
    return y;
}

// crv's naive loop: what the additive arms are held to.
const std::vector<double>& crv_reference() {
    static const std::vector<double> y = [] {
        std::vector<double> r(kSamples);
        crv_naive_blocked4(r.data());
        return r;
    }();
    return y;
}

// How far `got` is off `want`, relative to `want`'s peak (infinite where a sample is NaN).
template <class T>
double off_of(const OutputVector<T>& got, const std::vector<double>& want) {
    double peak = 0.0;
    double off = 0.0;
    for (int s = 0; s < kSamples; ++s) {
        peak = std::fmax(peak, std::fabs(want[s]));
        const double d = std::fabs(static_cast<double>(got[s]) - want[s]);
        off = d == d ? std::fmax(off, d) : INFINITY;
    }
    return off / peak;
}

// One arm: one run checked against `want` to `relative` of its peak, then timed run after run.
template <class T, class Run>
void timed(benchmark::State& state, Run run, const std::vector<double>& want, double relative) {
    OutputVector<T> y(kSamples);
    run(y.data());
    const double off = off_of(y, want);
    if (!(off <= relative)) {
        state.SkipWithError("additive: off its reference");
        return;
    }
    state.counters["max_rel_err"] = off;
    for (auto _ : state) {
        run(y.data());
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * kSamples);
}

void bench_crv(benchmark::State& state, void (*arm)(double*)) { timed<double>(state, arm, crv_reference(), 1e-6); }

template <double (*Sine)(double), bool Fma>
void bench_hand(benchmark::State& state, const std::vector<double>& want) {
    timed<double>(state, hand<Sine, Fma>, want, 1e-6);
}

template <auto Build, class T>
void bench_heinzel(benchmark::State& state, const std::vector<double>& want, double relative) {
    const auto run = std::make_unique<hz::Executor<Build, heinzel_bench::kMachine>>();
    run->param("start") = T{0};
    const auto once = [&](T* y) {
        run->output("y") = y;
        run->run();
    };
    timed<T>(state, once, want, relative);
}

void bench_heinzel_params(benchmark::State& state) {
    const auto run = std::make_unique<hz::Executor<additive_params, heinzel_bench::kMachine>>();
    run->param("start") = 0.0;
    run->param("fundamental") = kFundamental;
    run->param("formant_freq") = kFormantFreq;
    run->param("formant_width") = kFormantWidth;
    run->param("formant_amp") = kFormantAmp;
    run->param("formant_detune") = kFormantDetune;
    const auto once = [&](double* y) {
        run->output("y") = y;
        run->run();
    };
    timed<double>(state, once, crv_reference(), 1e-6);
}

} // namespace

BENCHMARK_CAPTURE((bench_heinzel<kParabola, double>), parabola, crv_reference(), 1e-6)->Name("additive/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_params)->Name("additive/heinzel-params")->UseRealTime();
BENCHMARK_CAPTURE((bench_hand<parabola, false>), parabola, crv_reference())->Name("additive/hand")->UseRealTime();
BENCHMARK_CAPTURE((bench_hand<parabola_fma, true>), fma, crv_reference())->Name("additive/hand-fma")->UseRealTime();
BENCHMARK_CAPTURE(bench_crv, col_4x4, crv_graph_col_4x4)->Name("additive/crv")->UseRealTime();
BENCHMARK_CAPTURE(bench_crv, col_1x16, crv_graph_col_1x16)->Name("additive/crv-col-1x16")->UseRealTime();
BENCHMARK_CAPTURE(bench_crv, row_2x8, crv_graph_row_2x8)->Name("additive/crv-row-2x8")->UseRealTime();
BENCHMARK_CAPTURE(bench_crv, row_4x4, crv_graph_row_4x4)->Name("additive/crv-row-4x4")->UseRealTime();
BENCHMARK_CAPTURE(bench_crv, naive, crv_naive_blocked4)->Name("additive/crv-naive")->UseRealTime();
BENCHMARK_CAPTURE(bench_crv, u_col_1x16, crv_graph_col_1x16_unrolled)->Name("additive/crv-unrolled-col-1x16")->UseRealTime();
BENCHMARK_CAPTURE(bench_crv, u_col_4x4, crv_graph_col_4x4_unrolled)->Name("additive/crv-unrolled-col-4x4")->UseRealTime();
BENCHMARK_CAPTURE(bench_crv, u_row_2x8, crv_graph_row_2x8_unrolled)->Name("additive/crv-unrolled-row-2x8")->UseRealTime();
BENCHMARK_CAPTURE(bench_crv, u_row_4x4, crv_graph_row_4x4_unrolled)->Name("additive/crv-unrolled-row-4x4")->UseRealTime();
BENCHMARK_CAPTURE((bench_heinzel<kSin, double>), sin, with_std_sin(), 1e-6)->Name("additive-sin/heinzel")->UseRealTime();
BENCHMARK_CAPTURE((bench_heinzel<kSinFast, double>), fast, with_std_sin(), 1e-6)->Name("additive-sin/heinzel-fast")->UseRealTime();
BENCHMARK_CAPTURE((bench_heinzel<kSinF32, float>), f32, with_std_sin(), 1e-5)->Name("additive-sin/heinzel-f32")->UseRealTime();
BENCHMARK_CAPTURE((bench_hand<std_sin, false>), std_sin, with_std_sin())->Name("additive-sin/hand")->UseRealTime();

BENCHMARK_MAIN();
