// Feedback cycles that feed each other at the present sample share one stage (Decision 65) where the cost model prices
// it cheaper (Plan/23 04, 4.3a). A cascade of sections runs as its first feed-forward part in lanes, then one stage
// whose time loop holds its sections, so the processor overlaps their chains instead of waiting for each in turn, as
// long as one sample's path through them fits the out-of-order window. What a cycle reads of the stage through cheap
// maps joins with it; anything else stays apart, as before. Planned as written (no rewrite: each sum left to right):
//
//   cascade k       s_k[n] = c0 s_{k-1} + c1 s_{k-1}[n-1] - c2 s_k[n-1] + c3 s_k[n-2]   1, 2 sections: one stage,
//                   the feed-forward part in it (it costs a light stage nothing: the cost model, `cost_corpus6.cpp`);
//                   4: the feed-forward part in lanes, then one feedback stage (four sections' issue meets their chain)
//   long cascade    8 sections: two feedback stages (one sample's path, 93 cycles deep, outgrows the window: one stage
//                   38.7 cycles a sample, two 29.5, `Plan/probes/audio-iir/cost_corpus4.cpp`); 12: three; no cycle split
//   tanh between    section 1 reads tanh(s_0): an expensive map keeps its lanes, the two cycles apart
//   from the feed   section 1 also adds half the input, a map of the feed alone that a later section reads: the chain
//                   breaks there
//   bank            16 voices of a two-section cascade: one feedback stage, its feed in it, its lanes across the voices
//   doubled         the last section doubled: the doubling runs after the cycles, in lanes along time
//   a sum, then a   a bank's sum joined to its sample (4.3g) keeps a sample the planner lays out (the review of P4):
//   double          a double after it (a gain), beside it (a damped comb) or widened beside it (a one-pole at Normal)
//                   stays apart where it would halve the lanes past four lane bodies
//
// Checked against loops by hand at `Normal`, exactly: every constant is exact in float and every operation rounds once,
// so the stages compute what the hand loop does however they are split. And against the reference walk at `High`, bit
// for bit, over a literal block. And with the sums in the orders the rewrite chooses between (Plan 25, step 0.5b),
// each fused as the build rounds a multiply-add: two sections with each one's own newest state last (B), four with the
// section before last (F), as zen2's prices choose them; and so again with each section's `c2` a Param, its subtracted
// product fused as a multiply-add of the Param negated (Plan 25, step 2), exact, so the same numbers.

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/rewrite/rewritten.hpp"
#include "heinzel/runtime/hardware_twins.hpp"
#include "heinzel/sema/analyze.hpp"

#include "canaried.hpp"
#include "interpret.hpp"
#include "planned.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <string_view>
#include <vector>

using namespace heinzel;

