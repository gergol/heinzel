#pragma once

// Shared data of the KFR comparison bench (bench_compare_kfr*.cpp, one target split across several files by workload
// family — each file's header says which): the RBJ biquad design and the two EQ sets (3 and 8 sections), the sine
// input every filter arm processes, the FIR taps, the reverb's room and the delay/allpass sizes. No library here:
// KFR's files, the hand arms and Heinzel's arms (compiled with other flags, `benchmarks/CMakeLists.txt`) all read
// it, so each design pins its rounding (`#pragma clang fp contract(off)`), and every file reads the same numbers.
//
// main's coefficient design (heinzel::dsp::coeffs/biquad_design.hpp) is pure constexpr math with no graph dependency;
// reproduced here in plain float arithmetic, evaluated once at static initialisation: bit-identical to main's.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "page_vector.hpp"

namespace kfrcmp {

using heinzel_bench::PageVector;

constexpr std::int32_t N = 1 << 20; // ~1M samples ≈ 22 s @ 48 kHz
constexpr float kFs = 48000.0f;
constexpr float kFreq = 440.0f;
constexpr float kAmp = 0.5f;
constexpr float kTwoPi = 6.283185307179586f;

struct BiquadCoeffs {
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;
};

inline BiquadCoeffs bq_normalize(float b0, float b1, float b2, float a0, float a1, float a2) {
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}
inline BiquadCoeffs bq_lowpass(float fs, float f0, float q) {
#pragma clang fp contract(off)
    const float w0 = kTwoPi * f0 / fs;
    const float c = std::cos(w0);
    const float s = std::sin(w0);
    const float al = s / (2.0f * q);
    return bq_normalize((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
}
inline BiquadCoeffs bq_highpass(float fs, float f0, float q) {
#pragma clang fp contract(off)
    const float w0 = kTwoPi * f0 / fs;
    const float c = std::cos(w0);
    const float s = std::sin(w0);
    const float al = s / (2.0f * q);
    return bq_normalize((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
}
inline BiquadCoeffs bq_peaking(float fs, float f0, float q, float gain_db) {
#pragma clang fp contract(off)
    const float w0 = kTwoPi * f0 / fs;
    const float c = std::cos(w0);
    const float s = std::sin(w0);
    const float al = s / (2.0f * q);
    const float A = std::pow(10.0f, gain_db / 40.0f);
    return bq_normalize(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
}

// The 3-section cascade (a highpass/peaking/lowpass graphic-EQ shape).
inline const std::array<BiquadCoeffs, 3> kEq{
    bq_highpass(kFs, 80.0f, 0.707f),
    bq_peaking(kFs, 1000.0f, 1.0f, 6.0f),
    bq_lowpass(kFs, 8000.0f, 0.707f),
};
// An 8-section cascade — a deeper graphic EQ (past KFR's/Heinzel's sections-in-lanes crossover).
inline const std::array<BiquadCoeffs, 8> kEq8{
    bq_highpass(kFs, 40.0f, 0.707f),
    bq_peaking(kFs, 120.0f, 0.9f, 3.0f),
    bq_peaking(kFs, 250.0f, 0.9f, -3.0f),
    bq_peaking(kFs, 500.0f, 0.9f, 3.0f),
    bq_peaking(kFs, 1000.0f, 0.9f, -3.0f),
    bq_peaking(kFs, 2000.0f, 0.9f, 3.0f),
    bq_peaking(kFs, 4000.0f, 0.9f, -3.0f),
    bq_lowpass(kFs, 12000.0f, 0.707f),
};

// A sine input buffer (the filter workload), filled with the textbook reference so every engine sees byte-identical
// input.
inline const PageVector<float> g_sine_in = [] {
#pragma clang fp contract(off)
    PageVector<float> v(static_cast<std::size_t>(N));
    for (std::size_t i = 0; i < static_cast<std::size_t>(N); ++i) {
        v[i] = kAmp * std::sin(kTwoPi * kFreq * static_cast<float>(i) / kFs);
    }
    return v;
}();

inline bool nearly_equal(const float* a, const float* b, std::size_t n, float tol) {
    for (std::size_t i = 0; i < n; ++i) {
        if (std::fabs(a[i] - b[i]) > tol) {
            return false;
        }
    }
    return true;
}

// The room the reverb rows run (main's `reverb_mono(x, 0.84, 0.2, 0.33, 0)`); its tuning is freeverb_ref.hpp's.
constexpr float kRoomFb = 0.84f;
constexpr float kDamp = 0.2f;
constexpr float kWet = 0.33f;
constexpr float kDry = 0.0f;

// The allpass rows: one deep Schroeder allpass, and a chain of four (a diffuser).
constexpr int kApD = 521;
constexpr std::array<int, 4> kChain{521, 443, 379, 257};

// The FIR rows: a Hamming-windowed sinc low-pass at 4 kHz, K taps, normalised to unity DC (K odd: symmetric).
constexpr float kFirCut = 4000.0f;
template <int K>
std::array<float, K> make_fir_taps() {
#pragma clang fp contract(off)
    std::array<float, K> h{};
    const double fcn = static_cast<double>(kFirCut) / kFs;
    const double Md = static_cast<double>(K - 1);
    double sum = 0.0;
    for (int k = 0; k < K; ++k) {
        const double m = static_cast<double>(k) - Md / 2.0;
        const double s = (m == 0.0) ? 2.0 * fcn : std::sin(2.0 * M_PI * fcn * m) / (M_PI * m);
        const double w = 0.54 - 0.46 * std::cos(2.0 * M_PI * static_cast<double>(k) / Md);
        h[static_cast<std::size_t>(k)] = static_cast<float>(s * w);
        sum += s * w;
    }
    for (float& c : h) {
        c = static_cast<float>(c / sum);
    }
    return h;
}
template <int K>
inline const std::array<float, K> kFirTaps = make_fir_taps<K>();

} // namespace kfrcmp
