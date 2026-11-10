// Tour 07: oscillators. An FM bell, rendered to `bell.wav`.
//
//   - A recurrence that no input signal drives does not know how long it is. A clock, counting seconds from the
//     start, declares how long the sound is: `Variable t{Signal{kSamples}}`. Everything else takes that length, so a
//     phase is just `Variable p;`. (In a graph with no length, or two, each such recurrence declares its own.)
//   - A phase in cycles, wrapped to [0, 1) by `frac` every sample, never grows, so it loses no precision however
//     long the sound; `sinpi(2 * phase)` turns it into a sine (sinpi is sin of pi times its operand).
//   - Frequency modulation is one phase driving another's increment: the modulator's sine, scaled by a decaying
//     envelope, bends the carrier's pitch. A bright attack mellows as the envelope falls.
//   - `audio::wav_writer` (`heinzel/audio/wav.hpp`) writes the result; `audio::wav_reader` reads one.
//
// The bell is checked against the same bell written as a plain C++ loop, then written to `bell.wav` (or the path
// given as the first argument).

#include <cmath>
#include <cstdio>
#include <vector>

#include "heinzel/audio/wav.hpp"
#include "heinzel/dsl.hpp"

using namespace heinzel;

namespace {

constexpr int kRate = 48'000;
constexpr int kSamples = 2 * kRate;
constexpr Index n{};

// Seconds since the start.
constexpr Expr clock() {
    Variable t{Signal{kSamples}};
    t[n] = t[n - 1] + 1.0 / kRate;
    return t;
}

// A phase in cycles, advanced by `hz` cycles a second, as long as the graph it is in (here, the clock's two seconds).
constexpr Expr phase(Expr hz) {
    Variable p;
    p[n] = frac(p[n - 1] + hz / kRate);
    return p;
}

constexpr Expr sine(Expr hz) { return sinpi(2.0 * phase(hz)); }

constexpr Graph bell() {
    Param pitch{"pitch"};
    Param ratio{"ratio"};
    Param depth{"depth"};
    Expr envelope = exp(-3.0 * clock());
    Expr modulator = sine(pitch * ratio);
    Expr carrier = sine(pitch * (1.0 + depth * envelope * modulator));
    Output out{"out"};
    out = 0.5 * envelope * carrier;
    return out;
}

std::vector<float> reference(float pitch, float ratio, float depth);

} // namespace

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "bell.wav";
    constexpr float kPitch = 440.0f;
    constexpr float kRatio = 3.5f; // a ratio off the harmonics rings like metal
    constexpr float kDepth = 2.0f;

    std::vector<float> out(kSamples);
    Executor<bell> ring;
    ring.output("out") = out.data();
    ring.param("pitch") = kPitch;
    ring.param("ratio") = kRatio;
    ring.param("depth") = kDepth;
    ring.run();

    // The program keeps the clock and the phases in double (at `Normal` a loop of cheap work around one voice
    // does, tour 13), and so does the loop below; what is left are the program's float cores of `exp` and `sinpi`,
    // a few roundings a sample, which the carrier's phase carries on: 2.4e-7, measured on x86-64. The bound leaves
    // room for a machine that rounds its cores otherwise.
    const std::vector<float> want = reference(kPitch, kRatio, kDepth);
    double off = 0.0;
    for (int t = 0; t < kSamples; ++t) {
        const double d = std::fabs(out[t] - want[t]);
        off = d == d ? std::fmax(off, d) : INFINITY;
    }
    if (!(off < 1e-5)) {
        std::fprintf(stderr, "07_oscillators: %.3g from the plain loop\n", off);
        return 1;
    }

    audio::wav_writer wav;
    if (!wav.open(path, kRate, 1) || !wav.write(out.data(), kSamples) || !wav.close()) {
        std::fprintf(stderr, "07_oscillators: %s: %s\n", path, wav.error());
        return 1;
    }
    std::printf("07_oscillators: 2 s of bell written to %s, within %.1e of the plain loop\n", path, off);
    return 0;
}

namespace {

// The increments in float, as the graph computes them; the sums they feed in double.
std::vector<float> reference(float pitch, float ratio, float depth) {
    const auto wrapped = [](double p) { return p - std::floor(p); };
    std::vector<float> out(kSamples);
    double t = 0;
    double modulator_phase = 0;
    double carrier_phase = 0;
    for (int i = 0; i < kSamples; ++i) {
        t = t + 1.0 / kRate;
        const float envelope = std::exp(-3.0f * static_cast<float>(t));
        modulator_phase = wrapped(modulator_phase + pitch * ratio / kRate);
        const float modulator = std::sin(2.0f * 3.14159265358979f * static_cast<float>(modulator_phase));
        carrier_phase = wrapped(carrier_phase + pitch * (1.0f + depth * envelope * modulator) / kRate);
        const float carrier = std::sin(2.0f * 3.14159265358979f * static_cast<float>(carrier_phase));
        out[i] = 0.5f * envelope * carrier;
    }
    return out;
}

} // namespace
