// The JUCE comparison's recurrent rows (bench_compare_juce, one target with bench_compare_juce.cpp, whose header has
// the whole picture): JUCE's arms, and the hand arm of the reverb.
//   ladder    — LadderFilter (LPF12, 1 kHz, resonance 0.7, drive 1.2): five states a sample, its `tanh` a 128-point
//               table, so it is checked against the exact-`tanh` loop with main's 2e-2.
//   ladder16  — one LadderFilter over 16 channels: JUCE steps every channel each sample, its state per channel.
//   voice     — the tutorial's ProcessorChain: Oscillator (`std::sin`) → LadderFilter → Gain.
//   svf       — StateVariableTPTFilter, a low-pass at 1 kHz, Q 0.707: two states, no transcendental.
//   svf16     — the same over 16 channels, one channel after the other.
//   linkwitz  — LinkwitzRileyFilter, a 4th-order low-pass at 2 kHz: two TPT stages.
//   envfollow — BallisticsFilter as a peak follower (1 ms attack, 100 ms release): its coefficient picked each sample.
//   bankmix16 — 16 one-poles (a ProcessorDuplicator of IIR::Filter, a state per channel), summed to mono by hand.
//   maiverb   — `hand`: MAI_verb's ConvolutionEngine, vendored as main has it (it holds a juce::AudioBuffer, so it is
//               here), processing its buffer in place: the input is copied in each run.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>
#include <juce_dsp/juce_dsp.h>

#include "juce_arms.hpp"
#include "juce_compare_common.hpp"
#include "juce_compare_refs.hpp"

#include "page_vector.hpp"

using heinzel_bench::PageVector;
using heinzel_bench::OutputVector;

