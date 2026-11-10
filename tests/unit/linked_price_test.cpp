// Sections in lanes (`docs/sections-in-lanes.md`): the cost model reads a link (`loops/graph/cycle_chains.hpp`). A link
// `u = _chain(x, V)` is a lag of one step: a path starts afresh there, the link's own latency on it (the lanes shifted
// along the sections, `Latency::Add`), and closes at V's definition over one step; no path runs through its operand 1,
// the element before. The k sections fill bodies_of([k, time]) lane bodies, as many as the double lanes hold.
//
// The linked graphs' LR8 (four RBJ biquads, k = 4) and five peaks (k = 5), worked out by hand on zen2 and the M1: the
// merged cycle's chain is the link's step, its add and one section's path from its input to V (a multiply and the sum's
// four adds and subtracts as written), 18 cycles; each section of the k-cycle twin keeps its own recurrence, through
// y[n - 1] (a multiply and two subtracts), 9.
//
// K = 2, two RBJ biquads (the probe's Param cascade) and LR4 (two TPT stages): today's cascade as the planner is given
// it (`sums_and_state(parallel_form(·))`), against the merged graph as the merge emits it and double state widens it,
// priced as the price P prices it (`feedback_cycles(sums_and_state(·))`). The probe's estimate by hand put both K = 2
// sides at 11.0 cycles a sample on zen2 and LR4's at 20.0 and 22.0. The cascade stays in both: the probe measured
// it 27-52 % and 16-26 % faster.
//
// The gather makes its coefficients over [k, 1], not the domain the merged cycle's stage walks ([k, time]),
// so it leads into no cycle (`detail::leads_into`), as the planner runs it in a stage of its own before the cycle's
// (`planner/stages.hpp` `joins`, `planner/trail.hpp` `as_it_closes`): the merged price gathered is the inputs' at
// K = 2, and K = 8 takes the lanes, as the probe measured them (2.1-3.2x).
//
// The merged graph priced here at K = 2 and K = 8 is what the merge and the rules after it make. The Param cascade
// written section by section (`lanes::params_apart`), merged by `sections_in_lanes`' merge (`detail::linked_runs`,
// unpriced) and widened by `double_state`, is `merged_rbj` node for node, and after both rules the same graph as
// `merged_rbj` after them, which P prices.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "heinzel/frontend/declarations.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/graph/cycle_chains.hpp"
#include "heinzel/loops/graph/staging.hpp"
#include "heinzel/rewrite/rewritten.hpp"
#include "heinzel/rewrite/sections_in_lanes.hpp"
#include "heinzel/schedule/cost.hpp"
#include "heinzel/schedule/machine.hpp"
#include "heinzel/sema/analyze.hpp"

#include "hand_built_graph.hpp"
#include "linked_sections.hpp"
#include "same_graph.hpp"
#include "sections_in_lanes_graphs.hpp"

using namespace heinzel;
using namespace heinzel::testing::linked_sections;

