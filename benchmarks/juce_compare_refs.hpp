#pragma once

// The JUCE comparison's library-independent references: the textbook loops every arm is checked against, and the
// checks themselves. In an unnamed namespace: JUCE's files and Heinzel's are compiled with different flags
// (`benchmarks/CMakeLists.txt`), so each keeps its own copy instead of one definition with two meanings.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include "juce_compare_common.hpp"

#include "page_vector.hpp"

namespace jucecmp {
using heinzel_bench::PageVector;
using heinzel_bench::OutputVector;

namespace {

inline double maxabs_diff(const float* a, const float* b, std::size_t n) {
    double m = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        m = std::max(m, std::fabs(static_cast<double>(a[i]) - b[i]));
    }
    return m;
}

inline double rms(const float* v, std::size_t n) {
    double s = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        s += static_cast<double>(v[i]) * v[i];
    }
    return std::sqrt(s / static_cast<double>(n));
}

// A filter's gain at `freq`, measured: a unit sine through `run(in, out)` over N samples, the transient dropped, the
// rest's RMS times √2. Whatever the filter is made of.
template <class Run>
double measured_gain(Run run, double freq) {
    PageVector<float> in(static_cast<std::size_t>(N));
    OutputVector<float> out(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        in[i] = static_cast<float>(std::sin(2.0 * kPi * freq * static_cast<double>(i) / kFs));
    }
    run(in.data(), out.data());
    constexpr std::size_t lo = 1 << 15;
    return rms(out.data() + lo, out.size() - lo) * std::sqrt(2.0);
}

// The EQ's sections in series, each the textbook direct form I.
inline void eq_ref(const std::array<Biquad, 3>& set, const float* x, float* y, std::size_t n) {
    float x1[3] = {};
    float x2[3] = {};
    float y1[3] = {};
    float y2[3] = {};
    for (std::size_t i = 0; i < n; ++i) {
        float s = x[i];
        for (std::size_t k = 0; k < 3; ++k) {
            const Biquad& c = set[k];
            const float o = c.b0 * s + c.b1 * x1[k] + c.b2 * x2[k] - c.a1 * y1[k] - c.a2 * y2[k];
            x2[k] = x1[k];
            x1[k] = s;
            y2[k] = y1[k];
            y1[k] = o;
            s = o;
        }
        y[i] = s;
    }
}

inline void strip_ref(const float* x, float* y, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        float s = kGin * x[i];
        s = std::tanh(kD1 * s + kSB1);
        s = std::tanh(kD2 * s + kSB2);
        s = std::tanh(kD3 * s + kSB3);
        y[i] = std::min(std::max(kGout * s, -1.0f), 1.0f);
    }
}

// The delay effect on a circular buffer: the line's sample D ago through the high-pass, the line fed the input plus the
// filtered feedback, the output the input plus the filtered line, wet.
inline void delay_ref(const FirstOrder& hp, const float* x, float* y, std::size_t n) {
    std::vector<float> line(static_cast<std::size_t>(kDelD) + 1, 0.0f);
    std::size_t w = 0;
    float raw1 = 0.0f;
    float out1 = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        const float raw = line[(w + 1) % line.size()]; // written D samples ago
        const float delayed = hp.b0 * raw + hp.b1 * raw1 - hp.a1 * out1;
        raw1 = raw;
        out1 = delayed;
        line[w] = x[i] + kDelFb * delayed;
        w = (w + 1) % line.size();
        y[i] = x[i] + kDelWet * delayed;
    }
}

