// The knob EQ (`docs/knob-math.md`): a filter whose coefficients the graph works out from its knobs, three Params a
// band (`freq`, `gain`, `q`) through the RBJ peak's formulas, as a plugin's EQ is written. One peak (`knobs1`, band 4,
// at 1 kHz), two in series (`knobs2`, bands 3 and 4) and eight (`knobs8`), band k at 62.5 · 2^k Hz, +6 dB, Q 1, at
// 48 kHz, over 1M samples run B at a time, the state carried from block to block. The knobs rest, but in the `-moving`
// arms: there every band's frequency changes before every block, so each band's knob math, which runs only in a run
// where one of its knobs changed, runs in every run, and its check with it:
//
//   knobs1/heinzel/B<n>               the peak, its knob math in the graph
//   knobs1/heinzel-moving/B<n>        the same, the knob moving: what the skip costs where it cannot help
//   knobs1/heinzel-coefficients/B<n>  the same biquad, its five coefficients Params worked out from the knobs in C++:
//                                     the floor for a knob at rest
//   knobs2/heinzel/B<n>               the two peaks, their knob math in the graph
//   knobs2/heinzel-coefficients/B<n>  the two biquads, their ten coefficients Params
//   knobs8/heinzel/B<n>               the eight peaks, their knob math in the graph
//   knobs8/heinzel-moving/B<n>        the same, every band's knob moving
//   knobs8/hand/B<n>                  the coefficients worked out in C++ when the knobs are set, then the direct form I
//                                     cascade in a plain loop: the yardstick a plugin writer writes
//
// A moving frequency sweeps exponentially over the stream, from half its rest value to twice it, a new value a block:
// the probes' sweep (a quarter to four times), narrowed so that every band stays inside its knob's range. Every
// arm is checked against the cascade in double before it is timed, its coefficients worked out in double from the
// knobs as the Executor holds them, block by block; `max_rel_err` is how far off it is, relative to the reference's
// largest magnitude. The 8-band graph outgrows the default values, nodes and statements: they are raised for this
// target (`CMakeLists.txt`).

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"

#include "bench_machine.hpp"
#include "iir_arms.hpp"
#include "page_vector.hpp"

using heinzel_bench::OutputVector;
using heinzel_bench::PageVector;

namespace hz = heinzel;