namespace {

namespace graph = heinzel::loops::graph;

// Each cycle's stage alone (`cycle_stages`) and chain, by the node naming the cycle, in the order the facts plan them.
struct Cycles {
    std::vector<cost::Stage> stages{};
    std::vector<double> chains{};
};

Cycles cycles_of(const Graph& g, const Machine& m) {
    const graph::Facts facts = graph::facts_of(g);
    const graph::Feedback feedback = graph::find_feedback(g, facts);
    const std::vector<cost::Stage> alone = graph::cycle_stages(g, facts, feedback, m);
    const std::vector<double> chains = graph::cycle_chains(g, facts, feedback, m);
    Cycles found{};
    for (const std::int32_t ni : feedback.order) {
        const auto i = static_cast<std::size_t>(ni);
        if (feedback.cycle[i] == ni) {
            found.stages.push_back(alone[i]);
            found.chains.push_back(chains[i]);
        }
    }
    return found;
}

bool near(double a, double b) { return std::abs(a - b) < 1e-4; }

// ---- the linked graphs ----------------------------------------------------------------------------------------------

// The linked graph's one cycle: chain and depth the link, b0 u and the four adds and subtracts after it, 3 cycles each
// on both machines (the link's row and `_mul`'s are of the add's class): 18; its slots the link's, five multiplies' and
// four adds'; its loads `x` and the five coefficient values over [k, 1]; one voice laid along the sections, k of them in
// the double lanes. The twin's k cycles each a multiply and two subtracts from y[n - 1]: 9.
static_assert(zen2.add.latency == 3 && m1_pro.add.latency == 3 && rules_of(op::Chain).latency == Latency::Add &&
                  rules_of(op::Mul).latency == Latency::Add && rules_of(op::Sub).latency == Latency::Add,
              "the hand count: the link, a multiply, an add and a subtract each the add's 3 cycles");

bool biquads_hold(const char* what, const Graph& linked, const Graph& twin, std::int32_t k, const Machine& m) {
    const double chain = 18;
    const double own = 9;
    const std::int64_t lanes = m.vector_bytes / 8;
    const double bodies = static_cast<double>((k + lanes - 1) / lanes);
    const Cycles merged = cycles_of(linked, m);
    const Cycles apart = cycles_of(twin, m);
    bool ok = merged.stages.size() == 1 && apart.stages.size() == static_cast<std::size_t>(k);
    if (ok) {
        const cost::Stage& s = merged.stages[0];
        ok = s.chain == chain && merged.chains[0] == chain && s.depth == chain && s.slots == 10 && s.loads == 6 && s.bodies == bodies &&
             s.lanes == 1 && s.stores == 1;
        for (const double section : apart.chains) {
            ok = ok && section == own;
        }
    }
    if (!ok) {
        const cost::Stage s = merged.stages.empty() ? cost::Stage{} : merged.stages[0];
        std::printf("linked_price: %s: %zu cycle(s), chain %g depth %g slots %g loads %g bodies %g lanes %g; %zu apart\n", what,
                    merged.stages.size(), s.chain, s.depth, s.slots, s.loads, s.bodies, s.lanes, apart.stages.size());
    }
    return ok;
}

bool b2_graphs_hold() {
    bool ok = true;
    for (const Machine& m : {zen2, m1_pro}) {
        ok = biquads_hold("LR8 as four RBJ biquads", lr8(Form::Linked), lr8(Form::Cycles), 4, m) && ok;
        ok = biquads_hold("five peaks", peaks(Form::Linked), peaks(Form::Cycles), 5, m) && ok;
    }
    return ok;
}

// ---- K = 2 and K = 8: today's cascades, as the user writes them ------------------------------------------------

constexpr Index n{};

inline constexpr std::array<std::array<Name, 5>, 8> kParams{{
    {Name{"b0_0"}, Name{"b1_0"}, Name{"b2_0"}, Name{"a1_0"}, Name{"a2_0"}},
    {Name{"b0_1"}, Name{"b1_1"}, Name{"b2_1"}, Name{"a1_1"}, Name{"a2_1"}},
    {Name{"b0_2"}, Name{"b1_2"}, Name{"b2_2"}, Name{"a1_2"}, Name{"a2_2"}},
    {Name{"b0_3"}, Name{"b1_3"}, Name{"b2_3"}, Name{"a1_3"}, Name{"a2_3"}},
    {Name{"b0_4"}, Name{"b1_4"}, Name{"b2_4"}, Name{"a1_4"}, Name{"a2_4"}},
    {Name{"b0_5"}, Name{"b1_5"}, Name{"b2_5"}, Name{"a1_5"}, Name{"a2_5"}},
    {Name{"b0_6"}, Name{"b1_6"}, Name{"b2_6"}, Name{"a1_6"}, Name{"a2_6"}},
    {Name{"b0_7"}, Name{"b1_7"}, Name{"b2_7"}, Name{"a1_7"}, Name{"a2_7"}},
}};

// LR4's TPT stage at 2 kHz (`Plan/probes/audio-iir/skewed_lanes.cpp`): both stages alike.
inline constexpr float kGain = 0x1.0d9fd4p-3f;
inline constexpr float kDamping = 0x1.6a09e6p+0f;
inline constexpr float kHigh = 0x1.a96b78p-1f;
inline constexpr double kAround = static_cast<double>(kDamping + kGain);

// The lanes' probe's Param cascade: section k's coefficients its five Params.
Expr rbj_section(Expr x, std::size_t k) {
    Param b0{kParams[k][0].view()};
    Param b1{kParams[k][1].view()};
    Param b2{kParams[k][2].view()};
    Param a1{kParams[k][3].view()};
    Param a2{kParams[k][4].view()};
    Variable y;
    y[n] = b0 * x + b1 * x[n - 1] + b2 * x[n - 2] - a1 * y[n - 1] - a2 * y[n - 2];
    return y;
}

Expr tpt_stage(Expr in) {
    Variable s1;
    Variable s2;
    const Expr hp = (in - kAround * s1[n - 1] - s2[n - 1]) * static_cast<double>(kHigh);
    const Expr bp = static_cast<double>(kGain) * hp + s1[n - 1];
    s1[n] = static_cast<double>(kGain) * hp + bp;
    const Expr lp = static_cast<double>(kGain) * bp + s2[n - 1];
    s2[n] = static_cast<double>(kGain) * bp + lp;
    return lp;
}

Graph today_rbj(std::size_t k) {
    Input x{"x", Signal{kLength}};
    Expr chain = x;
    for (std::size_t j = 0; j + 1 < k; ++j) {
        chain = rbj_section(chain, j);
    }
    Output y{"y"};
    y = rbj_section(chain, k - 1);
    return y;
}

Graph today_lr4() {
    Input x{"x", Signal{kLength}};
    const Expr first = tpt_stage(x);
    Output y{"y"};
    y = tpt_stage(first);
    return y;
}

// ---- K = 2 and K = 8 merged, as the merge emits it and double state widens it ----------------------------------
//
// The link, section 0's arithmetic over [k, time] in double (each variable declared f64: Normal promises a merged DF1
// cycle double, as each section is today), what leaves it converted down after V and section k − 1 sliced out of that.

template <class Body>
Graph normal_graph_of(Body body) {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{kLength}, ValueCategory::Input);
    g.inputs.push_back(NamedValue{Name{"x"}, x});
    const auto y = body(g, x);
    g.outputs.push_back(NamedValue{Name{"y"}, y});
    set_default_precision(g, numerics::Precision::Normal);
    if (const Diagnostic d = infer(g); !d.ok) {
        fail(d.message.view());
    }
    return g;
}

