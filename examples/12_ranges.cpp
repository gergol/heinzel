// Tour 12: ranges. A level meter in decibels, which the compiler proves never takes the logarithm of 0, and a level
// window whose promise a checked build holds the host to.
//
//   - Every graph is held to a proof: no operation may receive a number outside its domain. No division by a range
//     that holds 0, no `sqrt` below 0, no `log` at or below 0. A graph that might is refused while it compiles, with a
//     message that says where (`static_assert` below).
//   - A port says what it may hold, last in its declaration: `Input x{"x", Signal{kSamples}, ge(-1), le(1)}`, the
//     converter's full scale; `Param speed{"speed", gt(0), le(1)}`. A port with no range may hold any number.
//   - The proof carries the ranges through the graph: `x * x` is a square, so at least 0, and in [0, 1] here.
//   - Where the graph knows more than the proof can see, it says so: `(…).promise(ge(0))`. On a recurrence's
//     definition that is a proof by induction. Each step maps "at least 0" into itself, so the meter's power is
//     proven, and its logarithm with it. (Write `speed * (x * x)`: `x * x` is known to be a square, `speed * x * x`
//     is `(speed * x) * x`, which is not, and the promise would be checked rather than proven.)
//   - A promise the proof cannot prove is checked after each run, in a checked build. An `Executor` is checked unless
//     `NDEBUG` is defined, as `assert` is, or as its third argument says: `Executor<window, default_machine, true>`
//     checks in every build. It also holds each ranged Param when set, and each ranged input before a run.
//   - A release build plans the graph as written without its ranges, so they cost nothing there.
//
// The meter is checked against a plain C++ loop, and the checked window's three refusals against their words.

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "heinzel/dsl.hpp"

using namespace heinzel;

namespace {

constexpr int kSamples = 4800;
constexpr Index n{};

// The mean square of x, followed with `speed`, in decibels of full scale.
constexpr Graph meter() {
    Input x{"x", Signal{kSamples}, ge(-1), le(1)};
    Param speed{"speed", gt(0), le(1)};
    Variable power;
    power[n] = ((1.0 - speed) * power[n - 1] + speed * (x * x)).promise(ge(0));
    Output db{"db"};
    db = 10.0 * log10(power + 1e-10);
    return db;
}

// The same meter without its floor of 1e-10: the power may be 0 (silence), and the logarithm of 0 is refused, in
// log's words: log10 is log times a constant.
constexpr Graph meter_without_floor() {
    Input x{"x", Signal{kSamples}, ge(-1), le(1)};
    Param speed{"speed", gt(0), le(1)};
    Variable power;
    power[n] = ((1.0 - speed) * power[n - 1] + speed * (x * x)).promise(ge(0));
    Output db{"db"};
    db = 10.0 * log10(power);
    return db;
}
static_assert(try_compile(meter_without_floor()).refusal.view() == "heinzel: log may see a number <= 0: its operand is ge(0)");

// x's level within a window from `low` to `high`, as 0 to 1. The host keeps `high` above `low`; the proof cannot
// know that, so the divisor is promised, and a checked build checks it each run.
constexpr Graph window() {
    Input x{"x", Signal{kSamples}, ge(-1), le(1)};
    Param low{"low", ge(0), le(1)};
    Param high{"high", ge(0), le(1)};
    Output level{"level"};
    level = (abs(x) - low) / (high - low).promise(gt(0));
    return level;
}

std::vector<float> fading_tone();
// The words a mistake is refused with, or none if it runs.
std::string refusal(auto&& mistake);
std::vector<double> reference(const std::vector<float>& x, double speed);

} // namespace

int main() {
    std::vector<float> x = fading_tone();
    std::vector<float> db(kSamples);
    Executor<meter> level_meter;
    level_meter.input("x") = x.data();
    level_meter.output("db") = db.data();
    level_meter.param("speed") = 0.01;
    level_meter.run();

    const std::vector<double> want = reference(x, 0.01f);
    double off = 0.0;
    for (int t = 0; t < kSamples; ++t) {
        const double d = std::fabs(db[t] - want[t]);
        off = d == d ? std::fmax(off, d) : INFINITY;
    }
    if (!(off < 1e-4)) { // in dB; measured 7.2e-6 on x86-64, the float `log` core's few roundings
        std::fprintf(stderr, "12_ranges: the meter is %.3g dB from the plain loop\n", off);
        return 1;
    }

    std::vector<float> level(kSamples);
    std::vector<float> loud = x;
    loud[7] = 1.5f; // past full scale
    Executor<window, default_machine, true> w;
    w.input("x") = x.data();
    w.output("level") = level.data();
    const std::string said[] = {
        refusal([&] { w.param("high") = 1.5; }),
        refusal([&] { // `high` below `low`: the run computes, then finds its promise broken
            w.param("low") = 0.75;
            w.param("high") = 0.25;
            w.run();
        }),
        refusal([&] {
            w.param("high") = 0.875;
            w.input("x") = loud.data();
            w.run();
        }),
        refusal([&] {
            w.input("x") = x.data();
            w.run();
        }),
    };
    const char* const kWords[] = {
        "heinzel::Executor: param high = 1.5 is outside ge(0), le(1)",
        "heinzel::Executor: promise #0 broken at element 0: -0.5 is outside gt(0)",
        "heinzel::Executor: input x[7] = 1.5 is outside ge(-1), le(1)",
        "",
    };
    for (int k = 0; k < 4; ++k) {
        if (said[k] != kWords[k]) {
            std::fprintf(stderr, "12_ranges: refused with \"%s\", not \"%s\"\n", said[k].c_str(), kWords[k]);
            return 1;
        }
    }
    std::printf("12_ranges: the meter within %.1e dB of the plain loop; a checked run refused a Param, a promise, an input\n", off);
    return 0;
}

namespace {

// Silence, then a tone fading in.
std::vector<float> fading_tone() {
    std::vector<float> x(kSamples);
    for (int t = 0; t < kSamples; ++t) {
        const float fade = static_cast<float>(t - 480) / kSamples;
        x[t] = t < 480 ? 0.0f : fade * std::sin(0.05f * static_cast<float>(t));
    }
    return x;
}

std::string refusal(auto&& mistake) {
    try {
        mistake();
        return "";
    } catch (const heinzel::error& e) {
        return std::string(e.message());
    }
}

std::vector<double> reference(const std::vector<float>& x, double speed) {
    std::vector<double> db(x.size());
    double power = 0.0;
    for (std::size_t t = 0; t < x.size(); ++t) {
        power = (1.0 - speed) * power + speed * x[t] * x[t];
        db[t] = 10.0 * std::log10(power + 1e-10);
    }
    return db;
}

} // namespace
