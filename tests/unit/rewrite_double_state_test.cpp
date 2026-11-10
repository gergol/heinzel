// The rewrite layer's second rule (Decision 66, Plan/23 3.4, by the cost model since P4 4.3b): at `Normal`, a feedback
// cycle runs in double where the cost model prices its stage so no dearer (a tie within 3 % widens) or where Normal
// promises it (one voice, cheap nodes, the past in registers), with the cheap work between two such cycles; what
// leaves it is converted down, and what enters is read as double beside the region's own values (no conversion up).
//
//   biquad      y = .3x + .4x[-1] + .2x[-2] + .5y[-1] - .2y[-2]  -> the cycle in double, its feed-forward sum read as
//                                                                     double; one conversion down (the result)
//   cascade     three sections, each reading the one before       -> one down (y3): the work between sections in double
//   read aside  a one-pole, its y[n-1] also read outside           -> the lag outside reads the converted value: one
//                                                                     conversion serves it and the result
//   behind tanh a cycle with a tanh feeding a one-pole             -> the tanh's cycle in float (a double core on its
//                                                                     chain), read by the one-pole as double
//   divided     y = .1x + .9y[-1] / (1 + .01y[-1]^2)              -> in double: dearer so (13 cycles a division, not
//                                                                     10), but promised (one voice, cheap, registers)
//   bank        four one-pole voices                              -> as written: a bank's lanes masked to its voices
//                                                                     cost what the model does not see (one voice
//                                                                     stands in)
//   bank of 8   eight biquad voices                               -> as written
//   comb        y = x + .5y[-40]                                  -> as written: its samples run in lanes, half as many
//                                                                     in double
//   damped comb d = .7y[-40] + .3d[-1], y = x + .5d                -> in double: one sample at a time, the same chain
//   combs       four damped combs of one input, summed             -> as written: each a tie alone, their stage (merged
//                                                                     by the planner) issue-bound with the conversions
//   High, Fast  the biquad                                        -> as written
//   crowded     the biquad beside ten `tanh` cycles and maps, near   -> the biquad in double: widened all to be priced,
//               the graph's capacities                                the candidates outgrow them; the promise holds
//   merged      four Param DF1 sections, one cycle over [4, time]   -> in double: a cycle holding a link counts as one
//               (`sections_in_lanes`)                                 voice, the link an eligible node, so promised, as
//                                                                     each section is; one down, after V, sliced
//
// The planner runs a conversion where the cost model prices it cheapest (`planner/trail.hpp`): the biquad is one stage.
//
// Every result is a fixed point of `infer` and of the rule. Numbers: each rewritten program against the walk of the
// rewritten graph, bit for bit (the region's arithmetic is the walk's: double, in the written order, the multiply-add
// the same function), but behind the tanh, whose float core the double walk does not have (within 1e-5); and RBJ
// low-passes written as users write them, against the same filter in double, within 1e-7 and no worse than as
// written (measured: 20 Hz -68.8 -> -144.9 dB, 1 kHz -112.1 -> -145.7 dB, 10 kHz -137.5 -> -140.9 dB). The merged
// cascade's walk is checked too: against the cascade in double, over 64 blocks of 256 samples of
// noise and of a 440 Hz sine, at most twice as far as today's cascade through the same rules and within 1e-5 (the
// measure and bound of `rewrite_feedback_sums_test`).

#include "heinzel/compile.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/rewrite/rewritten.hpp"
#include "heinzel/rewrite/sections_in_lanes.hpp"
#include "heinzel/sema/analyze.hpp"

#include "interpret.hpp"
#include "planned.hpp"
#include "sections_in_lanes_graphs.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <numbers>
#include <vector>

using namespace heinzel;

