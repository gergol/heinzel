#pragma once

// The whole-picture rows of the KFR comparison (Plan/23 09 (a)): KFR's own eight-section EQ (`kEq8`: a high-pass at
// 40 Hz, six ±3 dB peaks, a low-pass at 12 kHz; eight sections in eight float lanes, KFR's best case), and one thing
// added outside it a row:
//
//   biquad8   y = EQ8(x)
//   eq8mix    y = 0.5·x + 0.5·clamp(EQ8(2·x), ±0.5)            a gain before the EQ, a clip and a dry/wet mix after it
//   eq8fb     u = x − 0.6·clamp(y[n−1], ±0.25), y = EQ8(u)       the output fed back through a clip
//   lr8       y = LR8(x)                                        an eighth-order Linkwitz-Riley low-pass at 2 kHz: four RBJ
//                                                                 low-pass sections, Q 0.5412, 1.3066, 0.5412, 1.3066
//   eq8sweep  y = band_n(EQ7(x))                                  kEq8's 2 kHz peak made +9 dB and Q 2, moved last, its
//             centre 1414·2^(1.5·sin 2π·phase[n]) (500 Hz to 4 kHz), phase[n] = frac(phase[n−1] + 2^-16)
//
// Each row runs the whole input (`g_sine_in`) and `/B256`: blocks of 256, the state carried, the same block fed again.
// Library-free, as kfr_compare_common.hpp, which it extends: the rows' settings, the moving band's design, the loop
// every hand arm runs (a section in transposed direct form II as KFR's `biquad_process` computes it, without its skew)
// and the references. KFR's, JUCE's and Heinzel's files (compiled with other flags) all read it, so its code is in an
// unnamed namespace, as the refs headers'.
//
// One check across the arms: every arm of a row against the row's definition in double (its float constants, the
// arithmetic in double), over the whole run or two blocks in a row, before it is timed; the counter `max_err` reports
// the error. The clamp is continuous, so the forms' rounding (DF-I, a bank, TDF2) stays of rounding size: one bound a
// row for every arm (`kRows`), about three times the worst arm measured (2026-10-09, zen2). The float arms are 1.1e-4
// to 1.7e-4 from the reference over a million samples, the 40 Hz high-pass's poles near 1 amplifying their rounding;
// Heinzel's 1.5e-8 to 3e-8 (its bank and its state in double), but eq8sweep's 2.8e-5 (its moving band's coefficients in
// float); JUCE's filters in double (`biquad8/juce-double`) 3e-8.

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include "kfr_compare_common.hpp"

#include "page_vector.hpp"

