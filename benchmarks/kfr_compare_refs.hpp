#pragma once

// The KFR comparison's library-independent hand references: the loops a developer writes without a DSP library, timed
// as the `hand` arms and every other arm is checked against. In an unnamed namespace: the hand arms' files and
// Heinzel's are compiled with different flags (`benchmarks/CMakeLists.txt`), so each keeps its own copy instead of
// one definition with two meanings.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "freeverb_ref.hpp"
#include "kfr_compare_common.hpp"

namespace kfrcmp {
namespace {

// What you'd write first: a phase accumulator and `std::sin`.
inline void idiomatic_sine(float* y, std::int32_t n) {
    const float inc = kTwoPi * kFreq / kFs;
    float ph = 0.0f;
    for (std::int32_t i = 0; i < n; ++i) {
        y[static_cast<std::size_t>(i)] = kAmp * std::sin(ph);
        ph += inc;
    }
}

// A textbook DF-I biquad cascade of K sections, sample by sample.
template <std::size_t K>
void df1_cascade(const std::array<BiquadCoeffs, K>& c, const float* x, float* y, std::int32_t n) {
    float x1[K] = {};
    float x2[K] = {};
    float y1[K] = {};
    float y2[K] = {};
    for (std::int32_t i = 0; i < n; ++i) {
        float s = x[static_cast<std::size_t>(i)];
        for (std::size_t k = 0; k < K; ++k) {
            const float o = c[k].b0 * s + c[k].b1 * x1[k] + c[k].b2 * x2[k] - c[k].a1 * y1[k] - c[k].a2 * y2[k];
            x2[k] = x1[k];
            x1[k] = s;
            y2[k] = y1[k];
            y1[k] = o;
            s = o;
        }
        y[static_cast<std::size_t>(i)] = s;
    }
}

inline void hand_allpass(const float* x, float* y, std::size_t n) {
    freeverb_ref::AllpassRef ap(kApD, 0.5f);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = ap.process(x[i]);
    }
}

inline void hand_allpass_chain(const float* x, float* y, std::size_t n) {
    freeverb_ref::AllpassRef a0(kChain[0], 0.5f);
    freeverb_ref::AllpassRef a1(kChain[1], 0.5f);
    freeverb_ref::AllpassRef a2(kChain[2], 0.5f);
    freeverb_ref::AllpassRef a3(kChain[3], 0.5f);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = a3.process(a2.process(a1.process(a0.process(x[i]))));
    }
}

inline void hand_freeverb(const float* x, float* y, std::size_t n) { freeverb_ref::run(x, y, n, kRoomFb, kDamp, kWet, kDry); }

// The exact zero-history delay: y[n] = x[n - D] for n >= D, 0 before.
inline void hand_delay(const float* x, float* y, std::size_t n, std::size_t d) {
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = (i >= d) ? x[i - d] : 0.0f;
    }
}

// The FIR's valid region: y[i] = Σ_j h[K-1-j]·x[i+j].
inline void hand_fir(const float* x, const float* h, int K, int nout, float* y) {
    for (int i = 0; i < nout; ++i) {
        float a = 0.0f;
        for (int j = 0; j < K; ++j) {
            a += h[K - 1 - j] * x[i + j];
        }
        y[i] = a;
    }
}

} // namespace
} // namespace kfrcmp
