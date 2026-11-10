#pragma once

// What the JUCE arms of the comparison share (bench_compare_juce*.cpp): one block through a juce::dsp processor, out of
// place, as a host hands a plugin its block; and the way an arm is checked and timed. Only JUCE's files include it.

#include <array>
#include <cstddef>
#include <cstdint>

#include <benchmark/benchmark.h>
#include <juce_dsp/juce_dsp.h>

#include "juce_compare_common.hpp"

namespace juce_arms {

// One block for a mono processor, and the largest a bank's processors are prepared for.
inline const juce::dsp::ProcessSpec kMono{jucecmp::kFs, static_cast<std::uint32_t>(jucecmp::N), 1};
inline const juce::dsp::ProcessSpec kBank{jucecmp::kFs, static_cast<std::uint32_t>(jucecmp::kNc), jucecmp::kCh};

// `proc` over `channels` channels of `samples` samples, each channel a slice of `in` and of `out`, one after another.
template <class Proc>
void process(Proc& proc, const float* in, float* out, std::size_t channels, std::size_t samples) {
    std::array<const float*, jucecmp::kCh> ins{};
    std::array<float*, jucecmp::kCh> outs{};
    for (std::size_t c = 0; c < channels; ++c) {
        ins[c] = in + c * samples;
        outs[c] = out + c * samples;
    }
    const juce::dsp::AudioBlock<const float> from(ins.data(), channels, samples);
    juce::dsp::AudioBlock<float> to(outs.data(), channels, samples);
    proc.process(juce::dsp::ProcessContextNonReplacing<float>(from, to));
}

// One arm: `run` (a block through the library, into `out`) once, checked (`check` names what differs, or returns
// nullptr), then timed run after run, the library's state carried from block to block; `items` samples a run.
template <class Run, class Check>
void timed(benchmark::State& state, std::int64_t items, const float* out, Run run, Check check) {
    run();
    if (const char* why = check()) {
        state.SkipWithError(why);
        return;
    }
    for (auto _ : state) {
        run();
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * items);
}

} // namespace juce_arms
