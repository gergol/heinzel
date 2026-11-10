// A synthesizer voice, from a melody to a WAV file.
//
// Two detuned sawtooths through a resonant low-pass whose cutoff follows the envelope, saturated, and sent into
// a feedback echo. The whole voice is one graph, built and compiled while this file compiles; at run time it is
// fed a melody — a pitch and a gate per sample — and renders three seconds of audio in one call.
//
//   - State is a recurrence: the envelope, the two phase accumulators, the filter's two integrators and the
//     echo are each a `Variable` defined by `v[n] = …` from its own past, `v[n - 1]`. The planner finds the
//     feedback loops and runs them, with what lies between them, a sample at a time; the rest runs in lanes.
//   - Composition is plain C++: a filter is a `constexpr` function from expressions to an expression.
//   - Times are physical: `n` knows the sample rate, so 5 ms, 60 ms and 375 ms are folded to whole samples
//     while compiling; a duration between two samples stops the build.
//   - `Executor<voice>` compiles the graph with this file and binds its ports by name, checked while
//     compiling: a misspelt one stops the build.
//
// The rendered voice is checked against the same voice written as one plain C++ loop, then written to
// `synth_voice.wav` (or the path given as the first argument).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include "heinzel/audio/wav.hpp"
#include "heinzel/dsl.hpp"

using namespace heinzel;

namespace {

constexpr int kRate = 48'000;
constexpr int kSamples = 3 * kRate;
constexpr Index n{kRate}; // the sample clock: `n - 375.0 * ms` is a whole number of samples
static_assert(samples(n, 5.0 * ms) == 240 && samples(n, 60.0 * ms) == 2880 && samples(n, 375.0 * ms) == 18'000);

// Follows the gate: rising at the attack's pace while the gate is above it, falling at the release's while below.
constexpr Expr envelope(Expr gate, units::Quantity attack, units::Quantity release) {
    Variable level;
    Expr last = level[n - 1];
    level[n] = last + max(gate[n] - last, 0.0) / samples(n, attack) + min(gate[n] - last, 0.0) / samples(n, release);
    return level;
}

// A sawtooth from a phase that counts cycles and wraps to [0, 1).
constexpr Expr saw(Expr hz) {
    Variable phase;
    phase[n] = frac(phase[n - 1] + hz[n] / kRate);
    return 2.0 * phase - 1.0;
}

// Chamberlin's state-variable filter: two integrators in a loop, the band-pass feeding the low-pass.
constexpr Expr lowpass(Expr x, Expr cutoff, double damping) {
    Expr f = 2.0 * sinpi(cutoff / kRate);
    Variable low;
    Variable band;
    low[n] = low[n - 1] + f * band[n - 1];
    band[n] = band[n - 1] + f * (x[n] - low[n] - damping * band[n - 1]);
    return low;
}

// Each repeat `time` later and `feedback` quieter than the one before.
constexpr Expr echo(Expr dry, units::Quantity time, double feedback) {
    Variable wet;
    wet[n] = dry[n] + feedback * wet[n - time];
    return wet;
}

constexpr Graph voice() {
    Input pitch{"pitch", Signal{kSamples}}; // in Hz
    Input gate{"gate", Signal{kSamples}};   // 1 while a key is held
    Output out{"out"};
    Expr amp = envelope(gate, 5.0 * ms, 60.0 * ms);
    Expr saw_a = saw(pitch);
    Expr saw_b = saw(pitch * 1.006);
    Expr osc = 0.5 * (saw_a + saw_b);
    Expr low = lowpass(osc, 150.0 + 4000.0 * amp * amp, 0.3);
    Expr tone = tanh(1.5 * low);
    Expr wet = echo(amp * tone, 375.0 * ms, 0.35);
    out = 0.4 * wet;
    return out;
}

std::vector<float> reference(const std::vector<float>& pitch, const std::vector<float>& gate);

} // namespace

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "synth_voice.wav";

