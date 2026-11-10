// Tour 08: streaming. A stereo echo run as an audio host runs a plug-in: blocks of whatever size the host hands it,
// processed in place.
//
//   - `Signal{2, Length{}}` is two channels whose length arrives at run time. A graph with a `Length` is prepared
//     once for the most samples a run may bring, `prepare(512)`, and then run on each block, `run(frames)`. Nothing
//     is allocated after `prepare`.
//   - A multichannel signal lies sample by sample, the channels of one sample side by side (left, right, left,
//     right, …), as a WAV file and most hosts lay audio out.
//   - A tensor holds the same numbers at every sample. `Input balance{"balance", {2}}`, one gain per channel,
//     multiplies the stereo signal channel by channel at every sample, the way numbers broadcast in numpy.
//   - The output may be bound to the input's own buffer: the program reads the block before it overwrites it, and
//     an output that keeps a past (as this echo does) is written aside and copied in. Each binding means what it
//     says.
//   - The echo reaches 250 ms back, longer than any block: the program keeps that past between runs.
//
// The blocks are checked against the echo computed over the whole signal by a plain C++ loop.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "heinzel/dsl.hpp"

using namespace heinzel;

namespace {

constexpr int kRate = 48'000;
constexpr int kMaxBlock = 512;
constexpr int kSeconds = 2;
constexpr Index n{kRate};

constexpr Graph echo() {
    Input dry{"dry", Signal{2, Length{}}};
    Input balance{"balance", {2}};
    Output wet{"wet"};
    wet[n] = balance * dry[n] + 0.4 * wet[n - 250.0 * ms];
    return wet;
}

constexpr float kBalance[2] = {1.0f, 0.6f}; // the right channel a little quieter

std::vector<double> reference(const std::vector<float>& dry);

} // namespace

int main() {
    constexpr int kFrames = kSeconds * kRate;
    std::vector<float> audio(2 * kFrames); // interleaved: left, right, left, right, …
    for (int t = 0; t < kFrames; ++t) {
        const float click = t % 30'000 == 0 ? 1.0f : 0.0f;
        audio[2 * t] = click;                                           // left: clicks
        audio[2 * t + 1] = 0.2f * std::sin(0.01f * static_cast<float>(t)); // right: a tone
    }
    const std::vector<float> dry = audio;

    Executor<echo> fx;
    fx.prepare(kMaxBlock);
    fx.input("balance") = kBalance;

    // The host's callback: blocks of uneven size, processed in place.
    const int sizes[] = {512, 64, 300, 1, 480, 512, 128};
    int at = 0;
    for (int call = 0; at < kFrames; ++call) {
        const int frames = std::min(sizes[call % 7], kFrames - at);
        float* block = audio.data() + 2 * at;
        fx.input("dry") = block;
        fx.output("wet") = block;
        fx.run(frames);
        at += frames;
    }

    const std::vector<double> want = reference(dry);
    double off = 0.0;
    for (std::size_t i = 0; i < want.size(); ++i) {
        const double d = std::fabs(audio[i] - want[i]);
        off = d == d ? std::fmax(off, d) : INFINITY;
    }
    if (!(off < 1e-6)) {
        std::fprintf(stderr, "08_streaming: %.3g from the plain loop over the whole signal\n", off);
        return 1;
    }
    std::printf("08_streaming: %d s of stereo echo in blocks of 1 to 512 frames, in place, within %.1e of one loop\n", kSeconds, off);
    return 0;
}

namespace {

std::vector<double> reference(const std::vector<float>& dry) {
    constexpr int kDelay = 12'000; // 250 ms
    std::vector<double> wet(dry.size());
    for (std::size_t i = 0; i < dry.size(); ++i) {
        const int channel = static_cast<int>(i % 2);
        wet[i] = kBalance[channel] * dry[i] + (i >= 2 * kDelay ? 0.4 * wet[i - 2 * kDelay] : 0.0);
    }
    return wet;
}

} // namespace