namespace {

constexpr hz::Index n{};
constexpr double kRate = 48'000;
constexpr std::int32_t kL = 1 << 20;
constexpr std::size_t kBands = 8;
constexpr std::size_t kOneBand = 4;  // `knobs1`'s band: 1 kHz
constexpr std::size_t kTwoBands = 3; // `knobs2`'s first band: 500 Hz, then 1 kHz
constexpr double kLowest = 20;       // a frequency knob's range, in Hz
constexpr double kHighest = 20000;

inline constexpr const char* kKnob[kBands][3] = {
    {"freq0", "gain0", "q0"}, {"freq1", "gain1", "q1"}, {"freq2", "gain2", "q2"}, {"freq3", "gain3", "q3"},
    {"freq4", "gain4", "q4"}, {"freq5", "gain5", "q5"}, {"freq6", "gain6", "q6"}, {"freq7", "gain7", "q7"},
};

// Band k's knobs: its frequency in Hz, its gain in dB, its Q.
constexpr std::array<double, 3> knobs_of(std::size_t k) { return {62.5 * static_cast<double>(1u << k), 6.0, 1.0}; }

// An RBJ peak over `x`, its coefficients from its three knobs (as the knob-math probe wrote it).
constexpr hz::Expr peak(hz::Expr x, const char* const* name) {
    hz::Param freq{name[0], hz::ge(kLowest), hz::le(kHighest)};
    hz::Param gain{name[1], hz::ge(-24), hz::le(24)};
    hz::Param q{name[2], hz::ge(0.1), hz::le(10)};
    const hz::Expr turns = freq * (2.0 / kRate);
    const hz::Expr c = cospi(turns);
    const hz::Expr s = sinpi(turns);
    const hz::Expr big = exp(gain * (std::numbers::ln10 / 40.0));
    const hz::Expr alpha = s / (2.0 * q);
    const hz::Expr a0 = (1.0 + alpha / big).promise(hz::gt(0));
    const hz::Expr b0 = (1.0 + alpha * big) / a0;
    const hz::Expr b1 = -2.0 * c / a0;
    const hz::Expr b2 = (1.0 - alpha * big) / a0;
    const hz::Expr a1 = -2.0 * c / a0;
    const hz::Expr a2 = (1.0 - alpha / big) / a0;
    hz::Variable y;
    y[n] = b0 * x + b1 * x[n - 1] + b2 * x[n - 2] - a1 * y[n - 1] - a2 * y[n - 2];
    return y;
}

// `Count` peaks in series from band `First`, over a block given at run time.
template <std::size_t First, std::size_t Count>
constexpr hz::Graph knob_eq() {
    hz::Input x{"x", hz::Signal{hz::Length{}}};
    hz::Expr s = x;
    for (std::size_t k = First; k < First + Count; ++k) {
        s = peak(s, kKnob[k]);
    }
    hz::Output y{"y"};
    y = s;
    return y;
}

constexpr hz::Graph one_peak() { return knob_eq<kOneBand, 1>(); }
constexpr hz::Graph two_peaks() { return knob_eq<kTwoBands, 2>(); }
constexpr hz::Graph eight_peaks() { return knob_eq<0, kBands>(); }
constexpr hz::Graph one_biquad() { return iir_arms::df1_cascade_param<1>(hz::Length{}); }
constexpr hz::Graph two_biquads() { return iir_arms::df1_cascade_param<2>(hz::Length{}); }

// The RBJ peak's coefficients in double from its knobs, as the graph works them out.
std::array<double, 5> rbj_peak(const std::array<double, 3>& knobs) {
    const auto [freq, gain, q] = knobs;
    const double w = 2.0 * std::numbers::pi * freq / kRate;
    const double big = std::pow(10.0, gain / 40.0);
    const double alpha = std::sin(w) / (2.0 * q);
    const double a0 = 1.0 + alpha / big;
    return {(1.0 + alpha * big) / a0, -2.0 * std::cos(w) / a0, (1.0 - alpha * big) / a0, -2.0 * std::cos(w) / a0, (1.0 - alpha / big) / a0};
}

// Band k's coefficients at rest, in float, as a plugin keeps them.
iir_arms::Section section_of(std::size_t k) {
    const std::array<double, 5> c = rbj_peak(knobs_of(k));
    return {static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c[2]), static_cast<float>(c[3]), static_cast<float>(c[4])};
}

// Every band's frequency, a block at a time, as the Executor holds it (a Param is a float): one block while the knobs
// rest.
using Sweep = std::vector<std::array<float, kBands>>;

Sweep at_rest() {
    Sweep s(1);
    for (std::size_t k = 0; k < kBands; ++k) {
        s[0][k] = static_cast<float>(knobs_of(k)[0]);
    }
    return s;
}

// Every band's frequency swept exponentially over the stream in blocks of `block` samples, from half its rest value to
// twice it.
Sweep swept(std::int32_t block) {
    Sweep s(static_cast<std::size_t>(kL / block));
    const auto last = static_cast<double>(s.size() - 1);
    for (std::size_t j = 0; j < s.size(); ++j) {
        const double octaves = 2.0 * static_cast<double>(j) / last - 1.0;
        const double scale = std::exp2(octaves);
        for (std::size_t k = 0; k < kBands; ++k) {
            s[j][k] = static_cast<float>(knobs_of(k)[0] * scale);
        }
    }
    return s;
}

// Does every frequency stay inside its knob's range and differ, bit for bit, from the block's before (the last block's
// for the first, as the timed passes follow each other)? Else a block would run with its knob math skipped, and a
// `-moving` row would time a knob at rest.
bool moves_every_block(const Sweep& s) {
    bool moves = true;
    for (std::size_t j = 0; j < s.size(); ++j) {
        const std::size_t before = (j + s.size() - 1) % s.size();
        for (std::size_t k = 0; k < kBands; ++k) {
            moves = moves && s[j][k] >= kLowest && s[j][k] <= kHighest;
            moves = moves && std::bit_cast<std::uint32_t>(s[j][k]) != std::bit_cast<std::uint32_t>(s[before][k]);
        }
    }
    return moves;
}

// A noise in [-1, 1), the same every run.
PageVector<float> noise() {
    PageVector<float> x(kL);
    std::uint32_t state = 1;
    for (float& v : x) {
        state = state * 1664525u + 1013904223u;
        v = static_cast<float>(state >> 8) / 8388608.0f - 1.0f;
    }
    return x;
}