namespace {

namespace loops = heinzel::loops;

constexpr std::int32_t kBlock = 4096;
constexpr Index n{};
constexpr double f(float v) { return static_cast<double>(v); }

constexpr Graph analysed(Graph g, numerics::Precision level) {
    set_default_precision(g, level);
    if (!infer(g).ok) {
        fail("rewrite_double_state_test: a graph did not resolve");
    }
    return g;
}

enum class Case : std::uint8_t { Biquad, Cascade, ReadAside, BehindTanh, Divided, Bank, Bank8, Comb, DampedComb, Combs };

// A comb of `back` samples damped by a one-pole, over `x`.
constexpr Expr damped(Expr x, std::int32_t back) {
    Variable y;
    Variable d;
    d[n] = 0.7 * y[n - back] + 0.3 * d[n - 1];
    y[n] = x + 0.5 * d;
    return y;
}

constexpr Graph graph(Case which, numerics::Precision level = numerics::Precision::Normal) {
    if (which == Case::Bank) {
        Input x{"x", Signal{4, kBlock}};
        Variable y;
        y[n] = x + 0.75 * y[n - 1];
        return analysed(finish(Output{"y"} = y), level);
    }
    if (which == Case::Bank8) {
        Input x{"x", Signal{8, kBlock}};
        Variable y;
        y[n] = f(0.3f) * x + f(0.4f) * x[n - 1] + f(0.2f) * x[n - 2] + f(0.5f) * y[n - 1] - f(0.2f) * y[n - 2];
        return analysed(finish(Output{"y"} = y), level);
    }
    Input x{"x", Signal{kBlock}};
    Variable y;
    if (which == Case::Comb) {
        y[n] = x + 0.5 * y[n - 40];
        return analysed(finish(Output{"y"} = y), level);
    }
    if (which == Case::DampedComb) {
        return analysed(finish(Output{"y"} = damped(x, 40)), level);
    }
    if (which == Case::Divided) {
        y[n] = f(0.1f) * x + f(0.9f) * y[n - 1] / (1.0 + f(0.01f) * y[n - 1] * y[n - 1]);
        return analysed(finish(Output{"y"} = y), level);
    }
    if (which == Case::Combs) {
        return analysed(finish(Output{"y"} = damped(x, 40) + damped(x, 41) + damped(x, 43) + damped(x, 47)), level);
    }
    if (which == Case::Biquad) {
        y[n] = f(0.3f) * x + f(0.4f) * x[n - 1] + f(0.2f) * x[n - 2] + f(0.5f) * y[n - 1] - f(0.2f) * y[n - 2];
        return analysed(finish(Output{"y"} = y), level);
    }
    if (which == Case::Cascade) {
        Variable y2;
        Variable y3;
        y[n] = f(0.3f) * x + f(0.2f) * x[n - 1] - f(0.4f) * y[n - 1] + f(0.1f) * y[n - 2];
        y2[n] = f(0.5f) * y + f(0.1f) * y[n - 1] + f(0.3f) * y2[n - 1] - f(0.2f) * y2[n - 2];
        y3[n] = f(0.4f) * y2 + f(0.2f) * y2[n - 1] - f(0.1f) * y3[n - 1] + f(0.15f) * y3[n - 2];
        return analysed(finish(Output{"y"} = y3), level);
    }
    if (which == Case::ReadAside) {
        y[n] = x + 0.75 * y[n - 1];
        Output aside{"aside"};
        aside = 3.0 * y[n - 1];
        return analysed(finish(Output{"y"} = y, aside), level);
    }
    Variable s;
    s[n] = tanh(x + 0.5 * s[n - 1]);
    y[n] = s + 0.75 * y[n - 1];
    return analysed(finish(Output{"y"} = y), level);
}

// The conversions a graph holds: up to double, and down to float.
struct Conversions {
    int up{0};
    int down{0};
};
constexpr Conversions conversions(const Graph& g) {
    Conversions c{};
    for (const Node& node : g.nodes) {
        if (node.attr.kind == AttrKind::Cast) {
            (node.attr.cast.to == ElemType::F64 ? c.up : c.down) += 1;
        }
    }
    return c;
}
constexpr bool defines_in_double(const Graph& g) {
    bool all = true;
    for (const Node& node : g.nodes) {
        all = all && (node.attr.kind != AttrKind::Recurrence || g.values[static_cast<std::size_t>(node.attr.recurrence.defines)].type == ElemType::F64);
    }
    return all;
}

// As the rewrite hands it on: its feedback sums ordered first, then double state; and as it stands before double state.
constexpr Graph widened(Case which) { return rewrite::sums_and_state(graph(which), zen2); }
constexpr Graph ordered(Case which) { return rewrite::ordered_feedback_sums(graph(which), zen2); }

static_assert(conversions(widened(Case::Biquad)).up == 0 && conversions(widened(Case::Biquad)).down == 1,
              "a biquad: its feed-forward sum read as double, its result converted down");
static_assert(conversions(widened(Case::Cascade)).up == 0 && conversions(widened(Case::Cascade)).down == 1,
              "a cascade: the work between its sections in double, converted only where the last is read");
static_assert(conversions(widened(Case::ReadAside)).up == 0 && conversions(widened(Case::ReadAside)).down == 1,
              "a lag read aside reads the converted value, as the result does: one conversion");
static_assert(conversions(widened(Case::BehindTanh)).up == 0 && conversions(widened(Case::BehindTanh)).down == 1,
              "a cycle behind a tanh stays in float, read by the one-pole as double");
static_assert(defines_in_double(widened(Case::Biquad)) && defines_in_double(widened(Case::Cascade)), "their variables are double");
static_assert(conversions(widened(Case::Divided)).up == 0 && conversions(widened(Case::Divided)).down == 1,
              "a division on the chain: priced dearer in double, but one voice of cheap nodes, its past in registers, is promised it");
static_assert(widened(Case::Bank) == ordered(Case::Bank) && widened(Case::Bank8) == ordered(Case::Bank8), "a bank runs in lanes: as written");
static_assert(widened(Case::Comb) == ordered(Case::Comb), "a comb reads its value 40 samples back: its samples run in lanes, as written");
static_assert(conversions(widened(Case::DampedComb)).up == 0 && conversions(widened(Case::DampedComb)).down == 1,
              "a damped comb runs a sample at a time, its chain alike: in double");
static_assert(widened(Case::Combs) == ordered(Case::Combs), "four damped combs, each a tie alone, cost more merged: as written");
static_assert(rewrite::double_state(graph(Case::Biquad, numerics::Precision::High), zen2) == graph(Case::Biquad, numerics::Precision::High) &&
                  rewrite::double_state(graph(Case::Biquad, numerics::Precision::Fast), zen2) == graph(Case::Biquad, numerics::Precision::Fast),
              "only Normal: High keeps every rounding, Fast its float");

// Near the graph's capacities (the review of P4): the biquad, ten one-voice `tanh` cycles summed into a second output,
// and `maps` maps after the sum, 119 to 127 values. Every candidate widened to be priced outgrows them, and the rule
// returned the graph as written; Normal's promise is a floor, so the biquad widens still, alone.
constexpr Graph crowded(int maps) {
    Input x{"x", Signal{kBlock}};
    Variable y;
    std::array<Variable, 10> p{};
    y[n] = f(0.2f) * x + f(0.3f) * x[n - 1] + f(0.1f) * x[n - 2] + f(1.2f) * y[n - 1] - f(0.5f) * y[n - 2];
    Expr sum = x;
    for (Variable& v : p) {
        v[n] = tanh(x + v[n - 1]);
        sum = sum + v;
    }
    for (int k = 0; k < maps; ++k) {
        sum = sum * 0.999;
    }
    Output a{"a"};
    Output b{"b"};
    a = y;
    b = sum;
    return analysed(finish(a, b), numerics::Precision::Normal);
}
// Output `o` of `g` is a double converted down.
constexpr bool converted_down(const Graph& g, std::size_t o) {
    for (const Node& node : g.nodes) {
        if (node.output == g.outputs[o].value && node.attr.kind == AttrKind::Cast) {
            return g.values[static_cast<std::size_t>(node.inputs[0])].type == ElemType::F64;
        }
    }
    return false;
}
static_assert(converted_down(rewrite::rewritten(crowded(19), zen2), 0) && converted_down(rewrite::rewritten(crowded(23), zen2), 0),
              "near the capacities, where every candidate in double does not fit, Normal's promise holds: the biquad in double");

constexpr bool settled(Graph g) {
    const Graph once = rewrite::rewritten(g, zen2);
    Graph again = once;
    return infer(again).ok && again == once && rewrite::rewritten(once, zen2) == once && rewrite::double_state(once, zen2) == once;
}
static_assert(settled(graph(Case::Biquad)) && settled(graph(Case::Cascade)) && settled(graph(Case::ReadAside)) &&
                  settled(graph(Case::BehindTanh)) && settled(graph(Case::Divided)) && settled(graph(Case::Bank)) && settled(graph(Case::DampedComb)) &&
                  settled(graph(Case::Combs)),
              "a rewritten graph is analysed already, and neither rule finds anything more in it");

// ---- numbers ----------------------------------------------------------------------------------------------

int g_wrong = 0;

std::vector<float> noise(std::size_t count) {
    std::vector<float> v(count);
    std::uint32_t state = 11;
    for (float& x : v) {
        state = state * 1664525u + 1013904223u;
        x = static_cast<float>(static_cast<std::int32_t>(state >> 8) % 2001 - 1000) * 0.001f;
    }
    return v;
}

// Graph `g`'s output `o` walked over `x` (each input fed `x`), in double.
std::vector<double> walked(const Graph& g, const std::vector<float>& x, std::size_t o = 0) {
    const std::vector<double> widened(x.begin(), x.end());
    Inputs feeds{};
    for (std::size_t i = 0; i < g.inputs.size(); ++i) {
        feeds.push_back(InputFeed{widened.data(), 1});
    }
    const auto out = std::make_unique<Signals<kBlock>>();
    run(g, feeds, *out);
    std::vector<double> y(kBlock);
    for (std::size_t t = 0; t < static_cast<std::size_t>(kBlock); ++t) {
        y[t] = sample_at(*out, g.outputs[o].value, 0, t);
    }
    return y;
}

template <const loops::Program& P>
std::vector<double> ran(const std::vector<float>& x, std::size_t o = 0) {
    loops::Workspace<P> work(loops::Sizes{});
    loops::run<P>(loops::Feeds{x.data()}, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(P.outputs[o]));
    return std::vector<double>(y, y + kBlock);
}

// RBJ low-passes, Q 1/sqrt 2 at 48 kHz, written as users write them (`y[n-2]`), the coefficients rounded to float so
// that the double graph is the same filter.
struct Lowpass {
    double b0;
    double b1;
    double b2;
    double a1;
    double a2;
};
constexpr Lowpass kLowpasses[] = {
    {1.7103058908716714e-06, 3.4206117817433428e-06, 1.7103058908716714e-06, -1.9962976017691223, 0.9963044429926857},
    {0.003916126660547383, 0.007832253321094766, 0.003916126660547383, -1.815341082704568, 0.8310055893467576},
    {0.2201947002729587, 0.4403894005459174, 0.2201947002729587, -0.3075663597922098, 0.18834516088404457},
};
template <std::size_t K>
constexpr Graph lowpass(ElemType type) {
    const Lowpass& c = kLowpasses[K];
    const auto q = [](double v) { return f(static_cast<float>(v)); };
    Input x{"x", Signal{kBlock}, type};
    Variable y;
    y[n] = q(c.b0) * x + q(c.b1) * x[n - 1] + q(c.b2) * x[n - 2] - q(c.a1) * y[n - 1] - q(c.a2) * y[n - 2];
    return analysed(finish(Output{"y"} = y), numerics::Precision::Normal);
}
template <std::size_t K>
struct Lowpassed {
    static constexpr Graph kGraph = lowpass<K>(ElemType::F32);
    static constexpr planned::Held kWritten{planned::program_of(kGraph, zen2)};
    static constexpr planned::Held kRewritten{planned::program_of(rewrite::rewritten(kGraph, zen2), zen2)};
};

// The largest error against `want`, relative to its largest magnitude.
double error(const std::vector<double>& got, const std::vector<double>& want) {
    double scale = 0;
    double worst = 0;
    for (std::size_t i = 0; i < want.size(); ++i) {
        scale = std::max(scale, std::fabs(want[i]));
        worst = std::max(worst, std::fabs(got[i] - want[i]));
    }
    return worst / std::max(scale, 1e-30);
}

// Low-pass `K` rewritten against the same filter in double: within `bound`, and no further than as written.
template <std::size_t K>
void check_accuracy(const char* what, double bound) {
    const std::vector<float> x = noise(kBlock);
    const std::vector<double> exact = walked(lowpass<K>(ElemType::F64), x);
    const double written = error(ran<Lowpassed<K>::kWritten.program>(x), exact);
    const double rewritten = error(ran<Lowpassed<K>::kRewritten.program>(x), exact);
    std::printf("  %s: rewritten %.3g (%.1f dB), as written %.3g (%.1f dB) against double\n", what, rewritten, 20 * std::log10(rewritten),
                written, 20 * std::log10(written));
    if (!(rewritten <= bound) || !(rewritten <= written)) {
        ++g_wrong;
        std::printf("  %s: over the bound %g, or worse than as written\n", what, bound);
    }
}

template <Case S>
struct Rewritten {
    static constexpr Graph kGraph = rewrite::rewritten(graph(S), zen2);
    static constexpr planned::Held kHeld{planned::program_of(kGraph, zen2)};
};

// The program of the rewritten graph against the walk of the same graph, bit for bit (`bound` 0) or within `bound`.
template <Case S>
void check_against_the_walk(const char* what, std::size_t outputs = 1, double bound = 0) {
    const std::vector<float> x = noise(kBlock);
    for (std::size_t o = 0; o < outputs; ++o) {
        const std::vector<double> got = ran<Rewritten<S>::kHeld.program>(x, o);
        const std::vector<double> want = walked(Rewritten<S>::kGraph, x, o);
        const auto differ = std::mismatch(got.begin(), got.end(), want.begin()).first - got.begin();
        if (bound > 0 ? error(got, want) > bound : differ != kBlock) {
            ++g_wrong;
            std::printf("  %s, output %zu: sample %td is %a, the walk %a\n", what, o, differ, got[static_cast<std::size_t>(differ)],
                        want[static_cast<std::size_t>(differ)]);
        }
    }
}

// ---- sections in lanes -----------------------------------------------------------------------------------------
//
// Four Param sections as `df1_param` writes them (RBJ peaks at 62.5 to 500 Hz), 64 blocks of 256 samples long, merged
// by `sections_in_lanes` into one cycle over [4, time], then handed on as `rewritten` does.

constexpr std::int32_t kLong = 64 * 256;
constexpr std::size_t kSections = 4;

Graph cascade_of(ElemType type) { return lanes::raw(kSections, numerics::Precision::Normal, type, kLong); }

// Per cycle of `g`, in node order, whether Normal promises it double.
std::vector<char> promised_cycles(const Graph& g) {
    const loops::graph::Facts facts = loops::graph::facts_of(g);
    const auto feedback = std::make_unique<loops::graph::Feedback>(loops::graph::find_feedback(g, facts));
    const std::vector<char> promise = rewrite::detail::promised(g, facts, *feedback);
    std::vector<char> each{};
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        if (feedback->cycle[i] == static_cast<std::int32_t>(i)) {
            each.push_back(promise[i]);
        }
    }
    return each;
}

