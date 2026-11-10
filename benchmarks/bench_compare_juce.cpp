// Third-party comparison: Heinzel vs JUCE's juce::dsp (8.0.13), the framework an audio developer knows — main's
// juce_compare_bench.cpp, ported (Plan/23 P7). Each library solves the same task its own idiomatic way: JUCE designs
// its own coefficients, runs its own Oscillator, composes a real ProcessorChain. Every arm is checked against a
// library-independent reference (juce_compare_refs.hpp) before it is timed, never against the other library. Where
// JUCE designs coefficients (the EQ, the delay's high-pass), the Heinzel arm runs a constexpr copy of JUCE's own
// numbers, checked bit for bit (the EQ within 4 ULPs), so both run the same filter. JUCE's filters, and the hand arms,
// read their coefficients at run time; the EQ's `heinzel-param` arm does too, its coefficients Params set from JUCE's.
//
// This file: sine, biquad3, biquad3dyn, strip, delay, fir, chorus, longdelay. bench_compare_juce_filters.cpp: ladder,
// ladder16, voice, svf, svf16, linkwitz, envfollow, bankmix16, maiverb. bench_compare_juce_eq8.cpp: biquad8 and eq8fb
// (Plan/23 09 (a), KFR's EQ alone and in a feedback loop, checked against eq8_common.hpp's references).
// bench_compare_juce_channels.cpp: biquad3ch (09 (b), the EQ on 2, 8 and 16 channels, planar and interleaved).
//   sine       — 440 Hz. JUCE's Oscillator over `std::sin` (`juce`) and over its 128-point table (`juce-lut`, cheaper
//                and coarser); each checked by its RMS.
//   biquad3    — a high-pass at 80 Hz, a peak at 1 kHz (+6 dB), a low-pass at 8 kHz: three IIR::Filter (transposed
//                direct form II) in a ProcessorChain; checked by its measured response and against the direct-form-I
//                cascade over its coefficients.
//   biquad3dyn — the same, one block of M samples a run (a plugin's block size, given at run time).
//   strip      — a ProcessorChain of Gain, three WaveShaper (`std::tanh`), Gain, WaveShaper (the clamp), one stage over
//                the whole block after the other.
//   delay      — the "DSP Delay Line" tutorial: a DelayLine and an IIR::Filter, sample by sample.
//   fir        — FIR::Filter, 65 taps of its own design (`designFIRLowpassWindowMethod`, Hamming); checked by its
//                measured response.
//   chorus     — a DelayLine with linear interpolation, its distance swept 5 to 25 ms by a triangle, sample by sample,
//                mixed half and half with the dry signal (`juce`); the same as a hand loop over a ring of a power of two
//                samples (`naive`); over the whole input and `/B64`, blocks of 64. Checked against the double reference.
//   longdelay  — the same over 10 to 120 ms, in blocks of 64 and of 4,096 (`/B64`, `/B4096`).
//
// The arms read the shared input and write a buffer of their own (out of place, as a host's block), the state carried
// from run to run; the oscillator clears its buffer each run, because Oscillator adds to its block. Heinzel's arms are
// bench_compare_juce_heinzel*.cpp's, an object library of the target: this file and JUCE's modules are compiled
// without Heinzel's `-ffp-contract=off`. Built only when HEINZEL_BENCH_COMPARE_JUCE=ON.

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>
#include <juce_dsp/juce_dsp.h>

#include "juce_arms.hpp"
#include "juce_compare_common.hpp"
#include "juce_compare_refs.hpp"

#include "page_vector.hpp"

using heinzel_bench::OutputVector;

namespace {

using namespace jucecmp;
using juce_arms::kMono;
using juce_arms::process;
using juce_arms::timed;

using Eq = juce::dsp::ProcessorChain<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Filter<float>>;

// JUCE's own designs of the spec; makePeakFilter takes a linear gain, so +6 dB is 10^(6/20).
void design(Eq& eq) {
    using C = juce::dsp::IIR::Coefficients<float>;
    eq.get<0>().coefficients = C::makeHighPass(kFs, kHpHz, kHpQ);
    eq.get<1>().coefficients = C::makePeakFilter(kFs, kPkHz, kPkQ, std::pow(10.0f, kPkDb / 20.0f));
    eq.get<2>().coefficients = C::makeLowPass(kFs, kLpHz, kLpQ);
    eq.prepare(kMono);
    eq.reset();
}

juce::dsp::IIR::Coefficients<float>::Ptr delay_hp() {
    return juce::dsp::IIR::Coefficients<float>::makeFirstOrderHighPass(kFs, kDelHpHz);
}

} // namespace

