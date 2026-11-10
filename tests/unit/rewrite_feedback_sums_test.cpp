// The rewrite layer's first rule (Decision 66): a feedback sum in the order its terms become ready, the last addition
// fused where the machine has a multiply-add (`_madd`, rounded as the build rounds), only where the precision grants
// reordering.
//
//   biquad      y = ff + .5y[-1] - .2y[-2]                  -> madd(.5, y[-1], ff - .2y[-2]): y[-1] and y[-2] a run,
//                                                              both in registers
//   past a gap  y = x + .5y[-3] + .25y[-1]                   -> as written: no y[-2], so y[-3] comes from memory, and in
//                                                              ready order that load would be on the chain
//   lagged      y = ff + .5a - .2b, a = y[-1], b = a[-1]     -> madd(.5, a, ff - .2b): b, older, first; both in registers
//   one-pole    y = x + a y[-1]                              -> madd(a, y[-1], x): two terms, the last fused
//   minus last  y = x + .25b - .5a, the lagged states       -> madd(-.5, a, x + .25b): the constant negated
//   by a param  y = x - g y[-1], g a Param                   -> madd(-g, y[-1], x): the Param negated by a `_neg` off the
//                                                              chain, on either side of the product (Plan 25, step 2)
//   ladder      s1 = B1 s0[-1] + A1 s1[-1] + B0 s0          -> in order already (s0 made this sample), its product fused
//   sections    b = .5a + .1a[-1] + .3b[-1] - .2b[-2]       -> madd(.3, b[-1], ...) on zen2: two sections, the stage's
//                                                              path short, the newest own state last
//   cascades    k sections, each reading the one before      -> on zen2 2 and 3: the newest own state last; 4 and 6: the
//                                                              section before last (the order the stage costs less in)
//   saturated   the same reading tanh(a)                    -> madd(.3, b[-1], ...): tanh(a) stored apart, ready at once
//   phase       p = p[-1] + inc                              -> unchanged: two terms, nothing to fuse
//   no fma      the lagged biquad for a machine without one  -> reordered, not fused
//   merged      four Param sections, one cycle over [4, time] -> madd(b0, u, ... - a1 V[-1]): it reads no other
//               (`sections_in_lanes`)                           cycle, so the ready order, the link's term last,
//                                                              fused, the section's own newest state before it
//
// And at `High` nothing moves; every result is a fixed point of `infer` and of the rule itself, fused or not. Numbers:
// at `Normal`, over filters at 20 Hz, 1 kHz and 10 kHz and the bench's cascade (their older states lagged), fed noise,
// the rewritten program's error against the same filter in double is at most twice the program as written's, and
// within the benches' bound where float can reach it. (Not against the walk of the float graph: that rounds every
// operation in the written order, the written program's twin.) That is a measurement on these filters and this input,
// not a bound: the order changes the rounding, and an ill-conditioned filter fed a tone can go either way (an RBJ peak,
// 10 Hz, Q 30, -6 dB, fed 15 kHz: 29x the written error over 4096 samples, 396x over 65536; fed 300 Hz, 0.3x; the
// review of Decision 66). `Normal` grants the reordering; `High` keeps the written order.

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
#include <string>
#include <string_view>
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
        fail("rewrite_feedback_sums_test: a graph did not resolve");
    }
    return g;
}

enum class Filter : std::uint8_t { Biquad, Gap, Lagged, OnePole, MinusLast, ByAParam, ByAParamAfter, ByAComputedParam, ByASignal, Ladder, Phase };