namespace {

namespace loops = heinzel::loops;

constexpr std::size_t kMaxSections = 12;
constexpr std::int32_t kBlock = 24;
constexpr std::int32_t kVoices = 16;
constexpr Length kSamples{.multiple_of = 1, .min = 1, .expect = 64};
constexpr Index n{};

struct Coeffs {
    double c0;
    double c1;
    double c2;
    double c3;
};
// Stable sections, every coefficient exact in float.
constexpr std::array<Coeffs, kMaxSections> kC{{{0.5, 0.25, 0.375, -0.125},
                                               {0.75, -0.25, -0.25, 0.125},
                                               {0.625, 0.125, 0.5, -0.25},
                                               {0.5, -0.375, -0.125, 0.0625},
                                               {0.875, 0.0625, 0.25, -0.125},
                                               {0.375, 0.25, -0.375, -0.0625},
                                               {0.5, 0.5, 0.125, 0.25},
                                               {0.25, -0.125, 0.625, -0.25},
                                               {0.75, 0.125, -0.5, -0.125},
                                               {0.5, -0.25, 0.25, 0.125},
                                               {0.625, 0.375, -0.25, 0.0625},
                                               {0.5, 0.125, 0.375, -0.1875}}};

enum class Between : std::uint8_t { Nothing, Tanh, FromTheFeed };

struct Form {
    std::size_t sections{4};
    Between between{Between::Nothing};
    std::int32_t voices{1};
    bool doubled{false};
    bool params{false}; // each section's c2 a Param, `a<k>`, fed c2's value
};
constexpr std::array<std::string_view, 4> kParamNames{"a0", "a1", "a2", "a3"};

constexpr Graph analysed(Graph g) {
    if (!infer(g).ok) {
        fail("nest_feedback_chain_test: a graph did not resolve");
    }
    return g;
}

// The chain over a run-time length at `Normal`, or over a literal block at `level` (which the walk needs).
constexpr Graph chain(Form form, bool literal = false, numerics::Precision level = numerics::Precision::Normal) {
    Expr x = form.voices > 1 ? (literal ? Input{"x", Signal{form.voices, kBlock}} : Input{"x", Signal{form.voices, kSamples}})
                             : (literal ? Input{"x", Signal{kBlock}} : Input{"x", Signal{kSamples}});
    std::array<Variable, kMaxSections> y{};
    Expr s = x;
    for (std::size_t k = 0; k < form.sections; ++k) {
        const Coeffs& c = kC[k];
        const auto written = [&] {
            if (form.params) {
                const Param c2{kParamNames[k]};
                return c.c0 * s + c.c1 * s[n - 1] - c2 * y[k][n - 1] + c.c3 * y[k][n - 2];
            }
            return c.c0 * s + c.c1 * s[n - 1] - c.c2 * y[k][n - 1] + c.c3 * y[k][n - 2];
        };
        s = k == 1 && form.between == Between::Tanh ? tanh(s) : s;
        if (k == 1 && form.between == Between::FromTheFeed) {
            const Expr half = x * 0.5; // (made before the section's nodes, so it is no node of its cycle)
            y[k][n] = written() + half;
        } else {
            y[k][n] = written();
        }
        s = y[k];
    }
    Graph g = finish(Output{"y"} = form.doubled ? s * 2.0 : s);
    set_default_precision(g, level);
    return analysed(g);
}

// The stage of `s` that holds node `ni` (-1: none, a node with no statement).
constexpr std::int32_t stage_of(const loops::Schedule& s, std::int32_t ni) {
    for (std::size_t k = 0; k < s.stages.size(); ++k) {
        for (const std::int32_t m : s.stages[k].nodes) {
            if (m == ni) {
                return static_cast<std::int32_t>(k);
            }
        }
    }
    return -1;
}

// The stages that hold a node of a cycle, and whether every cycle lies whole in one of them.
struct Cycles {
    std::int32_t stages{0};
    bool whole{true};
};
constexpr Cycles cycles_of(const Graph& g, const loops::Schedule& s) {
    const loops::graph::Facts facts = loops::graph::facts_of(g);
    const loops::graph::Feedback feedback = loops::graph::find_feedback(g, facts);
    Cycles out{};
    std::vector<char> holds(s.stages.size(), 0);
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        const std::int32_t at = stage_of(s, static_cast<std::int32_t>(i));
        const std::int32_t opened = feedback.cycle[i];
        if (opened < 0 || at < 0) {
            continue;
        }
        const std::int32_t first = stage_of(s, opened);
        out.whole = out.whole && (first < 0 || first == at);
        out.stages += holds[static_cast<std::size_t>(at)] ? 0 : 1;
        holds[static_cast<std::size_t>(at)] = 1;
    }
    return out;
}

constexpr loops::Schedule plan(Form form) { return planned::schedule_of(chain(form), zen2); }

// Stage `k` walks its first loop in lanes along axis `axis`.
constexpr bool laned(const loops::Schedule& s, std::size_t k, std::int32_t axis) {
    return !s.stages[k].loops.empty() && s.stages[k].loops[0].kind == loops::LoopKind::Vector && s.stages[k].loops[0].axis == axis;
}

constexpr loops::Schedule kOne = plan({.sections = 1});
constexpr loops::Schedule kTwo = plan({.sections = 2});
constexpr loops::Schedule kFour = plan({.sections = 4});
constexpr loops::Schedule kEight = plan({.sections = 8});
constexpr loops::Schedule kTwelve = plan({.sections = 12});
static_assert(kOne.stages.size() == 1 && kTwo.stages.size() == 1 && kFour.stages.size() == 2,
              "one and two sections: one feedback stage, the feed-forward part in it; four: the feed-forward part first, in lanes");