namespace {

using namespace jucecmp;
using juce_arms::kBank;
using juce_arms::kMono;
using juce_arms::process;
using juce_arms::timed;

// One processor over the input, mono (`channels` 1, N samples) or as the bank (kCh channels of kNc), each channel
// checked against `reference` over its own slice of the input with `tol`.
template <class Proc>
void bench_filter(benchmark::State& state, Proc& proc, std::size_t channels, void (*reference)(const float*, float*, std::size_t),
                  double tol, const char* what) {
    const std::size_t samples = static_cast<std::size_t>(N) / channels;
    OutputVector<float> y(static_cast<std::size_t>(N));
    std::vector<float> ref(y.size());
    for (std::size_t c = 0; c < channels; ++c) {
        reference(g_in.data() + c * samples, ref.data() + c * samples, samples);
    }
    timed(
        state, N, y.data(), [&] { process(proc, g_in.data(), y.data(), channels, samples); },
        [&]() -> const char* { return maxabs_diff(y.data(), ref.data(), y.size()) <= tol ? nullptr : what; });
}

// ---- the ladder: the tutorial's setting; reset() snaps the smoothed cutoff and resonance to it --------------------
void set_ladder(juce::dsp::LadderFilter<float>& ladder) {
    ladder.setMode(juce::dsp::LadderFilterMode::LPF12);
    ladder.setCutoffFrequencyHz(kLadCut);
    ladder.setResonance(kLadRes);
    ladder.setDrive(kLadDrive);
    ladder.reset();
}
void exact_ladder(const float* x, float* y, std::size_t n) { ladder_ref(tanh_exact, x, y, n); }

void bench_juce_ladder(benchmark::State& state, std::size_t channels) {
    juce::dsp::LadderFilter<float> ladder;
    ladder.prepare(channels == 1 ? kMono : kBank);
    set_ladder(ladder);
    bench_filter(state, ladder, channels, exact_ladder, 2e-2, "ladder/juce: mismatch vs the exact-tanh ladder");
}

// ---- the voice: the oscillator at 110 Hz, the ladder, the gain ------------------------------------------------------
void bench_juce_voice(benchmark::State& state) {
    juce::dsp::ProcessorChain<juce::dsp::Oscillator<float>, juce::dsp::LadderFilter<float>, juce::dsp::Gain<float>> chain;
    chain.prepare(kMono); // the sample rate before the parameters
    // Oscillator calls its generator on (phase − π), so a plain sine would be −sin: negated, the voice starts as the
    // reference does.
    chain.get<0>().initialise([](float x) { return -kVoiceAmp * std::sin(x); });
    chain.get<0>().setFrequency(kVoiceFreq, true);
    set_ladder(chain.get<1>());
    chain.get<2>().setGainLinear(kVoiceGain);
    chain.reset();
    OutputVector<float> y(static_cast<std::size_t>(N));
    std::vector<float> osc(y.size());
    std::vector<float> ref(y.size());
    exact_sine(osc.data(), osc.size());
    voice_ref(osc.data(), ref.data(), ref.size());
    timed(
        state, N, y.data(),
        [&] {
            std::fill(y.begin(), y.end(), 0.0f); // Oscillator adds to its block
            float* channels[] = {y.data()};
            juce::dsp::AudioBlock<float> block(channels, 1, y.size());
            chain.process(juce::dsp::ProcessContextReplacing<float>(block));
        },
        // main's bound: JUCE's table `tanh` and its float phase, against the exact ones.
        [&]() -> const char* { return maxabs_diff(y.data(), ref.data(), y.size()) <= 0.05 ? nullptr : "voice/juce: mismatch vs the voice loop"; });
}

// ---- the TPT filters and the follower ----------------------------------------------------------------------------
void bench_juce_svf(benchmark::State& state, std::size_t channels) {
    juce::dsp::StateVariableTPTFilter<float> svf;
    svf.prepare(channels == 1 ? kMono : kBank);
    svf.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    svf.setCutoffFrequency(kSvfCut);
    svf.setResonance(kSvfRes);
    svf.reset();
    bench_filter(state, svf, channels, svf_ref, 1e-3, "svf/juce: mismatch vs the TPT loop");
}

void bench_juce_linkwitz(benchmark::State& state) {
    juce::dsp::LinkwitzRileyFilter<float> lr;
    lr.prepare(kMono);
    lr.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
    lr.setCutoffFrequency(kLrCut);
    lr.reset();
    bench_filter(state, lr, 1, linkwitz_ref, 1e-3, "linkwitz/juce: mismatch vs the two TPT stages");
}

void bench_juce_envfollow(benchmark::State& state) {
    juce::dsp::BallisticsFilter<float> env;
    env.prepare(kMono);
    env.setLevelCalculationType(juce::dsp::BallisticsFilterLevelCalculationType::peak);
    env.setAttackTime(kBalAtkMs);
    env.setReleaseTime(kBalRelMs);
    env.reset();
    // A sample where |x| and the envelope nearly meet may take the other branch, so main's bound is looser here.
    bench_filter(state, env, 1, envfollow_ref, 2e-3, "envfollow/juce: mismatch vs the peak follower");
}

// ---- the bank mixed to mono: one IIR::Filter per channel (y = b0·x + fb·y[n-1]), then the channels summed -----------
void bench_juce_bankmix(benchmark::State& state) {
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> bank;
    bank.state = new juce::dsp::IIR::Coefficients<float>(kBankB0, 0.0f, 1.0f, -kBankFb);
    bank.prepare(kBank);
    bank.reset();
    OutputVector<float, 1> work(static_cast<std::size_t>(N));
    OutputVector<float> y(static_cast<std::size_t>(kNc));
    std::vector<float> ref(y.size());
    bank_mix_ref(g_in.data(), ref.data());
    timed(
        state, N, y.data(),
        [&] {
            process(bank, g_in.data(), work.data(), kCh, kNc);
            for (std::size_t i = 0; i < kNc; ++i) {
                float s = 0.0f;
                for (std::size_t c = 0; c < kCh; ++c) {
                    s += work[c * kNc + i];
                }
                y[i] = s;
            }
        },
        [&]() -> const char* { return maxabs_diff(y.data(), ref.data(), y.size()) <= 1e-3 ? nullptr : "bankmix16/juce: mismatch vs the bank loop"; });
}

// ---- maiverb: MAI_verb's ConvolutionEngine (source/DSP/ConvolutionEngine.cpp), vendored verbatim as main has it -----
// (its reset(), which nothing here calls, left out). The pre-delay is off (0 ms), so its branch never runs; the sizes are
// the constructor's 44.1 kHz tunings (prepare()'s rescale is not called).
struct ConvolutionEngine {
    std::vector<float> comb1Buffer;
    std::vector<float> comb2Buffer;
    std::vector<float> comb3Buffer;
    std::vector<float> comb4Buffer;
    std::vector<float> allpass1Buffer;
    std::vector<float> allpass2Buffer;
    std::vector<float> allpass3Buffer;
    std::vector<float> allpass4Buffer;
    juce::AudioBuffer<float> preDelayBuffer;
    std::size_t comb1Index = 0;
    std::size_t comb2Index = 0;
    std::size_t comb3Index = 0;
    std::size_t comb4Index = 0;
    std::size_t allpass1Index = 0;
    std::size_t allpass2Index = 0;
    std::size_t allpass3Index = 0;
    std::size_t allpass4Index = 0;
    int preDelayWritePos = 0;
    float feedbackFilter = 0.0f;
    float feedback = kVerbFeedback;
    float damping = kVerbDamp;
    float wetDryMix = kVerbWet;
    float preDelayMs = 0.0f;

