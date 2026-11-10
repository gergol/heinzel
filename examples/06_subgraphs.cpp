// Tour 06: subgraphs. A tone control, its bass and treble shelves each a piece with Params of its own.
//
//   - A function that declares ports, like `bass_shelf`'s `Param gain{"gain"}`, can be made a piece of a graph
//     with a name: `auto bass = subgraph("bass", bass_shelf);`. Applied, `bass(x)` runs the function, and the ports
//     it declares come out named after the piece: "bass.cutoff" and "bass.gain". Two pieces of one function never
//     clash, and nesting composes the names ("strip.bass.gain").
//   - A piece is made once and applied once; a second use is a second piece ("left_bass", "right_bass"), so every
//     port has a name of its own. The function runs when the piece is applied, not when it is made.
//   - A piece lives inside the graph function that applies it. It remembers that it was applied, and while a graph
//     is built at compile time, only what its graph function made can change: a `constexpr` piece made outside it
//     does not compile when applied.
//   - What a piece's function takes besides expressions must be plain data (numbers, shapes, durations), and it
//     captures nothing, so every port it makes is seen and named.
//   - The pieces splice into the caller's graph like any composition: there is no subgraph at run time.
//
// The tone control is checked against a plain C++ loop.

#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include "heinzel/dsl.hpp"

using namespace heinzel;

namespace {

constexpr int kRate = 48'000;
constexpr int kSamples = 4800;
constexpr Index n{};

constexpr Expr lowpass(Expr x, Expr cutoff) {
    Expr a = 1.0 - exp(-2.0 * std::numbers::pi / kRate * cutoff);
    Variable y;
    y[n] = y[n - 1] + a * (x[n] - y[n - 1]);
    return y;
}

// Below `cutoff`, x times `gain`; above it, x as it was.
constexpr Expr bass_shelf(Expr x) {
    Param cutoff{"cutoff"};
    Param gain{"gain"};
    Expr lows = lowpass(x, cutoff);
    return x + (gain - 1.0) * lows;
}

// Above `cutoff`, x times `gain`; below it, x as it was.
constexpr Expr treble_shelf(Expr x) {
    Param cutoff{"cutoff"};
    Param gain{"gain"};
    Expr highs = x - lowpass(x, cutoff);
    return x + (gain - 1.0) * highs;
}

constexpr Graph tone_control() {
    auto bass = subgraph("bass", bass_shelf);
    auto treble = subgraph("treble", treble_shelf);
    Input x{"x", Signal{kSamples}};
    Param volume{"volume"};
    Output y{"y"};
    Expr tone = bass(x);
    tone = treble(tone);
    y = volume * tone;
    return y;
}

struct Knobs {
    double volume;
    double bass_cutoff;
    double bass_gain;
    double treble_cutoff;
    double treble_gain;
};

std::vector<double> reference(const std::vector<float>& x, Knobs k);

} // namespace

int main() {
    std::vector<float> x(kSamples);
    std::vector<float> y(kSamples);
    for (int t = 0; t < kSamples; ++t) { // a sawtooth at 110 Hz: every harmonic
        x[t] = static_cast<float>(2.0 * std::fmod(110.0 * t / kRate, 1.0) - 1.0);
    }

    const Knobs k{0.7, 200.0, 2.0, 4000.0, 0.5}; // bass up, treble down
    Executor<tone_control> tone;
    tone.input("x") = x.data();
    tone.output("y") = y.data();
    tone.param("volume") = k.volume;
    tone.param("bass.cutoff") = k.bass_cutoff;
    tone.param("bass.gain") = k.bass_gain;
    tone.param("treble.cutoff") = k.treble_cutoff;
    tone.param("treble.gain") = k.treble_gain;
    tone.run();

    const std::vector<double> want = reference(x, k);
    double off = 0.0;
    for (int t = 0; t < kSamples; ++t) {
        const double d = std::fabs(y[t] - want[t]);
        off = d == d ? std::fmax(off, d) : INFINITY;
    }
    if (!(off < 1e-5)) {
        std::fprintf(stderr, "06_subgraphs: %.3g from the plain loop\n", off);
        return 1;
    }
    std::printf("06_subgraphs: bass and treble shelves, five Params by name, within %.1e of the plain loop\n", off);
    return 0;
}

namespace {

std::vector<double> reference(const std::vector<float>& x, Knobs k) {
    const auto coefficient = [](double cutoff) { return 1.0 - std::exp(-2.0 * std::numbers::pi / kRate * cutoff); };
    const double a_bass = coefficient(k.bass_cutoff);
    const double a_treble = coefficient(k.treble_cutoff);
    double bass_state = 0;
    double treble_state = 0;
    std::vector<double> y(x.size());
    for (std::size_t t = 0; t < x.size(); ++t) {
        bass_state += a_bass * (x[t] - bass_state);
        const double bassy = x[t] + (k.bass_gain - 1.0) * bass_state;
        treble_state += a_treble * (bassy - treble_state);
        y[t] = k.volume * (bassy + (k.treble_gain - 1.0) * (bassy - treble_state));
    }
    return y;
}

} // namespace
