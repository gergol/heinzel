// Adversarial review of the V6 slice — affine views, values made in parts, generated ramps, the past
// of an INPUT, and feedback state handed on in registers — against the reference walk
// (`tests/reference/interpret.hpp`) and against loops by hand, bit for bit.
//
//   PART A — what held up, kept as regression coverage: view CHAINS and views read by a product, a
//            lag, a fold and a join; lanes over a transposed and a stepped operand; the past of an
//            INPUT at short block lengths, in a cycle, of rank 3, inside a fold's, a product's and an
//            order statistic's stage; banks at four widths with and without a lagged feed; a cycle
//            whose span holds a fold and a product that are no part of it; pads on the lane axis, of
//            a view, with a NaN fill, read through a window, and a join read by a product; ramps of
//            rank 1 and 3; a workspace reset and re-run; four refusals by name.
//
//   PART B — three findings, each a check that FAILED when the review was written (at `257c5716`), fixed since:
//     B1  a program needing more loop nests than there are failed UNNAMED while the "does it lower" predicate said yes
//     B2  handed-on state did not compose: a statement that is handed on AND reads handed-on state
//         kept a stale register, and lowering accepted the schedule that asked for it
//     B3  an affine view between a cheap producer and its window switched the inline decision off

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/notation/einsum.hpp"
#include "heinzel/sema/analyze.hpp"

#include "canaried.hpp"
#include "edited_schedules.hpp"
#include "interpret.hpp"
#include "planned.hpp"
#include "tree_facts.hpp"

#include <bit>
#include <cstdio>
#include <memory>
#include <vector>

using namespace heinzel;

