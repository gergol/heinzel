#pragma once

// Shared data of the JUCE comparison bench (bench_compare_juce*.cpp, one target split across files by workload family;
// each file's header says which): the task each row sets (the EQ, the strip, the delay, the ladder, the filters'
// cutoffs), the input every filter arm processes, and the designs that need `std::tan` or `std::exp`, evaluated once
// at static initialisation the way JUCE evaluates them. No library here: JUCE's files and Heinzel's arms (compiled with
// other flags, `benchmarks/CMakeLists.txt`) both read it, so each design pins its rounding
// (`#pragma clang fp contract(off)`), and every file reads the same numbers.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "page_vector.hpp"

namespace jucecmp {

using heinzel_bench::PageVector;

constexpr int N = 1 << 20; // ~1M samples ≈ 22 s @ 48 kHz
constexpr float kFs = 48000.0f;
constexpr float kFreq = 440.0f;
constexpr float kAmp = 0.5f;
constexpr double kPi = 3.14159265358979323846;

// The banks (ladder16, svf16, bankmix16): 16 voices of N/16 samples, as many samples as one voice of N. JUCE takes a
// buffer per voice, the input's 16 consecutive slices.
constexpr int kCh = 16;
constexpr int kNc = N / kCh;

// biquad3: a high-pass at 80 Hz, a peak at 1 kHz (+6 dB), a low-pass at 8 kHz. JUCE designs it with its own
// `IIR::Coefficients::make*`, which bench_compare_juce.cpp reads out of the library (`juce_eq`): the Heinzel arm runs a
// constexpr copy of those numbers, checked bit for bit, so both run the same filter.
constexpr float kHpHz = 80.0f;
constexpr float kHpQ = 0.707f;
constexpr float kPkHz = 1000.0f;
constexpr float kPkQ = 1.0f;
constexpr float kPkDb = 6.0f;
constexpr float kLpHz = 8000.0f;
constexpr float kLpQ = 0.707f;
struct Biquad {
    // y = b0 x + b1 x[n-1] + b2 x[n-2] - a1 y[n-1] - a2 y[n-2]
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;
};
std::array<Biquad, 3> juce_eq();

// The channel strip: a gain, three `tanh` saturators `tanh(d·s + b)`, a gain, clamped to ±1.
constexpr float kGin = 2.0f;
constexpr float kGout = 0.7f;
constexpr float kD1 = 1.5f;
constexpr float kD2 = 1.3f;
constexpr float kD3 = 1.1f;
constexpr float kSB1 = 0.10f;
constexpr float kSB2 = -0.05f;
constexpr float kSB3 = 0.0f;

// delay: JUCE's "DSP Delay Line" tutorial — a 0.5 s line whose feedback runs through a first-order high-pass at 1 kHz,
// mixed wet. The high-pass is JUCE's `makeFirstOrderHighPass`, read out of the library as the EQ is (`juce_delay_hp`).
// The feedback is linear: the ladder and sine rows time `tanh`, this one the line.
constexpr float kDelHpHz = 1000.0f;
constexpr float kDelFb = 0.5f;
constexpr float kDelWet = 0.8f;
constexpr int kDelD = 24000;
struct FirstOrder {
    // y = b0 x + b1 x[n-1] - a1 y[n-1]
    float b0;
    float b1;
    float a1;
};
FirstOrder juce_delay_hp();

// chorus: x and x as it was d ago, mixed half and half, d swept from 5 to 25 ms by a triangle whose phase steps 2^-16 of a
// turn a sample (0.73 Hz at 48 kHz): its phase, the triangle, d and d in samples are exact in float, so every arm reads
// at the same position, at most 1,200 samples back. longdelay: the same over 10 to 120 ms, 5,760 samples
// (Plan/28-dsl-proposals/05 D4).
struct Sweep {
    float centre; // ms
    float depth;  // ms either side
};
constexpr Sweep kChorus{15.0f, 10.0f};
constexpr Sweep kLongDelay{65.0f, 55.0f};
constexpr float kSweepInc = 0x1p-16f;
constexpr float kSweepMix = 0.5f;
constexpr float kSamplesPerMs = kFs / 1000.0f;
constexpr int kChorusBlock = 64;
constexpr int kLongBlock = 4096;
// The distance at `phase`, in samples: (centre + depth (4 |phase - 1/2| - 1)) 48.
constexpr float sweep_samples(const Sweep& s, float phase) {
    const float off = phase - 0.5f;
    const float tri = 4.0f * (off < 0.0f ? -off : off) - 1.0f;
    return (s.centre + s.depth * tri) * kSamplesPerMs;
}
// The longest distance, in samples.
constexpr int longest_samples(const Sweep& s) { return static_cast<int>((s.centre + s.depth) * kSamplesPerMs); }

// ladder: JUCE's `LadderFilter` at the "Introduction to DSP" tutorial's setting (LPF12, 1 kHz, resonance 0.7, drive
// 1.2), and the coefficients JUCE derives from it, evaluated once as floats (main's): a1 = exp(−2π·cut/fs), g = 1 − a1,
// b0 = g·0.769…, b1 = g·0.231…; gain = 1.2^−2.642·0.6103 + 0.3903, drive2 = 1.2·0.04 + 0.96, gain2 likewise; comp 0.5
// (LPF12); K = −4·(0.1 + 0.9·0.7); the 2nd stage out, times 1.2.
constexpr float kLadCut = 1000.0f;
constexpr float kLadRes = 0.7f;
constexpr float kLadDrive = 1.2f;
constexpr float kLadA1 = 0.87730577f;
constexpr float kLadG = 1.0f - kLadA1;
constexpr float kLadB0 = kLadG * 0.76923076923f;
constexpr float kLadB1 = kLadG * 0.23076923076f;
constexpr float kLadGain = 0.76730449f;
constexpr float kLadDrive2 = 1.008f;
constexpr float kLadGain2 = 0.98788632f;
constexpr float kLadComp = 0.5f;
constexpr float kLadK = -2.92f;
constexpr float kLadOut = 1.2f;

// voice: the tutorial's chain, an oscillator at 110 Hz through the ladder, times a gain. The amplitude keeps the ladder
// out of its near-self-oscillation, where JUCE's table `tanh` and the exact one part.
constexpr float kVoiceFreq = 110.0f;
constexpr float kVoiceAmp = 0.5f;
constexpr float kVoiceGain = 0.8f;

// bankmix16: 16 one-poles y = b0·x + fb·y[n-1], summed to mono.
constexpr float kBankB0 = 0.5f;
constexpr float kBankFb = 0.7f;

// fir: a 64th-order (65-tap) Hamming-windowed sinc low-pass at 4 kHz, normalised to unity DC — the KFR comparison's
// design at K = 65 (JUCE designs its own taps, `designFIRLowpassWindowMethod`).
constexpr int kFirOrder = 64;
constexpr int kFirK = kFirOrder + 1;
constexpr int kFirNout = N - kFirK + 1;
constexpr float kFirCut = 4000.0f;
inline const std::array<float, kFirK> kFirTaps = [] {
#pragma clang fp contract(off)
    std::array<float, kFirK> h{};
    const double fcn = static_cast<double>(kFirCut) / kFs;
    double sum = 0.0;
    for (int k = 0; k < kFirK; ++k) {
        const double m = static_cast<double>(k) - kFirOrder / 2.0;
        const double s = (m == 0.0) ? 2.0 * fcn : std::sin(2.0 * kPi * fcn * m) / (kPi * m);
        const double w = 0.54 - 0.46 * std::cos(2.0 * kPi * static_cast<double>(k) / kFirOrder);
        h[static_cast<std::size_t>(k)] = static_cast<float>(s * w);
        sum += s * w;
    }
    for (float& c : h) {
        c = static_cast<float>(c / sum);
    }
    return h;
}();

// svf and linkwitz: Zavalishin's TPT state-variable stage, with JUCE's coefficients as its `update()` computes them
// (`StateVariableTPTFilter`, `LinkwitzRileyFilter`): g = tan(π·cut/fs) and h = 1/(1 + R2·g + g²) in double, R2 = 1/Q.
struct Tpt {
    float g;
    float r2;
    float h;
};
inline Tpt tpt_design(float cutoff, double r2) {
#pragma clang fp contract(off)
    const auto g = static_cast<float>(std::tan(kPi * cutoff / static_cast<double>(kFs)));
    const auto R2 = static_cast<float>(r2);
    return {g, R2, static_cast<float>(1.0 / (1.0 + R2 * g + g * g))};
}
constexpr float kSvfCut = 1000.0f;
constexpr float kSvfRes = 0.70710678f;
constexpr float kLrCut = 2000.0f;
inline const Tpt kSvf = tpt_design(kSvfCut, 1.0 / kSvfRes);
inline const Tpt kLr = tpt_design(kLrCut, std::sqrt(2.0)); // two stages, Butterworth each

// envfollow: JUCE's `BallisticsFilter` as a peak follower, 1 ms attack, 100 ms release; its coefficients
// exp(−2π·1000/(fs·ms)) in double, as the library computes them.
struct Ballistics {
    float at;
    float rl;
};
constexpr float kBalAtkMs = 1.0f;
constexpr float kBalRelMs = 100.0f;
inline const Ballistics kBal = [] {
#pragma clang fp contract(off)
    const double e = -2.0 * kPi * 1000.0 / static_cast<double>(kFs);
    return Ballistics{static_cast<float>(std::exp(e / kBalAtkMs)), static_cast<float>(std::exp(e / kBalRelMs))};
}();

// maiverb: MAI_verb's ConvolutionEngine (despite its name a Schroeder reverb: four combs sharing one damping state, four
// allpasses, wet/dry, `tanh`), at its constructor's 44.1 kHz tuning, used as is.
constexpr std::array<int, 4> kVerbComb{1116, 1188, 1277, 1356};
constexpr std::array<int, 4> kVerbAllpass{556, 441, 341, 225};
constexpr float kVerbAllpassFb = 0.5f;
constexpr float kVerbFeedback = 0.84f;
constexpr float kVerbDamp = 0.2f;
constexpr float kVerbWet = 0.33f;

// The input every filter arm processes, the same bytes for every engine.
inline const PageVector<float> g_in = [] {
#pragma clang fp contract(off)
    PageVector<float> v(static_cast<std::size_t>(N));
    for (std::size_t i = 0; i < v.size(); ++i) {
        v[i] = 0.6f * std::sin(0.01f * static_cast<float>(i));
    }
    return v;
}();

} // namespace jucecmp
