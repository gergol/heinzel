// P, the price that decides sections in lanes (`rewrite/sections_in_lanes.hpp`, `docs/sections-in-lanes.md`). The lanes
// are taken where the cost model prices the graph's feedback stages below today's cascade by more than `kTie`; the
// cascade wins a tie. The lanes' probe's corpus, each graph written as its probe wrote it: Param cascades of K = 2 to 8
// RBJ sections, and the constant chains of `Plan/probes/audio-iir/skewed_lanes.cpp`, LR4 (two TPT stages), LR8 (four
// RBJ biquads) and `sat4` (LR8's biquads, saturated). Each is given to the rule as `rewritten` gives it, after the
// parallel form, on zen2. Its choice is pinned against what zen2 measured wherever the probe found a clear winner:
// every round and block past `kTie` the same way. K = 3 measured a tie and goes either way. That the merge links each
// run whole, as one cycle, is checked too, so a cascade kept is P's choice and not the merge's decline. The prices are
// printed, not pinned: the choice is judged, not the price. Beside the corpus, cases the lanes' review found priced
// unlike the planner stages them.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <vector>

#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/rewrite/parallel_form.hpp"
#include "heinzel/rewrite/sections_in_lanes.hpp"
#include "heinzel/schedule/machine.hpp"

#include "sections_in_lanes_graphs.hpp"

using namespace heinzel;
using lanes::Precision;