    ConvolutionEngine() {
        comb1Buffer.assign(kVerbComb[0], 0.0f);
        comb2Buffer.assign(kVerbComb[1], 0.0f);
        comb3Buffer.assign(kVerbComb[2], 0.0f);
        comb4Buffer.assign(kVerbComb[3], 0.0f);
        allpass1Buffer.assign(kVerbAllpass[0], 0.0f);
        allpass2Buffer.assign(kVerbAllpass[1], 0.0f);
        allpass3Buffer.assign(kVerbAllpass[2], 0.0f);
        allpass4Buffer.assign(kVerbAllpass[3], 0.0f);
        preDelayBuffer.setSize(2, 48000, true, true, true);
    }
    float processCombFilter(float input, std::vector<float>& buffer, std::size_t& writeIndex, float fb, float damp) {
        float bufferOut = buffer[writeIndex];
        feedbackFilter = (bufferOut * (1.0f - damp)) + (feedbackFilter * damp);
        buffer[writeIndex] = input + (feedbackFilter * fb);
        writeIndex = (writeIndex + 1) % buffer.size();
        return bufferOut;
    }
    float processAllpassFilter(float input, std::vector<float>& buffer, std::size_t& writeIndex) {
        const float allpassFeedback = 0.5f;
        float bufferOut = buffer[writeIndex];
        float output = -input + bufferOut;
        buffer[writeIndex] = input + (bufferOut * allpassFeedback);
        writeIndex = (writeIndex + 1) % buffer.size();
        return output;
    }
    void processBlock(juce::AudioBuffer<float>& buffer) {
        int numSamples = buffer.getNumSamples();
        auto numChannels = buffer.getNumChannels();
        int preDelaySize = preDelayBuffer.getNumSamples();
        for (int ch = 0; ch < numChannels; ++ch) {
            auto* channelData = buffer.getWritePointer(ch);
            for (int n = 0; n < numSamples; ++n) {
                float input = channelData[n];
                if (preDelayMs > 0.01f) {
                    int delayIndex = preDelayWritePos;
                    float delayedSample = preDelayBuffer.getSample(ch, delayIndex);
                    preDelayBuffer.setSample(ch, delayIndex, input);
                    preDelayWritePos = (preDelayWritePos + 1) % preDelaySize;
                    input = delayedSample;
                }
                float combOut1 = processCombFilter(input, comb1Buffer, comb1Index, feedback, damping);
                float combOut2 = processCombFilter(input, comb2Buffer, comb2Index, feedback, damping);
                float combOut3 = processCombFilter(input, comb3Buffer, comb3Index, feedback, damping);
                float combOut4 = processCombFilter(input, comb4Buffer, comb4Index, feedback, damping);
                float combMix = (combOut1 + combOut2 + combOut3 + combOut4) * 0.25f;
                float allpassOut = processAllpassFilter(combMix, allpass1Buffer, allpass1Index);
                allpassOut = processAllpassFilter(allpassOut, allpass2Buffer, allpass2Index);
                allpassOut = processAllpassFilter(allpassOut, allpass3Buffer, allpass3Index);
                allpassOut = processAllpassFilter(allpassOut, allpass4Buffer, allpass4Index);
                channelData[n] = (allpassOut * wetDryMix) + (channelData[n] * (1.0f - wetDryMix));
                channelData[n] = std::tanh(channelData[n]);
            }
        }
    }
};

void bench_hand_maiverb(benchmark::State& state) {
    ConvolutionEngine engine;
    // The samples are ours, half a page into a page (Plan/30: the arm writes them): the buffer refers to them (JUCE's
    // constructor over a caller's channels) and is never resized, which would move it into memory of its own. Over
    // JUCE's own buffer the inlined engine ran 2 % fewer instructions, from inlining alone: out of line, as a host
    // calls a plugin's processBlock, both buffers run the same count.
    OutputVector<float> samples(static_cast<std::size_t>(N));
    float* const channel = samples.data();
    juce::AudioBuffer<float> buffer(&channel, 1, N);
    std::vector<float> ref(static_cast<std::size_t>(N));
    maiverb_ref(g_in.data(), ref.data(), ref.size());
    timed(
        state, N, buffer.getReadPointer(0),
        [&] {
            std::copy(g_in.begin(), g_in.end(), buffer.getWritePointer(0));
            engine.processBlock(buffer);
        },
        [&]() -> const char* {
            return maxabs_diff(buffer.getReadPointer(0), ref.data(), ref.size()) <= 2e-3 ? nullptr : "maiverb/hand: mismatch vs the reverb loop";
        });
}

void bench_juce_ladder1(benchmark::State& state) { bench_juce_ladder(state, 1); }
void bench_juce_ladder16(benchmark::State& state) { bench_juce_ladder(state, kCh); }
void bench_juce_svf1(benchmark::State& state) { bench_juce_svf(state, 1); }
void bench_juce_svf16(benchmark::State& state) { bench_juce_svf(state, kCh); }

BENCHMARK(bench_juce_ladder1)->Name("ladder/juce")->UseRealTime();
BENCHMARK(bench_juce_ladder16)->Name("ladder16/juce")->UseRealTime();
BENCHMARK(bench_juce_voice)->Name("voice/juce")->UseRealTime();
BENCHMARK(bench_juce_svf1)->Name("svf/juce")->UseRealTime();
BENCHMARK(bench_juce_svf16)->Name("svf16/juce")->UseRealTime();
BENCHMARK(bench_juce_linkwitz)->Name("linkwitz/juce")->UseRealTime();
BENCHMARK(bench_juce_envfollow)->Name("envfollow/juce")->UseRealTime();
BENCHMARK(bench_juce_bankmix)->Name("bankmix16/juce")->UseRealTime();
BENCHMARK(bench_hand_maiverb)->Name("maiverb/hand")->UseRealTime();

} // namespace
