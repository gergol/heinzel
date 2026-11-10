// The JUCE arms of the whole-picture rows biquad8 and eq8fb (bench_compare_juce, Plan/23 09 (a); eq8_common.hpp has the
// rows, their references and their bounds, bench_compare_kfr_eq8.cpp their `naive` arms): KFR's eight-section EQ as
// eight IIR::Filter (transposed direct form II) holding kEq8's numbers, read back bit for bit.
//   biquad8 — the EQ alone, as a JUCE user writes a cascade: the eight filters in a ProcessorChain, a block through it
//             (`juce`). Out of place, as the other JUCE arms: with a ProcessContextNonReplacing the chain runs its
//             first filter from x into y and the other seven in place on y, so a filter runs the whole block before the
//             next starts. `juce-replacing`: the chain in place with a ProcessContextReplacing, as a plugin's
//             processBlock runs it on the host's buffer, the block copied into y first (the harness feeds the same
//             input again). `juce-double`: the same in IIR::Filter<double>, the block converted into a double buffer
//             and back, as Heinzel's arms keep the sections' state in double. `juce-moving` and `juce-double-moving`
//             (B256): one band swept (eq8_common.hpp), the moving band's five coefficients written before every block,
//             as `heinzel-param-moving` and `naive-moving` set theirs.
//   eq8fb   — the output fed back through a clip, `u = x − 0.6·clamp(y[n−1], ±0.25)`, `y = EQ8(u)`. A filter that runs
//             a block cannot close that loop, so `juce` runs it as JUCE's delay tutorial runs its filter:
//             `processSample` in the loop, a sample at a time.
// Over the whole input and `/B256` (blocks of 256, the state carried, the same block fed again). The Heinzel arms are
// bench_compare_juce_heinzel_eq8.cpp's, the KFR target's arms beside these; this file is compiled as JUCE is used,
// without Heinzel's flags.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>
#include <juce_dsp/juce_dsp.h>

#include "eq8_common.hpp"
#include "juce_arms.hpp"

#include "page_vector.hpp"

namespace {

using namespace kfrcmp;
using heinzel_bench::OutputVector;

constexpr auto kWhole = static_cast<std::size_t>(N);

template <class T>
using Section = juce::dsp::IIR::Filter<T>;

// A section's numbers as JUCE's coefficients in `T`: a0 = 1, so JUCE's normalisation keeps their bits.
template <class T>
typename juce::dsp::IIR::Coefficients<T>::Ptr coefficients(const BiquadCoeffs& c) {
    return new juce::dsp::IIR::Coefficients<T>(c.b0, c.b1, c.b2, T{1}, c.a1, c.a2);
}

// Whether `filter` holds `c`'s numbers: its coefficients, in JUCE's order b0 b1 b2 a1 a2, read back.
template <class T>
bool holds(const Section<T>& filter, const BiquadCoeffs& c) {
    const T want[] = {c.b0, c.b1, c.b2, c.a1, c.a2};
    return std::memcmp(filter.coefficients->getRawCoefficients(), want, sizeof want) == 0;
}

// ---- biquad8: the EQ in a ProcessorChain -------------------------------------------------------------------------
template <class T>
using Eq8 = juce::dsp::ProcessorChain<Section<T>, Section<T>, Section<T>, Section<T>, Section<T>, Section<T>,
                                      Section<T>, Section<T>>;

// `fn(filter, c)` on the chain's filters in order, `c` kEq8's section of each (`get` takes its index at compile time).
template <class T, class Fn>
void each_section(Eq8<T>& eq, Fn fn) {
    [&]<std::size_t... K>(std::index_sequence<K...>) {
        (fn(eq.template get<static_cast<int>(K)>(), kEq8[K]), ...);
    }(std::make_index_sequence<8>{});
}

// kEq8 in eight filters of `T`, blocks of up to `m` samples. In double, the float block is converted into `wide`, run
// in place and converted back: JUCE has no vector conversion between the two (AudioBuffer::makeCopyOf is this loop).
template <class T>
struct ChainEq {
    Eq8<T> eq;
    OutputVector<T, 1> wide;