constexpr Graph filter(Filter which, numerics::Precision level = numerics::Precision::Normal) {
    Input x{"x", Signal{kBlock}};
    Variable y;
    switch (which) {
    case Filter::Biquad:
        y[n] = f(0.3f) * x + f(0.4f) * x[n - 1] + f(0.2f) * x[n - 2] + f(0.5f) * y[n - 1] - f(0.2f) * y[n - 2];
        break;
    case Filter::Gap:
        y[n] = x + 0.5 * y[n - 3] + 0.25 * y[n - 1];
        break;
    case Filter::Lagged: {
        Expr a = y[n - 1];
        Expr b = a[n - 1];
        y[n] = f(0.3f) * x + f(0.4f) * x[n - 1] + f(0.2f) * x[n - 2] + f(0.5f) * a - f(0.2f) * b;
        break;
    }
    case Filter::OnePole:
        y[n] = x + 0.75 * y[n - 1];
        break;
    case Filter::MinusLast: {
        Expr a = y[n - 1];
        Expr b = a[n - 1];
        y[n] = x + 0.25 * b - 0.5 * a;
        break;
    }
    case Filter::ByAParam: {
        Param g{"g"};
        y[n] = x - g * y[n - 1];
        break;
    }
    case Filter::ByAParamAfter: {
        Param g{"g"};
        y[n] = x - y[n - 1] * g;
        break;
    }
    case Filter::ByAComputedParam: { // (timeless: computed from a Param alone)
        Param g{"g"};
        y[n] = x - (0.5 * g) * y[n - 1];
        break;
    }
    case Filter::ByASignal: { // (not timeless: it changes every sample)
        Input g{"g", Signal{kBlock}};
        y[n] = x - g * y[n - 1];
        break;
    }
    case Filter::Ladder: {
        Variable s0;
        s0[n] = x + 0.5 * y[n - 1];
        y[n] = 0.25 * s0[n - 1] + 0.5 * y[n - 1] + 0.125 * s0;
        break;
    }
    case Filter::Phase:
        y[n] = y[n - 1] + x;
        break;
    }
    return analysed(finish(Output{"y"} = y), level);
}

// The node defining the result, and the node making what it defines.
constexpr const Node& last_step(const Graph& g) {
    const Node* recur = nullptr;
    for (const Node& node : g.nodes) {
        recur = node.attr.kind == AttrKind::Recurrence ? &node : recur;
    }
    for (const Node& node : g.nodes) {
        if (node.output == recur->inputs[0]) {
            return node;
        }
    }
    fail("rewrite_feedback_sums_test: no step defines the result");
    return g.nodes[0];
}
constexpr Algebra last_role(const Graph& g) { return rules_of(last_step(g).op).algebra; }
constexpr double constant(const Graph& g, std::int32_t v) { return g.values[static_cast<std::size_t>(v)].constant; }
// Whether a factor of step `s` of `g` is the negation of an input (a Param here).
constexpr bool negated_input(const Graph& g, const Node& s) {
    for (const Node& node : g.nodes) {
        if ((node.output == s.inputs[0] || node.output == s.inputs[1]) && rules_of(node.op).algebra == Algebra::Negate &&
            g.values[static_cast<std::size_t>(node.inputs[0])].category == ValueCategory::Input) {
            return true;
        }
    }
    return false;
}

// The rule as zen2, a machine with the multiply-add, applies it.
constexpr Graph fused(const Graph& g) { return rewrite::ordered_feedback_sums(g, zen2); }

constexpr Graph kBiquad = filter(Filter::Biquad);
static_assert(last_role(fused(kBiquad)) == Algebra::ProductAdd && constant(fused(kBiquad), last_step(fused(kBiquad)).inputs[0]) == f(0.5f),
              "the biquad as written: y[n-1] and y[n-2] a run in registers, the newest term last, fused");
static_assert(fused(filter(Filter::Gap)) == filter(Filter::Gap), "a lag past a gap is read from memory: the sum stays as written");

// Two sections as users write them, the second reading the first this sample, directly or through a tanh.
constexpr Graph two_sections(bool saturated) {
    Input x{"x", Signal{kBlock}};
    Variable a;
    Variable b;
    a[n] = f(0.3f) * x - f(0.4f) * a[n - 1] + f(0.1f) * a[n - 2];
    const Expr in = saturated ? Expr{tanh(a)} : Expr{a};
    b[n] = f(0.5f) * in + f(0.1f) * a[n - 1] + f(0.3f) * b[n - 1] - f(0.2f) * b[n - 2];
    return analysed(finish(Output{"y"} = b), numerics::Precision::Normal);
}
constexpr Graph kTwo = fused(two_sections(false));
constexpr Graph kSaturated = fused(two_sections(true));
static_assert(constant(kTwo, last_step(kTwo).inputs[0]) == f(0.3f),
              "two sections: the newest own state last, the section before early; their stage's path is short enough for "
              "zen2's window (Plan/probes/cascade-order/sections.cpp)");