// `g`'s conversion down reads what defines V, a double over the sections, and the slice reads it; the link is double.
bool converted_after_v(const Graph& g) {
    const loops::graph::Facts facts = loops::graph::facts_of(g);
    bool after_v = false;
    bool sliced = false;
    bool linked = false;
    for (const Node& node : g.nodes) {
        const Value& operand = g.values[static_cast<std::size_t>(node.inputs[0] < 0 ? 0 : node.inputs[0])];
        if (node.attr.kind == AttrKind::Cast) {
            const std::int32_t p = facts.producer[static_cast<std::size_t>(node.inputs[0])];
            after_v = p >= 0 && g.nodes[static_cast<std::size_t>(p)].attr.kind == AttrKind::Recurrence && operand.type == ElemType::F64 &&
                      operand.shape == Shape{static_cast<std::int32_t>(kSections), kLong};
            for (const Node& reader : g.nodes) {
                sliced = sliced || (reader.inputs[0] == node.output && reader.attr.kind == AttrKind::View && reader.attr.view.kind == ViewKind::Slice);
            }
        }
        linked = linked || (loops::graph::is_a_link(node) && g.values[static_cast<std::size_t>(node.output)].type == ElemType::F64);
    }
    return after_v && sliced && linked;
}

// The merged DF1 cycle is promised double, as each section is today; one conversion down, none up, after V and sliced;
// its variable double; a fixed point of `infer` and of the rule.
void check_merged() {
    const Graph g = cascade_of(ElemType::F32);
    const auto merged = std::make_unique<Graph>(rewrite::detail::linked_runs(g, zen2));
    const auto ordered = std::make_unique<Graph>(rewrite::ordered_feedback_sums(*merged, zen2));
    const auto apart = std::make_unique<Graph>(rewrite::ordered_feedback_sums(g, zen2));
    const auto once = std::make_unique<Graph>(rewrite::double_state(*ordered, zen2));
    const auto again = std::make_unique<Graph>(*once);
    const Conversions c = conversions(*once);
    const bool promised = promised_cycles(*ordered) == std::vector<char>{1} && promised_cycles(*apart) == std::vector<char>(kSections, 1);
    const bool shaped = c.up == 0 && c.down == 1 && defines_in_double(*once) && converted_after_v(*once);
    const bool fixed = infer(*again).ok && *again == *once && rewrite::double_state(*once, zen2) == *once;
    if (!promised || !shaped || !fixed) {
        ++g_wrong;
        std::printf("  merged: promised %d, %d up and %d down, after V %d, fixed %d\n", promised, c.up, c.down, converted_after_v(*once), fixed);
    }
}

