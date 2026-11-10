// Tour 03: the past. A smoother and a slapback echo, and a chorus, run block by block.
//
//   - `constexpr Index n{48'000};` is the sample clock, carrying its rate. `x[n]` is x now and `x[n - 2]` is x two
//     samples ago; a duration is folded to whole samples while compiling, so `x[n - 120.0 * ms]` reads 5760 samples
//     back (a duration that falls between two samples stops the build).
//   - A delay that changes, `x[n - d.at_most(25.0) * ms]`, reads x as it was `d` milliseconds ago, `d` any value,
//     between two samples the two blended. It says its longest distance, so the graph knows how much of x's past to
//     keep; `d` below 0 reads now, past the bound as far back as is kept.
//   - A graph is built by running C++: the loop in `smooth` writes one term per tap, so the five taps become the
//     five products of a hand-written FIR.
//   - A graph function may be a template. `slapback<N>` makes a graph for any block length.
//   - Every run continues where the last one stopped: the samples a graph reads in its past are kept from the run
//     before. So four runs of 4096 samples give the numbers of one run of 16384, and `reset()` goes back to
//     silence.

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

#include "heinzel/dsl.hpp"

using namespace heinzel;

namespace {

constexpr Index n{48'000};
static_assert(samples(n, 120.0 * ms) == 5760);

constexpr double kTaps[] = {0.1, 0.2, 0.4, 0.2, 0.1}; // a gentle low-pass, its gain 1 at DC

// The weighted sum of x now and over the last four samples.
constexpr Expr smooth(Expr x) {
    Expr sum = kTaps[0] * x[n];
    for (int k = 1; k < 5; ++k) {
        sum = sum + kTaps[k] * x[n - k];
    }
    return sum;
}

template <int N>
constexpr Graph slapback() {
    Input x{"x", Signal{N}};
    Output y{"y"};
    Expr dry = smooth(x);
    y = dry + 0.4 * dry[n - 120.0 * ms];
    return y;
}

// x and x as it was 5 to 25 ms ago, the distance swept by a slow sine: a chorus.
template <int N>
constexpr Graph chorus() {
    Input x{"x", Signal{N}};
    Variable phase;
    phase[n] = frac(phase[n - 1] + 1.0 / 65'536); // 0.73 turns a second, a step a float holds exactly
    Expr wobble = sinpi(2.0 * phase);
    Expr d = 15.0 + 10.0 * wobble; // ms
    Expr wet = x[n - d.at_most(25.0) * ms];
    Output y{"y"};
    y = 0.5 * x + 0.5 * wet;
    return y;
}

constexpr int kBlock = 4096;
constexpr int kBlocks = 4;
constexpr int kWhole = kBlock * kBlocks;

std::vector<double> reference(const std::vector<float>& x);
std::vector<double> chorus_reference(const std::vector<float>& x);
bool chorus_checks(const std::vector<float>& x);

} // namespace

int main() {
    std::vector<float> x(kWhole);
    std::vector<float> whole(kWhole);
    std::vector<float> blocks(kWhole);
    for (int t = 0; t < kWhole; ++t) { // clicks every 3000 samples on a low hum
        x[t] = (t % 3000 == 0 ? 1.0f : 0.0f) + 0.1f * std::sin(0.002f * static_cast<float>(t));
    }

    Executor<slapback<kWhole>> once;
    once.input("x") = x.data();
    once.output("y") = whole.data();
    once.run();

    Executor<slapback<kBlock>> streamed; // the same graph, a quarter as long: each run picks up where the last stopped
    for (int b = 0; b < kBlocks; ++b) {
        streamed.input("x") = x.data() + b * kBlock;
        streamed.output("y") = blocks.data() + b * kBlock;
        streamed.run();
    }
    for (int t = 0; t < kWhole; ++t) {
        if (blocks[t] != whole[t]) {
            std::fprintf(stderr, "03_the_past: in blocks, sample %d is %g; in one run, %g\n", t, blocks[t], whole[t]);
            return 1;
        }
    }

    // After `reset()` the past is silence again, as before the first run: the first block comes out as it did.
    std::vector<float> again(kBlock);
    streamed.reset();
    streamed.input("x") = x.data();
    streamed.output("y") = again.data();
    streamed.run();
    for (int t = 0; t < kBlock; ++t) {
        if (again[t] != whole[t]) {
            std::fprintf(stderr, "03_the_past: after reset, sample %d is %g, not %g\n", t, again[t], whole[t]);
            return 1;
        }
    }

    const std::vector<double> want = reference(x);
    for (int t = 0; t < kWhole; ++t) {
        if (!(std::fabs(whole[t] - want[t]) < 1e-6)) {
            std::fprintf(stderr, "03_the_past: sample %d is %g, the plain loop's %g\n", t, whole[t], want[t]);
            return 1;
        }
    }
    std::printf("03_the_past: %d samples in one run and in %d blocks alike, within 1e-6 of the plain loop\n", kWhole, kBlocks);
    return chorus_checks(x) ? 0 : 1;
}

namespace {

// Before the first sample everything is silence, so a read before the start is 0.
std::vector<double> reference(const std::vector<float>& x) {
    const auto at = [&](int t) { return t < 0 ? 0.0 : static_cast<double>(x[t]); };
    const auto dry = [&](int t) {
        double sum = 0.0;
        for (int k = 0; k < 5; ++k) {
            sum += kTaps[k] * at(t - k);
        }
        return sum;
    };
    std::vector<double> y(x.size());
    for (int t = 0; t < kWhole; ++t) {
        y[t] = dry(t) + 0.4 * dry(t - 5760);
    }
    return y;
}

// The chorus in one run and in blocks, against its plain loop: the phase steps, then x is read d ago, between two
// samples blended.
bool chorus_checks(const std::vector<float>& x) {
    std::vector<float> whole(kWhole);
    std::vector<float> blocks(kWhole);
    Executor<chorus<kWhole>> once;
    once.input("x") = x.data();
    once.output("y") = whole.data();
    once.run();
    Executor<chorus<kBlock>> streamed;
    for (int b = 0; b < kBlocks; ++b) {
        streamed.input("x") = x.data() + b * kBlock;
        streamed.output("y") = blocks.data() + b * kBlock;
        streamed.run();
    }
    const std::vector<double> want = chorus_reference(x);
    for (int t = 0; t < kWhole; ++t) {
        if (blocks[t] != whole[t] || !(std::fabs(whole[t] - want[t]) < 1e-4)) {
            std::fprintf(stderr, "03_the_past: the chorus's sample %d is %g in blocks, %g in one run, %g in the plain loop\n", t, blocks[t],
                         whole[t], want[t]);
            return false;
        }
    }
    std::printf("03_the_past: a chorus, 5 to 25 ms, in one run and in %d blocks alike, within 1e-4 of the plain loop\n", kBlocks);
    return true;
}

std::vector<double> chorus_reference(const std::vector<float>& x) {
    const auto at = [&](long t) { return t < 0 ? 0.0 : static_cast<double>(x[static_cast<std::size_t>(t)]); };
    std::vector<double> y(x.size());
    double phase = 0.0;
    for (int t = 0; t < kWhole; ++t) {
        phase += 1.0 / 65'536;
        phase -= std::floor(phase);
        const double d = (15.0 + 10.0 * std::sin(2.0 * 3.141592653589793 * phase)) * 48.0; // in samples
        const auto whole = static_cast<long>(d);
        const double f = d - static_cast<double>(whole);
        const double near = at(t - whole);
        const double far = at(t - whole - 1);
        y[t] = 0.5 * x[t] + 0.5 * (near + f * (far - near));
    }
    return y;
}

} // namespace
