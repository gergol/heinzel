// The knob moving (`docs/sections-in-lanes.md`). `biquad8/heinzel-param-moving`'s graph, kEq8's eight sections with
// their 40 coefficients Params over a Length, on a release `Executor<…, zen2>` (the lanes, one skewed stage), over 64
// blocks of 256 samples of noise. Before every block band 4's five coefficients are set from a sweep (the probes':
// kEq8's 1 kHz peak, -3 dB, Q 0.9, moving from 250 Hz to 4 kHz exponentially, its 2 s laid over the 64 blocks, each
// block's design in double rounded to float as a host hands it); the other seven rest. Against the cascade in double
// following the same coefficients, from silence, it is at most twice as far as the program of the same graph rewritten
// without the lanes rule (the parallel form, the feedback sums and the state, the multiply-adds: `rewritten` before the
// lanes), planned by `planned::` and run in the same blocks: `rewrite_feedback_sums_test`'s measure.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <numbers>
#include <string_view>
#include <utility>
#include <vector>

#include "heinzel/compile.hpp"
#include "heinzel/executor.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/loops/workspace.hpp"
#include "heinzel/rewrite/parallel_form.hpp"
#include "heinzel/rewrite/priced.hpp"
#include "heinzel/rewrite/product_adds.hpp"
#include "heinzel/schedule/machine.hpp"
#include "heinzel/sema/analyze.hpp"
#include "heinzel/support/fail.hpp"

#include "param_arms_graphs.hpp"
#include "planned.hpp"

using namespace param_arms;

namespace {

constexpr std::size_t kBlock = 256;
constexpr std::size_t kBlocks = 64;
constexpr std::size_t kSamples = kBlock * kBlocks;
constexpr std::size_t kMoving = 4; // (kEq8's 1 kHz peak)

constexpr Graph biquad8_block() { return param_cascade<8, 0>(); }

// The graph analysed, without the ranges the proof would take (as `compile` hands it on), and rewritten as `rewritten`
// did before the lanes were switched on.
constexpr Graph without_the_lanes() {
    Graph g = biquad8_block();
    if (!infer(g).ok) {
        fail("param_moving_test: the graph does not analyse");
    }
    g.ranges.clear();
    const Graph form = rewrite::parallel_form(g, zen2);
    Graph out = rewrite::sums_and_state(form, zen2);
    rewrite::fuse_product_adds(out, zen2);
    return out;
}
constexpr Graph kWithout = without_the_lanes();
constexpr planned::Held kWithoutHeld{planned::program_of(kWithout, zen2)};
constexpr const loops::Program& kWithoutLanes = kWithoutHeld.program;

// The skewed stages of `p`.
constexpr std::int32_t skewed_in(const loops::Program& p) {
    std::int32_t count = 0;
    for (const loops::Stage& stage : p.stages) {
        count += stage.skewed ? 1 : 0;
    }
    return count;
}
static_assert(skewed_in(kWithoutLanes) == 0, "the cascade as before D5");

// Block j's moving band: the sweep's frequency at its first sample, designed in double, rounded to float.
Coefficients moving_peak(std::size_t j) {
    const double freq = 250.0 * std::pow(16.0, static_cast<double>(j) / static_cast<double>(kBlocks));
    const double w = std::numbers::pi * freq * (2.0 / kRate);
    const double alpha = std::sin(w) / (2.0 * 0.9);
    const double big = std::exp(-3.0 * (std::numbers::ln10 / 40.0));
    const double a0 = 1.0 + alpha / big;
    const double k = -2.0 * std::cos(w) / a0;
    const auto f = [](double v) { return static_cast<float>(v); };
    return {f((1.0 + alpha * big) / a0), f(k), f((1.0 - alpha * big) / a0), f(k), f((1.0 - alpha / big) / a0)};
}

// kEq8 as block j has it.
std::array<Coefficients, 8> bank_at(std::size_t j) {
    std::array<Coefficients, 8> bank = kEq8;
    bank[kMoving] = moving_peak(j);
    return bank;
}

// The cascade in direct form I in double over x, from silence, block j's coefficients `bank_at(j)`.
std::vector<double> reference(const std::vector<double>& x) {
    std::vector<double> y(x.size());
    std::array<std::array<double, 4>, 8> past{}; // x1, x2, y1, y2
    for (std::size_t i = 0; i < x.size(); ++i) {
        const std::array<Coefficients, 8> bank = bank_at(i / kBlock);
        double v = x[i];
        for (std::size_t k = 0; k < bank.size(); ++k) {
            const auto c = [&](std::size_t at) { return static_cast<double>(bank[k][at]); };
            std::array<double, 4>& t = past[k];
            const double o = c(0) * v + c(1) * t[0] + c(2) * t[1] - c(3) * t[2] - c(4) * t[3];
            t = {v, t[0], o, t[2]};
            v = o;
        }
        y[i] = v;
    }
    return y;
}

// The moving band's five Params set to `c`, as a host sets a knob's (the names are compile-time, hence the fold).
template <class E>
void set_moving_band(E& ex, const Coefficients& c) {
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        ((ex.param(kCoefficient[kMoving][I]) = static_cast<double>(c[I])), ...);
    }(std::make_index_sequence<5>{});
}