namespace {

namespace graph = heinzel::loops::graph;

constexpr Index n{};
constexpr std::int32_t kBlock = 256;

// ---- the corpus, as its probes write it ----------------------------------------------------------------------------

// `skewed_lanes.cpp`'s sections and their numbers.
struct Bq {
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;
};
struct Tpt {
    float g;
    float r2;
    float h;
};
constexpr Bq kLrA{0x1.c28a14p-7f, 0x1.c28a14p-6f, 0x1.c28a14p-7f, -0x1.8f1e2ep+0f, 0x1.3a64fep-1f}; // Q 0.541
constexpr Bq kLrB{0x1.fbf5c2p-7f, 0x1.fbf5c2p-6f, 0x1.fbf5c2p-7f, -0x1.c1fc24p+0f, 0x1.a3b7a4p-1f}; // Q 1.307
constexpr Bq kLr8[4]{kLrA, kLrB, kLrA, kLrB};
constexpr Tpt kLr4{0x1.0d9fd4p-3f, 0x1.6a09e6p+0f, 0x1.a96b78p-1f};

constexpr double f(float v) { return static_cast<double>(v); }

Expr df1(Expr u, const Bq& c) {
    Variable y;
    const Expr s = f(c.b0) * u + f(c.b1) * u[n - 1] + f(c.b2) * u[n - 2];
    y[n] = s - f(c.a1) * y[n - 1] - f(c.a2) * y[n - 2];
    return y;
}

Expr sat(Expr u, const Bq& c) {
    Variable y;
    const Expr v = f(c.b0) * u + f(c.b1) * u[n - 1] + f(c.b2) * u[n - 2] - f(c.a1) * y[n - 1] -
                   f(c.a2) * y[n - 2] + 0.1;
    const Expr k = clamp(v, -1.0, 1.0);
    y[n] = k - k * k * k * (1.0 / 3.0);
    return y;
}

Expr tpt(Expr in, const Tpt& c) {
    Variable s1;
    Variable s2;
    const Expr hp = (in - f(c.r2 + c.g) * s1[n - 1] - s2[n - 1]) * f(c.h);
    const Expr bp = f(c.g) * hp + s1[n - 1];
    s1[n] = f(c.g) * hp + bp;
    const Expr lp = f(c.g) * bp + s2[n - 1];
    s2[n] = f(c.g) * bp + lp;
    return lp;
}

// The probe's Param cascade: section j's coefficients its five f32 Params `b0_j` ... `a2_j`.
template <std::size_t K>
Graph params() {
    return lanes::raw(K, Precision::Normal, f32, kBlock);
}

Graph lr4() {
    return lanes::series(2, [](Expr s, std::size_t) { return tpt(s, kLr4); }, Precision::Normal, f32, 1, kBlock);
}
Graph lr8() {
    return lanes::series(4, [](Expr s, std::size_t j) { return df1(s, kLr8[j]); }, Precision::Normal, f32, 1, kBlock);
}
Graph sat4() {
    return lanes::series(4, [](Expr s, std::size_t j) { return sat(s, kLr8[j]); }, Precision::Normal, f32, 1, kBlock);
}

// KFR's three-section EQ (`benchmarks/iir_arms.hpp`), its first section's value an output too: the parallel form
// declines it, and the merge links sections 1 and 2 alone.
constexpr Bq kEq3[3]{
    {0x1.fc38e2p-1f, -0x1.fc38e2p+0f, 0x1.fc38e2p-1f, -0x1.fc355p+0f, 0x1.f878e4p-1f},
    {0x1.0b4082p+0f, -0x1.e533bcp+0f, 0x1.bc4618p-1f, -0x1.e533bcp+0f, 0x1.d2c71cp-1f},
    {0x1.3d86bap-3f, 0x1.3d86bap-2f, 0x1.3d86bap-3f, -0x1.3d86bap-1f, 0x1.ec35d2p-3f},
};
Graph eq3_read_after_the_first() {
    Input x{"x", Signal{kBlock}};
    const Expr first = df1(x, kEq3[0]);
    const Expr second = df1(first, kEq3[1]);
    const Expr third = df1(second, kEq3[2]);
    Output y{"y"};
    Output also{"first"};
    y = third;
    also = first;
    return lanes::analysed(finish(y, also), Precision::Normal);
}

// ---- what zen2 measured --------------------------------------------------------------------------------------------

// A case: its name in the probe, its graph, its sections, and today's cascade's time over the lanes', the least and the
// most of the probe's three rounds at every block it ran (B32, B256 and the whole stream; the constant chains B1 too):
// the lanes with their coefficients laid out before the run; with the gather no clear winner flips, and K = 4 at B32
// comes onto the tie's line (1.03-1.07).
struct Measured {
    const char* name;
    Graph (*graph)();
    std::int32_t k;
    double least;
    double most;
};

inline constexpr Measured kCorpus[] = {
    {"params2", params<2>, 2, 0.66, 0.79},
    {"params3", params<3>, 3, 0.97, 1.09},
    {"params4", params<4>, 4, 1.08, 1.24},
    {"params5", params<5>, 5, 1.44, 2.00},
    {"params6", params<6>, 6, 1.46, 2.12},
    {"params7", params<7>, 7, 1.65, 2.49},
    {"params8", params<8>, 8, 2.05, 3.19},
    {"lr4", lr4, 2, 0.60, 0.86},
    {"lr8", lr8, 4, 1.09, 1.27},
    {"sat4", sat4, 4, 1.04, 1.51},
};

enum class Side : std::uint8_t { Cascade, Lanes, Tie };

constexpr const char* named(Side s) { return s == Side::Cascade ? "the cascade" : s == Side::Lanes ? "the lanes" : "a tie"; }

// The probe's clear winner: every round and block past `kTie` the same way.
constexpr Side measured(const Measured& c) {
    if (c.least > rewrite::detail::kTie) {
        return Side::Lanes;
    }
    return c.most * rewrite::detail::kTie < 1 ? Side::Cascade : Side::Tie;
}

// ---- the rule's choice ---------------------------------------------------------------------------------------------

// The links of `g`, and its feedback cycles.
std::int32_t links_in(const Graph& g) {
    std::int32_t count = 0;
    for (const Node& node : g.nodes) {
        count += graph::is_a_link(node) ? 1 : 0;
    }
    return count;
}
std::int32_t cycles_in(const Graph& g) {
    const graph::Facts facts = graph::facts_of(g);
    const graph::Feedback feedback = graph::find_feedback(g, facts);
    std::int32_t count = 0;
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        count += feedback.cycle[i] == static_cast<std::int32_t>(i) ? 1 : 0;
    }
    return count;
}

