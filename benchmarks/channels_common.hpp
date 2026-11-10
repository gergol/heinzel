#pragma once

// The many-channel rows of the KFR and JUCE comparisons (Plan/23 09 (b)), `biquad3ch/{planar,inter}/<arm>/C{2,8,16}`:
// each target's own three-section EQ (KFR's `kEq`, JUCE's `kJuceEq`) on C channels, blocks of kFrames frames, the state
// carried, the same block fed again. In block b channel c reads the target's input from (c + b)·kFrames on, so a
// swapped channel fails the check. Two layouts:
//   planar — a pointer a channel (VST3, AU, CLAP, juce::AudioBuffer): block b's channel c at input + (b + c)·kFrames,
//            the output's channel c at y + c·kFrames.
//   inter  — frame-major (drivers, files): x[t·C + c], interleaved once, before the timed runs; the output alike.
// An arm converts the layout it does not take inside its timed run. Every channel of two blocks in a row is checked
// against the target's textbook DF-I cascade (2e-4, `time_arm`) before block 0 is timed again and again; items are
// channel-samples. Library-free, read by both targets' library and Heinzel files (compiled with other flags), so its
// code is in an unnamed namespace, as the refs headers'.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>
#include "page_vector.hpp"

namespace channels {

using heinzel_bench::PageVector;

constexpr std::size_t kFrames = 256;
constexpr std::size_t kMaxChannels = 16;
constexpr std::array<std::size_t, 3> kCounts{2, 8, 16};

enum class Layout { planar, inter };

namespace {

inline std::string name_of(Layout layout, const char* arm, std::size_t channels) {
    const char* layout_name = layout == Layout::planar ? "planar/" : "inter/";
    return std::string("biquad3ch/") + layout_name + arm + "/C" + std::to_string(channels);
}

// A block of planar channels (channel c at planar + c·kFrames) frame-major into x (x[t·C + c]), and back: the plain
// loops of an arm that converts.
inline void to_inter(const float* planar, std::size_t channels, float* x) {
    for (std::size_t t = 0; t < kFrames; ++t) {
        for (std::size_t c = 0; c < channels; ++c) {
            x[t * channels + c] = planar[c * kFrames + t];
        }
    }
}
inline void to_planar(const float* x, std::size_t channels, float* planar) {
    for (std::size_t c = 0; c < channels; ++c) {
        for (std::size_t t = 0; t < kFrames; ++t) {
            planar[c * kFrames + t] = x[t * channels + c];
        }
    }
}

// Both blocks of the `inter` input, block b at b·kFrames·C.
inline PageVector<float> interleaved(const float* input, std::size_t channels) {
    PageVector<float> x(2 * kFrames * channels);
    for (std::size_t b = 0; b < 2; ++b) {
        to_inter(input + b * kFrames, channels, x.data() + b * kFrames * channels);
    }
    return x;
}

// Every channel's two blocks through `cascade(x, y, n)`, the target's textbook cascade from silence: channel c's at
// 2·kFrames·c.
template <class Cascade>
std::vector<float> reference(const float* input, std::size_t channels, Cascade cascade) {
    std::vector<float> ref(2 * kFrames * channels);
    for (std::size_t c = 0; c < channels; ++c) {
        cascade(input + c * kFrames, ref.data() + 2 * kFrames * c, 2 * kFrames);
    }
    return ref;
}

// How far block b of `y`, laid out as `layout`, is from every channel's reference (NaN, if any sample is).
inline double max_error(const float* y, Layout layout, std::size_t channels, std::size_t b, const std::vector<float>& ref) {
    double worst = 0.0;
    for (std::size_t c = 0; c < channels; ++c) {
        for (std::size_t t = 0; t < kFrames; ++t) {
            const float got = layout == Layout::planar ? y[c * kFrames + t] : y[t * channels + c];
            const double e = std::fabs(static_cast<double>(got) - ref[(2 * c + b) * kFrames + t]);
            worst = e > worst || std::isnan(e) ? e : worst;
        }
    }
    return worst;
}

// One arm: `run(b)` takes block b through the EQ into `y`, the state carried. Both blocks checked within 2e-4 (about
// three times the worst arm measured, juce-simd's 6.8e-5; 2026-10-09, zen2), then block 0 timed again and again; the
// counter `max_err` reports the error.
template <class Run>
void time_arm(benchmark::State& state, Layout layout, std::size_t channels, const std::vector<float>& ref, const float* y,
              Run run) {
    double err = 0.0;
    for (std::size_t b = 0; b < 2; ++b) {
        run(b);
        const double e = max_error(y, layout, channels, b, ref);
        err = e > err || std::isnan(e) ? e : err;
    }
    if (!(err <= 2e-4)) {
        state.SkipWithError("biquad3ch: a channel differs from the DF-I cascade");
        return;
    }
    state.counters["max_err"] = err;
    for (auto _ : state) {
        run(0);
        benchmark::DoNotOptimize(y);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(channels * kFrames));
}

} // namespace
} // namespace channels