std::int32_t f32_input(Graph& g, const Name& name, Shape shape) {
    const auto v = add_value(g, ElemType::F32, shape, ValueCategory::Input);
    g.values[static_cast<std::size_t>(v)].timeless = true;
    g.inputs.push_back(NamedValue{name, v});
    return v;
}

std::int32_t narrowed_last(Graph& g, std::int32_t v, std::int32_t k) {
    OpAttr down{};
    down.kind = AttrKind::Cast;
    down.cast.to = ElemType::F32;
    const auto narrow = add_node(g, op::Cast, v, kNoValue, kNoValue, down);
    return section_of(g, narrow, k - 1, Shape{kLength});
}

// Each coefficient one [k, 1] value: gathered from the k sections' Params as the merge gathers them (p_0, then
// where(e_j, p_j, ·) for j = 1 … k − 1, the one-hot masks shared), or held in a tensor input the host fills (no gather).
enum class Held : std::uint8_t { Gathered, Inputs };
inline constexpr std::array<Name, 5> kTensors{Name{"b0"}, Name{"b1"}, Name{"b2"}, Name{"a1"}, Name{"a2"}};

Biquad rbj_coefficients(Graph& g, Held held, std::size_t k) {
    std::vector<std::int32_t> masks{}; // (e_j at j - 1)
    for (std::size_t j = 1; j < k && held == Held::Gathered; ++j) {
        std::vector<double> one_hot(k, 0);
        one_hot[j] = 1;
        masks.push_back(per_section(g, one_hot));
    }
    Biquad c{};
    for (std::size_t i = 0; i < c.size(); ++i) {
        if (held == Held::Inputs) {
            c[i] = f32_input(g, kTensors[i], Shape{static_cast<std::int32_t>(k), 1});
            continue;
        }
        c[i] = f32_input(g, kParams[0][i], Shape{});
        for (std::size_t j = 1; j < k; ++j) {
            const auto section = f32_input(g, kParams[j][i], Shape{});
            c[i] = add_node(g, op::Select, masks[j - 1], section, c[i]);
        }
    }
    return c;
}