// Bands `[first, first + count)` in series over `x`, in double, each block's frequencies `freq`'s: the reference.
std::vector<double> reference(const PageVector<float>& x, std::size_t first, std::size_t count, const Sweep& freq) {
    const std::size_t block = x.size() / freq.size();
    std::vector<double> y(x.begin(), x.end());
    for (std::size_t k = first; k < first + count; ++k) {
        std::array<double, 5> c{};
        double x1 = 0;
        double x2 = 0;
        double y1 = 0;
        double y2 = 0;
        for (std::size_t i = 0; i < y.size(); ++i) {
            if (i % block == 0) {
                std::array<double, 3> knobs = knobs_of(k);
                knobs[0] = freq[i / block][k];
                c = rbj_peak(knobs);
            }
            const double o = c[0] * y[i] + c[1] * x1 + c[2] * x2 - c[3] * y1 - c[4] * y2;
            x2 = x1, x1 = y[i], y2 = y1, y1 = o;
            y[i] = o;
        }
    }
    return y;
}

// The largest distance of `y` from `want`, relative to `want`'s largest magnitude.
double off_by(const float* y, const std::vector<double>& want) {
    double scale = 0;
    double worst = 0;
    for (const double w : want) {
        scale = std::max(scale, std::fabs(w));
    }
    for (std::size_t i = 0; i < want.size(); ++i) {
        worst = std::max(worst, std::fabs(static_cast<double>(y[i]) - want[i]) / scale);
    }
    return worst;
}

constexpr double kTolerance = 1e-3; // (float coefficients put the eight bands 2e-4 off the cascade in double, a hand loop's too)

// `all` checked against `want` first, then timed.
template <class Run>
void timed(benchmark::State& state, OutputVector<float>& y, const std::vector<double>& want, const Run& all) {
    all();
    const double off = off_by(y.data(), want);
    if (!(off <= kTolerance)) {
        state.SkipWithError("bench_knobs: the arm is outside its bound of the reference");
        return;
    }
    state.counters["max_rel_err"] = off;
    for (auto _ : state) {
        all();
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * kL);
}

// Sets bands `[First, First + Count)`'s knobs (the names are compile-time, hence the fold).
template <std::size_t First, std::size_t Count>
void set_knobs(auto& run) {
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        ((run.param(kKnob[First + I / 3][I % 3]) = knobs_of(First + I / 3)[I % 3]), ...);
    }(std::make_index_sequence<3 * Count>{});
}

// Sets bands `[First, First + Count)`'s frequencies to `freq`'s, as a host does when a knob moves.
template <std::size_t First, std::size_t Count>
void set_freqs(auto& run, const std::array<float, kBands>& freq) {
    [&]<std::size_t... K>(std::index_sequence<K...>) {
        ((run.param(kKnob[First + K][0]) = freq[First + K]), ...);
    }(std::make_index_sequence<Count>{});
}

// `Build`'s Executor prepared for B, its knobs or coefficients set once (`set`), over all kL samples a block at a time.
template <auto Build, std::int32_t B, class Set>
void block_heinzel(benchmark::State& state, std::size_t first, std::size_t count, const Set& set) {
    const PageVector<float> x = noise();
    OutputVector<float> y(kL);
    const auto run = std::make_unique<hz::Executor<Build, heinzel_bench::kMachine>>();
    run->prepare(B);
    set(*run);
    timed(state, y, reference(x, first, count, at_rest()), [&] {
        for (std::int32_t b = 0; b < kL; b += B) {
            run->input("x") = x.data() + b;
            run->output("y") = y.data() + b;
            run->run(B);
        }
    });
}

// The same with bands `[First, First + Count)`'s knobs set once, then every frequency changed before every block, to the
// sweep's.
template <auto Build, std::size_t First, std::size_t Count, std::int32_t B>
void block_moving(benchmark::State& state) {
    const Sweep freq = swept(B);
    if (!moves_every_block(freq)) {
        state.SkipWithError("bench_knobs: a frequency leaves its range or repeats the block's before");
        return;
    }
    const PageVector<float> x = noise();
    OutputVector<float> y(kL);
    const auto run = std::make_unique<hz::Executor<Build, heinzel_bench::kMachine>>();
    run->prepare(B);
    set_knobs<First, Count>(*run);
    timed(state, y, reference(x, First, Count, freq), [&] {
        for (std::size_t j = 0; j < freq.size(); ++j) {
            const std::int32_t b = static_cast<std::int32_t>(j) * B;
            set_freqs<First, Count>(*run, freq[j]);
            run->input("x") = x.data() + b;
            run->output("y") = y.data() + b;
            run->run(B);
        }
    });
}

