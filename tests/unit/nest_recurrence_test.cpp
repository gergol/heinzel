// Feedback and lags on the loop-nest path, against the loops written by hand and against the reference walk.
//
// A value's past is read out of STORAGE: a lag is a `Load` some samples back, and the buffer it reads
// keeps that many zeroed samples in front of the block — silence before the block, asked of nobody per
// element. A lag of a variable defined LATER is feedback: everything from that lag to the definition is
// one stage whose time loop runs a sample at a time, with lanes across whatever else there is; what
// follows the cycle carries nothing and runs in lanes along time again.
//
//   bank        y[c,t] = x[c,t] + a * y[c,t-1]          11 voices fit one block of lane bodies: time runs
//                                                       INNERMOST and the state is handed on in registers
//   wide bank   the same, 43 voices                     more than a block holds: time outermost, the lanes
//                                                       inside it, the state read back out of the result
//   accumulator p[t] = frac(p[t-1] + inc[t]) ; y = p/2  one voice: the cycle serial, the halving beside it
//   coupled     u[t] = x[t] + v[t-1]/2 ; v[t] = u[t-1]/4 + v[t-1]/2 + u[t]   two variables in ONE cycle,
//                                                       and a lag of u that runs AFTER u is defined: a
//                                                       register would hold this sample's u by then
//   two poles   y[t] = x[t] + a1*y[t-1] - a2*y[t-2]     the silence is two samples deep
//   echo        a = 2x ; y[t] = a[t] + a[t-3]           no feedback: both stages in lanes along time
//   fir         y[t] = x[t] + x[t-1]/2 + x[t-3]/4        the past of an INPUT, which keeps no silence before
//                                                       its block: the first three samples run one at a
//                                                       time with the too-deep reads standing in silence,
//                                                       the rest in lanes along time
//   biquad      the same over 11 voices, + y[t-1]/4     a lagged input inside a feedback stage
//   by name     the two poles, doubled — read through the VARIABLE, which is its definition's number
//
// Every constant is exact in float, so an immediate is the same number on every path and all of it is
// exact — under `Normal` in float (one-step operations: `native_f32_is_exact`), under `High` via double.

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/sema/analyze.hpp"

#include "canaried.hpp"
#include "interpret.hpp"
#include "planned.hpp"
#include "tree_facts.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace heinzel;