Graph merged_rbj(Held held, std::size_t k) {
    return normal_graph_of([held, k](Graph& g, std::int32_t x) {
        const Biquad c = rbj_coefficients(g, held, k);
        const auto outputs = variable(g, Shape{static_cast<std::int32_t>(k), kLength});
        const auto u = add_node(g, op::Chain, x, outputs);
        biquad(g, u, c, outputs);
        return narrowed_last(g, outputs, static_cast<std::int32_t>(k));
    });
}

// `tpt_stage` over the sections: its gains one scalar each, the same in both. Defines `s1` and `s2`; returns lp.
std::int32_t tpt_stages(Graph& g, std::int32_t in, std::int32_t s1, std::int32_t s2) {
    const auto around = scalar(g, kAround);
    const auto high = scalar(g, static_cast<double>(kHigh));
    const auto gain = scalar(g, static_cast<double>(kGain));
    const auto first_before = lag(g, s1, 1);
    const auto damped = add_node(g, op::Mul, around, first_before);
    const auto less = add_node(g, op::Sub, in, damped);
    const auto second_before = lag(g, s2, 1);
    const auto error = add_node(g, op::Sub, less, second_before);
    const auto hp = add_node(g, op::Mul, error, high);
    const auto gained = add_node(g, op::Mul, gain, hp);
    const auto first_again = lag(g, s1, 1);
    const auto bp = add_node(g, op::Add, gained, first_again);
    const auto gained_again = add_node(g, op::Mul, gain, hp);
    const auto first_next = add_node(g, op::Add, gained_again, bp);
    settle(g, first_next, s1);
    const auto band = add_node(g, op::Mul, gain, bp);
    const auto second_again = lag(g, s2, 1);
    const auto lp = add_node(g, op::Add, band, second_again);
    const auto band_again = add_node(g, op::Mul, gain, bp);
    const auto second_next = add_node(g, op::Add, band_again, lp);
    settle(g, second_next, s2);
    return lp;
}

Graph merged_lr4() {
    return normal_graph_of([](Graph& g, std::int32_t x) {
        const auto outputs = variable(g, Shape{2, kLength});
        const auto s1 = variable(g, Shape{2, kLength});
        const auto s2 = variable(g, Shape{2, kLength});
        const auto u = add_node(g, op::Chain, x, outputs);
        const auto lp = tpt_stages(g, u, s1, s2);
        settle(g, lp, outputs);
        return narrowed_last(g, outputs, 2);
    });
}

// ---- K = 2 and K = 8 priced ------------------------------------------------------------------------------------

// Today's cascade as the planner is given it.
double today_priced(Graph g, const Machine& m) {
    if (const Diagnostic d = infer(g); !d.ok) {
        fail(d.message.view());
    }
    const Graph form = rewrite::parallel_form(g, m);
    return graph::feedback_cycles(rewrite::sums_and_state(form, m), m);
}

// Which side P takes: the lanes where the merged price is below today's by more than the tie, else the cascade.
enum class Side : std::uint8_t { Cascade, Lanes };

// Today's and the merged price as pinned, the merged cycle's chain, and the side P takes.
bool prices_hold(const char* what, const Graph& today, const Graph& merged, double today_pin, double merged_pin, double chain_pin, Side side) {
    const double now = today_priced(today, zen2);
    const Graph summed = rewrite::sums_and_state(merged, zen2);
    const double lanes = graph::feedback_cycles(summed, zen2);
    const Cycles cycles = cycles_of(summed, zen2);
    const double chain = cycles.chains.size() == 1 ? cycles.chains[0] : -1;
    const Side taken = lanes * rewrite::detail::kTie < now ? Side::Lanes : Side::Cascade;
    const bool ok = near(now, today_pin) && near(lanes, merged_pin) && chain == chain_pin && taken == side;
    if (!ok) {
        std::printf("linked_price: %s: today %.6f, merged %.6f (its chain %g, %zu cycle(s)), the %s\n", what, now, lanes, chain,
                    cycles.chains.size(), taken == Side::Lanes ? "lanes" : "cascade");
    }
    return ok;
}