// JUCE's coefficients as JUCE stores them (normalised by a0, in its processing order b0 b1 b2 a1 a2).
std::array<jucecmp::Biquad, 3> jucecmp::juce_eq() {
    Eq eq;
    design(eq);
    const auto section = [](const juce::dsp::IIR::Filter<float>& filter) {
        const float* c = filter.coefficients->getRawCoefficients();
        return Biquad{c[0], c[1], c[2], c[3], c[4]};
    };
    return {section(eq.get<0>()), section(eq.get<1>()), section(eq.get<2>())};
}

jucecmp::FirstOrder jucecmp::juce_delay_hp() {
    const auto hp = delay_hp();
    const float* c = hp->getRawCoefficients();
    return {c[0], c[1], c[2]};
}

namespace {

// ---- sine: Oscillator, the generator called on the phase each sample ----------------------------------------------
void bench_juce_sine(benchmark::State& state, std::size_t table) {
    juce::dsp::Oscillator<float> osc([](float x) { return kAmp * std::sin(x); }, table);
    osc.setFrequency(kFreq, true);
    osc.prepare(kMono);
    OutputVector<float> y(static_cast<std::size_t>(N));
    const double want = kAmp / std::sqrt(2.0);
    const double tol = table == 0 ? 0.02 : 0.03;
    timed(
        state, N, y.data(),
        [&] {
            std::fill(y.begin(), y.end(), 0.0f);
            float* channels[] = {y.data()};
            juce::dsp::AudioBlock<float> block(channels, 1, y.size());
            osc.process(juce::dsp::ProcessContextReplacing<float>(block));
        },
        [&]() -> const char* {
            return std::fabs(rms(y.data(), y.size()) - want) <= tol * want ? nullptr : "sine/juce: RMS off the analytic sine";
        });
}

// ---- biquad3: the cascade designed by JUCE ---------------------------------------------------------------------
// The response the spec asks for, measured (main's check): an octave below the high-pass, at the peak, an octave above
// the low-pass.
const char* eq_response() {
    const auto gain = [](double freq) {
        return measured_gain(
            [](const float* in, float* out) {
                Eq eq;
                design(eq);
                process(eq, in, out, 1, static_cast<std::size_t>(N));
            },
            freq);
    };
    const double lo = gain(kHpHz / 2.0);
    const double mid = gain(kPkHz);
    const double hi = gain(kLpHz * 2.0);
    return lo < 0.6 && mid > 1.5 && mid < 2.5 && hi < 0.6 ? nullptr : "biquad3/juce: the response is not the high-pass, peak and low-pass";
}

// M samples a run (N, or a block size given at run time); the first run, from silence, against the direct form I.
void bench_juce_eq(benchmark::State& state, std::size_t m) {
    Eq eq;
    design(eq);
    OutputVector<float> y(m);
    std::vector<float> ref(m);
    eq_ref(juce_eq(), g_in.data(), ref.data(), m);
    timed(
        state, static_cast<std::int64_t>(m), y.data(), [&] { process(eq, g_in.data(), y.data(), 1, m); },
        [&]() -> const char* {
            return maxabs_diff(y.data(), ref.data(), m) <= 1e-3 ? nullptr : "biquad3/juce: mismatch vs the direct-form-I cascade";
        });
}
void bench_juce_eq3(benchmark::State& state) {
    if (const char* why = eq_response()) {
        state.SkipWithError(why);
        return;
    }
    bench_juce_eq(state, static_cast<std::size_t>(N));
}
void bench_juce_eq_dyn(benchmark::State& state) { bench_juce_eq(state, static_cast<std::size_t>(state.range(0))); }

// ---- strip: a ProcessorChain, one stage over the whole block after the other -------------------------------------
float sat1(float s) { return std::tanh(kD1 * s + kSB1); }
float sat2(float s) { return std::tanh(kD2 * s + kSB2); }
float sat3(float s) { return std::tanh(kD3 * s + kSB3); }
float clamp_unit(float s) { return std::min(std::max(s, -1.0f), 1.0f); }

void bench_juce_strip(benchmark::State& state) {
    juce::dsp::ProcessorChain<juce::dsp::Gain<float>, juce::dsp::WaveShaper<float>, juce::dsp::WaveShaper<float>,
                              juce::dsp::WaveShaper<float>, juce::dsp::Gain<float>, juce::dsp::WaveShaper<float>>
        chain;
    chain.get<0>().setGainLinear(kGin);
    chain.get<1>().functionToUse = &sat1;
    chain.get<2>().functionToUse = &sat2;
    chain.get<3>().functionToUse = &sat3;
    chain.get<4>().setGainLinear(kGout);
    chain.get<5>().functionToUse = &clamp_unit;
    chain.prepare(kMono);
    chain.reset();
    OutputVector<float> y(static_cast<std::size_t>(N));
    std::vector<float> ref(y.size());
    strip_ref(g_in.data(), ref.data(), ref.size());
    timed(
        state, N, y.data(), [&] { process(chain, g_in.data(), y.data(), 1, y.size()); },
        [&]() -> const char* { return maxabs_diff(y.data(), ref.data(), y.size()) <= 1e-4 ? nullptr : "strip/juce: mismatch vs the tanh reference"; });
}

// ---- delay: the tutorial's per-sample loop over a DelayLine (no interpolation: an integer delay) and the high-pass ----
struct Delay {
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> line{kDelD + 4};
    juce::dsp::IIR::Filter<float> hp;