// The chorus from silence: x and x as it was the swept distance ago, between two samples blended linearly, in double.
// The phase steps first, so the first sample reads at its first step.
inline void chorus_ref(const Sweep& sweep, const float* x, float* y, std::size_t n) {
    float phase = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        phase += kSweepInc;
        phase -= std::floor(phase);
        const double d = sweep_samples(sweep, phase);
        const auto whole = static_cast<std::size_t>(d);
        const double f = d - static_cast<double>(whole);
        const double near = i >= whole ? static_cast<double>(x[i - whole]) : 0.0;
        const double far = i >= whole + 1 ? static_cast<double>(x[i - whole - 1]) : 0.0;
        y[i] = static_cast<float>(kSweepMix * x[i] + kSweepMix * (near + f * (far - near)));
    }
}

inline float tanh_exact(float z) { return std::tanh(z); }
// main's register-only saturator: clamped to ±3, where the [3/2] Padé form reaches ±1, then z(27 + z²)/(27 + 9z²).
inline float tanh_pade(float z) {
    const float c = std::max(std::min(z, 3.0f), -3.0f);
    const float c2 = c * c;
    return c * (27.0f + c2) / (27.0f + 9.0f * c2);
}

// The ladder's textbook loop (LPF12), one sample a step: five states, the drive and the resonance feedback through
// `sat`; the 2nd stage out.
struct LadderRef {
    float (*sat)(float);
    float s0 = 0;
    float s1 = 0;
    float s2 = 0;
    float s3 = 0;
    float s4 = 0;

    float step(float x) {
        const float dx = kLadGain * sat(kLadDrive * x);
        const float a0 = dx + kLadK * (kLadGain2 * sat(kLadDrive2 * s4) - dx * kLadComp);
        const float a1 = kLadB1 * s0 + kLadA1 * s1 + kLadB0 * a0;
        const float a2 = kLadB1 * s1 + kLadA1 * s2 + kLadB0 * a1;
        const float a3 = kLadB1 * s2 + kLadA1 * s3 + kLadB0 * a2;
        const float a4 = kLadB1 * s3 + kLadA1 * s4 + kLadB0 * a3;
        s0 = a0, s1 = a1, s2 = a2, s3 = a3, s4 = a4;
        return a2;
    }
};
inline void ladder_ref(float (*sat)(float), const float* x, float* y, std::size_t n) {
    LadderRef ladder{sat};
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = kLadOut * ladder.step(x[i]);
    }
}

// The voice: an oscillator's samples `osc` through the exact-`tanh` ladder, times the voice's gain.
inline void voice_ref(const float* osc, float* y, std::size_t n) {
    LadderRef ladder{tanh_exact};
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = kLadOut * kVoiceGain * ladder.step(osc[i]);
    }
}
// The voice's oscillator, the phase in exact arithmetic: amp·sin(2π·110·i/fs).
inline void exact_sine(float* y, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = kVoiceAmp * static_cast<float>(std::sin(2.0 * kPi * kVoiceFreq * static_cast<double>(i) / kFs));
    }
}
// The same, its phase an accumulator of cycles (p = frac(p + inc), the sample read before the step) in `T`: Heinzel's
// graph keeps it in double at `Normal` (a one-voice cycle, Decision 66), read as float by the sine; over N samples a
// float phase drifts from the exact one by more than the check's tolerance.
template <class T = float>
inline void accumulated_sine(float* y, std::size_t n) {
    const float inc = kVoiceFreq / kFs;
    T p = 0;
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = kVoiceAmp * std::sin(6.283185307179586f * static_cast<float>(p));
        const T s = p + static_cast<T>(inc);
        p = s - std::floor(s);
    }
}

// The bank: each of the kCh voices of `x` (voice after voice, kNc samples each) through its one-pole, summed per sample.
inline void bank_mix_ref(const float* x, float* y) {
    std::fill(y, y + kNc, 0.0f);
    for (std::size_t c = 0; c < kCh; ++c) {
        float p = 0.0f;
        for (std::size_t i = 0; i < kNc; ++i) {
            p = kBankB0 * x[c * kNc + i] + kBankFb * p;
            y[i] += p;
        }
    }
}

