// The many-channel rows of the JUCE comparison (bench_compare_juce, Plan/23 09 (b)): biquad3's EQ as JUCE designs it
// (`kJuceEq`'s spec) on 2, 8 and 16 channels, blocks of 256 frames, planar and interleaved; channels_common.hpp has the
// rows, their layouts and the check (against the direct-form-I cascade over JUCE's own coefficients). This file:
//   juce      — a ProcessorChain of three ProcessorDuplicator<IIR::Filter<float>, IIR::Coefficients<float>>, JUCE's
//               multichannel idiom: a filter a channel, one channel after the other.
//   juce-simd — JUCE's own channels-in-lanes demo (examples/DSP/SIMDRegisterDemo.h): three
//               IIR::Filter<SIMDRegister<float>> a group of eight channels (a register under AVX), a group short of eight
//               padded with silent channels, its block interleaved into the registers and back with AudioData, as the
//               demo does. Eight interleaved channels already are its layout: that block runs where it lies, its
//               buffers aligned for the register, as the demo's are.
// Interleaved data goes through AudioData::deinterleaveSamples and interleaveSamples inside the timed run. The Heinzel
// arm is bench_compare_juce_heinzel_channels.cpp's; this file is compiled as JUCE is used, without Heinzel's flags.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <benchmark/benchmark.h>
#include <juce_dsp/juce_dsp.h>

#include "channels_common.hpp"
#include "juce_arms.hpp"
#include "juce_compare_common.hpp"
#include "juce_compare_refs.hpp"

#include "page_vector.hpp"

using heinzel_bench::PageVector;
using heinzel_bench::OutputVector;

namespace {

using namespace jucecmp;
using channels::kFrames;
using channels::kMaxChannels;
using channels::Layout;

using Format = juce::AudioData::Format<juce::AudioData::Float32, juce::AudioData::NativeEndian>;
using Lanes = juce::dsp::SIMDRegister<float>;
constexpr std::size_t kLanes = Lanes::size();

// JUCE's own designs of the EQ's spec, as bench_compare_juce.cpp's `design` makes them.
std::array<juce::dsp::IIR::Coefficients<float>::Ptr, 3> sections() {
    using C = juce::dsp::IIR::Coefficients<float>;
    return {C::makeHighPass(kFs, kHpHz, kHpQ), C::makePeakFilter(kFs, kPkHz, kPkQ, std::pow(10.0f, kPkDb / 20.0f)),
            C::makeLowPass(kFs, kLpHz, kLpQ)};
}

// A channel's two blocks through the direct-form-I cascade over JUCE's coefficients: the reference.
void eq_reference(const float* x, float* y, std::size_t n) { eq_ref(juce_eq(), x, y, n); }

// `C` interleaved channels into planar ones (channel c at planar + c·kFrames), and back, with AudioData.
void deinterleave(const float* x, std::size_t C, float* planar) {
    std::array<float*, kMaxChannels> to{};
    for (std::size_t c = 0; c < C; ++c) {
        to[c] = planar + c * kFrames;
    }
    juce::AudioData::deinterleaveSamples(juce::AudioData::InterleavedSource<Format>{x, static_cast<int>(C)},
                                         juce::AudioData::NonInterleavedDest<Format>{to.data(), static_cast<int>(C)},
                                         static_cast<int>(kFrames));
}
void interleave(const float* planar, std::size_t C, float* x) {
    std::array<const float*, kMaxChannels> from{};
    for (std::size_t c = 0; c < C; ++c) {
        from[c] = planar + c * kFrames;
    }
    juce::AudioData::interleaveSamples(juce::AudioData::NonInterleavedSource<Format>{from.data(), static_cast<int>(C)},
                                       juce::AudioData::InterleavedDest<Format>{x, static_cast<int>(C)},
                                       static_cast<int>(kFrames));
}

// ---- juce: a filter a channel ----------------------------------------------------------------------------------------
using Duplicated = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;

void bench_juce(benchmark::State& state, Layout layout, std::size_t C) {
    juce::dsp::ProcessorChain<Duplicated, Duplicated, Duplicated> eq;
    const auto designed = sections();
    eq.get<0>().state = designed[0];
    eq.get<1>().state = designed[1];
    eq.get<2>().state = designed[2];
    eq.prepare({kFs, static_cast<std::uint32_t>(kFrames), static_cast<std::uint32_t>(C)});
    eq.reset();
    const PageVector<float> xi = channels::interleaved(g_in.data(), C);
    const std::vector<float> ref = channels::reference(g_in.data(), C, eq_reference);
    OutputVector<float> y(C * kFrames);
    OutputVector<float, 1> in(C * kFrames);
    OutputVector<float, 2> out(C * kFrames);
    channels::time_arm(state, layout, C, ref, y.data(), [&](std::size_t b) {
        if (layout == Layout::planar) {
            juce_arms::process(eq, g_in.data() + b * kFrames, y.data(), C, kFrames);
            return;
        }
        deinterleave(xi.data() + b * kFrames * C, C, in.data());
        juce_arms::process(eq, in.data(), out.data(), C, kFrames);
        interleave(out.data(), C, y.data());
    });
}

// ---- juce-simd: eight channels in the lanes of a register --------------------------------------------------------
using LaneFilter = juce::dsp::IIR::Filter<Lanes>;
using LaneEq = juce::dsp::ProcessorChain<LaneFilter, LaneFilter, LaneFilter>;

// One group: the demo's filters and its block of registers.
struct LaneGroup {
    LaneEq eq;
    OutputVector<Lanes, 2> block = OutputVector<Lanes, 2>(kFrames); // after bench_juce_simd's y and in, before its out