static_assert(laned(kFour, 0, 0) && !laned(kFour, 1, 0) && cycles_of(chain({.sections = 4}), kFour).stages == 1,
              "the feed-forward part in lanes, every section's cycle in the one serial stage");
static_assert(cycles_of(chain({.sections = 8}), kEight).stages == 2 && cycles_of(chain({.sections = 8}), kEight).whole &&
                  cycles_of(chain({.sections = 12}), kTwelve).stages == 3 && cycles_of(chain({.sections = 12}), kTwelve).whole,
              "eight sections as written outgrow the window: a second feedback stage takes the rest (twelve: a third), "
              "and no cycle is split");

constexpr Form kTanh{.sections = 2, .between = Between::Tanh};
constexpr Form kFromTheFeed{.sections = 2, .between = Between::FromTheFeed};
constexpr loops::Schedule kTanhPlan = plan(kTanh);
constexpr loops::Schedule kFromTheFeedPlan = plan(kFromTheFeed);
static_assert(cycles_of(chain(kTanh), kTanhPlan).stages == 2 && cycles_of(chain(kTanh), kTanhPlan).whole,
              "a tanh between two sections keeps its lanes: the two cycles stay apart");
static_assert(cycles_of(chain(kFromTheFeed), kFromTheFeedPlan).stages == 1,
              "a map of the feed alone that a later section reads chains on with it, the model's call: work into a cycle "
              "needs no relation to the stage it joins (Plan/23 04 4.3g)");

constexpr Form kBank{.sections = 2, .voices = kVoices};
constexpr Form kDoubled{.sections = 2, .doubled = true};
constexpr loops::Schedule kBankPlan = plan(kBank);
constexpr loops::Schedule kDoubledPlan = plan(kDoubled);
static_assert(kBankPlan.stages.size() == 1 && kBankPlan.stages[0].loops[0].kind == loops::LoopKind::Vector &&
                  kBankPlan.stages[0].loops[0].axis == 0,
              "a bank of a cascade: one feedback stage, its feed-forward part in it, its lanes across the voices");
static_assert(kDoubledPlan.stages.size() == 1,
              "what follows the last cycle runs in its stage, one multiply beside two sections (the trailing decision, Plan/23 04 4.3a)");

// A bank's sum in the bank's sample lays the sample out in four lane bodies or none (`plan_feedback_loops`), and a
// double joining after it halves the lanes: 17 float voices fit four bodies of eight floats, not of four doubles. The
// planner refused its own stage ("a fold in a cycle takes a literal count of voices, four lane bodies at most"), on
// zen2 from 17 to 32 voices, on 16-byte vectors from 9 to 16, and at Normal, where double state widened a promised
// one-pole that then joined, `compile` planned the graph as written, without double state. Now every join asks the
// planner's predicate (`planner/chains.hpp` `sample_fits`): the sum stays in the sample, the double apart.
template <std::size_t V>
constexpr std::array<float, V> ramp(float from, float step) {
    std::array<float, V> c{};
    for (std::size_t v = 0; v < V; ++v) {
        c[v] = from + step * static_cast<float>(v);
    }
    return c;
}
template <std::size_t V>
constexpr Expr bank_of(Expr u, Variable& b, bool second) {
    const Expr first = constant(ramp<V>(0.1f, 0.01f)) * u + constant(ramp<V>(0.5f, 0.02f)) * b[n - 1];
    b[n] = second ? first + constant(ramp<V>(-0.1f, 0.01f)) * b[n - 2] : first;
    return Expr(b).sum(0);
}
template <std::size_t V>
constexpr Graph bank_times_gain() { // (the double after the sum: the work after the bank's cycles)
    Input x{"x", Signal{kSamples}, f32};
    Input gain{"gain", Signal{kSamples}, f64};
    Variable b;
    return analysed(finish(Output{"y"} = bank_of<V>(x, b, false) * gain));
}
template <std::size_t V>
constexpr Graph bank_beside_comb() { // (a cycle in double over the sum's shape, chaining on)
    Input x{"x", Signal{kSamples}, f32};
    Input w{"w", Signal{kSamples}, f64};
    Variable b;
    Variable c;
    const Expr sum = bank_of<V>(x, b, true);
    c[n] = w + 0.43 * (0.6 * c[n - 8] + 0.4 * c[n - 9]);
    Graph g = finish(Output{"s"} = sum, Output{"o"} = c);
    set_default_precision(g, numerics::Precision::High);
    return analysed(g);
}
template <std::size_t V>
constexpr Graph bank_beside_pole() { // (at Normal: double state widens the one-pole, Normal's promise)
    Input x{"x", Signal{kSamples}, f32};
    Variable b;
    Variable y;
    const Expr u = x * 0.5;
    const Expr sum = bank_of<V>(u, b, true); // (the bank first: the one-pole chains onto its stage)
    y[n] = 0.28 * u + 0.72 * y[n - 1];
    return analysed(finish(Output{"a"} = y * 0.75, Output{"c"} = sum * 0.25));
}