static_assert(constant(kSaturated, last_step(kSaturated).inputs[0]) == f(0.3f),
              "through a tanh the section before is stored apart, read as the sample starts: the newest own state last");

// A cascade of `k` sections as the bench and users write them, each reading the one before this sample. The two orders
// trade the stage's bounds: the section before last, a short path through the stage and three steps on each section's
// own chain; the newest own state last, one multiply-add there and a path a section several steps longer, which zen2's
// window overlaps with the next samples' only so far. The sweep (B/F 0.51 0.63 0.89 1.30 1.94 for 1 2 3 4 6 sections)
// and the cost model flip between three and four.
constexpr Graph sections(std::size_t k) {
    Input x{"x", Signal{kBlock}};
    std::array<Variable, 6> y{};
    Expr s = x;
    for (std::size_t i = 0; i < k; ++i) {
        y[i][n] = 0.5 * s + 0.25 * s[n - 1] - 0.375 * y[i][n - 1] + 0.125 * y[i][n - 2];
        s = y[i];
    }
    return analysed(finish(Output{"y"} = s), numerics::Precision::Normal);
}
constexpr double last_factor(const Graph& g, const Machine& m) {
    const Graph re = rewrite::ordered_feedback_sums(g, m);
    return last_role(re) == Algebra::ProductAdd ? constant(re, last_step(re).inputs[0]) : 0;
}
static_assert(last_factor(sections(2), zen2) == -0.375 && last_factor(sections(3), zen2) == -0.375,
              "two and three sections on zen2: each section's newest own state last, fused, its constant negated");
static_assert(last_factor(sections(4), zen2) == 0.5 && last_factor(sections(6), zen2) == 0.5,
              "four and six sections on zen2: the section before last, fused, as Decision 66 had it");
static_assert(last_factor(sections(2), m1_pro) == -0.375, "on the M1 too, two sections take the newest own state last");
static_assert(last_factor(sections(1), zen2) == -0.375, "one section reads no other section: its newest state last, as ever");

// Two cascades in one graph, each on an input of its own: two sections (feed 0.5, own -0.375) and four (feed 0.75, own
// -0.3125). No stage holds both, so each takes the order its own stages cost less in, as alone: on zen2 the two their
// newest own state last, the four the section before last but in the first section, which reads only the input. Priced
// whole, the two would have flipped the four to B (19.75 against 23.63 cycles; apart 6.31 and 12.62).
constexpr Graph two_cascades() {
    Input x{"x", Signal{kBlock}};
    Input w{"w", Signal{kBlock}};
    std::array<Variable, 6> a{};
    std::array<Variable, 6> b{};
    Expr s = x;
    Expr t = w;
    for (std::size_t i = 0; i < 2; ++i) {
        a[i][n] = 0.5 * s + 0.25 * s[n - 1] - 0.375 * a[i][n - 1] + 0.125 * a[i][n - 2];
        s = a[i];
    }
    for (std::size_t i = 0; i < 4; ++i) {
        b[i][n] = 0.75 * t + 0.25 * t[n - 1] - 0.3125 * b[i][n - 1] + 0.125 * b[i][n - 2];
        t = b[i];
    }
    return analysed(finish(Output{"a"} = s, Output{"b"} = t), numerics::Precision::Normal);
}
// How many multiply-adds of `g` take constant `factor`.
constexpr int fused_by(const Graph& g, double factor) {
    int count = 0;
    for (const Node& node : g.nodes) {
        count += rules_of(node.op).algebra == Algebra::ProductAdd &&
                 g.values[static_cast<std::size_t>(node.inputs[0])].category == ValueCategory::Const && constant(g, node.inputs[0]) == factor;
    }
    return count;
}
constexpr Graph kTwoCascades = rewrite::ordered_feedback_sums(two_cascades(), zen2);
static_assert(fused_by(kTwoCascades, -0.375) == 2 && fused_by(kTwoCascades, 0.75) == 3 && fused_by(kTwoCascades, -0.3125) == 1,
              "two independent cascades on zen2: the two sections the newest own state last, the four the section before");