    Delay() {
        line.prepare(kMono);
        line.setDelay(static_cast<float>(kDelD));
        hp.coefficients = delay_hp();
        hp.prepare(kMono);
        line.reset();
        hp.reset();
    }
    void process(const float* x, float* y, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            const float delayed = hp.processSample(line.popSample(0));
            line.pushSample(0, x[i] + kDelFb * delayed);
            y[i] = x[i] + kDelWet * delayed;
        }
    }
};

void bench_juce_delay(benchmark::State& state) {
    Delay delay;
    OutputVector<float> y(static_cast<std::size_t>(N));
    std::vector<float> ref(y.size());
    delay_ref(juce_delay_hp(), g_in.data(), ref.data(), ref.size());
    timed(
        state, N, y.data(), [&] { delay.process(g_in.data(), y.data(), y.size()); },
        [&]() -> const char* { return maxabs_diff(y.data(), ref.data(), y.size()) <= 1e-3 ? nullptr : "delay/juce: mismatch vs the delay loop"; });
}

// ---- chorus and longdelay: JUCE's DelayLine, linear, a sample at a time; and the hand loop ---------------------------
template <const Sweep& S>
struct JuceChorus {
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> line{longest_samples(S)};
    float phase = 0.0f;

    JuceChorus() {
        line.prepare(kMono);
        line.reset();
    }
    void process(const float* x, float* y, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            phase += kSweepInc;
            phase -= std::floor(phase);
            line.pushSample(0, x[i]);
            const float wet = line.popSample(0, sweep_samples(S, phase));
            y[i] = kSweepMix * x[i] + kSweepMix * wet;
        }
    }
};

// A ring of a power of two samples, the newest written first, the two taps blended in float.
template <const Sweep& S>
struct NaiveChorus {
    OutputVector<float, 1> ring = OutputVector<float, 1>(std::bit_ceil(static_cast<std::size_t>(longest_samples(S) + 2)));
    std::size_t mask = ring.size() - 1;
    std::size_t at = 0;
    float phase = 0.0f;

    void process(const float* x, float* y, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            phase += kSweepInc;
            phase -= std::floor(phase);
            ring[at] = x[i];
            const float d = sweep_samples(S, phase);
            const auto whole = static_cast<std::size_t>(d);
            const float f = d - static_cast<float>(whole);
            const float near = ring[(at - whole) & mask];
            const float far = ring[(at - whole - 1) & mask];
            y[i] = kSweepMix * x[i] + kSweepMix * (near + f * (far - near));
            at = (at + 1) & mask;
        }
    }
};

// One chorus arm, `m` samples a run: the whole input, or one block fed again and again, its state carried; checked over
// the whole run or two blocks in a row against the double reference first.
template <const Sweep& S, class Arm>
void bench_sweep(benchmark::State& state, std::size_t m, const char* mismatch) {
    Arm arm;
    const std::size_t checked = m == static_cast<std::size_t>(N) ? m : 2 * m;
    OutputVector<float> y(checked);
    std::vector<float> ref(checked);
    for (std::size_t at = 0; at < checked; at += m) {
        arm.process(g_in.data() + at, y.data() + at, m);
    }
    chorus_ref(S, g_in.data(), ref.data(), checked);
    if (maxabs_diff(y.data(), ref.data(), checked) > 1e-5) {
        state.SkipWithError(mismatch);
        return;
    }
    for (auto _ : state) {
        arm.process(g_in.data(), y.data(), m);
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(m));
}