namespace kfrcmp {

using heinzel_bench::OutputVector;

enum class Row { biquad8, mix, fb, sweep, lr8 };

// A row's name and the bound every arm of it is held to against its reference.
struct RowSpec {
    const char* name;
    double bound;
};
constexpr std::array<RowSpec, 5> kRows{{
    {"biquad8", 5e-4},  // measured worst 1.64e-4 (kfr, whole)
    {"eq8mix", 5e-4},   // measured worst 1.62e-4 (kfr, whole)
    {"eq8fb", 4e-4},    // measured worst 1.18e-4 (juce, whole)
    {"eq8sweep", 5e-4}, // measured worst 1.65e-4 (kfr, whole)
    {"lr8", 3e-6},      // measured worst 1.02e-6 (naive, whole)
}};
constexpr const RowSpec& spec(Row row) { return kRows[static_cast<std::size_t>(row)]; }

// The `/B256` rows' block.
constexpr std::size_t kBlock = 256;

// lr8: Butterworth's fourth-order Qs (1 / (2 cos(pi/8)), 1 / (2 cos(3 pi/8))) twice, at 2 kHz: the design below, as
// constants (the hex floats of `bq_lowpass`; `lr8_designed` says whether they still are).
constexpr float kLr8Cutoff = 2000.0f;
constexpr float kLr8QLow = 0.5411961f;
constexpr float kLr8QHigh = 1.306563f;
constexpr std::array<BiquadCoeffs, 4> kLr8{{
    {0x1.c28a2p-7f, 0x1.c28a2p-6f, 0x1.c28a2p-7f, -0x1.8f1e2ep+0f, 0x1.3a64fep-1f},
    {0x1.fbf5cep-7f, 0x1.fbf5cep-6f, 0x1.fbf5cep-7f, -0x1.c1fc24p+0f, 0x1.a3b7a4p-1f},
    {0x1.c28a2p-7f, 0x1.c28a2p-6f, 0x1.c28a2p-7f, -0x1.8f1e2ep+0f, 0x1.3a64fep-1f},
    {0x1.fbf5cep-7f, 0x1.fbf5cep-6f, 0x1.fbf5cep-7f, -0x1.c1fc24p+0f, 0x1.a3b7a4p-1f},
}};

namespace {

// Whether `kLr8` holds exactly the bits of the RBJ design; else an arm would time another filter.
bool lr8_designed() {
    for (std::size_t k = 0; k < kLr8.size(); ++k) {
        const BiquadCoeffs d = bq_lowpass(kFs, kLr8Cutoff, k % 2 == 0 ? kLr8QLow : kLr8QHigh);
        const float want[] = {d.b0, d.b1, d.b2, d.a1, d.a2};
        const float have[] = {kLr8[k].b0, kLr8[k].b1, kLr8[k].b2, kLr8[k].a1, kLr8[k].a2};
        for (std::size_t i = 0; i < 5; ++i) {
            if (std::bit_cast<std::uint32_t>(want[i]) != std::bit_cast<std::uint32_t>(have[i])) {
                return false;
            }
        }
    }
    return true;
}

// The moving rows (`biquad8/heinzel-param-moving`, `naive-moving`, `juce-moving`): one band swept,
// kEq8's 1 kHz peak (band 4, -3 dB, Q 0.9) moving from 250 Hz to 4 kHz over 2 s, exponentially, repeating; the other
// seven bands rest. A block takes the frequency at its first sample, designed in double and rounded to float, as a host
// hands it. 2 s is a whole number of blocks, so one table of kMovingBlocks serves the stream, computed before timing.
constexpr std::size_t kMovingBand = 4;
constexpr std::int64_t kMovingPeriod = 96000;
constexpr std::size_t kMovingBlocks = kMovingPeriod / kBlock;
static_assert(kMovingPeriod % kBlock == 0);

BiquadCoeffs moving_peak(std::int64_t sample) {
    const double freq = 250.0 * std::pow(16.0, static_cast<double>(sample % kMovingPeriod) / static_cast<double>(kMovingPeriod));
    const double w = std::numbers::pi * freq * (2.0 / kFs);
    const double alpha = std::sin(w) / (2.0 * 0.9);
    const double big = std::exp(-3.0 * (std::numbers::ln10 / 40.0));
    const double a0 = 1.0 + alpha / big;
    const double k = -2.0 * std::cos(w) / a0;
    const auto f = [](double v) { return static_cast<float>(v); };
    return {f((1.0 + alpha * big) / a0), f(k), f((1.0 - alpha * big) / a0), f(k), f((1.0 - alpha / big) / a0)};
}

// The moving band's coefficients, block j of the sweep.
const std::vector<BiquadCoeffs>& moving_table() {
    static const std::vector<BiquadCoeffs> table = [] {
        std::vector<BiquadCoeffs> t(kMovingBlocks);
        for (std::size_t j = 0; j < t.size(); ++j) {
            t[j] = moving_peak(static_cast<std::int64_t>(j * kBlock));
        }
        return t;
    }();
    return table;
}

// Does every block's band differ, bit for bit, from the block's before (the last's for the first, as the timed runs
// follow each other)? Else a block would run on coefficients that did not change, and a `-moving` row would time a knob
// at rest.
bool moving_every_block() {
    const std::vector<BiquadCoeffs>& t = moving_table();
    for (std::size_t j = 0; j < t.size(); ++j) {
        const BiquadCoeffs& a = t[j];
        const BiquadCoeffs& b = t[(j + t.size() - 1) % t.size()];
        if (std::memcmp(&a, &b, sizeof(a)) == 0) {
            return false;
        }
    }
    return true;
}

// kEq8 with its moving band as block `j` of the sweep has it.
std::array<BiquadCoeffs, 8> moving_bank(std::size_t j) {
    std::array<BiquadCoeffs, 8> bank = kEq8;
    bank[kMovingBand] = moving_table()[j];
    return bank;
}

// `n` samples of the input through the moving bank in double, block by block, from silence: the direct form I Heinzel
// runs (`df1`), or the transposed direct form II of the hand loop and JUCE's filters. A form's own: once coefficients
// move, the form changes the numbers.
std::vector<double> moving_reference(bool df1, std::size_t n) {
    std::vector<double> y(n);
    double s[8][4]{}; // DF-I: x1, x2, y1, y2; TDF2: s1, s2
    for (std::size_t i = 0; i < n; ++i) {
        const std::array<BiquadCoeffs, 8> bank = moving_bank(i / kBlock % kMovingBlocks);
        double v = g_sine_in[i];
        for (std::size_t k = 0; k < 8; ++k) {
            const BiquadCoeffs& c = bank[k];
            double* t = s[k];
            double o;
            if (df1) {
                o = c.b0 * v + c.b1 * t[0] + c.b2 * t[1] - c.a1 * t[2] - c.a2 * t[3];
                t[1] = t[0], t[0] = v, t[3] = t[2], t[2] = o;
            } else {
                o = c.b0 * v + t[0];
                t[0] = t[1] + c.b1 * v - c.a1 * o;
                t[1] = c.b2 * v - c.a2 * o;
            }
            v = o;
        }
        y[i] = v;
    }
    return y;
}

} // namespace

// eq8mix: the gain before the EQ, the clip and the dry/wet mix after it.
constexpr float kMixGain = 2.0f;
constexpr float kMixClip = 0.5f;
constexpr float kMixDry = 0.5f;
constexpr float kMixWet = 0.5f;

// eq8fb: the output fed back through a clip. The sine through the loop peaks above it, so it clips every period; a
// clamped feedback around a stable EQ stays bounded for any gain.
constexpr float kFbGain = 0.6f;
constexpr float kFbClip = 0.25f;

// eq8sweep: kEq8's 2 kHz peak (`kSweepBand`) made +9 dB (A = 10^(9/40)) and Q 2, last in the chain (a moving section
// does not commute with fixed ones), its centre swept by an LFO whose phase steps 2^-16 a sample: exact in float, so
// every arm's LFO is the same number.
constexpr std::size_t kSweepBand = 5;
constexpr float kSweepInc = 0x1p-16f;
constexpr float kSweepCentre = 1414.0f;
constexpr float kSweepOctaves = 1.5f;
constexpr float kSweepQ = 2.0f;
constexpr float kSweepA = 1.6788040f;
constexpr float kSweepInvA = 1.0f / kSweepA;

// kEq8 without its moving band: eq8sweep's fixed seven, in order. A template, so that the graphs' copy is cut alike.
template <class S>
constexpr std::array<S, 7> fixed_seven(const std::array<S, 8>& eq) {
    std::array<S, 7> seven{};
    for (std::size_t k = 0, at = 0; k < eq.size(); ++k) {
        if (k != kSweepBand) {
            seven[at++] = eq[k];
        }
    }
    return seven;
}
inline const std::array<BiquadCoeffs, 7> kEq7 = fixed_seven(kEq8);

namespace {

// The moving band's five coefficients in `T`, made anew each sample.
template <class T>
struct Band {
    T b0;
    T b1;
    T b2;
    T a1;
    T a2;
};

// One section in transposed direct form II, as KFR's `biquad_process` computes it.
template <class T>
struct Tdf2 {
    T s1 = 0;
    T s2 = 0;

