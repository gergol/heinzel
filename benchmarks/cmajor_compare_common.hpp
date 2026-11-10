#pragma once

// What the Cmajor comparison (bench_compare_cmajor) shares: Cmajor's two example patches as numbers (Freeverb's tuning
// and its knobs as its ParameterScaler maps their `init` values; the Filter's TPT coefficients as Cmajor's
// std::filters::tpt::svf designs them), the test signals, and the hand references every arm is checked against, each
// written op for op as the patch writes it. The constants are literals a Heinzel graph can read; each is checked
// against the formula Cmajor computes it by before an arm runs (`designed_ok`).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "page_vector.hpp"

namespace cmajorcmp {

using heinzel_bench::PageVector;

constexpr std::size_t N = 1 << 20; // frames a run
constexpr double kFs = 48000.0;

// Freeverb.cmajor: eight combs and four allpasses a channel, the right one's 23 samples longer, the comb's input gain.
constexpr std::array<std::int32_t, 8> kComb{1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr std::array<std::int32_t, 4> kAllpass{225, 341, 441, 556};
constexpr std::int32_t kSpread = 23;
constexpr float kGain = 0.015f;
constexpr float kAllpassFb = 0.5f;

// The knobs at roomSize 80, damping 50, width 100, wetLevel 33, dryLevel 40, through ParameterScaler, and the mixer's
// two wet gains, wet·(1 + width) and wet·(1 − width).
constexpr float kFeedback = 0x1.d91686p-1f;
constexpr float kDamp = 0x1.99999ap-3f;
constexpr float kWidth = 0x1p+0f;
constexpr float kWet = 0x1.fae148p-2f;
constexpr float kDry = 0x1.99999ap-1f;
constexpr float kWet1 = 0x1.fae148p-1f;
constexpr float kWet2 = 0.0f;

// Filter.cmajor: std::filters::tpt::svf at its `init` values, 1 kHz and Q 0.707107, low-pass.
struct Svf {
    float d;
    float a;
    float g1;
};
constexpr Svf kSvf{0x1.d2bb7ep-1f, 0x1.0c774ep-4f, 0x1.7ad154p+0f};

// The knobs and the coefficients as Cmajor computes them (in float; the filter's design in double, rounded), against
// the literals above.
inline bool designed_ok() {
#pragma clang fp contract(off)
    volatile float room = 80.0f;
    volatile float damping = 50.0f;
    volatile float width = 100.0f;
    volatile float wet_level = 33.0f;
    volatile float dry_level = 40.0f;
    const float fb = room * 0.28f / 100.0f + 0.7f;
    const float damp = damping * 0.4f / 100.0f;
    const float w = width / 100.0f;
    const float wet = wet_level * 1.5f / 100.0f;
    const float dry = dry_level * 2.0f / 100.0f;
    const bool knobs = fb == kFeedback && damp == kDamp && w == kWidth && wet == kWet && dry == kDry &&
                       wet * (1.0f + w) == kWet1 && wet * (1.0f - w) == kWet2;
    volatile double fc = 1000.0;
    const double q = static_cast<double>(0.707107f);
    const double period = 0.5 / kFs;
    const double arg = 6.283185307179586 * fc * period;
    const double f = 2.0 * kFs * (std::sin(arg) / std::cos(arg)) * period;
    const double inv_q = 1.0 / q;
    const Svf s{static_cast<float>(1.0 / (1.0 + inv_q * f + f * f)), static_cast<float>(f), static_cast<float>(f + inv_q)};
    return knobs && s.d == kSvf.d && s.a == kSvf.a && s.g1 == kSvf.g1;
}

// The mono test signal, and a stereo one (interleaved), its right channel a different sine.
inline PageVector<float> make_mono(std::size_t n) {
#pragma clang fp contract(off)
    PageVector<float> x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = 0.4f * std::sin(0.001f * static_cast<float>(i));
    }
    return x;
}
inline PageVector<float> make_stereo(std::size_t n) {
#pragma clang fp contract(off)
    PageVector<float> x(2 * n);
    for (std::size_t i = 0; i < n; ++i) {
        x[2 * i] = 0.4f * std::sin(0.001f * static_cast<float>(i));
        x[2 * i + 1] = 0.3f * std::sin(0.0173f * static_cast<float>(i));
    }
    return x;
}

// The references, in an unnamed namespace: the Cmajor arms, the hand arms and Heinzel's are compiled with different
// flags (`benchmarks/CMakeLists.txt`), so each file keeps its own copy.
namespace {

inline double maxabs_diff(const float* a, const float* b, std::size_t n) {
    double m = 0;
    for (std::size_t i = 0; i < n; ++i) {
        m = std::fmax(m, std::fabs(static_cast<double>(a[i]) - b[i]));
    }
    return m;
}

// How far `y` is from the double run `exact`, at worst, relative to its peak: the arm's `max_rel_err`.
inline double peak_rel_err(const float* y, const double* exact, std::size_t n) {
    double peak = 0;
    double off = 0;
    for (std::size_t i = 0; i < n; ++i) {
        peak = std::fmax(peak, std::fabs(exact[i]));
        off = std::fmax(off, std::fabs(static_cast<double>(y[i]) - exact[i]));
    }
    return off / peak;
}

// Freeverb.cmajor's CombFilter and AllpassFilter in `T`: the float loop the arms are checked against, and the double
// one their accuracy is measured by, at the same constants. Each line in slot 1, after the arm's output (page_vector).
template <class T>
struct Comb {
    heinzel_bench::OutputVector<T, 1> buf;
    std::size_t idx = 0;
    T last = 0;