// The same cascade with its feedback coefficients Params (the bench's `heinzel-param`): in either order the newest own
// state's subtracted product fuses, the Param negated; on zen2 two sections take that order, four the section before
// last.
constexpr Graph param_sections(std::size_t k) {
    Input x{"x", Signal{kBlock}};
    Param a1{"a1"};
    std::array<Variable, 6> y{};
    Expr s = x;
    for (std::size_t i = 0; i < k; ++i) {
        y[i][n] = 0.5 * s + 0.25 * s[n - 1] - a1 * y[i][n - 1] + 0.125 * y[i][n - 2];
        s = y[i];
    }
    return analysed(finish(Output{"y"} = s), numerics::Precision::Normal);
}
constexpr bool last_negated(const Graph& g, const Machine& m) {
    const Graph re = rewrite::ordered_feedback_sums(g, m);
    return last_role(re) == Algebra::ProductAdd && negated_input(re, last_step(re));
}
static_assert(last_negated(param_sections(2), zen2) && last_negated(param_sections(2), m1_pro) && last_factor(param_sections(4), zen2) == 0.5,
              "Param cascades: two sections take the newest own state last, fused, the Param negated; four the section before");

constexpr Graph kLagged = filter(Filter::Lagged);
constexpr Graph kLaggedRe = fused(kLagged);
static_assert(last_role(kLaggedRe) == Algebra::ProductAdd && constant(kLaggedRe, last_step(kLaggedRe).inputs[0]) == f(0.5f) &&
                  kLaggedRe.nodes.size() + 1 == kLagged.nodes.size(),
              "the lagged biquad: the newest state's term last, fused, its multiply gone");
static_assert(last_role(kLagged) == Algebra::Subtract, "as written, the lagged biquad ends in the older state's subtraction");

constexpr Graph kOnePoleRe = fused(filter(Filter::OnePole));
static_assert(last_role(kOnePoleRe) == Algebra::ProductAdd && constant(kOnePoleRe, last_step(kOnePoleRe).inputs[0]) == 0.75,
              "a one-pole: two terms, the last a product, fused");

constexpr Graph kMinusLastRe = fused(filter(Filter::MinusLast));
static_assert(last_role(kMinusLastRe) == Algebra::ProductAdd && constant(kMinusLastRe, last_step(kMinusLastRe).inputs[0]) == -0.5,
              "a subtracted product of a constant: fused, the constant negated");

// A sum whose first term in ready order is subtracted, an added term after it before the last or none.
constexpr Graph minus_first(bool added) {
    Input x{"x", Signal{kBlock}};
    Variable y;
    y[n] = added ? -x + 0.25 * y[n - 2] + 0.5 * y[n - 1] : 0.5 * y[n - 1] - 0.25 * x[n - 1] - x;
    return analysed(finish(Output{"y"} = y), numerics::Precision::Normal);
}
// The node making `v` (else the first); whether the last multiply-add of `g` adds to a product of `factor` less a term.
constexpr const Node& made(const Graph& g, std::int32_t v) {
    const auto at = std::find_if(g.nodes.begin(), g.nodes.end(), [v](const Node& node) { return node.output == v; });
    return at == g.nodes.end() ? g.nodes[0] : *at;
}
constexpr bool subtracted_from(const Graph& g, double factor) {
    const Node& before = made(g, last_step(g).inputs[2]);
    const Node& product = made(g, before.inputs[0]);
    return last_role(g) == Algebra::ProductAdd && rules_of(before.op).algebra == Algebra::Subtract && constant(g, product.inputs[0]) == factor;
}
static_assert(subtracted_from(fused(minus_first(true)), 0.25) && subtracted_from(fused(minus_first(false)), -0.25),
              "the first term in ready order subtracted: -x + .25y[-2] + .5y[-1] -> madd(.5, y[-1], .25y[-2] - x); "
              ".5y[-1] - .25x[-1] - x, none added before the fused last -> madd(.5, y[-1], -.25x[-1] - x), the constant negated");