// The stage of `g`'s schedule for `m` holding its fold holds a cycle too and makes no double.
constexpr bool sum_in_the_sample(const Graph& g, const Machine& m) {
    const loops::Schedule s = planned::schedule_of(g, m);
    const loops::graph::Facts facts = loops::graph::facts_of(g);
    const loops::graph::Feedback feedback = loops::graph::find_feedback(g, facts);
    for (const loops::Schedule::Stage& stage : s.stages) {
        bool fold = false;
        bool cycle = false;
        bool wide = false;
        for (const std::int32_t ni : stage.nodes) {
            const Node& node = g.nodes[static_cast<std::size_t>(ni)];
            fold = fold || loops::graph::is_a_fold(node);
            cycle = cycle || feedback.cycle[static_cast<std::size_t>(ni)] >= 0;
            wide = wide || g.values[static_cast<std::size_t>(node.output)].type == ElemType::F64;
        }
        if (fold) {
            return cycle && !wide;
        }
    }
    return false;
}
// `g` rewritten for `m` holds a double and plans: `compile` keeps it.
constexpr bool keeps_double_state(const Graph& g, const Machine& m) {
    const Graph rewritten = rewrite::rewritten(g, m);
    bool wide = false;
    for (const Value& v : rewritten.values) {
        wide = wide || v.type == ElemType::F64;
    }
    return wide && loops::lowers(rewritten, m);
}

static_assert(planned::refusal_of(bank_times_gain<17>(), zen2).empty() && planned::refusal_of(bank_times_gain<32>(), zen2).empty() &&
                  planned::refusal_of(bank_times_gain<9>(), default_machine).empty() && sum_in_the_sample(bank_times_gain<17>(), zen2),
              "a double gain after a bank's sum: the sum in the bank's sample, the gain apart");
static_assert(planned::refusal_of(bank_beside_comb<12>(), default_machine).empty() && sum_in_the_sample(bank_beside_comb<12>(), default_machine),
              "a damped comb in double beside a bank's sum stays apart where it would halve the lanes");
static_assert(keeps_double_state(bank_beside_pole<24>(), zen2) && keeps_double_state(bank_beside_pole<11>(), default_machine),
              "at Normal the promised one-pole in double beside a bank's sum: the rewritten graph plans");

constexpr planned::Held kOneHeld{planned::program_of(chain({.sections = 1}), zen2)};
constexpr planned::Held kFourHeld{planned::program_of(chain({.sections = 4}), zen2)};
constexpr planned::Held kEightHeld{planned::program_of(chain({.sections = 8}), zen2)};
constexpr planned::Held kTwelveHeld{planned::program_of(chain({.sections = 12}), zen2)};
constexpr planned::Held kFromTheFeedHeld{planned::program_of(chain(kFromTheFeed), zen2)};
constexpr planned::Held kBankHeld{planned::program_of(chain(kBank), zen2)};
constexpr planned::Held kDoubledHeld{planned::program_of(chain(kDoubled), zen2)};
constexpr planned::Held kTwoOrdered{planned::program_of(rewrite::ordered_feedback_sums(chain({.sections = 2}), zen2), zen2)};
constexpr planned::Held kFourOrdered{planned::program_of(rewrite::ordered_feedback_sums(chain({.sections = 4}), zen2), zen2)};
constexpr Form kTwoParams{.sections = 2, .params = true};
constexpr Form kFourParams{.sections = 4, .params = true};
constexpr planned::Held kTwoParamsOrdered{planned::program_of(rewrite::ordered_feedback_sums(chain(kTwoParams), zen2), zen2)};
constexpr planned::Held kFourParamsOrdered{planned::program_of(rewrite::ordered_feedback_sums(chain(kFourParams), zen2), zen2)};
constexpr Graph kTwoParamsRe = rewrite::ordered_feedback_sums(chain(kTwoParams), zen2);
static_assert(planned::schedule_of(kTwoParamsRe, zen2).stages.size() ==
                      planned::schedule_of(rewrite::ordered_feedback_sums(chain({.sections = 2}), zen2), zen2).stages.size() + 2 &&
                  cycles_of(kTwoParamsRe, planned::schedule_of(kTwoParamsRe, zen2)).stages == 1,
              "two sections by Params: each Param negated first, in a stage of no time of its own (work ahead), then the stages "
              "of the constants' cascade (the negation placed with its chain cut a feed off its cycle: `biquad3ch`'s Param rows "
              "at C8 on zen2)");

