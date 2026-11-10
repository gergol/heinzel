// A mixing console: sixteen channels, each trimmed, cleaned of rumble, gently saturated and panned, summed to a
// stereo bus, and run as a host runs it, 256 frames a block. Written to `console.wav`.
//
//   - The sixteen channels are one signal, `Signal{kChannels, Length{}}`: a channel strip is written once and runs
//     on every channel, in vector lanes across them.
//   - Each channel's settings are a tensor, one number a channel (`Input pan{"pan", {kChannels}}`), so every
//     channel has its own trim, low cut and position, with nothing written per channel.
//   - A low cut whose cutoff is a tensor is sixteen filters at sixteen cutoffs; the coefficient is worked out by the
//     graph from Hz.
//   - Panning keeps the power constant: the left gain is cos(pan · 90°), the right sin(pan · 90°), each spelled with
//     `sinpi` (a quarter turn apart).
//   - The bus is a reduction over the channel axis, `.sum(0)`: sixteen channels in, one sample out per frame.
//
// The bus is checked against the console written as a plain C++ loop over the whole signal, then written to
// `console.wav` (or the path given as the first argument).

#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include "heinzel/audio/wav.hpp"
#include "heinzel/dsl.hpp"

using namespace heinzel;

namespace {

constexpr int kRate = 48'000;
constexpr int kChannels = 16;
constexpr int kBlock = 256;
constexpr int kFrames = 2 * kRate;
constexpr Index n{};

// What lies above `cutoff` Hz.
constexpr Expr low_cut(Expr x, Expr cutoff) {
    Expr a = 1.0 - exp(-2.0 * std::numbers::pi / kRate * cutoff);
    Variable low;
    low[n] = low[n - 1] + a * (x[n] - low[n - 1]);
    return x - low;
}

constexpr Graph console() {
    Input channels{"channels", Signal{kChannels, Length{}}};
    Input trim{"trim", {kChannels}};
    Input cutoff{"cutoff", {kChannels}};
    Input pan{"pan", {kChannels}, ge(0), le(1)};
    Param drive{"drive"};
    Param master{"master"};
    Expr clean = low_cut(trim * channels, cutoff);
    Expr strip = tanh(drive * clean);
    Output left{"left"};
    Output right{"right"};
    left = master * (strip * sinpi(0.5 * (1.0 - pan))).sum(0);
    right = master * (strip * sinpi(0.5 * pan)).sum(0);
    return finish(left, right);
}

struct Settings {
    float trim[kChannels];
    float cutoff[kChannels];
    float pan[kChannels];
    float drive;
    float master;
};

Settings session(std::vector<float>& channels);
void reference(const std::vector<float>& channels, const Settings& s, std::vector<double>& left, std::vector<double>& right);

} // namespace

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "console.wav";

    std::vector<float> channels(kChannels * kFrames);
    const Settings s = session(channels);

    std::vector<float> left(kFrames);
    std::vector<float> right(kFrames);
    Executor<console> desk;
    desk.prepare(kBlock);
    desk.input("trim") = s.trim;
    desk.input("cutoff") = s.cutoff;
    desk.input("pan") = s.pan;
    desk.param("drive") = s.drive;
    desk.param("master") = s.master;
    for (int at = 0; at < kFrames; at += kBlock) {
        desk.input("channels") = channels.data() + at * kChannels;
        desk.output("left") = left.data() + at;
        desk.output("right") = right.data() + at;
        desk.run(kBlock);
    }

    std::vector<double> want_left;
    std::vector<double> want_right;
    reference(channels, s, want_left, want_right);
    double off = 0.0;
    for (int t = 0; t < kFrames; ++t) {
        const double d = std::fmax(std::fabs(left[t] - want_left[t]), std::fabs(right[t] - want_right[t]));
        off = d == d ? std::fmax(off, d) : INFINITY;
    }
    if (!(off < 1e-5)) { // a sum of sixteen float strips, each a few roundings off the loop's
        std::fprintf(stderr, "mixing_console: %.3g from the plain loop\n", off);
        return 1;
    }

    std::vector<float> stereo(2 * kFrames);
    for (int t = 0; t < kFrames; ++t) {
        stereo[2 * t] = left[t];
        stereo[2 * t + 1] = right[t];
    }
    audio::wav_writer wav;
    if (!wav.open(path, kRate, 2) || !wav.write(stereo.data(), kFrames) || !wav.close()) {
        std::fprintf(stderr, "mixing_console: %s: %s\n", path, wav.error());
        return 1;
    }
    std::printf("mixing_console: %d channels to a stereo bus in blocks of %d, written to %s, within %.1e of the plain loop\n",
                kChannels, kBlock, path, off);
    return 0;
}

namespace {

// Sixteen voices of a chord, each with a little rumble, laid out as a host lays them: channel by channel within a
// frame; and the desk's settings for them.
Settings session(std::vector<float>& channels) {
    Settings s{};
    for (int c = 0; c < kChannels; ++c) {
        const double hz = 55.0 * std::exp2((c * 7 % 24) / 12.0);
        for (int t = 0; t < kFrames; ++t) {
            const double seconds = static_cast<double>(t) / kRate;
            const double tone = std::sin(2 * std::numbers::pi * hz * seconds);
            const double decay = std::exp(-seconds * (1 + c % 4));
            const double rumble = std::sin(2 * std::numbers::pi * 7 * seconds);
            channels[t * kChannels + c] = static_cast<float>(tone * decay + 0.2 * rumble);
        }
        s.trim[c] = 0.5f + 0.03f * static_cast<float>(c);
        s.cutoff[c] = 40.0f + 10.0f * static_cast<float>(c);
        s.pan[c] = static_cast<float>(c) / (kChannels - 1);
    }
    s.drive = 1.5f;
    s.master = 0.15f;
    return s;
}

void reference(const std::vector<float>& channels, const Settings& s, std::vector<double>& left, std::vector<double>& right) {
    double low[kChannels] = {};
    left.assign(kFrames, 0.0);
    right.assign(kFrames, 0.0);
    for (int t = 0; t < kFrames; ++t) {
        for (int c = 0; c < kChannels; ++c) {
            const double x = s.trim[c] * channels[t * kChannels + c];
            low[c] += (1.0 - std::exp(-2.0 * std::numbers::pi / kRate * s.cutoff[c])) * (x - low[c]);
            const double strip = std::tanh(s.drive * (x - low[c]));
            left[t] += s.master * strip * std::cos(0.5 * std::numbers::pi * s.pan[c]);
            right[t] += s.master * strip * std::sin(0.5 * std::numbers::pi * s.pan[c]);
        }
    }
}

} // namespace