    explicit ChainEq(std::size_t m) : wide(std::is_same_v<T, float> ? 0 : m) {
        each_section(eq, [](Section<T>& filter, const BiquadCoeffs& c) { filter.coefficients = coefficients<T>(c); });
        eq.prepare(juce_arms::kMono);
        eq.reset();
    }

    bool holds_eq8() {
        bool all = true;
        each_section(eq, [&](const Section<T>& filter, const BiquadCoeffs& c) { all = all && holds(filter, c); });
        return all;
    }

    void process(const float* x, float* y, std::size_t m) {
        if constexpr (std::is_same_v<T, float>) {
            juce_arms::process(eq, x, y, 1, m);
        } else {
            T* d = wide.data();
            for (std::size_t i = 0; i < m; ++i) {
                d[i] = x[i];
            }
            T* channels[] = {d};
            juce::dsp::AudioBlock<T> block(channels, 1, m);
            eq.process(juce::dsp::ProcessContextReplacing<T>(block));
            for (std::size_t i = 0; i < m; ++i) {
                y[i] = static_cast<float>(d[i]);
            }
        }
    }

    void process_in_place(const float* x, float* y, std::size_t m) {
        std::memcpy(y, x, m * sizeof(float));
        float* channels[] = {y};
        juce::dsp::AudioBlock<float> block(channels, 1, m);
        eq.process(juce::dsp::ProcessContextReplacing<float>(block));
    }
};

template <class T>
constexpr const char* kArm = std::is_same_v<T, float> ? "juce" : "juce-double";

template <class T>
void bench_juce_eq8(benchmark::State& state, std::size_t m) {
    ChainEq<T> eq(m);
    if (!eq.holds_eq8()) {
        state.SkipWithError("biquad8/juce: JUCE's coefficients are not kEq8's");
        return;
    }
    time_arm<Row::biquad8>(state, m, kArm<T>, [&](const float* x, float* y, std::size_t k) { eq.process(x, y, k); });
}

void bench_juce_eq8_replacing(benchmark::State& state, std::size_t m) {
    ChainEq<float> eq(m);
    if (!eq.holds_eq8()) {
        state.SkipWithError("biquad8/juce-replacing: JUCE's coefficients are not kEq8's");
        return;
    }
    time_arm<Row::biquad8>(state, m, "juce-replacing",
                           [&](const float* x, float* y, std::size_t k) { eq.process_in_place(x, y, k); });
}

// ---- biquad8 moving: the band's coefficients written before every block -------------------------------------------
// By JUCE's assignment of an array to the band's Coefficients object: in place, no allocation (its array keeps room for
// eight), normalised by a0 = 1, so the table's bits. A plugin's usual `coefficients = makePeakFilter(…)` a block would
// also allocate a new object and work out the RBJ formulas; this row times neither (the knob math is knobs8's row). Two
// blocks in a row are checked against the sweep's first two in the filters' form, then the first block is fed again and
// again.
template <class T>
void bench_juce_moving(benchmark::State& state) {
    if (!moving_every_block()) {
        state.SkipWithError("biquad8/juce-moving: the moving band repeats the block's before");
        return;
    }
    ChainEq<T> eq(kBlock);
    if (!eq.holds_eq8()) {
        state.SkipWithError("biquad8/juce-moving: JUCE's coefficients are not kEq8's");
        return;
    }
    juce::dsp::IIR::Coefficients<T>& band = *eq.eq.template get<static_cast<int>(kMovingBand)>().coefficients;
    const auto set = [&band](const BiquadCoeffs& c) { band = std::array<T, 6>{c.b0, c.b1, c.b2, T{1}, c.a1, c.a2}; };
    const std::vector<BiquadCoeffs>& table = moving_table();
    OutputVector<float> out(2 * kBlock);
    float* y = out.data();
    for (std::size_t at = 0, j = 0; at < out.size(); at += kBlock, ++j) {
        set(table[j]);
        eq.process(g_sine_in.data() + at, y + at, kBlock);
    }
    const std::vector<double> want = moving_reference(false, out.size());
    double err = 0.0;
    for (std::size_t i = 0; i < out.size(); ++i) {
        err = std::max(err, std::fabs(static_cast<double>(y[i]) - want[i]));
    }
    if (!(err <= spec(Row::biquad8).bound)) {
        state.SkipWithError("biquad8/juce-moving: mismatch vs the double reference");
        return;
    }
    std::size_t j = 0;
    for (auto _ : state) {
        set(table[j]);
        j = j + 1 == table.size() ? 0 : j + 1;
        eq.process(g_sine_in.data(), y, kBlock);
        benchmark::DoNotOptimize(y);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(kBlock));
    state.counters["max_err"] = err;
}

// ---- eq8fb: the EQ as eight JUCE filters, the clipped output fed back to its input ---------------------------------
struct FeedbackEq {
    std::array<Section<float>, 8> eq;
    float fed = 0.0f;

