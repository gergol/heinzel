// A reverb: Jezar's Freeverb, eight damped combs in parallel and four allpasses in a row, put behind three drum hits
// and written to `reverb.wav`.
//
//   - Freeverb is two small filters used twelve times. Each is a `constexpr` function that keeps its own delay line
//     in a `Variable`, so the reverb itself is two loops of C++ that build twenty feedback loops.
//   - A delay line is a read of the past: `line[n - size]`. A comb is 1116 to 1617 samples long, an allpass 225 to
//     556, and the program keeps exactly that much of each between runs.
//   - Feedback loops that feed each other nothing, as the combs do, share one sample loop where the planner's cost
//     model prices that no dearer, so that the processor overlaps their latencies.
//   - Twenty feedback loops are a larger graph than the defaults hold (128 values): a graph's capacities are fixed,
//     so that it can be built while compiling, and each is raised with a definition, `-DHEINZEL_MAX_VALUES=256` and
//     `-DHEINZEL_MAX_NODES=256` here (`CMakeLists.txt`). Going over one stops the build; the first error's notes name
//     the definition.
//
// The rendered reverb is checked against Freeverb written as a plain C++ loop, then written to `reverb.wav` (or the
// path given as the first argument).

#include <cmath>
#include <cstdio>
#include <vector>

#include "heinzel/audio/wav.hpp"
#include "heinzel/dsl.hpp"

using namespace heinzel;

namespace {

constexpr int kRate = 44'100;
constexpr int kSamples = 3 * kRate;
constexpr Index n{};

// Freeverb's tuning at 44.1 kHz: its delays, in samples, and its gains.
constexpr int kCombs[] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr int kAllpasses[] = {556, 441, 341, 225};
constexpr double kRoom = 0.84;
constexpr double kDamp = 0.2;
constexpr double kInputGain = 0.015;

// A comb whose feedback passes a one-pole low-pass, so the highs die away first, as in a real room.
constexpr Expr comb(Expr in, int size) {
    Variable line;
    Variable low;
    Expr out = line[n - size];
    low[n] = (1.0 - kDamp) * out + kDamp * low[n - 1];
    line[n] = in + kRoom * low;
    return out;
}

// An allpass: it passes every frequency at the same gain, and smears each in time.
constexpr Expr allpass(Expr in, int size) {
    Variable line;
    Expr out = line[n - size];
    line[n] = in + 0.5 * out;
    return out - in;
}

constexpr Expr freeverb(Expr dry) {
    Expr in = kInputGain * dry;
    Expr wet = comb(in, kCombs[0]);
    for (int k = 1; k < 8; ++k) {
        wet = wet + comb(in, kCombs[k]);
    }
    for (const int size : kAllpasses) {
        wet = allpass(wet, size);
    }
    return wet;
}

constexpr Graph room() {
    Input dry{"dry", Signal{kSamples}};
    Param mix{"mix"};
    Output out{"out"};
    Expr wet = freeverb(dry);
    out = (1.0 - mix) * dry + 3.0 * mix * wet;
    return out;
}

std::vector<double> reference(const std::vector<float>& dry, double mix);

} // namespace

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "reverb.wav";

    std::vector<float> dry(kSamples, 0.0f);
    std::vector<float> out(kSamples);
    unsigned seed = 1;
    for (const int hit : {0, kRate / 2, kRate}) { // three drum hits: bursts of noise, fading over 50 ms
        for (int t = 0; t < kRate / 20; ++t) {
            seed = seed * 1664525u + 1013904223u;
            const float noise = static_cast<float>(seed >> 8) / 8388608.0f - 1.0f;
            dry[hit + t] = 0.8f * noise * std::exp(-static_cast<float>(t) / 300.0f);
        }
    }

    Executor<room> reverb;
    reverb.input("dry") = dry.data();
    reverb.output("out") = out.data();
    reverb.param("mix") = 0.35;
    reverb.run();

    // Twenty feedback loops, three seconds long: the program orders its sums by when their terms are ready and may
    // keep a loop's state in double, where the loop below sums as written, in double. Measured 4.0e-8 on x86-64.
    const std::vector<double> want = reference(dry, 0.35f);
    double off = 0.0;
    for (int t = 0; t < kSamples; ++t) {
        const double d = std::fabs(out[t] - want[t]);
        off = d == d ? std::fmax(off, d) : INFINITY;
    }
    if (!(off < 1e-6)) {
        std::fprintf(stderr, "reverb: %.3g from the plain Freeverb\n", off);
        return 1;
    }

    audio::wav_writer wav;
    if (!wav.open(path, kRate, 1) || !wav.write(out.data(), kSamples) || !wav.close()) {
        std::fprintf(stderr, "reverb: %s: %s\n", path, wav.error());
        return 1;
    }
    std::printf("reverb: 3 s of Freeverb written to %s, within %.1e of the plain loop\n", path, off);
    return 0;
}

namespace {

// Freeverb as Jezar wrote it, a circular buffer per filter, in double.
std::vector<double> reference(const std::vector<float>& dry, double mix) {
    struct Line {
        std::vector<double> samples;
        int at = 0;
        double& here() { return samples[at]; }
        void step() { at = (at + 1) % static_cast<int>(samples.size()); }
    };
    std::vector<Line> combs;
    std::vector<Line> allpasses;
    std::vector<double> lows(8, 0.0);
    for (const int size : kCombs) {
        combs.push_back(Line{std::vector<double>(size, 0.0)});
    }
    for (const int size : kAllpasses) {
        allpasses.push_back(Line{std::vector<double>(size, 0.0)});
    }
    std::vector<double> out(dry.size());
    for (std::size_t t = 0; t < dry.size(); ++t) {
        const double in = kInputGain * dry[t];
        double wet = 0.0;
        for (int k = 0; k < 8; ++k) {
            const double delayed = combs[k].here(); // the sample written `size` steps ago
            lows[k] = (1.0 - kDamp) * delayed + kDamp * lows[k];
            combs[k].here() = in + kRoom * lows[k];
            combs[k].step();
            wet += delayed;
        }
        for (Line& line : allpasses) {
            const double delayed = line.here();
            line.here() = wet + 0.5 * delayed;
            line.step();
            wet = delayed - wet;
        }
        out[t] = (1.0 - mix) * dry[t] + 3.0 * mix * wet;
    }
    return out;
}

} // namespace