    // An arpeggio in A minor, sixteenth notes at 120 bpm, each held for half its length: one bar, then the echo.
    constexpr int kNotes[] = {45, 52, 57, 60, 64, 57, 60, 67}; // MIDI note numbers
    constexpr int kStep = kRate / 8;
    std::vector<float> pitch(kSamples);
    std::vector<float> gate(kSamples);
    for (int t = 0; t < kSamples; ++t) {
        const int note = t / kStep;
        pitch[t] = 440.0f * std::exp2(static_cast<float>(kNotes[note % 8] - 69) / 12.0f);
        gate[t] = note < 16 && t % kStep < kStep / 2 ? 1.0f : 0.0f;
    }

    std::vector<float> out(kSamples);
    Executor<voice> synth;
    synth.input("pitch") = pitch.data();
    synth.input("gate") = gate.data();
    synth.output("out") = out.data();
    synth.run();

    // The loop computes what the graph computes, in its order where it matters, and keeps in double what the
    // program keeps in double (below); what is left are the float cores of `sinpi` and `tanh`, an ulp or two:
    // 1.8e-7 of a 0.55 peak, measured on x86-64. The bound leaves a machine that fuses or rounds its cores
    // differently room for fifty times that.
    const std::vector<float> want = reference(pitch, gate);
    double off = 0.0;
    for (int t = 0; t < kSamples; ++t) {
        if (const double d = std::fabs(out[t] - want[t]); std::isnan(d) || d > off) {
            off = d; // a NaN stays
        }
    }
    if (!(off < 1e-5)) {
        std::fprintf(stderr, "synth_voice: %.3g away from the plain C++ voice\n", off);
        return 1;
    }

    audio::wav_writer wav;
    if (!wav.open(path, kRate, 1) || !wav.write(out.data(), kSamples) || !wav.close()) {
        std::fprintf(stderr, "synth_voice: %s: %s\n", path, wav.error());
        return 1;
    }
    std::printf("synth_voice: 3 s at %d Hz written to %s, within %.1e of the plain C++ voice\n", kRate, path, off);
    return 0;
}

namespace {

// ---- the same voice as one plain C++ loop, to check the compiled one against -------------------------------

double wrapped(double phase) { return phase - std::floor(phase); }

// 5 ms, 60 ms and 375 ms at 48 kHz are 240, 2880 and 18000 samples (asserted at the top). The envelope, the phases,
// their mix and the filter are kept in double, as the program keeps them: at `Normal` a loop of cheap work around one
// voice keeps its state in double (tour 13), and a phase rounded otherwise moves a sawtooth's jump to the next sample,
// a difference of 1.
std::vector<float> reference(const std::vector<float>& pitch, const std::vector<float>& gate) {
    std::vector<float> wet(kSamples);
    double level = 0;
    double phase_a = 0;
    double phase_b = 0;
    double low = 0;
    double band = 0;
    for (int t = 0; t < kSamples; ++t) {
        level = level + std::max(gate[t] - level, 0.0) / 240 + std::min(gate[t] - level, 0.0) / 2880;
        const float amp = static_cast<float>(level);
        phase_a = wrapped(phase_a + pitch[t] / kRate);
        phase_b = wrapped(phase_b + pitch[t] * 1.006f / kRate);
        const double osc = 0.5 * ((2 * phase_a - 1) + (2 * phase_b - 1));
        const float cutoff = 150 + 4000 * amp * amp;
        const float f = 2 * std::sin(std::numbers::pi_v<float> * (cutoff / kRate));
        const double last_band = band;
        low += f * last_band;
        band += f * (osc - low - 0.3 * last_band);
        const float tone = std::tanh(1.5f * static_cast<float>(low));
        wet[t] = amp * tone + (t >= 18'000 ? 0.35f * wet[t - 18'000] : 0.0f);
    }
    for (float& s : wet) {
        s *= 0.4f;
    }
    return wet;
}

} // namespace