    LaneGroup() {
        const auto designed = sections();
        eq.get<0>().coefficients = designed[0];
        eq.get<1>().coefficients = designed[1];
        eq.get<2>().coefficients = designed[2];
        eq.prepare({kFs, static_cast<std::uint32_t>(kFrames), 1});
        eq.reset();
    }

    // `count` planar channels (channel j at in + j·kFrames) interleaved into the registers, the lanes past `count`
    // silent, filtered in place, and back into `out`.
    void process(const float* in, float* out, std::size_t count) {
        std::array<const float*, kLanes> from{};
        std::array<float*, kLanes> to{};
        for (std::size_t j = 0; j < count; ++j) {
            from[j] = in + j * kFrames;
            to[j] = out + j * kFrames;
        }
        float* lanes = reinterpret_cast<float*>(block.data());
        const int k = static_cast<int>(count);
        const int width = static_cast<int>(kLanes);
        const int n = static_cast<int>(kFrames);
        juce::AudioData::interleaveSamples(juce::AudioData::NonInterleavedSource<Format>{from.data(), k},
                                           juce::AudioData::InterleavedDest<Format>{lanes, width}, n);
        Lanes* channel[] = {block.data()};
        juce::dsp::AudioBlock<Lanes> registers(channel, 1, kFrames);
        eq.process(juce::dsp::ProcessContextReplacing<Lanes>(registers));
        juce::AudioData::deinterleaveSamples(juce::AudioData::InterleavedSource<Format>{lanes, width},
                                             juce::AudioData::NonInterleavedDest<Format>{to.data(), k}, n);
    }

    // kLanes interleaved channels: a register a frame already, filtered from `x` into `y`.
    void process_interleaved(const float* x, float* y) {
        const Lanes* from[] = {reinterpret_cast<const Lanes*>(x)};
        Lanes* to[] = {reinterpret_cast<Lanes*>(y)};
        const juce::dsp::AudioBlock<const Lanes> in(from, 1, kFrames);
        juce::dsp::AudioBlock<Lanes> out(to, 1, kFrames);
        eq.process(juce::dsp::ProcessContextNonReplacing<Lanes>(in, out));
    }
};

void bench_juce_simd(benchmark::State& state, Layout layout, std::size_t C) {
    std::vector<LaneGroup> groups((C + kLanes - 1) / kLanes);
    const PageVector<float> xi = channels::interleaved(g_in.data(), C);
    const std::vector<float> ref = channels::reference(g_in.data(), C, eq_reference);
    // The interleaved input and the output aligned for the registers (the demo's `toBasePointer` view of them).
    PageVector<Lanes> x_lanes(xi.size() / kLanes);
    OutputVector<Lanes> y_lanes(C * kFrames / kLanes);
    std::memcpy(x_lanes.data(), xi.data(), xi.size() * sizeof(float));
    const float* x = reinterpret_cast<const float*>(x_lanes.data());
    float* y = reinterpret_cast<float*>(y_lanes.data());
    OutputVector<float, 1> in(C * kFrames);
    OutputVector<float, 3> out(C * kFrames);
    // A block of planar channels, group after group.
    const auto planar = [&](const float* from, float* to) {
        for (std::size_t g = 0; g < groups.size(); ++g) {
            const std::size_t first = g * kLanes;
            groups[g].process(from + first * kFrames, to + first * kFrames, std::min(kLanes, C - first));
        }
    };
    channels::time_arm(state, layout, C, ref, y, [&](std::size_t b) {
        if (layout == Layout::planar) {
            planar(g_in.data() + b * kFrames, y);
        } else if (C == kLanes) {
            groups[0].process_interleaved(x + b * kFrames * C, y);
        } else {
            deinterleave(x + b * kFrames * C, C, in.data());
            planar(in.data(), out.data());
            interleave(out.data(), C, y);
        }
    });
}

const bool g_registered = [] {
    for (const Layout layout : {Layout::planar, Layout::inter}) {
        for (const std::size_t c : channels::kCounts) {
            using benchmark::RegisterBenchmark;
            RegisterBenchmark(channels::name_of(layout, "juce", c), bench_juce, layout, c)->UseRealTime();
            RegisterBenchmark(channels::name_of(layout, "juce-simd", c), bench_juce_simd, layout, c)->UseRealTime();
        }
    }
    return true;
}();

} // namespace