int g_wrong = 0;

std::vector<float> noise(std::size_t count, std::uint32_t seed) {
    std::vector<float> v(count);
    std::uint32_t state = seed;
    for (float& x : v) {
        state = state * 1664525u + 1013904223u;
        x = static_cast<float>(static_cast<std::int32_t>(state >> 8) % 2001 - 1000) * 0.00173f;
    }
    return v;
}

// How a section's sum is evaluated: as written; the newest own state last, fused (B, the first section's order in
// each); the section before last, fused (F, Decision 66).
enum class Order : std::uint8_t { Written, OwnLast, BeforeLast };

float section(Order order, const Coeffs& c, float s, const std::array<float, 3>& past, bool first) {
    const auto f = [](double d) { return static_cast<float>(d); };
    if (order == Order::Written) {
        return ((f(c.c0) * s + f(c.c1) * past[0]) - f(c.c2) * past[1]) + f(c.c3) * past[2];
    }
    if (order == Order::OwnLast || first) {
        return multiply_add_as_the_machine_rounds(-f(c.c2), past[1], (f(c.c0) * s + f(c.c1) * past[0]) + f(c.c3) * past[2]);
    }
    return multiply_add_as_the_machine_rounds(f(c.c0), s, (f(c.c1) * past[0] + f(c.c3) * past[2]) - f(c.c2) * past[1]);
}

// The chain by hand, in float, in `order`; `x` sample after sample, `voices` numbers a sample.
std::vector<float> by_hand(Form form, const std::vector<float>& x, std::size_t samples, Order order = Order::Written) {
    const std::size_t voices = static_cast<std::size_t>(form.voices);
    std::vector<float> y(voices * samples);
    for (std::size_t v = 0; v < voices; ++v) {
        std::array<std::array<float, 3>, kMaxSections> past{}; // per section: its input, its output, the output before
        for (std::size_t t = 0; t < samples; ++t) {
            const float in = x[t * voices + v];
            float s = in;
            for (std::size_t k = 0; k < form.sections; ++k) {
                float o = section(order, kC[k], s, past[k], k == 0);
                o = k == 1 && form.between == Between::FromTheFeed ? o + in * 0.5f : o;
                past[k] = {s, o, past[k][1]};
                s = o;
            }
            y[t * voices + v] = form.doubled ? s * 2.0f : s;
        }
    }
    return y;
}

// Run TWICE in one workspace, `reset` between, so that the second starts from silence as the first did.
template <const loops::Program& P>
void check_by_hand(const char* what, Form form, std::int64_t samples, Order order = Order::Written) {
    const std::size_t count = static_cast<std::size_t>(form.voices) * static_cast<std::size_t>(samples);
    const std::vector<float> x = noise(count, 7);
    std::array<float, kParamNames.size()> c2{};
    loops::Feeds feeds{x.data()};
    for (std::size_t k = 0; form.params && k < form.sections; ++k) {
        c2[k] = static_cast<float>(kC[k].c2);
        feeds[1 + k] = &c2[k]; // (the Params are the inputs after x, in their sections' order)
    }
    canaried::Workspace<P> work(loops::Sizes{samples});
    canaried::run<P>(feeds, work);
    work.reset();
    canaried::run<P>(feeds, work);
    const float* got = work.template data<float>(static_cast<std::size_t>(P.outputs[0]));
    const std::vector<float> want = by_hand(form, x, static_cast<std::size_t>(samples), order);
    for (std::size_t i = 0; i < count; ++i) {
        if (!(got[i] == want[i]) && ++g_wrong <= 10) {
            std::printf("  %s, %lld samples [%zu]: got %a, want %a\n", what, static_cast<long long>(samples), i,
                        static_cast<double>(got[i]), static_cast<double>(want[i]));
        }
    }
}