// Graph `g`'s output walked over `x`, each Param its number rounded to float, so that the double cascade is the same
// filter.
std::vector<double> walked_long(const Graph& g, const std::vector<float>& x) {
    std::vector<std::vector<double>> numbers{};
    for (const NamedValue& in : g.inputs) {
        const bool signal = in.name.view() == "x";
        const double held = signal ? 0.0 : static_cast<double>(static_cast<float>(lanes::param_number(in.name.view())));
        numbers.emplace_back(static_cast<std::size_t>(kLong), held);
        if (signal) {
            numbers.back().assign(x.begin(), x.end());
        }
    }
    Inputs feeds{};
    for (const std::vector<double>& fed : numbers) {
        feeds.push_back(InputFeed{fed.data(), 1});
    }
    const auto out = std::make_unique<Signals<static_cast<std::size_t>(kLong), 256>>();
    run(g, feeds, *out);
    std::vector<double> y(static_cast<std::size_t>(kLong));
    for (std::size_t t = 0; t < y.size(); ++t) {
        y[t] = sample_at(*out, g.outputs[0].value, 0, t);
    }
    return y;
}

// The merged cascade's walk against the cascade in double: at most twice as far as today's cascade through the same
// rules (and a float's rounding of the output), and within 1e-5.
void check_merged_numbers(const char* what, const std::vector<float>& x) {
    const Graph in_double = cascade_of(ElemType::F64);
    const Graph g = cascade_of(ElemType::F32);
    const auto linked = std::make_unique<Graph>(rewrite::detail::linked_runs(g, zen2));
    const auto today_planned = std::make_unique<Graph>(rewrite::sums_and_state(g, zen2));
    const auto merged_planned = std::make_unique<Graph>(rewrite::sums_and_state(*linked, zen2));
    const std::vector<double> exact = walked_long(in_double, x);
    const double today = error(walked_long(*today_planned, x), exact);
    const double merged = error(walked_long(*merged_planned, x), exact);
    std::printf("  four sections merged, %s: %.3g, today %.3g against double\n", what, merged, today);
    if (!(merged <= 2 * today + 1e-7) || !(merged <= 1e-5)) {
        ++g_wrong;
        std::printf("  four sections merged, %s: over the bound 1e-5, or twice today's\n", what);
    }
}