constexpr bool fused_by_its_param(const Graph& g) {
    const Graph re = fused(g);
    return last_role(re) == Algebra::ProductAdd && negated_input(re, last_step(re)) && re.nodes.size() == g.nodes.size();
}
static_assert(fused_by_its_param(filter(Filter::ByAParam)) && fused_by_its_param(filter(Filter::ByAParamAfter)),
              "a subtracted product of a Param and the state, the Param on either side: fused, the Param negated off the chain "
              "(a `_neg` for the `_mul`, the `_sub` a multiply-add)");
static_assert(last_role(fused(filter(Filter::ByAComputedParam))) == Algebra::ProductAdd,
              "a factor computed from a Param alone is timeless too: fused");
static_assert(fused(filter(Filter::ByASignal)) == filter(Filter::ByASignal),
              "a subtracted product of a signal and the state: the signal is not timeless, and two terms gain nothing reordered");
static_assert(fused(filter(Filter::Phase)) == filter(Filter::Phase), "a phase accumulator: two terms, no product");

constexpr Graph kLadderRe = fused(filter(Filter::Ladder));
static_assert(last_role(kLadderRe) == Algebra::ProductAdd && constant(kLadderRe, last_step(kLadderRe).inputs[0]) == 0.125,
              "the ladder's section: in order already (what is made this sample last), its product fused");

constexpr Machine kNoFma{.vector_bytes = 32, .registers = 16, .fma = false};
static_assert(last_role(rewrite::rewritten(kLagged, kNoFma)) == Algebra::Add && last_role(rewrite::ordered_feedback_sums(kLagged, kNoFma)) == Algebra::Add,
              "no fused multiply-add: reordered only");
static_assert(last_role(rewrite::rewritten(kLagged, zen2)) == Algebra::ProductAdd,
              "for zen2, fused in every build: `_madd` rounds as the build does, and never runs a software core");
constexpr Graph once(Graph g) { return rewrite::compute_equal_values_once(g), g; }
static_assert(rewrite::rewritten(filter(Filter::Lagged, numerics::Precision::High), zen2) == once(filter(Filter::Lagged, numerics::Precision::High)) &&
                  rewrite::rewritten(filter(Filter::Ladder, numerics::Precision::High), zen2) == once(filter(Filter::Ladder, numerics::Precision::High)),
              "High grants no reordering: nothing moves but a value written twice, computed once (`rewrite/equal_values.hpp`)");

// Three lags deep, a cycle-free pair of terms first: it once came back another graph from every second pass (the
// chain's values handed out in reverse, a multiply-add opened product first). A bank of four over a mono input: the
// step adding the mono terms once took the bank's shape, where `infer` gives the input's.
constexpr Graph lagged_thrice() {
    Input x{"x", Signal{kBlock}};
    Variable y;
    Expr a = y[n - 1];
    Expr b = a[n - 1];
    Expr c = b[n - 1];
    y[n] = x + 0.5 * a + 0.25 * x[n - 1] - 0.2 * b + 0.125 * c;
    return analysed(finish(Output{"y"} = y), numerics::Precision::Normal);
}
constexpr Graph bank_over_mono() {
    Input x{"x", Signal{4, 64}};
    Input m{"m", Signal{64}};
    Variable y;
    y[n] = 0.5 * y[n - 1] + m + m[n - 1] + x;
    return analysed(finish(Output{"y"} = y), numerics::Precision::Normal);
}

constexpr bool settled(Graph g) {
    for (const Machine& m : {kNoFma, zen2}) {
        const Graph once = rewrite::ordered_feedback_sums(g, m);
        Graph again = once;
        if (!infer(again).ok || !(again == once) || !(rewrite::ordered_feedback_sums(once, m) == once)) {
            return false;
        }
    }
    return true;
}
static_assert(settled(kBiquad) && settled(filter(Filter::Lagged)) && settled(filter(Filter::OnePole)) && settled(filter(Filter::MinusLast)) &&
                  settled(minus_first(true)) && settled(minus_first(false)) && settled(filter(Filter::Ladder)) &&
                  settled(lagged_thrice()) && settled(bank_over_mono()) && settled(sections(2)) && settled(sections(4)) &&
                  settled(filter(Filter::ByAParam)) && settled(filter(Filter::ByAComputedParam)) && settled(param_sections(2)),
              "a rewritten graph is analysed already, and the rule finds nothing more in it, fused or not");