// The program a release `Executor` runs (the lanes), kEq8 set once, the moving band before every block: y.
std::vector<double> on_the_executor(const std::vector<double>& x) {
    using E = Executor<biquad8_block, zen2, false>;
    const auto ex = std::make_unique<E>();
    ex->prepare(kBlock);
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        ((ex->param(kCoefficient[I / 5][I % 5]) = static_cast<double>(kEq8[I / 5][I % 5])), ...);
    }(std::make_index_sequence<40>{});
    const std::vector<float> in(x.begin(), x.end());
    std::vector<float> y(x.size());
    for (std::size_t j = 0; j < kBlocks; ++j) {
        set_moving_band(*ex, moving_peak(j));
        ex->input("x") = in.data() + j * kBlock;
        ex->output("y") = y.data() + j * kBlock;
        ex->run(kBlock);
    }
    return std::vector<double>(y.begin(), y.end());
}

// The program without the lanes over its own workspace, each Param fed from `held` (section k's coefficient i at
// 5k + i), the moving band's set before every block: y.
std::vector<double> without_the_lanes_run(const std::vector<double>& x) {
    std::array<float, 40> held{};
    loops::Feeds feeds{};
    std::size_t x_at = 0;
    for (std::size_t i = 0; i < kWithout.inputs.size(); ++i) {
        const std::string_view name = kWithout.inputs[i].name.view();
        x_at = name == "x" ? i : x_at;
        for (std::size_t at = 0; at < held.size(); ++at) {
            feeds[i] = name == kCoefficient[at / 5][at % 5] ? &held[at] : feeds[i];
        }
    }
    for (std::size_t at = 0; at < held.size(); ++at) {
        held[at] = kEq8[at / 5][at % 5];
    }
    const std::vector<float> in(x.begin(), x.end());
    std::vector<double> y{};
    loops::Workspace<kWithoutLanes> work(static_cast<std::int64_t>(kBlock));
    for (std::size_t j = 0; j < kBlocks; ++j) {
        const Coefficients moving = moving_peak(j);
        std::copy(moving.begin(), moving.end(), held.begin() + 5 * kMoving);
        work.set_length(kBlock);
        feeds[x_at] = in.data() + j * kBlock;
        loops::run<kWithoutLanes>(feeds, work);
        const float* out = work.template data<float>(static_cast<std::size_t>(kWithoutLanes.outputs[0]));
        y.insert(y.end(), out, out + kBlock);
    }
    return y;
}

} // namespace

int main() {
    const std::unique_ptr<const CompileResult> lanes_program(new auto(try_compile(biquad8_block(), zen2, Bindings{}, false)));
    if (!lanes_program->refusal.view().empty() || skewed_in(lanes_program->compiled.program) != 1) {
        std::printf("param_moving_test: the Executor's program is not in lanes\n");
        return 1;
    }
    const std::vector<double> x = noise(kSamples);
    const std::vector<double> exact = reference(x);
    const std::vector<double> in_lanes = on_the_executor(x);
    const std::vector<double> as_before = without_the_lanes_run(x);
    const double lanes = off_by(in_lanes, exact);
    const double cascade = off_by(as_before, exact);
    std::printf("param_moving_test: band 4 moving over 64 blocks of 256, against the cascade in double: the lanes %.3g, "
                "without them %.3g\n",
                lanes, cascade);
    if (!(lanes <= 2 * cascade)) {
        std::printf("param_moving_test: the lanes are more than twice as far as the program without them\n");
        return 1;
    }
    return 0;
}
