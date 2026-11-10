#pragma once

// Shared setup for the klang comparison bench, one target in two files: `bench_compare_klang.cpp` (klang's arms) and
// `bench_compare_klang_heinzel.cpp` (Heinzel's, apart because klang.h's macros break Heinzel's headers). The RBJ
// coefficient design, the EQ example's and the serial cascade's coefficients, the shared input, and the
// library-independent transposed-DF-II references every arm is checked against. Neither klang nor Heinzel here.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "page_vector.hpp"

namespace klangcmp {
using heinzel_bench::PageVector;

constexpr std::int32_t N = 1 << 20; // ~1M samples ≈ 22 s @ 48 kHz
constexpr float kFs = 48000.0f;
constexpr float kFreq = 440.0f;
constexpr float kAmp = 0.5f;
constexpr float kTwoPi = 6.283185307179586f;
// klang's default filter Q is root2.inv = 1/√2; use the same value everywhere so coefficients line up.
constexpr float kQ = 0.70710678118654752f;

// ---- RBJ cookbook biquad design (library-independent reference math; see file header) -------------
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

// ---- EQ example: two parallel biquads + a distinct-gain band mix -----------------------------------
inline const BiquadCoeffs kLpfEq = bq_lowpass(kFs, 200.0f, kQ);
inline const BiquadCoeffs kHpfEq = bq_highpass(kFs, 4000.0f, kQ);
constexpr float kGLow = 0.8f; // per-band gains (Dial values in the example)
constexpr float kGMid = 0.4f;
constexpr float kGHigh = 0.6f;
constexpr float kWLow = kGLow * kGLow; // sqr(control)
constexpr float kWMid = kGMid * kGMid;
constexpr float kWHigh = kGHigh * kGHigh;

// ---- serial 3-section cascade (LPF/HPF only — klang has no peaking section) ------------------------
inline const std::array<BiquadCoeffs, 3> kBq3{
    bq_highpass(kFs, 80.0f, kQ),
    bq_lowpass(kFs, 8000.0f, kQ),
    bq_lowpass(kFs, 3000.0f, kQ),
};

// A sine input buffer (the filter workload), filled with the textbook reference so every engine
// sees byte-identical input.
inline const PageVector<float> g_in = [] {
#pragma clang fp contract(off)
    PageVector<float> v(N);
    for (std::size_t i = 0; i < static_cast<std::size_t>(N); ++i) {
        v[i] = kAmp * std::sin(kTwoPi * kFreq * static_cast<float>(i) / kFs);
    }
    return v;
}();

// The references and checks, in an unnamed namespace: klang's file and Heinzel's are compiled with different flags
// (`benchmarks/CMakeLists.txt`), so each keeps its own copy.
namespace {

// ---- library-independent hand references (plain transposed DF-II, klang's topology) ----------------
struct Tdf2 {
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;
    float z0 = 0.0f;
    float z1 = 0.0f;
    explicit Tdf2(BiquadCoeffs c) : b0(c.b0), b1(c.b1), b2(c.b2), a1(c.a1), a2(c.a2) {}
    inline float step(float x) {
        const float y = b0 * x + z0;
        z0 = b1 * x - a1 * y + z1;
        z1 = b2 * x - a2 * y;
        return y;
    }
};

inline void hand_biquad3(const float* x, float* y, std::size_t n) {
    Tdf2 s0(kBq3[0]);
    Tdf2 s1(kBq3[1]);
    Tdf2 s2(kBq3[2]);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = s2.step(s1.step(s0.step(x[i])));
    }
}

inline void hand_eq(const float* x, float* y, std::size_t n) {
    Tdf2 lpf(kLpfEq);
    Tdf2 hpf(kHpfEq);
    for (std::size_t i = 0; i < n; ++i) {
        const float lows = lpf.step(x[i]);
        const float highs = hpf.step(x[i]);
        const float mids = x[i] - (lows + highs);
        y[i] = kWLow * lows + kWMid * mids + kWHigh * highs;
    }
}

inline bool max_abs_diff_ok(const float* a, const float* b, std::size_t n, float tol) {
    for (std::size_t i = 0; i < n; ++i) {
        if (std::fabs(a[i] - b[i]) > tol) {
            return false;
        }
    }
    return true;
}

} // namespace

} // namespace klangcmp