static_assert(!(fused(lagged_thrice()) == lagged_thrice()) && !(fused(bank_over_mono()) == bank_over_mono()), "both are rewritten");

// A double sum over float terms: as written each is added to the double running sum; in ready order the two float
// terms would come first and be added in float, a rounding no precision grants (the second review of Decision 66).
constexpr Graph double_over_floats() {
    Input a{"a", Signal{kBlock}};
    Input b{"b", Signal{kBlock}};
    Input x{"x", Signal{kBlock}, f64};
    Variable y;
    y[n] = 0.5 * y[n - 1] + a + b + x;
    return analysed(finish(Output{"y"} = y), numerics::Precision::Normal);
}
static_assert(fused(double_over_floats()) == double_over_floats(), "a sum with a term narrower than itself stays as written");

// ---- sections in lanes ---------------------------------------------------------------------------------------------

// Value `v` of a merged cascade as written: `u` the link, `V` the variable it reads (or V's definition), a coefficient
// gathered by its first section's Param, a lag as [n-d], a product as its factors side by side.
std::string spelled(const Graph& g, const loops::graph::Facts& facts, std::int32_t v) {
    const std::int32_t p = facts.producer[static_cast<std::size_t>(facts.read_as[static_cast<std::size_t>(v)])];
    if (p < 0) {
        const auto named = [v](const NamedValue& in) { return in.value == v; };
        const std::string_view name = std::find_if(g.inputs.begin(), g.inputs.end(), named)->name.view();
        return std::string(name.substr(0, name.find('_')));
    }
    const Node& n = g.nodes[static_cast<std::size_t>(p)];
    const auto at = [&](std::size_t k) { return spelled(g, facts, n.inputs[k]); };
    const auto term = [&](std::size_t k) {
        const std::int32_t q = facts.producer[static_cast<std::size_t>(n.inputs[k])];
        const Algebra role = q < 0 ? Algebra::None : rules_of(g.nodes[static_cast<std::size_t>(q)].op).algebra;
        return role == Algebra::Add || role == Algebra::Subtract ? "(" + at(k) + ")" : at(k);
    };
    switch (rules_of(n.op).algebra) {
    case Algebra::Add:
        return at(0) + " + " + term(1);
    case Algebra::Subtract:
        return at(0) + " - " + term(1);
    case Algebra::Product:
        return at(0) + " " + at(1);
    case Algebra::ProductAdd:
        return "madd(" + at(0) + ", " + at(1) + ", " + at(2) + ")";
    default:
        break;
    }
    if (n.attr.kind == AttrKind::Delay) {
        return at(0) + "[n-" + std::to_string(n.attr.delay.samples) + "]";
    }
    return loops::graph::is_a_link(n) ? "u" : n.attr.kind == AttrKind::Recurrence ? "V" : n.op == op::Select ? at(2) : "?";
}

// The merged cascade's sum, `V = …`.
std::string merged_sum(const Graph& g) {
    const loops::graph::Facts facts = loops::graph::facts_of(g);
    std::string sum = "none";
    for (const Node& node : g.nodes) {
        sum = node.attr.kind == AttrKind::Recurrence ? "V = " + spelled(g, facts, node.inputs[0]) : sum;
    }
    return sum;
}

std::size_t cycles_of(const Graph& g) {
    const loops::graph::Facts facts = loops::graph::facts_of(g);
    const auto feedback = std::make_unique<loops::graph::Feedback>(loops::graph::find_feedback(g, facts));
    std::size_t cycles = 0;
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        cycles += feedback->cycle[i] == static_cast<std::int32_t>(i) ? 1 : 0;
    }
    return cycles;
}

int g_wrong = 0;

