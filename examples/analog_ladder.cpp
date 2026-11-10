// An analog ladder filter: a sawtooth through a resonant four-pole low-pass in the manner of Moog's, its cutoff
// sweeping down, rendered twice, with two saturators, to the two channels of `ladder.wav`.
//
//   - The ladder is four one-pole stages in a row, the last fed back to the first. Each stage saturates what enters
//     it and its own state, as transistors do (Huovilainen's model), so the resonance stays bounded and growls.
//   - The saturator is a C++ argument: `ladder<Sat>` makes the graph with whichever `Expr (*)(Expr)` it is given.
//     `tanh` is the textbook curve; `algebraic`, x / sqrt(1 + x²), a cheaper one of the same shape, a square root
//     instead of an exponential. Each is its own graph, compiled on its own.
//   - The cutoff arrives as a signal in Hz, a sample at a time; the graph turns it into the stages' coefficient.
//   - `algebraic`'s division is proven safe while compiling: `1 + x * x` is at least 1, since `x * x` is a square.
//
// Both renders are checked against the same ladder written as a plain C++ loop, then written to `ladder.wav` (left
// `tanh`, right `algebraic`; or the path given as the first argument).

#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include "heinzel/audio/wav.hpp"
#include "heinzel/dsl.hpp"

using namespace heinzel;

namespace {

constexpr int kRate = 48'000;
constexpr int kSamples = 2 * kRate;
constexpr double kResonance = 3.6; // 4 is where the ladder rings on its own
constexpr Index n{};

// Like tanh, from -1 to 1 through 0 with slope 1, but softer at the knee.
constexpr Expr algebraic(Expr x) { return x / sqrt(1.0 + x * x); }

// One stage: its state follows its saturated input, `g` of the way each sample.
template <Expr (*Sat)(Expr)>
constexpr Expr stage(Expr in, Expr g) {
    Variable s;
    s[n] = s[n - 1] + g * (Sat(in) - Sat(s[n - 1]));
    return s;
}

template <Expr (*Sat)(Expr)>
constexpr Graph ladder() {
    Input x{"x", Signal{kSamples}};
    Input cutoff{"cutoff", Signal{kSamples}};
    Expr g = 1.0 - exp(-2.0 * std::numbers::pi / kRate * cutoff);
    Output y{"y"};
    Expr first = stage<Sat>(x - kResonance * y[n - 1], g);
    Expr second = stage<Sat>(first, g);
    Expr third = stage<Sat>(second, g);
    y[n] = stage<Sat>(third, g);
    return y;
}

std::vector<double> reference(const std::vector<float>& x, const std::vector<float>& cutoff, double (*sat)(double));

} // namespace

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "ladder.wav";

    // A 55 Hz sawtooth, its cutoff gliding from 5 kHz down to 100 Hz.
    std::vector<float> x(kSamples);
    std::vector<float> cutoff(kSamples);
    std::vector<float> warm(kSamples);
    std::vector<float> cheap(kSamples);
    for (int t = 0; t < kSamples; ++t) {
        x[t] = static_cast<float>(0.8 * (2.0 * std::fmod(55.0 * t / kRate, 1.0) - 1.0));
        cutoff[t] = static_cast<float>(5000.0 * std::pow(100.0 / 5000.0, static_cast<double>(t) / kSamples));
    }
    Executor<ladder<tanh>> tanh_ladder;
    tanh_ladder.input("x") = x.data();
    tanh_ladder.input("cutoff") = cutoff.data();
    tanh_ladder.output("y") = warm.data();
    tanh_ladder.run();
    Executor<ladder<algebraic>> algebraic_ladder;
    algebraic_ladder.input("x") = x.data();
    algebraic_ladder.input("cutoff") = cutoff.data();
    algebraic_ladder.output("y") = cheap.data();
    algebraic_ladder.run();

    // A nonlinear loop near self-oscillation could carry a rounding far. It does not here: both stay within 4e-7 of
    // the double loop on x86-64, and the bound leaves room for a machine that rounds its cores otherwise.
    const auto distance = [](const std::vector<float>& got, const std::vector<double>& want) {
        double off = 0.0;
        for (std::size_t t = 0; t < want.size(); ++t) {
            const double d = std::fabs(got[t] - want[t]);
            off = d == d ? std::fmax(off, d) : INFINITY;
        }
        return off;
    };
    const std::vector<double> want_tanh = reference(x, cutoff, [](double v) { return std::tanh(v); });
    const std::vector<double> want_algebraic = reference(x, cutoff, [](double v) { return v / std::sqrt(1.0 + v * v); });
    const double off_tanh = distance(warm, want_tanh);
    const double off_algebraic = distance(cheap, want_algebraic);
    if (!(off_tanh < 1e-5 && off_algebraic < 1e-5)) {
        std::fprintf(stderr, "analog_ladder: %.3g (tanh) and %.3g (algebraic) from the plain loop\n", off_tanh, off_algebraic);
        return 1;
    }

    std::vector<float> stereo(2 * kSamples);
    for (int t = 0; t < kSamples; ++t) {
        stereo[2 * t] = warm[t];
        stereo[2 * t + 1] = cheap[t];
    }
    audio::wav_writer wav;
    if (!wav.open(path, kRate, 2) || !wav.write(stereo.data(), kSamples) || !wav.close()) {
        std::fprintf(stderr, "analog_ladder: %s: %s\n", path, wav.error());
        return 1;
    }
    std::printf("analog_ladder: 2 s of sweep written to %s, tanh within %.1e and algebraic within %.1e of the plain loop\n",
                path, off_tanh, off_algebraic);
    return 0;
}

namespace {

std::vector<double> reference(const std::vector<float>& x, const std::vector<float>& cutoff, double (*sat)(double)) {
    std::vector<double> y(x.size());
    double s[4] = {};
    for (std::size_t t = 0; t < x.size(); ++t) {
        const double g = 1.0 - std::exp(-2.0 * std::numbers::pi / kRate * cutoff[t]);
        double in = x[t] - kResonance * s[3];
        for (double& state : s) {
            state += g * (sat(in) - sat(state));
            in = state;
        }
        y[t] = s[3];
    }
    return y;
}

} // namespace