    template <class C>
    T step(const C& c, T in) {
        const T out = c.b0 * in + s1;
        s1 = s2 + c.b1 * in - c.a1 * out;
        s2 = c.b2 * in - c.a2 * out;
        return out;
    }
};

// The moving band at the LFO's phase `p` (in cycles): RBJ's peak from cos w and sin w, one division by a0. A peak's b1
// is its a1.
template <class T>
Band<T> moving_band(T p) {
    const T tau = static_cast<T>(2 * std::numbers::pi);
    const T w = kSweepCentre * std::exp2(kSweepOctaves * std::sin(tau * p)) * (tau / kFs);
    const T alpha = std::sin(w) * (0.5f / kSweepQ);
    const T r = 1 / (1 + alpha * kSweepInvA);
    const T k = -2 * std::cos(w) * r;
    return {(1 + alpha * kSweepA) * r, k, (1 - alpha * kSweepA) * r, k, (1 - alpha * kSweepInvA) * r};
}

// What a row carries from one block to the next: its sections', the band's, the output fed back, the LFO's phase.
template <class T>
struct RowState {
    std::array<Tdf2<T>, 8> eq{};
    Tdf2<T> band{};
    T fed = 0;
    T phase = 0;
};

// The sections row R runs in series: kEq8, or eq8sweep's fixed seven.
template <Row R>
const auto& sections_of() {
    if constexpr (R == Row::sweep) {
        return kEq7;
    } else if constexpr (R == Row::lr8) {
        return kLr8;
    } else {
        return kEq8;
    }
}

template <class T, std::size_t K>
T cascade(std::array<Tdf2<T>, 8>& st, const std::array<BiquadCoeffs, K>& c, T s) {
    for (std::size_t k = 0; k < K; ++k) {
        s = st[k].step(c[k], s);
    }
    return s;
}

template <class T>
T clip(T v, float level) {
    return std::min(std::max(v, static_cast<T>(-level)), static_cast<T>(level));
}

// Row R over `n` samples of `x` into `y`, on from the state `st` holds, which it carries on: the naive arm in float, the
// reference in double. One loop a sample; the coefficients and the state are copied into it, so that no store to `y`
// can touch them (the strongest hand loop is one the compiler keeps in registers).
template <Row R, class T>
void run_row(RowState<T>& st, const float* x, T* y, std::size_t n) {
    const auto eq = sections_of<R>();
    RowState<T> s = st;
    for (std::size_t i = 0; i < n; ++i) {
        const T in = x[i];
        if constexpr (R == Row::biquad8 || R == Row::lr8) {
            y[i] = cascade(s.eq, eq, in);
        } else if constexpr (R == Row::mix) {
            y[i] = kMixDry * in + kMixWet * clip(cascade(s.eq, eq, kMixGain * in), kMixClip);
        } else if constexpr (R == Row::fb) {
            s.fed = cascade(s.eq, eq, in - kFbGain * clip(s.fed, kFbClip));
            y[i] = s.fed;
        } else {
            s.phase += kSweepInc;
            s.phase -= std::floor(s.phase);
            y[i] = s.band.step(moving_band(s.phase), cascade(s.eq, eq, in));
        }
    }
    st = s;
}

// Row R in double over the whole input, from silence: every arm's reference (two blocks in a row are its first
// 2·kBlock samples). Made once a program.
template <Row R>
const std::vector<double>& reference() {
    static const std::vector<double> ref = [] {
        std::vector<double> y(static_cast<std::size_t>(N));
        RowState<double> st{};
        run_row<R>(st, g_sine_in.data(), y.data(), y.size());
        return y;
    }();
    return ref;
}

// How far `y` is from row R's first `n` samples (NaN, if any is). An arm passes within the row's bound; the counter
// `max_err` reports it.
template <Row R>
double max_error(const float* y, std::size_t n) {
    const std::vector<double>& ref = reference<R>();
    double worst = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double e = std::fabs(static_cast<double>(y[i]) - ref[i]);
        worst = e > worst || std::isnan(e) ? e : worst;
    }
    return worst;
}

// One library or hand arm of row R, `m` samples a run (N, or kBlock): `run(x, y, m)` processes them, its state carried
// from run to run. Checked over the whole run, or two blocks in a row, then timed, the same input fed again.
template <Row R, class Run>
void time_arm(benchmark::State& state, std::size_t m, const char* arm, Run run) {
    if (R == Row::lr8 && !lr8_designed()) {
        state.SkipWithError("lr8: kLr8 differs from the RBJ design");
        return;
    }
    const float* x = g_sine_in.data();
    const std::size_t checked = m == static_cast<std::size_t>(N) ? m : 2 * m;
    OutputVector<float> out(checked);
    float* y = out.data();
    for (std::size_t at = 0; at < checked; at += m) {
        run(x + at, y + at, m);
    }
    const double err = max_error<R>(y, checked);
    if (!(err <= spec(R).bound)) {
        state.SkipWithError(std::string(spec(R).name) + "/" + arm + ": mismatch vs the double reference");
        return;
    }
    for (auto _ : state) {
        run(x, y, m);
        benchmark::DoNotOptimize(y);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(m));
    state.counters["max_err"] = err;
}

} // namespace
} // namespace kfrcmp