    explicit Comb(std::int32_t size) : buf(static_cast<std::size_t>(size), T(0)) {}
    T process(T in, T feedback, T damp) {
        const T delayed = buf[idx];
        last = last * damp + delayed * (T(1) - damp);
        buf[idx] = last * feedback + T(kGain) * in;
        idx = idx + 1 == buf.size() ? 0 : idx + 1;
        return delayed;
    }
};
template <class T>
struct Allpass {
    heinzel_bench::OutputVector<T, 1> buf;
    std::size_t idx = 0;

    explicit Allpass(std::int32_t size) : buf(static_cast<std::size_t>(size), T(0)) {}
    T process(T in) {
        const T delayed = buf[idx];
        buf[idx] = in + delayed * T(kAllpassFb);
        idx = idx + 1 == buf.size() ? 0 : idx + 1;
        return delayed - in;
    }
};

// One channel of Freeverb.cmajor's MonoReverb: the combs on x, summed, through the allpasses, shortest first.
template <class T>
struct Room {
    std::vector<Comb<T>> combs;
    std::vector<Allpass<T>> allpasses;

    explicit Room(std::int32_t spread) {
        for (std::int32_t s : kComb) {
            combs.emplace_back(s + spread);
        }
        for (std::int32_t s : kAllpass) {
            allpasses.emplace_back(s + spread);
        }
    }
    T process(T x, T feedback, T damp) {
        T acc = 0;
        for (auto& c : combs) {
            acc += c.process(x, feedback, damp);
        }
        for (auto& a : allpasses) {
            acc = a.process(acc);
        }
        return acc;
    }
};

// The patch: both rooms, then WetDryMixer, `wet + dry·x`; `y` stereo, interleaved.
template <class T>
void freeverb_cmajor(const float* x, T* y, std::size_t n) {
#pragma clang fp contract(off)
    Room<T> left(0);
    Room<T> right(kSpread);
    for (std::size_t i = 0; i < n; ++i) {
        const T in = x[i];
        const T l = left.process(in, T(kFeedback), T(kDamp));
        const T r = right.process(in, T(kFeedback), T(kDamp));
        y[2 * i] = l * T(kWet1) + r * T(kWet2) + T(kDry) * in;
        y[2 * i + 1] = r * T(kWet1) + l * T(kWet2) + T(kDry) * in;
    }
}

// std::filters::tpt::svf's processMultimode, its low-pass out, on each channel of interleaved `x`.
template <class T>
void svf_cmajor(const float* x, T* y, std::size_t n) {
#pragma clang fp contract(off)
    const T d = kSvf.d;
    const T a = kSvf.a;
    const T g1 = kSvf.g1;
    for (std::size_t c = 0; c < 2; ++c) {
        T z0 = 0;
        T z1 = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const T high = (T(x[2 * i + c]) - g1 * z0 - z1) * d;
            const T band = a * high + z0;
            const T low = a * band + z1;
            z0 = a * high + band;
            z1 = a * band + low;
            y[2 * i + c] = low;
        }
    }
}

// Knobs that move, as a host moves them: a block of kBlock frames, the knobs sent at its start, set `b % 2` of each
// table in block b. Every knob changes at every block: an event that repeats its value restarts Cmajor's ramp, which
// the Heinzel arms, seeing values only, cannot tell from no event. 512 is Cmajor's maxFramesPerBlock and a multiple
// of 32, so the Filter's coefficients change exactly at a block's start, as Heinzel's knob math does.
constexpr std::size_t kBlock = 512;
// roomSize, damping, width, wetLevel, dryLevel.
constexpr std::array<std::array<float, 5>, 2> kFreeverbMoves{{{80.0f, 50.0f, 100.0f, 33.0f, 40.0f},
                                                              {55.0f, 30.0f, 70.0f, 45.0f, 25.0f}}};
// frequency, q.
constexpr std::array<std::array<float, 2>, 2> kFilterMoves{{{1000.0f, 0.707107f}, {2500.0f, 1.5f}}};

// std::smoothing::SmoothedValue as Freeverb.cmajor's SmoothedValueStream (0.02 s at 48 kHz) runs it: a new target
// ramps from where the last ramp got to, in 960 equal steps.
template <class T>
struct Smoother {
    T value = 0;
    T increment = 0;
    std::int32_t remaining = 0;