template <std::int32_t B>
void knobs1_heinzel(benchmark::State& state) {
    block_heinzel<one_peak, B>(state, kOneBand, 1, [](auto& run) { set_knobs<kOneBand, 1>(run); });
}
template <std::int32_t B>
void knobs1_coefficients(benchmark::State& state) {
    const std::array<iir_arms::Section, 1> designed{section_of(kOneBand)};
    block_heinzel<one_biquad, B>(state, kOneBand, 1, [&](auto& run) { iir_arms::bind_coefficients(run, designed); });
}
template <std::int32_t B>
void knobs2_heinzel(benchmark::State& state) {
    block_heinzel<two_peaks, B>(state, kTwoBands, 2, [](auto& run) { set_knobs<kTwoBands, 2>(run); });
}
template <std::int32_t B>
void knobs2_coefficients(benchmark::State& state) {
    const std::array<iir_arms::Section, 2> designed{section_of(kTwoBands), section_of(kTwoBands + 1)};
    block_heinzel<two_biquads, B>(state, kTwoBands, 2, [&](auto& run) { iir_arms::bind_coefficients(run, designed); });
}
template <std::int32_t B>
void knobs8_heinzel(benchmark::State& state) {
    block_heinzel<eight_peaks, B>(state, 0, kBands, [](auto& run) { set_knobs<0, kBands>(run); });
}

// One section's past, as a plugin keeps it between blocks.
struct Past {
    float x1 = 0;
    float x2 = 0;
    float y1 = 0;
    float y2 = 0;
};

// The cascade over one block of `len` samples, sample by sample through every section.
[[gnu::noinline]] void cascade_block(const std::array<iir_arms::Section, kBands>& c, std::array<Past, kBands>& past, const float* x, float* y,
                                     std::int32_t len) {
    for (std::int32_t i = 0; i < len; ++i) {
        float s = x[i];
        for (std::size_t k = 0; k < kBands; ++k) {
            Past& p = past[k];
            const float o = c[k].b0 * s + c[k].b1 * p.x1 + c[k].b2 * p.x2 - c[k].a1 * p.y1 - c[k].a2 * p.y2;
            p = {s, p.x1, o, p.y1};
            s = o;
        }
        y[i] = s;
    }
}

template <std::int32_t B>
void knobs8_hand(benchmark::State& state) {
    const PageVector<float> x = noise();
    OutputVector<float> y(kL);
    std::array<iir_arms::Section, kBands> c{};
    for (std::size_t k = 0; k < kBands; ++k) {
        c[k] = section_of(k);
    }
    timed(state, y, reference(x, 0, kBands, at_rest()), [&] {
        std::array<Past, kBands> past{};
        for (std::int32_t b = 0; b < kL; b += B) {
            cascade_block(c, past, x.data() + b, y.data() + b, B);
        }
    });
}

template <std::int32_t B>
void register_blocks() {
    const std::string size = "/B" + std::to_string(B);
    using benchmark::RegisterBenchmark;
    RegisterBenchmark("knobs1/heinzel" + size, knobs1_heinzel<B>)->UseRealTime();
    RegisterBenchmark("knobs1/heinzel-moving" + size, block_moving<one_peak, kOneBand, 1, B>)->UseRealTime();
    RegisterBenchmark("knobs1/heinzel-coefficients" + size, knobs1_coefficients<B>)->UseRealTime();
    RegisterBenchmark("knobs2/heinzel" + size, knobs2_heinzel<B>)->UseRealTime();
    RegisterBenchmark("knobs2/heinzel-coefficients" + size, knobs2_coefficients<B>)->UseRealTime();
    RegisterBenchmark("knobs8/heinzel" + size, knobs8_heinzel<B>)->UseRealTime();
    RegisterBenchmark("knobs8/heinzel-moving" + size, block_moving<eight_peaks, 0, kBands, B>)->UseRealTime();
    RegisterBenchmark("knobs8/hand" + size, knobs8_hand<B>)->UseRealTime();
}

const bool g_blocks = [] {
    register_blocks<32>();
    register_blocks<64>();
    register_blocks<256>();
    return true;
}();

} // namespace

BENCHMARK_MAIN();
