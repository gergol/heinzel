#pragma once

// Freeverb (Schroeder-Moorer), mono: main's tuning (heinzel::dsp::coeffs/freeverb.hpp) and the hand loop the reverb
// rows of the KFR and FAUST comparisons are timed and checked by. No library here. The code is in an unnamed
// namespace: the hand arms and Heinzel's are compiled with different flags (`benchmarks/CMakeLists.txt`), so each
// file keeps its own copy.

#include <array>
#include <cstddef>
#include <vector>
#include "page_vector.hpp"

namespace freeverb_ref {

using heinzel_bench::OutputVector;

inline constexpr std::array<int, 8> kComb{1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
inline constexpr std::array<int, 4> kAllpass{556, 441, 341, 225};
inline constexpr float kFixedGain = 0.015f;
inline constexpr float kAllpassFB = 0.5f;

// The same, as one value (`heinzel_arms::freeverb` reads it so).
struct Tuning {
    std::array<int, 8> comb;
    std::array<int, 4> allpass;
    float gain;
    float allpass_feedback;
};
inline constexpr Tuning kTuning{kComb, kAllpass, kFixedGain, kAllpassFB};

namespace {

// Every ring in slot 1, after the arm's output: each wraps every `size` samples, so its offset against the other
// buffers runs through the page over a run (Plan/30), and a slot of its own would only move where it starts.
struct CombRef {
    OutputVector<float, 1> buf;
    int idx = 0;
    float lp = 0.0f;
    float fb;
    float damp;

    CombRef(int size, float f, float d) : buf(static_cast<std::size_t>(size), 0.0f), fb(f), damp(d) {}

    float process(float in) {
        const float o = buf[static_cast<std::size_t>(idx)];
        lp = o * (1.0f - damp) + lp * damp;
        buf[static_cast<std::size_t>(idx)] = in + lp * fb;
        if (++idx >= static_cast<int>(buf.size())) {
            idx = 0;
        }
        return o;
    }
};

struct AllpassRef {
    OutputVector<float, 1> buf;
    int idx = 0;
    float fb;

    AllpassRef(int size, float f) : buf(static_cast<std::size_t>(size), 0.0f), fb(f) {}

    float process(float in) {
        const float bo = buf[static_cast<std::size_t>(idx)];
        buf[static_cast<std::size_t>(idx)] = in + bo * fb;
        if (++idx >= static_cast<int>(buf.size())) {
            idx = 0;
        }
        return -in + bo;
    }
};

// The room: eight combs (`feedback`, `damp`) in parallel on the input times the gain, four allpasses in series.
inline void run(const float* x, float* y, std::size_t n, float feedback, float damp, float wet, float dry) {
    std::vector<CombRef> combs;
    for (int s : kComb) {
        combs.emplace_back(s, feedback, damp);
    }
    std::vector<AllpassRef> aps;
    for (int s : kAllpass) {
        aps.emplace_back(s, kAllpassFB);
    }
    for (std::size_t i = 0; i < n; ++i) {
        const float in = x[i] * kFixedGain;
        float acc = combs[0].process(in);
        for (std::size_t k = 1; k < combs.size(); ++k) {
            acc += combs[k].process(in);
        }
        float yy = acc;
        for (auto& ap : aps) {
            yy = ap.process(yy);
        }
        y[i] = yy * wet + x[i] * dry;
    }
}

} // namespace
} // namespace freeverb_ref