namespace {

namespace loops = heinzel::loops;

constexpr std::int32_t kT = 19; // nineteen samples: two lanes of eight and three more
constexpr Index n{};

constexpr Graph analysed(Graph g) {
    if (!infer(g).ok) {
        fail("nest_v6_review_test: a graph did not resolve");
    }
    return g;
}

int g_wrong = 0;

// Sixteenths: every product and sum below is exact in float and in double alike.
std::vector<float> noise(std::size_t n, std::uint32_t seed) {
    std::vector<float> v(n);
    for (float& x : v) {
        seed = seed * 1664525u + 1013904223u;
        x = static_cast<float>(static_cast<std::int32_t>(seed >> 8) % 2001 - 1000) * 0.0625f;
    }
    return v;
}

std::int64_t width_of(const Graph& g, std::int32_t v) {
    const Shape& shape = g.values[static_cast<std::size_t>(v)].shape;
    std::int64_t width = 1;
    for (std::int32_t a = 0; a + 1 < shape.rank; ++a) {
        width *= extent_at(shape, a).encoded;
    }
    return width;
}

// Every element of every output of `P`, against the reference walk of the same graph, BIT for bit —
// a NaN fill has to arrive as the one the walk writes.
template <std::int32_t Block, const loops::Program& P>
void against(const char* what, const Graph& g, std::uint32_t seed, int runs) {
    std::vector<std::vector<float>> held;
    std::vector<std::vector<double>> widened;
    loops::Feeds nest_feeds{};
    Inputs feeds{};
    for (std::size_t i = 0; i < g.inputs.size(); ++i) {
        held.push_back(noise(static_cast<std::size_t>(width_of(g, g.inputs[i].value) * Block), seed + static_cast<std::uint32_t>(i)));
        widened.emplace_back(held[i].begin(), held[i].end());
    }
    for (std::size_t i = 0; i < g.inputs.size(); ++i) {
        nest_feeds[i] = held[i].data();
        feeds.push_back(InputFeed{widened[i].data(), static_cast<std::int32_t>(held[i].size()) / Block});
    }
    const auto out = std::make_unique<Signals<Block>>();
    run(g, feeds, *out);
    canaried::Workspace<P> work(loops::Sizes{});
    for (int r = 0; r < runs; ++r) { // each from silence: `reset` zeroes the feed's tail a head reads (Plan/archive/14-dsl/05)
        work.reset();
        canaried::run<P>(nest_feeds, work);
    }
    int bad = 0;
    for (std::size_t o = 0; o < g.outputs.size(); ++o) {
        const std::int32_t made = g.outputs[o].value;
        const auto width = static_cast<std::size_t>(width_of(g, made));
        const float* y = work.template data<float>(static_cast<std::size_t>(P.outputs[o]));
        // (a constant holds one sample: the same at every one)
        const bool constant = P.buffers[static_cast<std::size_t>(P.outputs[o])].home == loops::Home::Constant;
        for (std::size_t k = 0; k < width * static_cast<std::size_t>(Block); ++k) {
            const auto want = static_cast<float>(
                sample_at(*out, made, static_cast<std::int32_t>(k % width), static_cast<std::int32_t>(k / width)));
            const float got = y[constant ? k % width : k];
            if (std::bit_cast<std::uint32_t>(got) != std::bit_cast<std::uint32_t>(want) && ++bad <= 2) {
                std::printf("  %-38s out %zu [%zu]: got %a want %a\n", what, o, k, static_cast<double>(got), static_cast<double>(want));
            }
        }
    }
    g_wrong += bad != 0 ? 1 : 0;
    std::printf("  %-38s %s\n", what, bad == 0 ? "ok" : "WRONG");
}

// ---- the views the notation has no builder for ----------------------------------------------------
constexpr OpAttr view_attr(ViewKind kind, std::int32_t axis) {
    OpAttr a{};
    a.kind = AttrKind::View;
    a.view.kind = kind;
    a.view.axis = axis;
    return a;
}
constexpr Expr flip(Expr x, std::int32_t axis) { return detail::append_unary(x, op::Flip, view_attr(ViewKind::Flip, axis)); }
constexpr Expr reshaped(Expr x, ViewKind kind, std::int32_t axis) { return detail::append_unary(x, op::Reshape, view_attr(kind, axis)); }
constexpr Expr padded(Expr x, std::int32_t axis, std::int32_t before, std::int32_t after, double fill) {
    OpAttr a = view_attr(ViewKind::Pad, axis);
    a.view.pad_before = before;
    a.view.pad_after = after;
    a.view.pad_bits = std::bit_cast<std::uint64_t>(fill);
    return detail::append_unary(x, op::Pad, a);
}
constexpr Expr joined(Expr a, Expr b, std::int32_t axis) {
    Arena* arena = bring_together(a, b);
    BuildNode n{};
    n.op = op::Concat;
    n.inputs[0] = a.position();
    n.inputs[1] = b.position();
    n.attr = view_attr(ViewKind::Concat, axis);
    arena->nodes.push_back(n);
    return Expr{arena, static_cast<std::int32_t>(arena->nodes.size() - 1)};
}
constexpr Expr median_of(Expr x, std::int32_t axis) { // the one order statistic the nest path lowers
    OpAttr a{};
    a.kind = AttrKind::Order;
    a.order.op = OrderOp::Quantile;
    a.order.axis = axis;
    a.order.q = 0.5;
    return detail::append_unary(x, op::Order, a);
}

// ================================ PART A — what held up ==========================================
enum class View { Squeeze, SliceOfSlice, FlipSliceTranspose, TransposeOfReshape, ReshapeOfAMap,
                  BothOperands, LagOfAView, ViewOfALag, ViewOfAFold, LagOfAJoin };

constexpr Graph a_view(View which) {
    Input x{"x", Signal{4, 6, kT}};
    Input v{"v", Signal{4, 5, kT}};
    Expr t = x.transpose({1, 0});
    switch (which) {
    case View::Squeeze: return analysed(finish(Output{"y"} = reshaped(Input{"s", Signal{4, 1, 6, kT}}, ViewKind::Squeeze, 1) * 2.0));
    case View::SliceOfSlice: return analysed(finish(Output{"y"} = x.slice(0, 0, 4, 1).slice(0, 1, 2, 2) + 1.0));
    case View::FlipSliceTranspose: return analysed(finish(Output{"y"} = flip(t.slice(0, 1, 3, 2), 1) * 2.0));
    case View::TransposeOfReshape: return analysed(finish(Output{"y"} = x.reshape({3, 8}).transpose({1, 0}) * 2.0));
    case View::ReshapeOfAMap: return analysed(finish(Output{"y"} = (x * 3.0).reshape({24}) + 1.0));
    case View::BothOperands: return analysed(finish(Output{"y"} = einsum("ba,ak->bk", t, flip(v, 1))));
    case View::LagOfAView: return analysed(finish(Output{"y"} = t + t[n - 2]));
    case View::ViewOfALag: return analysed(finish(Output{"y"} = (x * 2.0)[n - 2].transpose({1, 0}) + 1.0));
    case View::ViewOfAFold: return analysed(finish(Output{"y"} = flip(x.sum(0), 0) * 2.0));
    case View::LagOfAJoin: return analysed(finish(Output{"y"} = joined(x, v, 1) + joined(x, v, 1)[n - 2]));
    }
    fail("nest_v6_review_test: no such view");
}

enum class Lanes { OverATranspose, OverAStep, ViewPlusPlain }; // a fold whose operand strides or steps

constexpr Graph laned(Lanes which) {
    Input x{"x", Signal{8, 12, kT}};
    Input v{"v", Signal{12, 8, kT}};
    switch (which) {
    case Lanes::OverATranspose: return analysed(finish(Output{"y"} = x.transpose({1, 0}).sum(1)));
    case Lanes::OverAStep: return analysed(finish(Output{"y"} = x.slice(1, 0, 6, 2).sum(1)));
    case Lanes::ViewPlusPlain: return analysed(finish(Output{"y"} = x.transpose({1, 0}) + v));
    }
    fail("nest_v6_review_test: no such lane case");
}

enum class PastRead { RankOneDeep, InACycle, RankThree, IntoAFold, InAMedian, InAProduct }; // of an INPUT

constexpr Graph a_past(PastRead which) {
    Input x{"x", Signal{5, kT}};
    Input c{"c", Signal{4, kT}};
    Input u{"u", Signal{5, 4, kT}};
    Input u2{"m", Signal{kT}};
    Variable y;
    switch (which) {
    case PastRead::RankOneDeep: // one number per sample: lanes run ALONG time, so the head is scalar
        return analysed(finish(Output{"y"} = u2 + u2[n - 8] * 0.5 + u2[n - 16] * 0.25));
    case PastRead::InACycle:
        y[n] = x + x[n - 2] * 0.25 + 0.5 * y[n - 1];
        return analysed(finish(Output{"y"} = y));
    case PastRead::RankThree: return analysed(finish(Output{"y"} = u + u[n - 2]));
    case PastRead::IntoAFold: return analysed(finish(Output{"y"} = (u + u[n - 2]).sum(0)));
    case PastRead::InAMedian: return analysed(finish(Output{"y"} = median_of(u, 0) + c[n - 3]));
    case PastRead::InAProduct: {
        Input a{"a", Signal{4, 6, kT}};
        Input b{"b", Signal{6, 5, kT}};
        Input d{"d", Signal{4, 5, kT}};
        return analysed(finish(Output{"y"} = einsum("ba,ak->bk", a, b) + d[n - 2]));
    }
    }
    fail("nest_v6_review_test: no such past");
}

template <std::int32_t B> // a block shorter than the lag that reads across it
constexpr Graph a_short_block() {
    Input x{"x", Signal{5, B}};
    return analysed(finish(Output{"y"} = x + x[n - 3] * 0.5));
}
constexpr Graph deep_lag_in_a_cycle() { // a lag of seven over a block of five, inside the cycle
    Input x{"x", Signal{9, 5}};
    Variable y;
    y[n] = x + 0.25 * x[n - 7] + 0.5 * y[n - 1];
    return analysed(finish(Output{"y"} = y));
}
// A bank of V one-pole filters, its state handed on in registers where a sample fits one block of
// lane bodies; `Fed` reads the input's past inside the cycle as well.
template <std::int32_t V, bool Fed>
constexpr Graph a_bank() {
    Input x{"x", Signal{V, kT}};
    Variable y;
    y[n] = Fed ? x + 0.25 * x[n - 2] + 0.5 * y[n - 1] : x + 0.5 * y[n - 1];
    return analysed(finish(Output{"y"} = y));
}
constexpr Graph a_ladder() { // three variables, each reading the one before it and its own past
    Input x{"x", Signal{9, kT}};
    Variable a;
    Variable b;
    Variable c;
    a[n] = x + 0.5 * a[n - 1];
    b[n] = a + 0.25 * b[n - 1];
    c[n] = b - 0.125 * c[n - 1];
    return analysed(finish(Output{"y"} = c));
}

enum class Parts { ZeroMargin, OfAView, OnTheLaneAxis, IntoAProduct, NanFill, ThroughAWindow, RampAlone,
                   RampRankThree, RampInAFold }; // a value made IN PARTS, and a generated ramp

constexpr Graph in_parts(Parts which) {
    Input x{"x", Signal{4, 6, kT}};
    Input lanes{"n", Signal{4, 3, kT}};
    Input v{"v", Signal{6, 5, kT}};
    switch (which) {
    case Parts::ZeroMargin: return analysed(finish(Output{"y"} = padded(x, 1, 0, 3, 0.5) * 2.0));
    case Parts::OfAView: return analysed(finish(Output{"y"} = padded(x.transpose({1, 0}), 1, 2, 1, -0.0) * 2.0));
    case Parts::OnTheLaneAxis: return analysed(finish(Output{"y"} = padded(lanes, 1, 2, 1, 0.25) * 2.0));
    case Parts::IntoAProduct: return analysed(finish(Output{"y"} = einsum("ba,ak->bk", joined(lanes, lanes, 1), v)));
    case Parts::NanFill: return analysed(finish(Output{"y"} = padded(lanes, 1, 2, 1, std::bit_cast<double>(0x7ff8000000000000ULL)) + 0.0));
    case Parts::ThroughAWindow: {
        Input m{"m", Signal{8, kT}};
        Input h{"h", Signal{3, kT}};
        return analysed(finish(Output{"y"} = (padded(m, 0, 1, 1, 0.0).unfold(0, 3) * h).sum(1)));
    }
    case Parts::RampAlone: return analysed(finish(Output{"y"} = iota(Signal{6, kT})));
    case Parts::RampRankThree: return analysed(finish(Output{"y"} = Input{"r", Signal{2, 3, 4, kT}} + iota(Signal{2, 3, 4, kT})));
    case Parts::RampInAFold: return analysed(finish(Output{"y"} = (v * iota(Signal{6, 5, kT})).sum(0)));
    }
    fail("nest_v6_review_test: no such part");
}

// A cycle whose SPAN holds a fold or a product that is no part of the cycle: those must lower, and
// must not be dragged into the stage that walks a sample at a time.
enum class Span { AFold, AProduct };

constexpr Graph a_span(Span which) {
    Input x{"x", Signal{8, kT}};
    Input u{"u", Signal{8, 6, kT}};
    Input b{"b", Signal{6, 5, kT}};
    Variable y;
    Expr lag = y[n - 1]; // the span opens here and closes at the definition, below
    Expr s = u.sum(1);
    Expr m = einsum("ba,ak->bk", u, b);
    if (which == Span::AFold) {
        y[n] = x + 0.5 * lag + s * 0.25;
        return analysed(finish(Output{"y"} = y));
    }
    y[n] = x + 0.5 * lag;
    return analysed(finish(Output{"y"} = y, Output{"z"} = m));
}

template <View W> constexpr Graph view_g = a_view(W);
template <Lanes W> constexpr Graph lane_g = laned(W);
template <PastRead W> constexpr Graph past_g = a_past(W);
template <Parts W> constexpr Graph part_g = in_parts(W);
template <Span W> constexpr Graph span_g = a_span(W);
template <std::int32_t V, bool Fed> constexpr Graph bank_g = a_bank<V, Fed>();
template <std::int32_t B> constexpr Graph short_g = a_short_block<B>();
constexpr Graph ladder_g = a_ladder();
constexpr Graph deep_g = deep_lag_in_a_cycle();

template <const Graph& G>
constexpr planned::Held nestHeld{planned::program_of(G, zen2)};
template <const Graph& G>
constexpr const loops::Program& nest = nestHeld<G>.program;

// One graph run both ways, over nineteen samples unless the graph asks for a shorter block.
template <const Graph& G, std::int32_t Block = kT>
void walks(const char* what, std::uint32_t seed, int runs = 1) {
    against<Block, nest<G>>(what, G, seed, runs);
}

// What was decided, where it is worth pinning. A statement whose value is handed on in a register is computed
// from a State register: its Compute node `reads` it.
constexpr bool reads_state(const loops::Program& p, std::size_t s, std::int32_t k) {
    for (std::int32_t n = p.stages[s].root + 1; n < tree_facts::subtree_end(p, s); ++n) {
        const loops::Node& node = loops::node_at(p, n);
        if (node.kind == loops::NodeKind::Compute && node.stmt == k && node.reads >= 0) {
            return true;
        }
    }
    return false;
}
constexpr int handed_on(const loops::Program& p) {
    int n = 0;
    for (std::size_t s = 0; s < p.stages.size(); ++s) {
        for (std::int32_t k = 0; k < p.stages[s].count; ++k) {
            n += reads_state(p, s, k) ? 1 : 0;
        }
    }
    return n;
}
static_assert(nest<part_g<Parts::RampAlone>>.stages.empty() && nest<part_g<Parts::RampAlone>>.buffers[0].ramp,
              "a ramp that is the whole graph is storage filled once and no loop nest at all");
static_assert(nest<part_g<Parts::ZeroMargin>>.stages.size() == 3, "an empty margin is no stage: the body, the margin, the doubling");
static_assert(handed_on(nest<bank_g<9, true>>) == 1 && handed_on(nest<bank_g<64, true>>) == 0,
              "nine voices fit one block of lane bodies, so the state is handed on in a register; sixty-four do not");

template <std::int32_t V>
void a_bank_of() {
    char what[48];
    std::snprintf(what, sizeof what, "A a bank of %d voices", V);
    walks<bank_g<V, false>>(what, 60 + static_cast<std::uint32_t>(V));
    walks<bank_g<V, true>>(what, 90 + static_cast<std::uint32_t>(V)); // ...and the same with a lagged feed inside it
}

constexpr Graph a_view_inside_a_cycle() {
    Input x{"x", Signal{8, 4, kT}};
    Variable y;
    y[n] = x + 0.5 * flip(y[n - 1], 0);
    return analysed(finish(Output{"y"} = y));
}
constexpr Graph a_pad_inside_a_cycle() {
    Input x{"x", Signal{8, 4, kT}};
    Variable y;
    y[n] = x + 0.5 * padded(y[n - 1], 0, 1, 1, 0.0).slice(0, 1, 8);
    return analysed(finish(Output{"y"} = y));
}
constexpr Graph a_view_of_a_map() {
    Input x{"x", Signal{8, 6, kT}};
    return analysed(finish(Output{"y"} = (x * 2.0).transpose({1, 0}) + 1.0));
}
static_assert(planned::refusal_of(a_view_inside_a_cycle(), zen2) == "heinzel::compile: feedback through anything but maps of one shape is not lowered yet",
              "A a view inside a cycle is refused by name");
static_assert(planned::refusal_of(a_pad_inside_a_cycle(), zen2) == "heinzel::compile: feedback through anything but maps of one shape is not lowered yet",
              "A a pad inside a cycle is refused by name");

constexpr loops::Schedule lanes_along_a_strided_view() {
    loops::Schedule d = planned::schedule_of(lane_g<Lanes::ViewPlusPlain>, zen2);
    loops::Schedule::Stage& one = d.stages[0];
    one.loops[one.loops.size() - 1] = loops::Schedule::Loop{.axis = one.loops[one.loops.size() - 1].axis, .step = 8, .kind = loops::LoopKind::Vector};
    return d;
}
static_assert(planned::refusal_of(lane_g<Lanes::ViewPlusPlain>, lanes_along_a_strided_view()) ==
                  "heinzel::compile: a Vector loop runs along an axis some buffer is not contiguous in",
              "A a lane along a strided view is refused by name");

constexpr Graph gViewOfAMap = a_view_of_a_map();
constexpr loops::Schedule a_view_in_its_sources_stage() { // its two stages merged into one
    const loops::Schedule as_planned = planned::schedule_of(gViewOfAMap, zen2);
    loops::Schedule d{};
    loops::Schedule::Stage one = as_planned.stages[0];
    decltype(loops::Schedule::Stage::nodes) all = one.nodes;
    for (const std::int32_t n : as_planned.stages[1].nodes) {
        all.push_back(n);
    }
    one.nodes = all;
    d.stages.push_back(one);
    return d;
}
static_assert(planned::refusal_of(gViewOfAMap, a_view_in_its_sources_stage()) ==
                  "heinzel::compile: a group holds a node that is neither its fold nor a map over the shape it runs at",
              "A a view in its source's own stage is refused by name");

// ================================ PART B — the findings ==========================================
// ---- B1: a program that needs more loop nests than there are ------------------------------------
constexpr Graph joins_in_a_row(int n) { // each join is one planned stage and TWO loop nests
    Input x{"x", Signal{4, 2, kT}};
    Expr y = x;
    for (int i = 0; i < n; ++i) {
        y = joined(y, x, 1);
    }
    return analysed(finish(Output{"y"} = y));
}
// A value made in parts is ONE planned stage and TWO or THREE loop nests, which the planner counts against
// `kMaxStages` (16): eight joins lower, nine are refused by name — not by a capacity that overflowed, which in a
// constant expression would stop the build here instead.
static_assert(planned::refusal_of(joins_in_a_row(8), zen2).empty(), "B1 eight joins in a row (sixteen loop nests) lower");
static_assert(planned::refusal_of(joins_in_a_row(9), zen2) == "heinzel::compile: more loop nests than kMaxStages; rebuild with -DHEINZEL_MAX_STAGES=N",
              "B1 nine joins in a row (eighteen loop nests) are refused by name");

// ---- B2: handed-on state composes, y[t] = x[t] + 0.5*y[t-1] + 0.25*y[t-2] ---------------------------
constexpr std::int32_t kV = 9;
constexpr Graph lag_of_a_lag() { // the second lag spelled as a lag OF the first
    Input x{"x", Signal{kV, kT}};
    Variable y;
    Expr a = y[n - 1];
    Expr b = a[n - 1]; // built before the definition, so the two lags stand side by side
    y[n] = x + 0.5 * a + 0.25 * b;
    return analysed(finish(Output{"y"} = y));
}
constexpr Graph kLag = lag_of_a_lag();
constexpr loops::Schedule kPlanned = planned::schedule_of(kLag, zen2);
// As planned, and with the two lags exchanged in the stage's node list (`edited_schedules::lags_swapped`). The planner
// runs the lag of the lag first (`planner/chains.hpp`, Decision 66), so that both are handed on; exchanged, the second
// is stored and read back a sample later.
constexpr planned::Held kAsPlannedHeld{planned::program_of(kLag, kPlanned)};
constexpr const loops::Program& kAsPlanned = kAsPlannedHeld.program;
constexpr planned::Held kSwappedHeld{planned::program_of(kLag, edited_schedules::lags_swapped(kLag, kPlanned))};
constexpr const loops::Program& kSwapped = kSwappedHeld.program;
// What the swap is for: a statement that reads handed-on state and is itself handed on.
constexpr bool reads_and_is_handed_on(const loops::Program& p) {
    for (const loops::Node& node : p.nodes) {
        if (node.kind == loops::NodeKind::Compute && node.reads >= 0 && node.keeps >= 0) {
            return true;
        }
    }
    return false;
}
static_assert(!(kSwapped == kAsPlanned) && reads_and_is_handed_on(kAsPlanned) && !reads_and_is_handed_on(kSwapped),
              "B2 as planned, a statement that reads handed-on state is handed on; with the lags exchanged, it is stored");

template <const loops::Program& P>
int wrong_samples_of_the_lag_of_a_lag() {
    const std::vector<float> x = noise(kV * kT, 5);
    canaried::Workspace<P> work(loops::Sizes{});
    canaried::run<P>(loops::Feeds{x.data()}, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(P.outputs[0]));
    std::vector<float> made(kV * kT, 0.0f);
    int bad = 0;
    for (std::size_t t = 0; t < kT; ++t) {
        for (std::size_t c = 0; c < kV; ++c) {
            made[t * kV + c] = x[t * kV + c] + (t >= 1 ? 0.5f * made[(t - 1) * kV + c] : 0.0f) +
                               (t >= 2 ? 0.25f * made[(t - 2) * kV + c] : 0.0f);
            bad += y[t * kV + c] == made[t * kV + c] ? 0 : 1;
        }
    }
    return bad;
}

void check_the_carried_state_composes() {
    if (wrong_samples_of_the_lag_of_a_lag<kAsPlanned>() != 0) {
        ++g_wrong;
        std::printf("  (B2) the PLANNED lag of a lag is already wrong\n");
    }
    const int swapped = wrong_samples_of_the_lag_of_a_lag<kSwapped>();
    if (swapped == 0) {
        return;
    }
    ++g_wrong;
    std::printf("  FAILING (B2) with the two lags exchanged, %d of %d numbers are wrong from sample 2 on.\n"
                "              A statement that both READS handed-on state and IS handed on must leave each\n"
                "              sample's value in its register, not what the register's open read once per\n"
                "              block — or the lowering must refuse to hand such a statement on.\n",
                swapped, kV * kT);
}

// ---- B3: a view does not turn the inline-producer decision off -------------------------------------
constexpr std::int32_t kLong = 4096;
constexpr Graph a_window_over_a_map(bool sliced) { // `sliced`: one affine view between the producer and its window
    Input x{"x", Signal{kLong, 64}};
    Input h{"h", Signal{3, 64}};
    Expr p = x * 2.0;
    Expr under = sliced ? p.slice(0, 0, kLong - 1) : p;
    return analysed(finish(Output{"y"} = (under.unfold(0, 3) * h).sum(1)));
}
constexpr std::int32_t inlined_producers(const Graph& g) {
    std::int32_t n = 0;
    for (const loops::Schedule::Stage& stage : planned::schedule_of(g, zen2).stages) {
        n += stage.through >= 0 ? 1 : 0;
    }
    return n;
}
// A cheap producer under a three-tap window is computed inside it; ONE SLICE between them must not store it again — a
// second pass over 1 MB, the 1.7-2.3x the inline decision measured. (The stage holding the view's source is found
// through the view: a view has no statement, so no stage of its own.)
static_assert(inlined_producers(a_window_over_a_map(false)) == inlined_producers(a_window_over_a_map(true)),
              "B3 a view between a cheap producer and its window does not move the inline decision");

void part_a_views() {
    walks<view_g<View::Squeeze>>("A a squeeze", 1);
    walks<view_g<View::SliceOfSlice>>("A a slice of a slice", 3);
    walks<view_g<View::FlipSliceTranspose>>("A a flip of a slice of a transpose", 4);
    walks<view_g<View::TransposeOfReshape>>("A a transpose of new extents", 5);
    walks<view_g<View::ReshapeOfAMap>>("A new extents over a made map", 6);
    walks<view_g<View::BothOperands>>("A a view on both of a product's operands", 7);
    walks<view_g<View::LagOfAView>>("A a lag of a view", 8);
    walks<view_g<View::ViewOfALag>>("A a view of a lag", 9);
    walks<view_g<View::ViewOfAFold>>("A a view of a fold", 10);
    walks<view_g<View::LagOfAJoin>>("A a lag of a join", 12);
    walks<lane_g<Lanes::OverATranspose>>("A lanes over a transposed fold", 13);
    walks<lane_g<Lanes::OverAStep>>("A lanes over a stepped slice", 15);
}

void part_a_the_past_of_an_input() {
    walks<past_g<PastRead::RankOneDeep>>("A lags of 8 and 16 along time", 17);
    walks<past_g<PastRead::InACycle>>("A a lagged input in a cycle", 19);
    walks<past_g<PastRead::RankThree>>("A a rank-3 lagged input", 20);
    walks<past_g<PastRead::IntoAFold>>("A a lagged input into a fold", 21);
    walks<past_g<PastRead::InAMedian>>("A a lagged input in a median's stage", 22);
    walks<past_g<PastRead::InAProduct>>("A a lagged input in a product's stage", 23);
    walks<short_g<1>, 1>("A a block of 1 against a lag of 3", 24);
    walks<deep_g, 5>("A a lag of 7 over a block of 5, in a cycle", 26);
}

void part_a_state_parts_and_ramps() {
    a_bank_of<1>();
    a_bank_of<9>();
    a_bank_of<64>();
    walks<ladder_g>("A a ladder of three variables", 27);
    walks<bank_g<9, true>>("A a lagged feed, reset and re-run", 77, 2);
    walks<span_g<Span::AFold>>("A a cycle's span holds a fold", 28);
    walks<span_g<Span::AProduct>>("A a cycle's span holds a product", 30);
    walks<part_g<Parts::ZeroMargin>>("A a pad with a zero margin", 31);
    walks<part_g<Parts::OfAView>>("A a pad of a view, -0.0 fill", 32);
    walks<part_g<Parts::OnTheLaneAxis>>("A a pad on the lane axis", 33);
    walks<part_g<Parts::IntoAProduct>>("A a join read by a product", 34);
    walks<part_g<Parts::NanFill>>("A a pad whose fill is a NaN", 36);
    walks<part_g<Parts::ThroughAWindow>>("A a pad read through a window", 37);
    walks<part_g<Parts::RampAlone>>("A a ramp and nothing else", 38);
    walks<part_g<Parts::RampRankThree>>("A a rank-3 ramp", 39);
    walks<part_g<Parts::RampInAFold>>("A a ramp inside a fold", 40);
}

} // namespace

int main() {
    part_a_views();
    part_a_the_past_of_an_input();
    part_a_state_parts_and_ramps();

    // ---- part B (B1 and B3 are the static_asserts above) ----
    check_the_carried_state_composes();

    if (g_wrong != 0) {
        std::printf("nest_v6_review_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("nest_v6_review_test: every finding is fixed\n");
    return 0;
}