// At `High` over a literal block, against the reference walk, bit for bit.
template <const loops::Program& P>
void check_against_the_walk(const char* what, Form form) {
    const std::size_t voices = static_cast<std::size_t>(form.voices);
    const std::vector<float> x = noise(voices * kBlock, 9);
    const std::vector<double> widened(x.begin(), x.end());
    const Graph g = chain(form, true, numerics::Precision::High);
    Inputs feeds{};
    feeds.push_back(InputFeed{widened.data(), static_cast<std::int32_t>(voices)});
    const auto out = std::make_unique<Signals<kBlock>>();
    run(g, feeds, *out);
    canaried::Workspace<P> work(loops::Sizes{});
    canaried::run<P>(loops::Feeds{x.data()}, work);
    const float* got = work.template data<float>(static_cast<std::size_t>(P.outputs[0]));
    for (std::size_t t = 0; t < static_cast<std::size_t>(kBlock); ++t) {
        for (std::size_t v = 0; v < voices; ++v) {
            const float want = static_cast<float>(sample_at(*out, g.outputs[0].value, static_cast<std::int32_t>(v), t));
            if (!(got[t * voices + v] == want) && ++g_wrong <= 10) {
                std::printf("  %s against the walk [%zu, %zu]: got %a, want %a\n", what, t, v, static_cast<double>(got[t * voices + v]),
                            static_cast<double>(want));
            }
        }
    }
}

constexpr planned::Held kFourHigh{planned::program_of(chain({.sections = 4}, true, numerics::Precision::High), zen2)};
constexpr planned::Held kTanhHigh{planned::program_of(chain(kTanh, true, numerics::Precision::High), zen2)};
constexpr planned::Held kFromTheFeedHigh{planned::program_of(chain(kFromTheFeed, true, numerics::Precision::High), zen2)};
constexpr planned::Held kBankHigh{planned::program_of(chain(kBank, true, numerics::Precision::High), zen2)};

} // namespace

int main() {
    for (const std::int64_t samples : {1, 2, 3, 50}) {
        check_by_hand<kOneHeld.program>("one section", {.sections = 1}, samples);
        check_by_hand<kFourHeld.program>("four sections", {.sections = 4}, samples);
        check_by_hand<kEightHeld.program>("eight sections", {.sections = 8}, samples);
        check_by_hand<kTwelveHeld.program>("twelve sections", {.sections = 12}, samples);
        check_by_hand<kFromTheFeedHeld.program>("from the feed", kFromTheFeed, samples);
        check_by_hand<kBankHeld.program>("bank", kBank, samples);
        check_by_hand<kDoubledHeld.program>("doubled", kDoubled, samples);
        check_by_hand<kTwoOrdered.program>("two sections, own state last", {.sections = 2}, samples, Order::OwnLast);
        check_by_hand<kFourOrdered.program>("four sections, the one before last", {.sections = 4}, samples, Order::BeforeLast);
        check_by_hand<kTwoParamsOrdered.program>("two sections by Params, own state last", kTwoParams, samples, Order::OwnLast);
        check_by_hand<kFourParamsOrdered.program>("four sections by Params, the one before last", kFourParams, samples, Order::BeforeLast);
    }
    check_against_the_walk<kFourHigh.program>("four sections", {.sections = 4});
    check_against_the_walk<kTanhHigh.program>("tanh between", kTanh);
    check_against_the_walk<kFromTheFeedHigh.program>("from the feed", kFromTheFeed);
    check_against_the_walk<kBankHigh.program>("bank", kBank);
    if (g_wrong != 0) {
        std::printf("nest_feedback_chain_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("nest_feedback_chain_test: chained cycles in one stage agree with the loops by hand and with the walk\n");
    return 0;
}