std::vector<float> sine(std::size_t count) {
    std::vector<float> v(count);
    for (std::size_t t = 0; t < count; ++t) {
        v[t] = static_cast<float>(std::sin(2.0 * std::numbers::pi * 440.0 * static_cast<double>(t) / 48000.0));
    }
    return v;
}

} // namespace

int main() {
    check_against_the_walk<Case::Biquad>("biquad");
    check_against_the_walk<Case::Cascade>("cascade");
    check_against_the_walk<Case::ReadAside>("read aside", 2);
    check_against_the_walk<Case::BehindTanh>("behind a tanh", 1, 1e-5);
    check_against_the_walk<Case::Divided>("divided");
    check_against_the_walk<Case::DampedComb>("damped comb");
    check_accuracy<0>("low-pass 20 Hz", 1e-7);
    check_accuracy<1>("low-pass 1 kHz", 1e-7);
    check_accuracy<2>("low-pass 10 kHz", 1e-7);
    check_merged();
    check_merged_numbers("noise", noise(static_cast<std::size_t>(kLong)));
    check_merged_numbers("440 Hz", sine(static_cast<std::size_t>(kLong)));
    if (g_wrong != 0) {
        std::printf("rewrite_double_state_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("rewrite_double_state_test: cycles in double where the cost model prices them no dearer, converted at the region's edge\n");
    return 0;
}