// One TPT state-variable low-pass, as JUCE's `processSample` writes it.
struct TptRef {
    Tpt c;
    float s1 = 0;
    float s2 = 0;

    float step(float x) {
        const float hp = (x - (c.r2 + c.g) * s1 - s2) * c.h;
        const float bp = c.g * hp + s1;
        s1 = c.g * hp + bp;
        const float lp = c.g * bp + s2;
        s2 = c.g * bp + lp;
        return lp;
    }
};
inline void svf_ref(const float* x, float* y, std::size_t n) {
    TptRef svf{kSvf};
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = svf.step(x[i]);
    }
}
// Linkwitz-Riley's 4th-order low-pass: two TPT stages in series.
inline void linkwitz_ref(const float* x, float* y, std::size_t n) {
    TptRef a{kLr};
    TptRef b{kLr};
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = b.step(a.step(x[i]));
    }
}

// The peak follower: a one-pole over |x| whose coefficient is the attack's while |x| rises above it, else the release's.
inline void envfollow_ref(const float* x, float* y, std::size_t n) {
    float env = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        const float xa = std::fabs(x[i]);
        env = xa + (xa > env ? kBal.at : kBal.rl) * (env - xa);
        y[i] = env;
    }
}

// MAI_verb's ConvolutionEngine, sample for sample, without its buffer class: four combs sharing one damping state,
// their sum through four allpasses, mixed with the input, `tanh`.
inline void maiverb_ref(const float* x, float* y, std::size_t n) {
    std::array<std::vector<float>, 4> comb;
    std::array<std::vector<float>, 4> ap;
    std::array<std::size_t, 4> ci{};
    std::array<std::size_t, 4> ai{};
    for (std::size_t k = 0; k < 4; ++k) {
        comb[k].assign(static_cast<std::size_t>(kVerbComb[k]), 0.0f);
        ap[k].assign(static_cast<std::size_t>(kVerbAllpass[k]), 0.0f);
    }
    float damped = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        float mix = 0.0f;
        for (std::size_t k = 0; k < 4; ++k) {
            const float out = comb[k][ci[k]];
            damped = out * (1.0f - kVerbDamp) + damped * kVerbDamp;
            comb[k][ci[k]] = x[i] + damped * kVerbFeedback;
            ci[k] = (ci[k] + 1) % comb[k].size();
            mix += out;
        }
        float a = mix * 0.25f;
        for (std::size_t k = 0; k < 4; ++k) {
            const float out = ap[k][ai[k]];
            ap[k][ai[k]] = a + out * kVerbAllpassFb;
            ai[k] = (ai[k] + 1) % ap[k].size();
            a = -a + out;
        }
        y[i] = std::tanh(a * kVerbWet + x[i] * (1.0f - kVerbWet));
    }
}

// The FIR taps' response at `freq`, analytically.
inline double fir_gain(double freq) {
    double re = 0.0;
    double im = 0.0;
    const double w = 2.0 * kPi * freq / kFs;
    for (std::size_t k = 0; k < kFirTaps.size(); ++k) {
        re += static_cast<double>(kFirTaps[k]) * std::cos(w * static_cast<double>(k));
        im -= static_cast<double>(kFirTaps[k]) * std::sin(w * static_cast<double>(k));
    }
    return std::sqrt(re * re + im * im);
}

// How far `y`, the FIR's valid region over `x`, is from the convolution in double: y[i] = Σ_k h[k]·x[i + K − 1 − k].
inline double fir_maxdiff(const float* y, const float* x) {
    double m = 0.0;
    for (std::size_t i = 0; i < kFirNout; ++i) {
        double acc = 0.0;
        for (std::size_t k = 0; k < kFirK; ++k) {
            acc += static_cast<double>(kFirTaps[k]) * x[i + kFirK - 1 - k];
        }
        m = std::max(m, std::fabs(static_cast<double>(y[i]) - acc));
    }
    return m;
}

} // namespace
} // namespace jucecmp