// Case `c` given to the rule as `rewritten` gives it: the merge links its k sections as one cycle, and the rule takes
// the merge where the probe measured the lanes, keeps the cascade where it measured the cascade, and either at a tie.
[[gnu::noinline]] bool decided_as_measured(const Measured& c) {
    const auto form = std::make_unique<Graph>(rewrite::parallel_form(c.graph(), zen2));
    const auto merged = std::make_unique<Graph>(rewrite::detail::linked_runs(*form, zen2));
    const auto chosen = std::make_unique<Graph>(rewrite::sections_in_lanes(*form, zen2));
    const double today = rewrite::detail::feedback_priced(*form, zen2);
    const double lanes = rewrite::detail::feedback_priced(*merged, zen2);
    const bool whole = cycles_in(*form) == c.k && cycles_in(*merged) == 1 && links_in(*merged) == 1;
    const bool taken = *chosen == *merged;
    const bool kept = *chosen == *form;
    const Side want = measured(c);
    const Side side = taken ? Side::Lanes : Side::Cascade;
    const bool right = whole && taken != kept && (want == Side::Tie || want == side);
    std::printf("lanes_price: %-8s K = %d: today %6.3f, lanes %6.3f cycles a sample, today / lanes %.3f: %s; measured %.2f-%.2f: %s%s\n",
                c.name, c.k, today, lanes, today / lanes, named(side), c.least, c.most, named(want), right ? "" : "  <- WRONG");
    if (!whole) {
        std::printf("  %s: %d cycle(s) as written, %d merged, %d link(s)\n", c.name, cycles_in(*form), cycles_in(*merged), links_in(*merged));
    }
    return right;
}

// A lone section before a linked pair: the planner runs the lone section's cycle in a stage of its own and the linked
// one skewed in another (`s0 loops | a0`, then `s1 skewed`), and the model prices them so, apart, which keeps the
// cascade. Priced as one stage, the lanes were taken and ran 1.46x slower (a first attempt: 2.80 against 4.10 ns a
// sample, zen2, `ranges_cascade_test`'s cascade).
[[gnu::noinline]] bool lone_section_keeps_the_cascade() {
    const auto form = std::make_unique<Graph>(rewrite::parallel_form(eq3_read_after_the_first(), zen2));
    const auto merged = std::make_unique<Graph>(rewrite::detail::linked_runs(*form, zen2));
    const auto chosen = std::make_unique<Graph>(rewrite::sections_in_lanes(*form, zen2));
    const double today = rewrite::detail::feedback_priced(*form, zen2);
    const double lanes = rewrite::detail::feedback_priced(*merged, zen2);
    const bool pair = cycles_in(*form) == 3 && cycles_in(*merged) == 2 && links_in(*merged) == 1;
    const bool right = pair && *chosen == *form;
    std::printf("lanes_price: a lone section, then a linked pair: today %6.3f, lanes %6.3f cycles a sample: %s%s\n", today, lanes,
                *chosen == *form ? "the cascade" : "the lanes", right ? "" : "  <- WRONG");
    return right;
}

