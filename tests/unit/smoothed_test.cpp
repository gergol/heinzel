// `dsp::smoothed` (Plan/33 S3): a knob smoothed as Cmajor smooths one, a ramp in `frames` equal steps from where the
// output stands to the knob's new value, restarted where a run starts with the knob changed. The example: a gain that
// glides over four frames,
//
//   y = x * dsp::smoothed(gain, 4);
//
// its knob moving between runs. Walked in runs at `High` (the knob's feed changing where a run starts) it is the ramp,
// exactly; on `Executor` in the same runs, the knob set before each, the walk's numbers bit for bit, a run shorter than
// the ramp restarting it from where it got to; at `Normal` on zen2 within rounding of the walk.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#include "heinzel/dsp/smoothed.hpp"
#include "heinzel/executor.hpp"
#include "heinzel/schedule/machine.hpp"

#include "executed.hpp"
#include "interpret.hpp"

using namespace heinzel;

namespace {

constexpr Graph glided() {
    Input x{"x", Signal{Length{}}};
    Param gain{"gain", ge(0.0), le(1.0)};
    Output y{"y"};
    y = x * dsp::smoothed(gain, 4);
    return finish(y);
}

constexpr std::size_t kRuns = 6;
constexpr std::array<double, kRuns> kGains{0.5, 0.5, 1.0, 0.25, 0.25, 0.75};

struct Fed {
    std::vector<double> x;
    std::vector<double> gain; // a number a sample, as the walk reads a Param: the run's knob
};

// x[n] = `ones` ? 1 : 1 + n / 64, the knob kGains[k] through run k of `run_length` samples.
Fed fed(std::size_t run_length, bool ones) {
    Fed f{std::vector<double>(kRuns * run_length), std::vector<double>(kRuns * run_length)};
    for (std::size_t i = 0; i < f.x.size(); ++i) {
        f.x[i] = ones ? 1.0 : 1.0 + static_cast<double>(i) / 64.0;
        f.gain[i] = kGains[i / run_length];
    }
    return f;
}

// The walk over kRuns runs of `Run` samples, at `High`.
template <std::size_t Run>
std::vector<double> walked(const Fed& f) {
    constexpr std::size_t samples = kRuns * Run;
    const Graph g = testing::analysed<glided>(samples);
    Inputs feeds{};
    for (const NamedValue& in : g.inputs) {
        feeds.push_back(InputFeed{in.name.view() == "x" ? f.x.data() : f.gain.data(), 1});
    }
    constexpr std::size_t planes = testing::store_of<glided, samples>().planes;
    const auto out = std::make_unique<Signals<samples, planes>>();
    run(g, feeds, *out, Run);
    std::vector<double> y(samples);
    for (std::size_t i = 0; i < samples; ++i) {
        y[i] = sample_at(*out, g.outputs[0].value, 0, i);
    }
    return y;
}

// On `Executor<Build, M>` in the same runs, the knob set before each.
template <auto Build, const Machine& M>
std::vector<double> on_the_executor(const Fed& f, std::size_t run_length) {
    const auto ex = std::make_unique<Executor<Build, M>>();
    testing::held(*ex);
    const std::vector<float> x(f.x.begin(), f.x.end());
    std::vector<float> y(x.size());
    ex->prepare(static_cast<std::int64_t>(run_length));
    for (std::size_t k = 0; k < kRuns; ++k) {
        ex->param("gain") = kGains[k];
        ex->input("x") = x.data() + k * run_length;
        ex->output("y") = y.data() + k * run_length;
        ex->run(static_cast<std::int64_t>(run_length));
    }
    return std::vector<double>(y.begin(), y.end());
}

// With x at 1 the output is the ramp: from 0 to 0.5 in four steps of 0.125 and held; held through an unchanged run;
// to 1 in steps of 0.125; to 0.25 in steps of -0.1875 (every number exact in a float).
bool ramps() {
    const std::vector<double> y = walked<6>(fed(6, true));
    constexpr std::array<double, 24> kRamp{0.125, 0.25, 0.375, 0.5, 0.5,    0.5,   0.5,    0.5, 0.5,  0.5,  0.5, 0.5,
                                           0.625, 0.75, 0.875, 1.0, 1.0,    1.0,   0.8125, 0.625, 0.4375, 0.25, 0.25, 0.25};
    for (std::size_t i = 0; i < kRamp.size(); ++i) {
        if (y[i] != kRamp[i]) {
            std::fprintf(stderr, "smoothed: sample %zu walked to %.17g, not %.17g\n", i, y[i], kRamp[i]);
            return false;
        }
    }
    return true;
}

// At `Normal` on zen2, the program a plugin runs: within a float's rounding of the walk.
bool near_the_walk() {
    const Fed f = fed(3, false);
    const std::vector<double> walk = walked<3>(f);
    const std::vector<double> ran = on_the_executor<glided, zen2>(f, 3);
    for (std::size_t i = 0; i < walk.size(); ++i) {
        if (std::fabs(ran[i] - walk[i]) > 1e-6 * std::fabs(walk[i]) + 1e-7) {
            std::fprintf(stderr, "smoothed at Normal: sample %zu ran %.9g, walked %.9g\n", i, ran[i], walk[i]);
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    bool ok = ramps();
    // Runs of 6, longer than the ramp, and of 3, shorter: a change in the middle of a ramp starts the next from where it
    // got to.
    const Fed six = fed(6, false);
    testing::require_walked("y", on_the_executor<testing::at_high<glided>, default_machine>(six, 6), walked<6>(six));
    const Fed three = fed(3, false);
    testing::require_walked("y", on_the_executor<testing::at_high<glided>, zen2>(three, 3), walked<3>(three));
    ok = ok && near_the_walk();
    std::printf("smoothed: %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