    FeedbackEq() {
        for (std::size_t k = 0; k < eq.size(); ++k) {
            eq[k].coefficients = coefficients<float>(kEq8[k]);
            eq[k].prepare(juce_arms::kMono);
            eq[k].reset();
        }
    }

    bool holds_eq8() const {
        for (std::size_t k = 0; k < eq.size(); ++k) {
            if (!holds(eq[k], kEq8[k])) {
                return false;
            }
        }
        return true;
    }

    void process(const float* x, float* y, std::size_t m) {
        for (std::size_t i = 0; i < m; ++i) {
            float s = x[i] - kFbGain * std::min(std::max(fed, -kFbClip), kFbClip);
            for (Section<float>& filter : eq) {
                s = filter.processSample(s);
            }
            y[i] = fed = s;
        }
    }
};

void bench_juce_feedback(benchmark::State& state, std::size_t m) {
    FeedbackEq fb;
    if (!fb.holds_eq8()) {
        state.SkipWithError("eq8fb/juce: JUCE's coefficients are not kEq8's");
        return;
    }
    time_arm<Row::fb>(state, m, "juce", [&](const float* x, float* y, std::size_t k) { fb.process(x, y, k); });
}

BENCHMARK_CAPTURE(bench_juce_eq8<float>, whole, kWhole)->Name("biquad8/juce")->UseRealTime();
BENCHMARK_CAPTURE(bench_juce_eq8<float>, block, kBlock)->Name("biquad8/juce/B256")->UseRealTime();
BENCHMARK_CAPTURE(bench_juce_eq8_replacing, whole, kWhole)->Name("biquad8/juce-replacing")->UseRealTime();
BENCHMARK_CAPTURE(bench_juce_eq8_replacing, block, kBlock)->Name("biquad8/juce-replacing/B256")->UseRealTime();
BENCHMARK_CAPTURE(bench_juce_eq8<double>, whole, kWhole)->Name("biquad8/juce-double")->UseRealTime();
BENCHMARK_CAPTURE(bench_juce_eq8<double>, block, kBlock)->Name("biquad8/juce-double/B256")->UseRealTime();
BENCHMARK(bench_juce_moving<float>)->Name("biquad8/juce-moving/B256")->UseRealTime();
BENCHMARK(bench_juce_moving<double>)->Name("biquad8/juce-double-moving/B256")->UseRealTime();
BENCHMARK_CAPTURE(bench_juce_feedback, whole, kWhole)->Name("eq8fb/juce")->UseRealTime();
BENCHMARK_CAPTURE(bench_juce_feedback, block, kBlock)->Name("eq8fb/juce/B256")->UseRealTime();

} // namespace