// K = 2 behind cheap work in front of the run, over [time] where the linked cycle walks [k, time]: the planner runs it in
// a stage of maps of its own, and the model prices it there, beside the lanes. Priced nowhere, the lanes were
// taken (`x * g`: 11.104 against 11.504) and ran 1.46x slower at B256 (knobs 1.51x). The lanes' price is now above the
// same sections' without the work, and the cascade is kept, by the tie (`x * g + 0.25` is still taken: its limits).
Graph gained(bool knobs) {
    Input x{"x", Signal{kBlock}};
    Param g{"g"};
    const Expr front = x * g;
    Expr s = front;
    for (std::size_t j = 0; j < 2; ++j) {
        s = knobs ? lanes::knob_peak(s, j) : lanes::df1_param(s, j, f32);
    }
    Output y{"y"};
    y = s;
    return lanes::analysed(y, Precision::Normal);
}
Graph summed() {
    Input x{"x", Signal{kBlock}};
    Input x2{"x2", Signal{kBlock}};
    const Expr front = x + x2;
    Expr s = front;
    for (std::size_t j = 0; j < 2; ++j) {
        s = lanes::df1_param(s, j, f32);
    }
    Output y{"y"};
    y = s;
    return lanes::analysed(y, Precision::Normal);
}
Graph gained_params() { return gained(false); }
Graph gained_knobs() { return gained(true); }
Graph bare_knobs() { return lanes::series(2, lanes::knob_peak, Precision::Normal, f32, 1, kBlock); }

[[gnu::noinline]] bool work_in_front_keeps_the_cascade(const char* name, Graph (*fed)(), Graph (*bare)()) {
    const auto form = std::make_unique<Graph>(rewrite::parallel_form(fed(), zen2));
    const auto merged = std::make_unique<Graph>(rewrite::detail::linked_runs(*form, zen2));
    const auto chosen = std::make_unique<Graph>(rewrite::sections_in_lanes(*form, zen2));
    const auto bare_form = std::make_unique<Graph>(rewrite::parallel_form(bare(), zen2));
    const auto alone = std::make_unique<Graph>(rewrite::detail::linked_runs(*bare_form, zen2));
    const double today = rewrite::detail::feedback_priced(*form, zen2);
    const double lanes = rewrite::detail::feedback_priced(*merged, zen2);
    const double without = rewrite::detail::feedback_priced(*alone, zen2);
    const bool right = links_in(*merged) == 1 && lanes > without && *chosen == *form;
    std::printf("lanes_price: %-12s K = 2: today %6.3f, lanes %6.3f (%6.3f without the work) cycles a sample: %s%s\n", name, today, lanes, without,
                *chosen == *form ? "the cascade" : "the lanes", right ? "" : "  <- WRONG");
    return right;
}

// x and x2 each through four Param one-poles sharing their Params, written section by section for both channels, or
// chain after chain: the planner runs the eight cycles in one stage either way (`chains_on` chains a cycle on that
// reads nothing of the stage, too), and the model prices them so, alike. Joined only where one read the stage,
// written section by section they were priced as eight stages (40.0 against the lanes' 16.0), taken, and ran 1.66x
// slower at B256.
Graph stereo(bool interleaved) {
    Input x{"x", Signal{kBlock}};
    Input x2{"x2", Signal{kBlock}};
    std::vector<Param> a{};
    std::vector<Param> p{};
    for (std::size_t j = 0; j < 4; ++j) {
        a.emplace_back(lanes::named("a", j));
        p.emplace_back(lanes::named("p", j));
    }
    Expr l = x;
    Expr r = x2;
    for (std::size_t q = 0; q < 8; ++q) { // (l0 r0 l1 r1 ... section by section, l0 l1 l2 l3 r0 ... chain after chain)
        const bool left = interleaved ? q % 2 == 0 : q < 4;
        const std::size_t j = interleaved ? q / 2 : q % 4;
        Expr& s = left ? l : r;
        s = lanes::one_pole(s, a[j], p[j]);
    }
    Output y{"y"};
    Output y2{"y2"};
    y = l;
    y2 = r;
    return lanes::analysed(finish(y, y2), Precision::Normal);
}

