#pragma once

// Shared setup for the FAUST comparison bench (bench_compare_faust*.cpp, one target split across two
// TUs by workload family): the RBJ biquad coefficient design (main's kEq — highpass/peaking/lowpass —
// reused verbatim as literals in benchmarks/faust/biquad3.dsp, so this MUST stay numerically
// identical), the channel-strip constants, the shared test signal, and the small check helpers
// (finite / RMS-band / max-abs-diff) main's `verify()` used.
//
// main's coefficient design (heinzel::dsp::coeffs/biquad_design.hpp) is pure constexpr math with no
// graph dependency; reproduced here in plain (non-constexpr) float arithmetic for the same reason as
// the KFR bench's copy (kfr_compare_common.hpp) — no Heinzel graph reads these as compile-time
// constants on this branch.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "page_vector.hpp"

namespace faustcmp {

using heinzel_bench::PageVector;

constexpr int N = 1 << 20; // ~1M samples; the ladder16 bank splits it 16 x (N/16)
constexpr float kFs = 48000.0f;
constexpr float kFreq = 440.0f;
constexpr float kAmp = 0.5f;

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
    const float w0 = 6.283185307179586f * f0 / fs;
    const float c = std::cos(w0);
    const float s = std::sin(w0);
    const float al = s / (2.0f * q);
    return bq_normalize((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
}
inline BiquadCoeffs bq_highpass(float fs, float f0, float q) {
#pragma clang fp contract(off)
    const float w0 = 6.283185307179586f * f0 / fs;
    const float c = std::cos(w0);
    const float s = std::sin(w0);
    const float al = s / (2.0f * q);
    return bq_normalize((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
}
inline BiquadCoeffs bq_peaking(float fs, float f0, float q, float gain_db) {
#pragma clang fp contract(off)
    const float w0 = 6.283185307179586f * f0 / fs;
    const float c = std::cos(w0);
    const float s = std::sin(w0);
    const float al = s / (2.0f * q);
    const float A = std::pow(10.0f, gain_db / 40.0f);
    return bq_normalize(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
}
// EQ spec: biquad3.dsp hardcodes these same coefficient values (computed once and pasted in as FAUST
// literals when the .dsp was authored — see that file's header comment).
inline const std::array<BiquadCoeffs, 3> kEq{
    bq_highpass(kFs, 80.0f, 0.707f),
    bq_peaking(kFs, 1000.0f, 1.0f, 6.0f),
    bq_lowpass(kFs, 8000.0f, 0.707f),
};

// Channel-strip constants (mirror g_strip / channel_strip_bench in main).
constexpr float kGin = 2.0f;
constexpr float kGout = 0.7f;
constexpr float kD1 = 1.5f;
constexpr float kD2 = 1.3f;
constexpr float kD3 = 1.1f;
constexpr float kSB1 = 0.10f;
constexpr float kSB2 = -0.05f;
constexpr float kSB3 = 0.0f;

constexpr int kLadCh = 16;
constexpr int kLadNc = N / kLadCh;

// FAUST's ve.moog_vcf(0.7, 1000), linear (no saturation): fConst0 = 2π·1000/fs, fConst1 = 1 − fConst0 (the pole),
// fConst2 = fConst0⁴ (the gain), resonance feedback 2.8 (main's faust_compare_bench.cpp).
constexpr float kMoogC0 = 6283.1855f / kFs;
constexpr float kMoogC1 = 1.0f - kMoogC0;
constexpr float kMoogC2 = kMoogC0 * kMoogC0 * kMoogC0 * kMoogC0;
constexpr float kMoogRes = 2.8f;

// The room Heinzel's and the hand Freeverb run (main's `reverb_mono(x, 0.84, 0.2, 0.33, 0)`; FAUST's re.mono_freeverb
// is tuned otherwise, so its arm is held to a sanity band).
constexpr float kRoomFb = 0.84f;
constexpr float kDamp = 0.2f;
constexpr float kWet = 0.33f;
constexpr float kDry = 0.0f;

inline PageVector<float> make_sig(std::size_t n) {
#pragma clang fp contract(off)
    PageVector<float> x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = 0.4f * std::sin(0.001f * static_cast<float>(i)); // gentle — keeps the ladder out of hard clip
    }
    return x;
}

// The checks and references, in an unnamed namespace: FAUST's files and Heinzel's are compiled with different flags
// (`benchmarks/CMakeLists.txt`), so each keeps its own copy.
namespace {

inline double rms(const float* v, std::size_t n) {
    double s = 0;
    for (std::size_t i = 0; i < n; ++i) {
        s += static_cast<double>(v[i]) * v[i];
    }
    return std::sqrt(s / static_cast<double>(n));
}

inline bool all_finite(const float* v, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        if (!std::isfinite(v[i])) {
            return false;
        }
    }
    return true;
}

inline double maxabs_diff(const float* a, const float* b, std::size_t n) {
    double m = 0;
    for (std::size_t i = 0; i < n; ++i) {
        m = std::max(m, std::fabs(static_cast<double>(a[i]) - b[i]));
    }
    return m;
}

// Scalar Direct-Form-I biquad cascade — the transfer-function reference (kEq), independent of FAUST.
inline void biquad_ref(const float* x, float* y, std::size_t n) {
    std::vector<float> buf(x, x + n);
    for (const auto& c : kEq) {
        float x1 = 0;
        float x2 = 0;
        float y1 = 0;
        float y2 = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const float in = buf[i];
            const float out = c.b0 * in + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2;
            x2 = x1;
            x1 = in;
            y2 = y1;
            y1 = out;
            buf[i] = out;
        }
    }
    std::copy(buf.begin(), buf.end(), y);
}

// Scalar channel strip — the std::tanh reference (FAUST's ma.tanh is a bounded approximation of it).
inline void strip_ref(const float* x, float* y, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        float s = kGin * x[i];
        s = std::tanh(kD1 * s + kSB1);
        s = std::tanh(kD2 * s + kSB2);
        s = std::tanh(kD3 * s + kSB3);
        s = kGout * s;
        y[i] = std::max(std::min(s, 1.0f), -1.0f);
    }
}

// Sanity band for a divergent-topology arm (FAUST's own Moog model / Freeverb tuning): finite output
// with RMS in a plausible band, not a sample match against anything.
inline bool sanity_ok(const float* v, std::size_t n) {
    if (!all_finite(v, n)) {
        return false;
    }
    const double r = rms(v, n);
    return r >= 1e-4 && r <= 2.0;
}

// One linear Moog voice, FAUST's generated compute loop op for op: four one-poles inside the resonance feedback.
inline void moog_ref(const float* x, float* y, std::size_t n) {
    float r4 = 0;
    float r3 = 0;
    float r2 = 0;
    float r1 = 0;
    float r0 = 0;
    for (std::size_t i = 0; i < n; ++i) {
        r4 = x[i] + kMoogC1 * r4 - kMoogRes * r0;
        r3 = r4 + kMoogC1 * r3;
        r2 = r3 + kMoogC1 * r2;
        r1 = r2 + kMoogC1 * r1;
        r0 = kMoogC2 * r1;
        y[i] = r0;
    }
}

} // namespace

} // namespace faustcmp