namespace {

namespace loops = heinzel::loops;

constexpr std::int32_t kVoices = 11;
constexpr std::int32_t kManyVoices = 43;
constexpr std::int32_t kBlock = 24;
constexpr double kPole = 0.9375;
constexpr double kFirst = 0.5;
constexpr double kSecond = 0.25;
constexpr Length kSamples{.multiple_of = 1, .min = 1, .expect = 64};
constexpr Index n{};

constexpr Graph analysed(Graph g) {
    if (!infer(g).ok) {
        fail("nest_recurrence_test: a graph did not resolve");
    }
    return g;
}

// Over a run-time length, or over a literal block (which the reference walk needs).
constexpr Graph bank(numerics::Precision level, bool literal, std::int32_t voices = kVoices) {
    Expr x = literal ? Input{"x", Signal{voices, kBlock}} : Input{"x", Signal{voices, kSamples}};
    Variable y;
    y[n] = x + kPole * y[n - 1];
    Graph g = finish(Output{"y"} = y);
    set_default_precision(g, level);
    return analysed(g);
}

// A bank of no voices, which no declaration says (a signal's extents are at least one): the old input (DS-18).
constexpr Graph bank_of_none() {
    const DimDecl samples{0, DimConstraints{1, 1}};
    Expr x = detail::input("x", Shape{0, Extent{Dim{samples.id}}}, ElemType::F32, samples);
    Variable y;
    y[n] = x + kPole * y[n - 1];
    return analysed(finish(Output{"y"} = y));
}

constexpr Graph accumulator() {
    Input inc{"inc", Signal{kSamples}};
    Variable phase;
    phase[n] = frac(phase[n - 1] + inc);
    return analysed(finish(Output{"y"} = phase * 0.5));
}

// `by_name`: the result is twice the VARIABLE, taken before it had a definition — a read of the present
// that names the variable and not the node defining it.
constexpr Graph two_poles(bool by_name) {
    Input x{"x", Signal{kSamples}};
    Variable y;
    const Expr named = y;
    y[n] = x + kFirst * y[n - 1] - kSecond * y[n - 2];
    return analysed(finish(Output{"y"} = by_name ? named * 2.0 : y));
}

constexpr Graph coupled() {
    Input x{"x", Signal{kVoices, kBlock}};
    Variable u;
    Variable v;
    u[n] = x + kFirst * v[n - 1];
    v[n] = kSecond * u[n - 1] + kFirst * v[n - 1] + u;
    return analysed(finish(Output{"y"} = v));
}

constexpr Graph fir(bool with_feedback) {
    Expr x = with_feedback ? Input{"x", Signal{kVoices, kSamples}} : Input{"x", Signal{kSamples}};
    Expr taps = x + kFirst * x[n - 1] + kSecond * x[n - 3];
    if (!with_feedback) {
        return analysed(finish(Output{"y"} = taps));
    }
    Variable y;
    y[n] = taps + kSecond * y[n - 1];
    return analysed(finish(Output{"y"} = y));
}

constexpr Graph echo() {
    Expr a = Input{"x", Signal{kSamples}} * 2.0;
    return analysed(finish(Output{"y"} = a + a[n - 3]));
}

constexpr Graph kBankGraph = bank(numerics::Precision::Normal, false);
constexpr planned::Held kBankHeld{planned::program_of(kBankGraph, zen2)};
constexpr const loops::Program& kBank = kBankHeld.program;
constexpr Graph kWideGraph = bank(numerics::Precision::Normal, false, kManyVoices);
constexpr planned::Held kWideBankHeld{planned::program_of(kWideGraph, zen2)};
constexpr const loops::Program& kWideBank = kWideBankHeld.program;
constexpr planned::Held kBankBlockHeld{planned::program_of(bank(numerics::Precision::Normal, true), zen2)};
constexpr const loops::Program& kBankBlock = kBankBlockHeld.program;
constexpr planned::Held kBankHighHeld{planned::program_of(bank(numerics::Precision::High, true), zen2)};
constexpr const loops::Program& kBankHigh = kBankHighHeld.program;
constexpr planned::Held kAccumulatorHeld{planned::program_of(accumulator(), zen2)};
constexpr const loops::Program& kAccumulator = kAccumulatorHeld.program;
constexpr planned::Held kTwoPolesHeld{planned::program_of(two_poles(false), zen2)};
constexpr const loops::Program& kTwoPoles = kTwoPolesHeld.program;
constexpr planned::Held kByNameHeld{planned::program_of(two_poles(true), zen2)};
constexpr const loops::Program& kByName = kByNameHeld.program;
constexpr planned::Held kCoupledHeld{planned::program_of(coupled(), zen2)};
constexpr const loops::Program& kCoupled = kCoupledHeld.program;
constexpr planned::Held kFirHeld{planned::program_of(fir(false), zen2)};
constexpr const loops::Program& kFir = kFirHeld.program;
constexpr planned::Held kBiquadHeld{planned::program_of(fir(true), zen2)};
constexpr const loops::Program& kBiquad = kBiquadHeld.program;
constexpr planned::Held kEchoHeld{planned::program_of(echo(), zen2)};
constexpr const loops::Program& kEcho = kEchoHeld.program;

// Stage `s` runs in lanes from its outermost loop on: a strip with a Vector straight inside it.
constexpr bool laned(const loops::Program& p, std::size_t s) {
    const auto w = tree_facts::walks(p, s);
    return w.size() >= 2 && loops::is_strip(w[0]) && w[1].kind == loops::NodeKind::Vector && loops::node_at(p, w[1].parent) == w[0];
}

// The State register statement `k` of stage `s` reads its value from (-1: it reads storage).
constexpr std::int32_t reads_state(const loops::Program& p, std::size_t s, std::int32_t k) {
    for (std::int32_t n = p.stages[s].root + 1; n < tree_facts::subtree_end(p, s); ++n) {
        const loops::Node& node = loops::node_at(p, n);
        if (node.kind == loops::NodeKind::Compute && node.stmt == k && node.reads >= 0) {
            return node.reads;
        }
    }
    return -1;
}

// What was decided. The bank: a strip of sixteen voices, two bodies of eight lanes in it, and under the State
// register the time loop with the same two bodies — the open of the state before it.
constexpr auto kBankWalks = tree_facts::walks(kBank, 0);
static_assert(kBank.stages.size() == 1 && kBankWalks.size() == 6 && loops::is_strip(kBankWalks[0]) && tree_facts::is(kBankWalks[0], loops::NodeKind::Loop, 0, 16) &&
                  tree_facts::is(kBankWalks[3], loops::NodeKind::Loop, 1, 1) && kBankWalks[3].tail == loops::Tail::Clip &&
                  loops::node_at(kBank, kBankWalks[3].parent).kind == loops::NodeKind::Registers &&
                  tree_facts::is(kBankWalks[4], loops::NodeKind::Unroll, 0, 8) && tree_facts::is(kBankWalks[5], loops::NodeKind::Vector, 0, 8),
              "one stage: the voices in two lanes of eight, time innermost and a sample at a time");
static_assert(tree_facts::stmt(kBank, 0, 0).kind == loops::StmtKind::Load && tree_facts::stmt(kBank, 0, 0).in[0].back == 1 &&
                  reads_state(kBank, 0, 0) == 4 && loops::node_at(kBank, kBankWalks[3].parent).role == loops::Role::State &&
                  loops::node_at(kBank, kBankWalks[3].parent).stmt == 4,
              "the lag is a Load of the sample before, which is what the definition made a turn ago: handed on in a register");
constexpr auto kWideWalks = tree_facts::walks(kWideBank, 0);
static_assert(kWideWalks.size() == 3 && tree_facts::is(kWideWalks[0], loops::NodeKind::Loop, 1, 1) && kWideWalks[0].tail == loops::Tail::Clip &&
                  loops::is_strip(kWideWalks[1]) && tree_facts::is(kWideWalks[1], loops::NodeKind::Loop, 0, 8) && tree_facts::is(kWideWalks[2], loops::NodeKind::Vector, 0, 8) &&
                  tree_facts::count(kWideBank, 0, loops::NodeKind::Registers) == 0 && reads_state(kWideBank, 0, 0) < 0,
              "more voices than a block holds: time outermost, lanes inside, the state through the result buffer");
static_assert(kBank.buffers.size() == 2 && kBank.buffers[1].prefix == 1, "the input, and the result with one sample of silence before it");
static_assert(planned::schedule_of(bank_of_none(), zen2).stages[0].inner_from == 2,
              "a bank of no voices: time outermost, not a block of no bodies (review of loop-tree M3)");
static_assert(kAccumulator.stages.size() == 1 && tree_facts::walks(kAccumulator, 0).size() == 1 &&
                  tree_facts::is(tree_facts::walk(kAccumulator, 0, 0), loops::NodeKind::Loop, 0, 1) && !loops::is_strip(tree_facts::walk(kAccumulator, 0, 0)),
              "the cycle serial, and the halving after it in its stage: one multiply beside the chain costs less than a pass in lanes "
              "(Plan/23 04 4.3a, the trailing decision)");
static_assert(kTwoPoles.stages.size() == 1 && kTwoPoles.buffers[1].prefix == 2, "as much silence as the deepest read of the past");
static_assert(kByName.stages.size() == 1, "the doubling follows the cycle, in its stage (the trailing decision)");
static_assert(kEcho.stages.size() == 2 && laned(kEcho, 0) && laned(kEcho, 1) && kEcho.buffers[1].prefix == 3,
              "a lag of a finished buffer carries nothing: the stage that makes it closes first, and both run in lanes");

static_assert(kFir.stages.size() == 1 && kFir.buffers.size() == 2 && kFir.buffers[0].prefix == 0 && laned(kFir, 0),
              "the input is read where the caller left it — no copy, no prefix — and the taps run in lanes along time");

int g_wrong = 0;

std::vector<float> noise(std::size_t n, std::uint32_t seed) {
    std::vector<float> v(n);
    std::uint32_t state = seed;
    for (float& x : v) {
        state = state * 1664525u + 1013904223u;
        x = static_cast<float>(static_cast<std::int32_t>(state >> 8) % 2001 - 1000) * 0.00173f;
    }
    return v;
}

void expect(const char* what, std::size_t at, float got, float want) {
    if (!(got == want) && ++g_wrong <= 10) {
        std::printf("  %s [%zu]: got %a, want %a\n", what, at, static_cast<double>(got), static_cast<double>(want));
    }
}

// Run TWICE in one workspace, `reset` between: the second run starts from silence as the first did (without
// it, it would continue the first: Plan/archive/14-dsl/05).
template <const loops::Program& P>
const float* run_twice(canaried::Workspace<P>& work, const std::vector<float>& x) {
    canaried::run<P>(loops::Feeds{x.data()}, work);
    work.reset();
    canaried::run<P>(loops::Feeds{x.data()}, work);
    return work.template data<float>(static_cast<std::size_t>(P.outputs[0]));
}

template <const loops::Program& P, std::size_t Voices>
void check_bank(const char* what, std::int64_t n) {
    const std::vector<float> x = noise(Voices * static_cast<std::size_t>(n), 1);
    canaried::Workspace<P> work(loops::Sizes{n});
    const float* y = run_twice<P>(work, x);
    for (std::size_t c = 0; c < Voices; ++c) {
        float before = 0.0f;
        for (std::size_t t = 0; t < static_cast<std::size_t>(n); ++t) {
            const float scaled = static_cast<float>(kPole) * before;
            before = x[t * Voices + c] + scaled;
            expect(what, t * Voices + c, y[t * Voices + c], before);
        }
    }
}

void check_accumulator(std::int64_t n) {
    std::vector<float> inc = noise(static_cast<std::size_t>(n), 2);
    canaried::Workspace<kAccumulator> work(loops::Sizes{n});
    const float* y = run_twice<kAccumulator>(work, inc);
    float phase = 0.0f;
    for (std::size_t t = 0; t < static_cast<std::size_t>(n); ++t) {
        const float sum = phase + inc[t];
        phase = sum - std::floor(sum);
        expect("accumulator", t, y[t], phase * 0.5f);
    }
}

template <const loops::Program& P>
void check_two_poles(std::int64_t n, float times) {
    const std::vector<float> x = noise(static_cast<std::size_t>(n), 3);
    canaried::Workspace<P> work(loops::Sizes{n});
    const float* y = run_twice<P>(work, x);
    float one = 0.0f;
    float two = 0.0f;
    for (std::size_t t = 0; t < static_cast<std::size_t>(n); ++t) {
        const float first = static_cast<float>(kFirst) * one;
        const float second = static_cast<float>(kSecond) * two;
        const float sum = x[t] + first;
        two = one;
        one = sum - second;
        expect("two poles", t, y[t], one * times);
    }
}

void check_coupled() {
    const std::vector<float> x = noise(static_cast<std::size_t>(kVoices * kBlock), 6);
    canaried::Workspace<kCoupled> work(loops::Sizes{});
    const float* y = run_twice<kCoupled>(work, x);
    for (std::size_t c = 0; c < kVoices; ++c) {
        float u = 0.0f;
        float v = 0.0f;
        for (std::size_t t = 0; t < kBlock; ++t) {
            const float half_v = static_cast<float>(kFirst) * v;
            const float now = x[t * kVoices + c] + half_v;
            const float quarter_u = static_cast<float>(kSecond) * u;
            const float sum = quarter_u + half_v;
            u = now;
            v = sum + now;
            expect("coupled", t * kVoices + c, y[t * kVoices + c], v);
        }
    }
}

template <const loops::Program& P, std::size_t Voices>
void check_fir(const char* what, std::int64_t n, bool with_feedback) {
    const std::vector<float> x = noise(Voices * static_cast<std::size_t>(n), 7);
    canaried::Workspace<P> work(loops::Sizes{n});
    const float* y = run_twice<P>(work, x);
    for (std::size_t c = 0; c < Voices; ++c) {
        float before = 0.0f;
        for (std::size_t t = 0; t < static_cast<std::size_t>(n); ++t) {
            const float one = t >= 1 ? x[(t - 1) * Voices + c] : 0.0f;
            const float three = t >= 3 ? x[(t - 3) * Voices + c] : 0.0f;
            const float half = static_cast<float>(kFirst) * one;
            const float quarter = static_cast<float>(kSecond) * three;
            const float sum = x[t * Voices + c] + half;
            float now = sum + quarter;
            if (with_feedback) {
                const float fed_back = static_cast<float>(kSecond) * before;
                now = now + fed_back;
            }
            before = now;
            expect(what, t * Voices + c, y[t * Voices + c], now);
        }
    }
}

void check_echo(std::int64_t n) {
    const std::vector<float> x = noise(static_cast<std::size_t>(n), 4);
    canaried::Workspace<kEcho> work(loops::Sizes{n});
    const float* y = run_twice<kEcho>(work, x);
    for (std::size_t t = 0; t < static_cast<std::size_t>(n); ++t) {
        expect("echo", t, y[t], x[t] * 2.0f + (t >= 3 ? x[t - 3] * 2.0f : 0.0f));
    }
}

// The same bank over a literal block, against the reference walk — which says what a variable, its
// definition and the silence before the block MEAN, under both precisions.
template <const loops::Program& P>
void check_against_the_walk(const char* what, numerics::Precision level) {
    const std::vector<float> x = noise(static_cast<std::size_t>(kVoices * kBlock), 5);
    const std::vector<double> widened(x.begin(), x.end());
    const Graph g = bank(level, true);
    Inputs feeds{};
    feeds.push_back(InputFeed{widened.data(), kVoices});
    const auto out = std::make_unique<Signals<kBlock>>();
    run(g, feeds, *out);
    canaried::Workspace<P> work(loops::Sizes{});
    const float* y = run_twice<P>(work, x);
    for (std::size_t t = 0; t < kBlock; ++t) {
        for (std::size_t c = 0; c < kVoices; ++c) {
            expect(what, t * kVoices + c, y[t * kVoices + c], static_cast<float>(sample_at(*out, g.outputs[0].value, static_cast<std::int32_t>(c), t)));
        }
    }
}

// What has no address here yet is refused by name, and `loops::lowers` says so first: feedback through a
// fold. And a planned schedule edited by hand is held to the rule the planner keeps: a stage that reads its own
// past does not run time in lanes.
constexpr Graph through_a_fold() {
    Input x{"x", Signal{kVoices, kBlock}};
    Variable y;
    y[n] = x + y[n - 1].sum(0) * 0.5;
    return analysed(finish(Output{"y"} = y));
}
static_assert(planned::refusal_of(through_a_fold(), zen2) == "heinzel::compile: feedback through anything but maps of one shape is not lowered yet" &&
                  !loops::lowers(through_a_fold(), zen2),
              "feedback through a fold must be refused by name");

constexpr loops::Schedule cycle_in_lanes() {
    loops::Schedule d = planned::schedule_of(accumulator(), zen2);
    d.stages[0].loops[0] = loops::Schedule::Loop{.axis = 0, .step = 8, .kind = loops::LoopKind::Vector};
    d.stages[0].inner_from = 1; // (time in lanes is no inner loop)
    return d;
}
static_assert(planned::refusal_of(accumulator(), zen2).empty() &&
                  planned::refusal_of(accumulator(), cycle_in_lanes()) == "heinzel::compile: a group that reads its own past walks time one sample at a time, in order",
              "a schedule that does not walk a feedback stage's time in order must be refused by name");

} // namespace

int main() {
    for (const std::int64_t n : {1, 2, 3, 50}) {
        check_bank<kBank, kVoices>("bank", n);
        check_bank<kWideBank, kManyVoices>("wide bank", n);
        check_accumulator(n);
        check_two_poles<kTwoPoles>(n, 1.0f);
        check_two_poles<kByName>(n, 2.0f);
        check_echo(n);
        check_fir<kFir, 1>("fir", n, false);
        check_fir<kBiquad, kVoices>("biquad", n, true);
    }
    check_coupled();
    check_against_the_walk<kBankBlock>("bank against the walk", numerics::Precision::Normal);
    check_against_the_walk<kBankHigh>("bank against the walk, High", numerics::Precision::High);
    if (g_wrong != 0) {
        std::printf("nest_recurrence_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("nest_recurrence_test: feedback and lags agree with the loops by hand and with the walk\n");
    return 0;
}