// Four Param sections as `df1_param` writes them, merged into one cycle over [4, time]. Its sum reads no other
// cycle, so it takes the ready order, the rule unchanged: the link's term `b0 u`, made this sample,
// last and fused, the section's own newest state before it, the older states first, at each age the input's before
// the section's own (5.2a's `lanes` order, measured better than fusing the newest state); on zen2 and the M1 alike. And
// with the oldest input term subtracted: the terms subtracted before the first added one summed and
// subtracted from it. Kept as written while the first term in ready order was subtracted, those lanes were priced
// 18.006 against 11.104 cycles a sample, still taken, and ran 1.72x slower than the cascade (B256, 4.99 against 2.90 ns).
void check_merged(const Graph& apart, const std::string& written, const std::string& ready) {
    for (const Machine& m : {zen2, m1_pro}) {
        const auto merged = std::make_unique<Graph>(rewrite::detail::linked_runs(apart, m));
        const auto once = std::make_unique<Graph>(rewrite::ordered_feedback_sums(*merged, m));
        const auto again = std::make_unique<Graph>(*once);
        const std::string as_merged = merged_sum(*merged);
        const std::string ordered = merged_sum(*once);
        const std::size_t cycles = cycles_of(*once);
        const bool fixed = infer(*again).ok && *again == *once && rewrite::ordered_feedback_sums(*once, m) == *once;
        if (as_merged != written || ordered != ready || cycles != 1 || !fixed) {
            ++g_wrong;
            std::printf("  merged: %s, ordered %s (%zu cycles), fixed %d\n", as_merged.c_str(), ordered.c_str(), cycles, fixed);
        }
    }
}

Graph minus_b2() {
    return lanes::series(4, [](Expr s, std::size_t j) {
        Param b0{lanes::named("b0", j)};
        Param b1{lanes::named("b1", j)};
        Param b2{lanes::named("b2", j)};
        Param a1{lanes::named("a1", j)};
        Param a2{lanes::named("a2", j)};
        Variable y;
        y[n] = b0 * s + b1 * s[n - 1] - b2 * s[n - 2] - a1 * y[n - 1] - a2 * y[n - 2];
        return Expr{y};
    });
}

// ---- numbers ----------------------------------------------------------------------------------------------

struct Lowpass {
    double b0;
    double b1;
    double b2;
    double a1;
    double a2;
    const char* name;
};
// RBJ low-passes, Q 1/sqrt 2 at 48 kHz, worked out in double (the runner needs a program's constants now).
constexpr Lowpass kLowpasses[] = {
    {1.7103058908716714e-06, 3.4206117817433428e-06, 1.7103058908716714e-06, -1.9962976017691223, 0.9963044429926857, "20 Hz"},
    {0.003916126660547383, 0.007832253321094766, 0.003916126660547383, -1.815341082704568, 0.8310055893467576, "1 kHz"},
    {0.2201947002729587, 0.4403894005459174, 0.2201947002729587, -0.3075663597922098, 0.18834516088404457, "10 kHz"},
};

// Low-pass `k` in direct form I, as written: the feed-forward terms, then the two of the state, the older a lag of the
// newer. Its coefficients rounded to float in either type, so that the double one is the same filter.
template <std::size_t K>
constexpr Graph lowpass(ElemType type) {
    const Lowpass& c = kLowpasses[K];
    const auto q = [](double v) { return f(static_cast<float>(v)); };
    Input x{"x", Signal{kBlock}, type};
    Variable y;
    Expr y1 = y[n - 1];
    Expr y2 = y1[n - 1]; // (the older state a lag of the newer, handed on in a register: the rule fires)
    y[n] = q(c.b0) * x + q(c.b1) * x[n - 1] + q(c.b2) * x[n - 2] - q(c.a1) * y1 - q(c.a2) * y2;
    return analysed(finish(Output{"y"} = y), numerics::Precision::Normal);
}