[[gnu::noinline]] bool channels_keep_the_cascade() {
    const auto across = std::make_unique<Graph>(rewrite::parallel_form(stereo(true), zen2));
    const auto along = std::make_unique<Graph>(rewrite::parallel_form(stereo(false), zen2));
    const auto merged = std::make_unique<Graph>(rewrite::detail::linked_runs(*across, zen2));
    const auto chosen = std::make_unique<Graph>(rewrite::sections_in_lanes(*across, zen2));
    const double today = rewrite::detail::feedback_priced(*across, zen2);
    const double written = rewrite::detail::feedback_priced(*along, zen2);
    const double lanes = rewrite::detail::feedback_priced(*merged, zen2);
    const bool right = cycles_in(*across) == 8 && links_in(*merged) == 2 && today == written && *chosen == *across;
    std::printf("lanes_price: two channels of four, section by section: today %6.3f (%6.3f chain after chain), lanes %6.3f cycles a sample: %s%s\n",
                today, written, lanes, *chosen == *across ? "the cascade" : "the lanes", right ? "" : "  <- WRONG");
    return right;
}

// Bands of four Param sections, each behind a gain of its own, summed: the planner runs each band in a stage of its
// own, its cost model declining to chain the next band's first section on (`cheaper_joined`), and the model prices
// them so, each band as alone. Chained on without that call, the second band's first three sections joined
// the first band's stage (two bands 51.011, against 46.016 apart).
Graph bands(std::size_t count) {
    Input x{"x", Signal{kBlock}};
    const auto band = [&](std::size_t b) {
        const Param g{lanes::named("g", b)};
        const Param h{lanes::named("h", b)};
        const Expr gained = x * g;
        Expr s = gained;
        for (std::size_t j = 0; j < 4; ++j) {
            s = lanes::df1_param(s, 4 * b + j, f32);
        }
        return Expr{s * h};
    };
    Expr sum = band(0);
    for (std::size_t b = 1; b < count; ++b) {
        sum = sum + band(b);
    }
    Output y{"y"};
    y = sum;
    return lanes::analysed(y, Precision::Normal);
}

[[gnu::noinline]] bool bands_priced_apart() {
    const auto one = std::make_unique<Graph>(rewrite::parallel_form(bands(1), zen2));
    const auto two = std::make_unique<Graph>(rewrite::parallel_form(bands(2), zen2));
    const double alone = rewrite::detail::feedback_priced(*one, zen2);
    const double both = rewrite::detail::feedback_priced(*two, zen2);
    const bool right = cycles_in(*two) == 8 && both > 2 * alone - 1e-9 && both < 2 * alone + 1e-9;
    std::printf("lanes_price: two bands of four, each behind its gain: today %6.3f, one band %6.3f cycles a sample%s\n", both, alone,
                right ? "" : "  <- WRONG");
    return right;
}

// The rule, priced, is a constant expression, as `rewritten` needs it: two constant one-poles merged, then
// priced, whichever side the price takes (the probe measured no such chain).
constexpr bool priced_while_compiling() {
    const Graph g = lanes::series(2, lanes::one_pole_constant);
    const Graph merged = rewrite::detail::linked_runs(g, zen2);
    const Graph h = rewrite::sections_in_lanes(g, zen2);
    return !(merged == g) && (h == g || h == merged);
}
static_assert(priced_while_compiling(), "two constant one-poles, merged and priced while compiling");

} // namespace

int main() {
    int wrong = 0;
    for (const Measured& c : kCorpus) {
        wrong += decided_as_measured(c) ? 0 : 1;
    }
    wrong += lone_section_keeps_the_cascade() ? 0 : 1;
    wrong += work_in_front_keeps_the_cascade("x * g", gained_params, params<2>) ? 0 : 1;
    wrong += work_in_front_keeps_the_cascade("knobs, x * g", gained_knobs, bare_knobs) ? 0 : 1;
    wrong += work_in_front_keeps_the_cascade("x + x2", summed, params<2>) ? 0 : 1;
    wrong += channels_keep_the_cascade() ? 0 : 1;
    wrong += bands_priced_apart() ? 0 : 1;
    if (wrong != 0) {
        std::printf("lanes_price_test: %d of %zu cases not decided as measured\n", wrong, std::size(kCorpus) + 6);
        return 1;
    }
    return 0;
}
