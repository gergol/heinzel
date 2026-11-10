// Tour 05: composition. A three-band splitter: lows, mids and highs, which add up to the input again.
//
//   - A filter is a function from expressions to an expression. `lowpass` keeps its state in a `Variable` of its
//     own, so each call is a filter of its own, and two in a row make a steeper one.
//   - Its cutoff is an expression too: here a Param in Hz, turned into the filter's coefficient by the graph
//     (`1 - exp(-2 pi f / rate)`), so the host sets a frequency, not a coefficient.
//   - A graph with several results names each as an `Output` and is finished with all of them,
//     `finish(low, mid, high)`. What they share (here the input's two filterings) is computed once.
//   - An `Output` is read like any value once assigned: `mid = x - low - high`, what the other two leave, so the
//     three add up to the input by construction.
//
// The bands are checked against a plain C++ loop.

#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include "heinzel/dsl.hpp"

using namespace heinzel;

namespace {

constexpr int kRate = 48'000;
constexpr int kSamples = 9600;
constexpr Index n{};

// A one-pole low-pass at `cutoff` Hz.
constexpr Expr lowpass(Expr x, Expr cutoff) {
    Expr a = 1.0 - exp(-2.0 * std::numbers::pi / kRate * cutoff);
    Variable y;
    y[n] = y[n - 1] + a * (x[n] - y[n - 1]);
    return y;
}

constexpr Graph splitter() {
    Input x{"x", Signal{kSamples}};
    Param low_cut{"low_cut"};
    Param high_cut{"high_cut"};
    Output low{"low"};
    Output mid{"mid"};
    Output high{"high"};
    Expr low_one_pole = lowpass(x, low_cut);
    low = lowpass(low_one_pole, low_cut); // two poles: 12 dB an octave
    Expr below_high_one_pole = lowpass(x, high_cut);
    Expr below_high = lowpass(below_high_one_pole, high_cut);
    high = x - below_high;
    mid = x - low - high;
    return finish(low, mid, high);
}

struct Bands {
    std::vector<double> low;
    std::vector<double> mid;
    std::vector<double> high;
};

Bands reference(const std::vector<float>& x, double low_cut, double high_cut);

} // namespace

int main() {
    std::vector<float> x(kSamples);
    std::vector<float> low(kSamples);
    std::vector<float> mid(kSamples);
    std::vector<float> high(kSamples);
    for (int t = 0; t < kSamples; ++t) { // 100 Hz, 1 kHz and 10 kHz together
        const double s = static_cast<double>(t) / kRate;
        const double tone_100hz = std::sin(2 * std::numbers::pi * 100 * s);
        const double tone_1khz = std::sin(2 * std::numbers::pi * 1000 * s);
        const double tone_10khz = std::sin(2 * std::numbers::pi * 10'000 * s);
        x[t] = static_cast<float>(tone_100hz + 0.5 * tone_1khz + 0.25 * tone_10khz);
    }

    Executor<splitter> split;
    split.input("x") = x.data();
    split.output("low") = low.data();
    split.output("mid") = mid.data();
    split.output("high") = high.data();
    split.param("low_cut") = 300.0;
    split.param("high_cut") = 3000.0;
    split.run();

    const Bands want = reference(x, 300.0f, 3000.0f);
    double off = 0.0;
    for (int t = 0; t < kSamples; ++t) {
        const double off_low = std::fabs(low[t] - want.low[t]);
        const double off_mid = std::fabs(mid[t] - want.mid[t]);
        const double off_high = std::fabs(high[t] - want.high[t]);
        for (const double d : {off_low, off_mid, off_high}) {
            off = d == d ? std::fmax(off, d) : INFINITY;
        }
    }
    // The filters' `exp` is the program's own float core, and their states may be kept in double (tour 13): a few
    // float roundings on a signal of at most 1.75, measured 2.2e-7 on x86-64.
    if (!(off < 1e-5)) {
        std::fprintf(stderr, "05_composition: the bands are %.3g from the plain loop\n", off);
        return 1;
    }
    std::printf("05_composition: three bands within %.1e of the plain loop\n", off);
    return 0;
}

namespace {

// The same filters in double, their cutoffs the floats the Params hold.
Bands reference(const std::vector<float>& x, double low_cut, double high_cut) {
    const auto coefficient = [](double cutoff) { return 1.0 - std::exp(-2.0 * std::numbers::pi / kRate * cutoff); };
    const double a_low = coefficient(low_cut);
    const double a_high = coefficient(high_cut);
    double l1 = 0;
    double l2 = 0;
    double h1 = 0;
    double h2 = 0;
    Bands b{std::vector<double>(x.size()), std::vector<double>(x.size()), std::vector<double>(x.size())};
    for (std::size_t t = 0; t < x.size(); ++t) {
        l1 += a_low * (x[t] - l1);
        l2 += a_low * (l1 - l2);
        h1 += a_high * (x[t] - h1);
        h2 += a_high * (h1 - h2);
        b.low[t] = l2;
        b.high[t] = x[t] - h2;
        b.mid[t] = x[t] - b.low[t] - b.high[t];
    }
    return b;
}

} // namespace