    T current() const { return value - increment * T(remaining); }
    void set(T target) {
        increment = (target - current()) / T(960);
        remaining = 960;
        value = target;
    }
    T next() {
        if (remaining > 0) {
            --remaining;
            return current();
        }
        return value;
    }
};

// Freeverb.cmajor with its knobs moving: ParameterScaler (in float, as Cmajor) on each block's knobs, the five smoothers
// a frame, the rooms and the mixer on the smoothed values.
template <class T>
void freeverb_cmajor_moving(const float* x, T* y, std::size_t n) {
#pragma clang fp contract(off)
    Room<T> left(0);
    Room<T> right(kSpread);
    std::array<Smoother<T>, 5> knob{};
    for (std::size_t i = 0; i < n; ++i) {
        if (i % kBlock == 0) {
            const auto& k = kFreeverbMoves[i / kBlock % 2];
            knob[0].set(k[0] * 0.28f / 100.0f + 0.7f);
            knob[1].set(k[1] * 0.4f / 100.0f);
            knob[2].set(k[2] / 100.0f);
            knob[3].set(k[3] * 1.5f / 100.0f);
            knob[4].set(k[4] * 2.0f / 100.0f);
        }
        const T feedback = knob[0].next();
        const T damp = knob[1].next();
        const T width = knob[2].next();
        const T wet = knob[3].next();
        const T dry = knob[4].next();
        const T in = x[i];
        const T l = left.process(in, feedback, damp);
        const T r = right.process(in, feedback, damp);
        const T wet1 = wet * (T(1) + width);
        const T wet2 = wet * (T(1) - width);
        y[2 * i] = l * wet1 + r * wet2 + dry * in;
        y[2 * i + 1] = r * wet1 + l * wet2 + dry * in;
    }
}

// std::filters::tpt::svf's setFrequency, in double, rounded to float, as Cmajor designs it.
inline Svf svf_design(float frequency, float q) {
#pragma clang fp contract(off)
    const double fc = std::clamp(static_cast<double>(frequency), 0.0, kFs * 0.48);
    const double period = 0.5 / kFs;
    const double f = 2.0 * kFs * std::tan(6.283185307179586 * fc * period) * period;
    const double inv_q = 1.0 / static_cast<double>(q);
    return {static_cast<float>(1.0 / (1.0 + inv_q * f + f * f)), static_cast<float>(f), static_cast<float>(f + inv_q)};
}

// Filter.cmajor with its knobs moving: each block's coefficients designed at its start, the state carried on.
template <class T>
void svf_cmajor_moving(const float* x, T* y, std::size_t n) {
#pragma clang fp contract(off)
    std::array<T, 2> z0{};
    std::array<T, 2> z1{};
    Svf c{};
    for (std::size_t i = 0; i < n; ++i) {
        if (i % kBlock == 0) {
            const auto& k = kFilterMoves[i / kBlock % 2];
            c = svf_design(k[0], k[1]);
        }
        for (std::size_t ch = 0; ch < 2; ++ch) {
            const T high = (T(x[2 * i + ch]) - T(c.g1) * z0[ch] - z1[ch]) * T(c.d);
            const T band = T(c.a) * high + z0[ch];
            const T low = T(c.a) * band + z1[ch];
            z0[ch] = T(c.a) * high + band;
            z1[ch] = T(c.a) * band + low;
            y[2 * i + ch] = low;
        }
    }
}

inline void freeverb_moving_ref(const float* x, float* y, std::size_t n) { freeverb_cmajor_moving<float>(x, y, n); }
inline void svf_moving_ref(const float* x, float* y, std::size_t n) { svf_cmajor_moving<float>(x, y, n); }

inline void freeverb_cmajor_ref(const float* x, float* y, std::size_t n) { freeverb_cmajor<float>(x, y, n); }
inline void svf_cmajor_ref(const float* x, float* y, std::size_t n) { svf_cmajor<float>(x, y, n); }

// The double run of a reference over the bench's input (`in_channels` 1: make_mono, 2: make_stereo), made once.
template <void (*Exact)(const float*, double*, std::size_t), std::size_t InChannels>
const std::vector<double>& exact_run() {
    static const std::vector<double> y = [] {
        const PageVector<float> x = InChannels == 1 ? make_mono(N) : make_stereo(N);
        std::vector<double> out(2 * N);
        Exact(x.data(), out.data(), N);
        return out;
    }();
    return y;
}
inline const std::vector<double>& freeverb_exact() { return exact_run<freeverb_cmajor<double>, 1>(); }
inline const std::vector<double>& svf_exact() { return exact_run<svf_cmajor<double>, 2>(); }
inline const std::vector<double>& freeverb_moving_exact() { return exact_run<freeverb_cmajor_moving<double>, 1>(); }
inline const std::vector<double>& svf_moving_exact() { return exact_run<svf_cmajor_moving<double>, 2>(); }

} // namespace

} // namespace cmajorcmp