// ---- fir: FIR::Filter, a circular buffer and a scalar inner product a sample, over taps of its own design ------------
void design(juce::dsp::FIR::Filter<float>& fir) {
    fir.coefficients = juce::dsp::FilterDesign<float>::designFIRLowpassWindowMethod(
        kFirCut, kFs, static_cast<std::size_t>(kFirOrder), juce::dsp::WindowingFunction<float>::hamming);
    fir.prepare(kMono);
    fir.reset();
}

void bench_juce_fir(benchmark::State& state) {
    juce::dsp::FIR::Filter<float> fir;
    design(fir);
    OutputVector<float> y(static_cast<std::size_t>(N));
    const auto gain = [](double freq) {
        return measured_gain(
            [](const float* in, float* out) {
                juce::dsp::FIR::Filter<float> f;
                design(f);
                process(f, in, out, 1, static_cast<std::size_t>(N));
            },
            freq);
    };
    timed(
        state, N, y.data(), [&] { process(fir, g_in.data(), y.data(), 1, y.size()); },
        [&]() -> const char* { return gain(1000.0) > 0.85 && gain(12000.0) < 0.15 ? nullptr : "fir/juce: its taps are not the low-pass"; });
}

BENCHMARK_CAPTURE(bench_juce_sine, std_sin, std::size_t{0})->Name("sine/juce")->UseRealTime();
BENCHMARK_CAPTURE(bench_juce_sine, table, std::size_t{128})->Name("sine/juce-lut")->UseRealTime();
BENCHMARK(bench_juce_eq3)->Name("biquad3/juce")->UseRealTime();
BENCHMARK(bench_juce_eq_dyn)->Name("biquad3dyn/juce")->RangeMultiplier(4)->Range(256, 16384)->UseRealTime();
BENCHMARK(bench_juce_strip)->Name("strip/juce")->UseRealTime();
BENCHMARK(bench_juce_delay)->Name("delay/juce")->UseRealTime();
BENCHMARK(bench_juce_fir)->Name("fir/juce")->UseRealTime();
constexpr std::size_t kWhole = N;
BENCHMARK_CAPTURE((bench_sweep<kChorus, JuceChorus<kChorus>>), whole, kWhole, "chorus/juce: mismatch vs the reference")->Name("chorus/juce")->UseRealTime();
BENCHMARK_CAPTURE((bench_sweep<kChorus, NaiveChorus<kChorus>>), whole, kWhole, "chorus/naive: mismatch vs the reference")->Name("chorus/naive")->UseRealTime();
BENCHMARK_CAPTURE((bench_sweep<kChorus, JuceChorus<kChorus>>), block, kChorusBlock, "chorus/juce/B64: mismatch vs the reference")->Name("chorus/juce/B64")->UseRealTime();
BENCHMARK_CAPTURE((bench_sweep<kChorus, NaiveChorus<kChorus>>), block, kChorusBlock, "chorus/naive/B64: mismatch vs the reference")->Name("chorus/naive/B64")->UseRealTime();
BENCHMARK_CAPTURE((bench_sweep<kLongDelay, JuceChorus<kLongDelay>>), block, kChorusBlock, "longdelay/juce/B64: mismatch vs the reference")->Name("longdelay/juce/B64")->UseRealTime();
BENCHMARK_CAPTURE((bench_sweep<kLongDelay, NaiveChorus<kLongDelay>>), block, kChorusBlock, "longdelay/naive/B64: mismatch vs the reference")->Name("longdelay/naive/B64")->UseRealTime();
BENCHMARK_CAPTURE((bench_sweep<kLongDelay, JuceChorus<kLongDelay>>), long_block, kLongBlock, "longdelay/juce/B4096: mismatch vs the reference")->Name("longdelay/juce/B4096")->UseRealTime();
BENCHMARK_CAPTURE((bench_sweep<kLongDelay, NaiveChorus<kLongDelay>>), long_block, kLongBlock, "longdelay/naive/B4096: mismatch vs the reference")->Name("longdelay/naive/B4096")->UseRealTime();

} // namespace

BENCHMARK_MAIN();