// The bench's four-section cascade (`benchmarks/bench_apps_dsp.cpp`), its older states lagged.
constexpr Graph cascade(ElemType type) {
    Input x{"x", Signal{kBlock}, type};
    Variable y1;
    Variable y2;
    Variable y3;
    Variable y4;
    const auto older = [](Variable& y) { return Expr{y[n - 1]}[n - 1]; }; // (the older state a lag of the newer)
    y1[n] = f(0.3f) * x + f(0.2f) * x[n - 1] - f(0.4f) * y1[n - 1] + f(0.1f) * older(y1);
    y2[n] = f(0.5f) * y1 + f(0.1f) * y1[n - 1] + f(0.3f) * y2[n - 1] - f(0.2f) * older(y2);
    y3[n] = f(0.4f) * y2 + f(0.2f) * y2[n - 1] - f(0.1f) * y3[n - 1] + f(0.15f) * older(y3);
    y4[n] = f(0.6f) * y3 + f(0.1f) * y3[n - 1] + f(0.2f) * y4[n - 1] - f(0.1f) * older(y4);
    return analysed(finish(Output{"y"} = y4), numerics::Precision::Normal);
}

template <auto Build>
struct Both {
    static constexpr Graph kGraph = Build(ElemType::F32);
    static constexpr planned::Held kWritten{planned::program_of(kGraph, zen2)};
    static constexpr planned::Held kRewritten{planned::program_of(rewrite::rewritten(kGraph, zen2), zen2)};
};

template <const loops::Program& P>
std::vector<double> ran(const std::vector<float>& x) {
    loops::Workspace<P> work(loops::Sizes{});
    loops::run<P>(loops::Feeds{x.data()}, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(P.outputs[0]));
    return std::vector<double>(y, y + kBlock);
}

std::vector<float> noise(std::size_t count) {
    std::vector<float> v(count);
    std::uint32_t state = 11;
    for (float& x : v) {
        state = state * 1664525u + 1013904223u;
        x = static_cast<float>(static_cast<std::int32_t>(state >> 8) % 2001 - 1000) * 0.001f;
    }
    return v;
}

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

// The rewritten program against the filter walked in double, at most twice as far from it as the program as written
// (and a float's rounding of the output), and within `bound` (where float can reach it: not at 20 Hz, whose poles sit
// so near the unit circle that each sample's rounding is amplified).
template <auto Build>
void check_numbers(const char* what, double bound) {
    const std::vector<float> x = noise(kBlock);
    const std::vector<double> widened(x.begin(), x.end());
    Inputs feeds{};
    feeds.push_back(InputFeed{widened.data(), 1});
    const auto out = std::make_unique<Signals<kBlock>>();
    const auto reference = std::make_unique<Graph>(Build(ElemType::F64));
    run(*reference, feeds, *out);
    std::vector<double> exact(kBlock);
    for (std::size_t t = 0; t < static_cast<std::size_t>(kBlock); ++t) {
        exact[t] = sample_at(*out, reference->outputs[0].value, 0, t);
    }
    const double written = error(ran<Both<Build>::kWritten.program>(x), exact);
    const double rewritten = error(ran<Both<Build>::kRewritten.program>(x), exact);
    std::printf("  %s: rewritten %.3g, as written %.3g against double\n", what, rewritten, written);
    if (!(rewritten <= 2 * written + 1e-7) || !(rewritten <= bound)) {
        ++g_wrong;
        std::printf("  %s: over the bound %g\n", what, bound);
    }
}

} // namespace

int main() {
    check_numbers<lowpass<0>>("low-pass 20 Hz", 1e-3);
    check_numbers<lowpass<1>>("low-pass 1 kHz", 1e-5);
    check_numbers<lowpass<2>>("low-pass 10 kHz", 1e-5);
    check_numbers<cascade>("the bench's cascade", 1e-5);
    check_merged(lanes::raw(4), "V = b0 u + b1 u[n-1] + b2 u[n-2] - a1 V[n-1] - a2 V[n-2]",
                 "V = madd(b0, u, b2 u[n-2] - a2 V[n-2] + b1 u[n-1] - a1 V[n-1])");
    check_merged(minus_b2(), "V = b0 u + b1 u[n-1] - b2 u[n-2] - a1 V[n-1] - a2 V[n-2]",
                 "V = madd(b0, u, b1 u[n-1] - (b2 u[n-2] + a2 V[n-2]) - a1 V[n-1])");
    if (g_wrong != 0) {
        std::printf("rewrite_feedback_sums_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("rewrite_feedback_sums_test: feedback sums in ready order, fused where granted, nothing else moved\n");
    return 0;
}