// The pair's merged cycle: its own recurrence through V[n - 1] (a multiply, a subtract and the multiply-add that adds
// the link's term last: 3 + 3 + 5) is its chain, the link's step (its add and that multiply-add: 8) shorter; one body;
// 16 slots, the conversions of a double cycle among them (`x` read by the link, the five float coefficients, V down
// after it). With the five [2, 1] values held in inputs the merged price is 11.104, today's 11.005: the cascade by the
// tie alone, as the probe's estimate had it (11.0 and 11.0). Gathered, the same: the gather's five selects make [2, 1],
// which the stage does not walk, so they lead into no cycle; counted in the merged stage every sample, they gave
// 11.745. LR4's merged chain is the link's step through the stage (an add, two subtracts, a multiply and two
// multiply-adds: 22); today's 22 too, the second stage's `in - k s1[n-1] - s2[n-1]` in ready order, the first stage's
// output last (20 as the probe's estimate while that sum, its first term subtracted, stayed as written).
bool k2_hold() {
    bool ok = prices_hold("two RBJ biquads, held in inputs", today_rbj(2), merged_rbj(Held::Inputs, 2), 11.0054, 11.1041, 11, Side::Cascade);
    ok = prices_hold("two RBJ biquads, gathered by G1", today_rbj(2), merged_rbj(Held::Gathered, 2), 11.0054, 11.1041, 11, Side::Cascade) && ok;
    ok = prices_hold("LR4", today_lr4(), merged_lr4(), 22.0107, 22.0002, 22, Side::Cascade) && ok;
    return ok;
}

// K = 8 RBJ biquads gathered (35 selects over [8, 1]): the merged cycle's chain as the pair's, its eight sections two
// bodies of four double lanes, against today's eight cycles, 49.511: the lanes, 3.08x (the probe measured them 2.1-3.2x
// faster). The gather counted in the stage priced it 51.000, the cascade.
bool k8_holds() {
    const Graph today = today_rbj(8);
    const Graph merged = merged_rbj(Held::Gathered, 8);
    return prices_hold("eight RBJ biquads, gathered by G1", today, merged, 49.5110, 16.0977, 11, Side::Lanes);
}

// The merge and double state make the merged graph priced above, at K = 2 and K = 8.
bool b5_and_b6_make_it() {
    bool ok = true;
    for (const std::size_t k : {2, 8}) {
        const Graph apart = lanes::params_apart(k);
        const Graph linked = rewrite::detail::linked_runs(apart, zen2);
        const Graph by_hand = merged_rbj(Held::Gathered, k);
        const Graph wide = rewrite::double_state(linked, zen2);
        const Graph planned_wide = rewrite::sums_and_state(linked, zen2);
        const Graph planned_by_hand = rewrite::sums_and_state(by_hand, zen2);
        const bool widened = heinzel::testing::same_graph(wide, by_hand);
        const bool planned = heinzel::testing::same_graph(planned_wide, planned_by_hand);
        if (!widened || !planned) {
            ok = false;
            std::printf("linked_price: %zu RBJ biquads merged and widened: double state %d, both rules %d\n", k, widened, planned);
        }
    }
    return ok;
}

} // namespace

int main() {
    try {
        bool ok = b2_graphs_hold();
        ok = k2_hold() && ok;
        ok = k8_holds() && ok;
        ok = b5_and_b6_make_it() && ok;
        return ok ? 0 : 1;
    } catch (const heinzel::error& e) {
        std::printf("linked_price: %.*s\n", static_cast<int>(e.message().size()), e.message().data());
        return 1;
    }
}
