// Tour 04: recurrences. A smoother and a resonant low-pass, each defined by its own past.
//
//   - `y[n] = … y[n - 1] …` defines an output by its past: a recurrence. Before the first sample its past is
//     silence, and each run carries it on from the last.
//   - The state need not be the output. A `Variable` is a value of the graph's own, defined the same way
//     (`w[n] = …`) and read like any other: the direct form II biquad below keeps `w` and puts out a sum of it.
//   - Params reach into the feedback: a biquad's five coefficients are worked out by the host for a cutoff and a Q
//     (here the RBJ cookbook's low-pass) and set as Params, so the filter is retuned between runs, not recompiled.
//   - The planner finds each feedback loop and runs it a sample at a time, with what lies between such loops; the
//     rest it runs in vector lanes, several samples at once.
//   - `Index n{}` is a sample clock with no rate: enough for lags of whole samples, `y[n - 1]` (tour 03 gave it one
//     to read durations).
//
// The two forms of the biquad are checked against each other and against a plain C++ loop in double.

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

// Closes the fraction `a` of the gap to x every sample.
constexpr Graph smoother() {
    Input x{"x", Signal{kSamples}};
    Param a{"a"};
    Output y{"y"};
    y[n] = y[n - 1] + a * (x[n] - y[n - 1]);
    return y;
}

// Direct form I: the output's own past and the input's.
constexpr Graph biquad() {
    Input x{"x", Signal{kSamples}};
    Param b0{"b0"};
    Param b1{"b1"};
    Param b2{"b2"};
    Param a1{"a1"};
    Param a2{"a2"};
    Output y{"y"};
    y[n] = b0 * x[n] + b1 * x[n - 1] + b2 * x[n - 2] - a1 * y[n - 1] - a2 * y[n - 2];
    return y;
}

// Direct form II: one state, `w`, fed back; the output reads it.
constexpr Graph biquad_ii() {
    Input x{"x", Signal{kSamples}};
    Param b0{"b0"};
    Param b1{"b1"};
    Param b2{"b2"};
    Param a1{"a1"};
    Param a2{"a2"};
    Variable w;
    w[n] = x[n] - a1 * w[n - 1] - a2 * w[n - 2];
    Output y{"y"};
    y = b0 * w + b1 * w[n - 1] + b2 * w[n - 2];
    return y;
}

struct Coefficients {
    double b0;
    double b1;
    double b2;
    double a1;
    double a2;
};

// The RBJ cookbook's low-pass, normalised so that a0 is 1.
Coefficients lowpass(double cutoff, double q) {
    const double w = 2.0 * std::numbers::pi * cutoff / kRate;
    const double alpha = std::sin(w) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    const double c = std::cos(w);
    return {(1.0 - c) / 2.0 / a0, (1.0 - c) / a0, (1.0 - c) / 2.0 / a0, -2.0 * c / a0, (1.0 - alpha) / a0};
}

// The Params hold floats, so the reference takes the coefficients as the program does.
Coefficients as_floats(Coefficients c) {
    const auto f = [](double v) { return static_cast<double>(static_cast<float>(v)); };
    return {f(c.b0), f(c.b1), f(c.b2), f(c.a1), f(c.a2)};
}

template <class Filter>
void tune(Filter& filter, Coefficients c) {
    filter.param("b0") = c.b0;
    filter.param("b1") = c.b1;
    filter.param("b2") = c.b2;
    filter.param("a1") = c.a1;
    filter.param("a2") = c.a2;
}

std::vector<double> reference(const std::vector<float>& x, Coefficients c);
double distance(const std::vector<float>& got, const std::vector<double>& want);

} // namespace

int main() {
    std::vector<float> x(kSamples, 0.0f);
    std::vector<float> smoothed(kSamples);
    std::vector<float> y(kSamples);
    std::vector<float> y_ii(kSamples);
    for (int t = 0; t < kSamples; t += 480) { // a click every 10 ms
        x[t] = 1.0f;
    }

    Executor<smoother> smooth;
    smooth.input("x") = x.data();
    smooth.output("y") = smoothed.data();
    smooth.param("a") = 0.01;
    smooth.run();
    double level = 0.0;
    double off_smooth = 0.0;
    for (int t = 0; t < kSamples; ++t) {
        level += 0.01 * (x[t] - level);
        const double d = std::fabs(smoothed[t] - level);
        off_smooth = d == d ? std::fmax(off_smooth, d) : INFINITY;
    }

    Executor<biquad> direct_i;
    Executor<biquad_ii> direct_ii;
    direct_i.input("x") = x.data();
    direct_i.output("y") = y.data();
    direct_ii.input("x") = x.data();
    direct_ii.output("y") = y_ii.data();
    double off = 0.0;
    for (const double cutoff : {400.0, 2000.0}) { // the same filters, retuned
        const Coefficients c = lowpass(cutoff, 4.0);
        tune(direct_i, c);
        tune(direct_ii, c);
        direct_i.reset(); // each tuning heard from silence, as the reference starts
        direct_ii.reset();
        direct_i.run();
        direct_ii.run();
        const std::vector<double> want = reference(x, as_floats(c));
        const double off_direct_i = distance(y, want);
        const double off_direct_ii = distance(y_ii, want);
        off = std::fmax(off, std::fmax(off_direct_i, off_direct_ii));
    }

    // At `Normal` a feedback loop keeps its state in double where that costs nothing (tour 13), as these do: they
    // land within about 2e-8 of the double loop. A float state would be about 1e-6 off where the resonance rings up
    // to 4; the bound allows either.
    if (!(off_smooth < 1e-6 && off < 1e-5)) {
        std::fprintf(stderr, "04_recurrences: %.3g (smoother) and %.3g (biquads) from the plain loops\n", off_smooth, off);
        return 1;
    }
    std::printf("04_recurrences: the smoother within %.1e, both biquads at two cutoffs within %.1e of the plain loop\n", off_smooth, off);
    return 0;
}

namespace {

std::vector<double> reference(const std::vector<float>& x, Coefficients c) {
    std::vector<double> y(x.size());
    double x1 = 0;
    double x2 = 0;
    double y1 = 0;
    double y2 = 0;
    for (std::size_t t = 0; t < x.size(); ++t) {
        y[t] = c.b0 * x[t] + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2;
        x2 = x1;
        x1 = x[t];
        y2 = y1;
        y1 = y[t];
    }
    return y;
}

// The largest difference; a NaN is infinitely far.
double distance(const std::vector<float>& got, const std::vector<double>& want) {
    double off = 0.0;
    for (std::size_t t = 0; t < want.size(); ++t) {
        const double d = std::fabs(got[t] - want[t]);
        off = d == d ? std::fmax(off, d) : INFINITY;
    }
    return off;
}

} // namespace
