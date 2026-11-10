// What a schedule's loops and choices make of a stage (`Plan/archive/13-loop-tree/05` §4, M2.4) on small stages: lanes, lanes
// in blocks, a split among workers, a register tile, a reorder, a relaid operand, a prefetch, bands, a value made in
// parts in lanes, one copy shared by two readers — the lowered program printed and compared with its listing in
// `tests/goldens/loops_primitives/`, checked once by eye against `02` §4.3. Then what a schedule may not plan, and the
// refusals of its loops and choices, by name, at compile time, each beside the case made legal (the mutation that shows
// the case can fail).
//
// A listing that changes is printed; `HEINZEL_REPIN=1 test_loops_primitives` rewrites the files, and the change is
// reviewed in `git diff` like any other — never accepted blind.
//
// Unreachable, kept as internal checks with no case: a store standing still along the split axis, or walked by a
// second axis too (no graph lowers to one); a band's accesses disagreeing with its layout, and a band's stages not
// consecutive (a band holds no value made in parts and no relaid contraction, so none is inserted).

#include "heinzel/loops/lower.hpp"
#include "heinzel/loops/print.hpp"

#include "corpus_graphs.hpp"
#include "loops_schedules.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>

namespace {

using namespace heinzel;
using namespace loops_schedules;
namespace loops = heinzel::loops;

constexpr Expr in(std::string_view name, Shape shape) { return Input{name, Signal{shape}}; } // a signal, of one sample or more
constexpr Expr vblur(Expr v, Expr w) { return (v.unfold(0, 3) * w).sum(1); }

// ---- the graphs ----------------------------------------------------------------------------------------------

constexpr Graph kElem = corpus::elem();                     // [d0]: n0 a*b, n1 +b
constexpr Graph kRowsum = corpus::rowsum<64>();             // [64 d1]: n0 a fold of 64 columns
constexpr Graph kMatmul = corpus::matmul<8, 8, 32>();       // [8 32 1 8]: n0 a contraction
constexpr Graph kBank = corpus::one_pole_bank(16, 0.5, 64); // [16 d1]: n0-n3 a one-pole per voice
constexpr Graph kBlurxy = corpus::blurxy<8, 6, 3>();        // n1-n2 a horizontal blur, n4-n5 a vertical one over it
constexpr Graph kPadded = corpus::padded();                 // n0 made in three parts, n1 *2
constexpr Graph kBrightness = corpus::brightness();         // [512 512 1]: n0-n3 a gain, an offset, a clamp
constexpr Graph kOscillator = corpus::fm_oscillator();      // n0-n4 the phase, n5-n7 the sine
constexpr Graph kLagOfTwo = [] {                            // [d0]: y = x + 0.5 y[t-2] — no one-sample state
    constexpr Index n{};
    Output y{"y"};
    y[n] = Input{"x", Signal{Length{.multiple_of = 1, .min = 1, .expect = 64}}} + 0.5 * y[n - 2];
    return corpus::analysed(y);
}();

constexpr Graph two_products() { // n0 a·b, n1 c·b, n2 their sum: two contractions reading b
    Expr b = in("b", Shape{8, 16, 1});
    return corpus::analysed(finish(Output{"y"} = einsum("ik,kj->ij", in("a", Shape{4, 8, 1}), b) + einsum("ik,kj->ij", in("c", Shape{4, 8, 1}), b)));
}
constexpr Graph kTwoProducts = two_products();

constexpr Graph strict_product() { // a contraction owed its strict order: High precision grants no fused multiply-add
    Graph g = finish(Output{"y"} = einsum("ik,kj->ij", in("a", Shape{8, 8, 1}), in("b", Shape{8, 8, 1})));
    set_default_precision(g, numerics::Precision::High);
    return corpus::analysed(g);
}
constexpr Graph kStrictProduct = strict_product();

// The band refusals' graphs of `nest_band_test.cpp`, 37 x 70.
constexpr std::int32_t kH = 37;
constexpr std::int32_t kW = 70;
enum class Banded : std::uint8_t { Contraction, Blurxy, Hop, Offset, Unread, Shorter, FeedPast, Silence, Wide, Many, Lag };
constexpr Graph banded(Banded what) {
    Expr w = in("w", Shape{1, 3, 1, 1});
    if (what == Banded::Contraction) {
        return corpus::analysed(finish(Output{"y"} = vblur(einsum("ik,kj->ij", in("a", Shape{kH, 5, 1}), in("b", Shape{5, kW, 1})), w)));
    }
    if (what == Banded::FeedPast || what == Banded::Silence || what == Banded::Lag) { // over voices and eight samples of time
        constexpr Index n{};
        Expr x = in("x", Shape{16, 8});
        Expr wv = in("wv", Shape{1, 3, 1});
        const auto voices_blur = [&](Expr v) { return (v.unfold(0, 3) * wv).sum(1); };
        if (what == Banded::FeedPast) {
            return corpus::analysed(finish(Output{"y"} = voices_blur(x + x[n - 1])));
        }
        if (what == Banded::Lag) {
            return corpus::analysed(finish(Output{"y"} = voices_blur((x * 2.0)[n - 1] * 3.0)));
        }
        Variable y;
        y[n] = x + 0.9375 * y[n - 1];
        return corpus::analysed(finish(Output{"y"} = voices_blur(y)));
    }
    Expr x = in("x", Shape{kH, kW, what == Banded::Wide ? 3 : 1});
    Expr bx = (x.unfold(1, 3) * in("wh", Shape{1, 1, 3, 1})).sum(2);
    switch (what) {
    case Banded::Hop: return corpus::analysed(finish(Output{"y"} = (bx.unfold(0, 3, 2) * w).sum(1)));
    case Banded::Offset: return corpus::analysed(finish(Output{"y"} = vblur(bx.slice(0, 1, kH - 1), w)));
    case Banded::Unread: {
        Output doubled{"a"};
        Output blurred{"y"};
        doubled = x * 2.0;
        blurred = vblur(in("z", Shape{kH, kW, 1}), w);
        return corpus::analysed(finish(doubled, blurred));
    }
    case Banded::Shorter: return corpus::analysed(finish(Output{"y"} = vblur(bx.slice(0, 0, kH - 4), w)));
    case Banded::Many: {
        Expr made = x * 1.0;
        Expr sum = vblur(made, w);
        for (int k = 2; k <= 9; ++k) {
            made = made * static_cast<double>(k);
            sum = sum + vblur(made, w);
        }
        return corpus::analysed(finish(Output{"y"} = sum));
    }
    default: return corpus::analysed(finish(Output{"y"} = vblur(bx, w)));
    }
}

constexpr Graph blurxy_and_its_rows() { // `corpus::blurxy`, the horizontal blur an output too
    Expr x = in("x", Shape{kH, kW, 1});
    Expr bx = (x.unfold(1, 3) * in("wh", Shape{1, 1, 3, 1})).sum(2);
    return corpus::analysed(finish(Output{"y"} = vblur(bx, in("wv", Shape{1, 3, 1, 1})), Output{"bx"} = bx));
}

constexpr Graph transposed_band() { // n0 m = x*2, then m + mᵀ blurred down its rows: a band value read along two axes
    Expr m = in("x", Shape{8, 8, 1}) * 2.0;
    return corpus::analysed(finish(Output{"y"} = vblur(m + m.transpose({1, 0}), in("w", Shape{1, 3, 1, 1}))));
}

constexpr Graph product_after_a_chain() { // n0-n14 fifteen maps of a, n15 their contraction with b
    Expr a = in("a", Shape{8, 8, 1});
    for (int k = 0; k < 15; ++k) {
        a = a * 1.5;
    }
    return corpus::analysed(finish(Output{"y"} = einsum("ik,kj->ij", a, in("b", Shape{8, 8, 1}))));
}

// ---- the schedules ---------------------------------------------------------------------------------------------

constexpr Schedule lanes() { return schedule({planned(stage({0, 1}), {loops_schedules::lanes(0, 8)}, 1)}); }
constexpr Schedule lanes_in_blocks() { return schedule({planned(stage({0, 1}), {loops_schedules::lanes(0, 8, 4)}, 1)}); }
constexpr Schedule split_among_workers() {
    Schedule d = lanes();
    d.stages[0].parallel = 0;
    return d;
}

// A register tile of the contraction: four rows unrolled, two blocks of eight lanes along the columns, fused.
constexpr Schedule::Stage register_tile(const Schedule::Stage& members = stage({0})) {
    Schedule::Stage ps = planned(members, {serial(2), unrolled(0, 4), loops_schedules::lanes(1, 8, 2), serial(3)}, 3);
    ps.contract = true;
    return ps;
}
constexpr Schedule alone(Schedule::Stage ps = register_tile()) { return schedule({ps}); }
constexpr Schedule::Stage relaid_by(Schedule::Stage ps, std::int32_t operand, std::int32_t block) {
    ps.relaid = operand, ps.relaid_block = block;
    return ps;
}
constexpr Schedule register_tiled() { return alone(); }
constexpr Schedule reordered() { return schedule({planned(stage({0}), {serial(2), serial(1), serial(0), serial(3)}, 3)}); }
constexpr Schedule relaid() { return alone(relaid_by(register_tile(), 1, 16)); }
constexpr Schedule fetched() {
    Schedule::Stage ps = register_tile();
    ps.fetches = true;
    Schedule d = alone(ps);
    d.line_bytes = 64;
    return d;
}
constexpr Schedule blurxy_band(bool parallel = false) {
    Schedule d = stages(kBlurxy, {stage({1, 2}), stage({4, 5})});
    d.bands.push_back(band(0, 2, 0, 2, parallel));
    return d;
}
constexpr Schedule banded_rows() { return blurxy_band(); }
constexpr Schedule banded_rows_split() { return blurxy_band(true); }
constexpr Schedule parts_in_lanes() { return schedule({planned(stage({0}), {serial(2), serial(0), loops_schedules::lanes(1, 4)}, 3), walked(kPadded, stage({1}))}); }
// Two blocks of lanes along the columns — the operand's blocked axis — relaying b in blocks of both.
constexpr Schedule::Stage columns_relaid(const Schedule::Stage& members) {
    return relaid_by(planned(members, {serial(2), serial(0), loops_schedules::lanes(1, 8, 2), serial(3)}, 3), 1, 16);
}
constexpr Schedule shared_copy() { return schedule({columns_relaid(stage({0})), columns_relaid(stage({1, 2}))}); }

template <const Graph& G, auto Plans>
inline constexpr std::expected<loops::Program, loops::Refusal> kLowered = lowered(G, Plans());

template <class Visit>
void each(const Visit& visit) {
    visit("lanes", kLowered<kElem, lanes>);
    visit("lanes_in_blocks", kLowered<kElem, lanes_in_blocks>);
    visit("split_among_workers", kLowered<kElem, split_among_workers>);
    visit("register_tile", kLowered<kMatmul, register_tiled>);
    visit("reordered", kLowered<kMatmul, reordered>);
    visit("relaid", kLowered<kMatmul, relaid>);
    visit("fetched", kLowered<kMatmul, fetched>);
    visit("banded_rows", kLowered<kBlurxy, banded_rows>);
    visit("banded_rows_split", kLowered<kBlurxy, banded_rows_split>);
    visit("parts_in_lanes", kLowered<kPadded, parts_in_lanes>);
    visit("shared_copy", kLowered<kTwoProducts, shared_copy>);
}

static_assert(kLowered<kElem, lanes>.has_value() && kLowered<kElem, lanes_in_blocks>.has_value() && kLowered<kElem, split_among_workers>.has_value());
static_assert(kLowered<kMatmul, register_tiled>.has_value() && kLowered<kMatmul, reordered>.has_value() && kLowered<kMatmul, relaid>.has_value());
static_assert(kLowered<kMatmul, fetched>.has_value() && kLowered<kBlurxy, banded_rows>.has_value() && kLowered<kBlurxy, banded_rows_split>.has_value());
static_assert(kLowered<kPadded, parts_in_lanes>.has_value() && kLowered<kTwoProducts, shared_copy>.has_value());

// Two stages relaying one value in the same blocks: one copy, made before the first reader.
static_assert(kLowered<kTwoProducts, shared_copy>->stages.size() == 3, "one copy stage, before both contractions");
// A value made in parts walks every part in the lanes its schedule plans.
static_assert(kLowered<kPadded, parts_in_lanes>->stages.size() == 4 && kLowered<kPadded, parts_in_lanes>->nodes[2].kind == loops::NodeKind::Loop &&
              kLowered<kPadded, parts_in_lanes>->nodes[7].kind == loops::NodeKind::Loop);

// ---- what a schedule may not plan ------------------------------------------------------------------------------

static_assert(refusal(kElem, schedule({planned(Schedule::Stage{}, {}, 0)})) == "heinzel::compile: a planned stage holds no node");
// The inner loops a schedule names are the ones its loops imply.
constexpr std::string_view kImplied = "heinzel::compile: the inner loops a schedule names are not the ones its loops imply";
static_assert(refusal(kRowsum, schedule({planned(stage({0}), {serial(1), serial(0)}, 0)})) == kImplied);
static_assert(refusal(kRowsum, schedule({planned(stage({0}), {serial(1), serial(0)}, 1)})) == "lowered");
static_assert(refusal(kMatmul, schedule({planned(stage({0}), {serial(2), serial(0), serial(1), serial(3)}, 2)})) == kImplied);
// Every axis walked once, a Tile outside its walk: nothing fills a gap in a schedule.
constexpr std::string_view kWalks = "heinzel::compile: a schedule's stage walks each axis once, in at most one Tile outside the walk";
static_assert(refusal(kRowsum, schedule({planned(stage({0}), {serial(1), serial(0), tile(0, 16)}, 1)})) == kWalks);
static_assert(refusal(kRowsum, schedule({planned(stage({0}), {serial(1), tile(0, 16), serial(0)}, 2)})) == "lowered");
static_assert(refusal(kElem, schedule({planned(stage({0, 1}), {}, 0)})) == kWalks);
static_assert(refusal(kElem, schedule({planned(stage({0, 1}), {tile(0, 8)}, 1)})) == kWalks);
static_assert(refusal(kElem, schedule({planned(stage({0, 1}), {serial(0), serial(3)}, 2)})) == kWalks);
static_assert(refusal(kElem, schedule({planned(stage({0, 1}), {tile(0, 8), tile(0, 4), serial(0)}, 3)})) == kWalks);
static_assert(refusal(kElem, schedule({planned(stage({0, 1}), {tile(0, 8), serial(0)}, 2)})) == "lowered");
// A stage without inner loops says so, `inner_from` past its loops (and nowhere else); naming inner loops it does not
// have — an Unroll, lanes a Tile outside chunks — is refused. It runs an Unroll one loop at a time, spelled as the
// serial loop, and chunked lanes as lanes: each lowers. Time unrolled in feedback is refused.
static_assert(refusal(kElem, schedule({planned(stage({0, 1}), {unrolled(0, 2)}, 0)})) == kImplied);
static_assert(refusal(kElem, schedule({planned(stage({0, 1}), {unrolled(0, 2)}, -1)})) == kImplied && refusal(kElem, schedule({planned(stage({0, 1}), {unrolled(0, 2)}, 2)})) == kImplied);
static_assert(refusal(kElem, schedule({planned(stage({0, 1}), {tile(0, 4), loops_schedules::lanes(0, 8)}, 1)})) == kImplied);
static_assert(refusal(kElem, schedule({planned(stage({0, 1}), {tile(0, 4), loops_schedules::lanes(0, 8)}, 2)})) == "lowered");
static_assert(refusal(kBrightness, schedule({planned(stage({0, 1, 2, 3}), {tile(2, 4), serial(2), serial(0), loops_schedules::lanes(1, 8)}, 1)})) == kImplied);
static_assert(refusal(kBrightness, schedule({planned(stage({0, 1, 2, 3}), {tile(2, 4), serial(2), serial(0), loops_schedules::lanes(1, 8)}, 4)})) == "lowered");
constexpr std::string_view kTimeUnrolled = "heinzel::compile: a stage that reads its own past plans time unrolled";
static_assert(refusal(kBank, schedule({planned(stage({0, 1, 2, 3}), {loops_schedules::lanes(0, 8, 2), unrolled(1, 4)}, 1)})) == kImplied);
static_assert(refusal(kBank, schedule({planned(stage({0, 1, 2, 3}), {loops_schedules::lanes(0, 8, 2), unrolled(1, 4)}, 2)})) == kTimeUnrolled);
static_assert(refusal(kLagOfTwo, schedule({planned(stage({0, 1, 2, 3}), {unrolled(0, 4)}, 1)})) == kTimeUnrolled);
// A rank-1 cycle whose only lag reaches two samples back hands no state on: it has no inner loops to name.
static_assert(refusal(kLagOfTwo, schedule({planned(stage({0, 1, 2, 3}), {serial(0)}, 0)})) == kImplied);
static_assert(refusal(kLagOfTwo, schedule({planned(stage({0, 1, 2, 3}), {serial(0)}, 1)})) == "lowered");
// Lanes a machine whose vector holds six numbers gives: the runner runs a power of two (M3 review round 4).
static_assert(refusal(kElem, schedule({planned(stage({0, 1}), {loops_schedules::lanes(0, 6)}, 1)})) == "heinzel::compile: a schedule's lanes are a power of two, two or more");
// A chunk of no elements.
constexpr std::string_view kChunks = "heinzel::compile: a Split cuts into chunks of one or more, and an exact one a known chunk it divides";
static_assert(refusal(kElem, schedule({planned(stage({0, 1}), {tile(0, 0), serial(0)}, 2)})) == kChunks);
static_assert(refusal(kElem, schedule({planned(stage({0, 1}), {loops_schedules::lanes(0, 8, 0)}, 1)})) == kChunks);
static_assert(refusal(kRowsum, schedule({planned(stage({0}), {serial(1), unrolled(0, 0)}, 1)})) == kChunks);
// A Tile of one: a Loop of one hands the loop inside it the whole axis again, so each row would walk all of them.
static_assert(refusal(kRowsum, schedule({planned(stage({0}), {tile(1, 1), serial(1), serial(0)}, 2)})) ==
              "heinzel::compile: a Tile walks chunks of two or more: a chunk of one is a Serial loop");
static_assert(refusal(kRowsum, schedule({planned(stage({0}), {tile(1, 2), serial(1), serial(0)}, 2)})) == "lowered");
// A skeleton past kMaxStageLoops: a contraction over six axes, each tiled and in blocks of lanes, walks in 25 nodes;
// one Tile fewer, in 24 — refused then for its lanes along six axes.
constexpr Graph kWideProduct = corpus::analysed(finish(Output{"y"} = einsum("abcik,kj->abcij", in("a", Shape{2, 2, 2, 2, 4, 1}), in("b", Shape{4, 2, 1}))));
constexpr Schedule every_axis_in_blocks(bool all_tiled) {
    Schedule::Stage ps = stage({0});
    for (const std::int32_t axis : {5, 0, 1, 2, 3, 4}) {
        if (all_tiled || axis != 4) {
            ps.loops.push_back(tile(axis, 2));
        }
        ps.loops.push_back(loops_schedules::lanes(axis, 2, 2));
    }
    ps.loops.push_back(serial(6));
    ps.inner_from = static_cast<std::int32_t>(ps.loops.size()) - 1;
    return schedule({ps});
}
static_assert(refusal(kWideProduct, every_axis_in_blocks(true)) == "heinzel::compile: a stage needs more loops than kMaxStageLoops");
static_assert(refusal(kWideProduct, every_axis_in_blocks(false)) == "heinzel::compile: a stage's lanes run along one axis");
// An Unroll and lanes in blocks inside a fold's loops, and a Tile chunking the fold: each lowers.
static_assert(refusal(kRowsum, schedule({planned(stage({0}), {serial(1), loops_schedules::lanes(0, 8, 2)}, 1)})) == "lowered");
static_assert(refusal(kMatmul, schedule({planned(stage({0}), {serial(2), tile(3, 4), unrolled(0, 4), loops_schedules::lanes(1, 8, 2), serial(3)}, 4)})) == "lowered");
static_assert(refusal(kElem, schedule({planned(stage({0, 1}), {unrolled(0, 4)}, 1)})) == "lowered");
// A relaid copy is made for a contraction only, and a band holds none: a band planning one for any stage is refused.
constexpr Schedule relaid_in_a_band() {
    Schedule d = blurxy_band();
    d.stages[0].relaid = 0, d.stages[0].relaid_block = 8;
    return d;
}
static_assert(refusal(kBlurxy, relaid_in_a_band()) == "heinzel::compile: a band holds a stage planned to read a relaid or turned copy");

// ---- the refusals of a schedule's choices: each beside the case it refuses made legal --------------------------

// A fused multiply-add the result's precision does not grant.
constexpr Schedule contracted(Schedule d) {
    d.stages[0].contract = true;
    return d;
}
static_assert(refusal(kStrictProduct, contracted(stages(kStrictProduct, {stage({0})}))) ==
              "heinzel::compile: the schedule fuses a multiply-add the result's precision does not grant");
static_assert(refusal(kStrictProduct, stages(kStrictProduct, {stage({0})})) == "lowered");

// A split along a loop the stage has: any but -1 (none) names one, the outermost over its axis.
constexpr Schedule split_along(Schedule d, std::int32_t loop) {
    d.stages[0].parallel = loop;
    return d;
}
constexpr std::string_view kNoSuchLoop = "heinzel::compile: a schedule's split names a loop its stage does not have";
static_assert(refusal(kElem, split_along(lanes(), 1)) == kNoSuchLoop && refusal(kElem, split_along(lanes(), -2)) == kNoSuchLoop);
static_assert(refusal(kElem, split_along(lanes(), 0)) == "lowered");
constexpr Schedule kRowsTiled = schedule({planned(stage({0}), {tile(1, 16), serial(1), serial(0)}, 2)});
static_assert(refusal(kRowsum, split_along(kRowsTiled, 1)) == "heinzel::compile: a group is split along the outermost loop over an axis");
static_assert(refusal(kRowsum, split_along(kRowsTiled, 0)) == "lowered");
static_assert(refusal(kRowsum, split_along(stages(kRowsum, {stage({0})}), 1)) == "heinzel::compile: a group is not split along an axis its fold collapses");
static_assert(refusal(kBank, split_along(stages(kBank, {stage({0, 1, 2, 3})}), 0)) == "heinzel::compile: a split group reads what it writes only at the point it makes");
static_assert(refusal(kBank, split_along(stages(kBank, {stage({0, 1, 2, 3})}), 1)) == "lowered");

// A kept stage, as the planner keeps only such: one stage of no axes, in no band. A value made in parts
// lowers into a stage a part; a map over time has an axis.
constexpr Schedule kept(Schedule d) {
    d.stages[0].kept = true;
    return d;
}
static_assert(refusal(kPadded, kept(parts_in_lanes())) == "heinzel::compile: a kept stage lowers into more than one stage");
static_assert(refusal(kElem, kept(lanes())) == "heinzel::compile: a kept stage has axes, or stands in a band");

// What a kept stage reads, makes and watches, worked out again from the program (the review of kept stages: a
// planner's schedule with its kept stage's watched cleared lowered, and ran stale numbers). Each graph is n0, n1 and
// x times n1, a stage a node walked as it stands: n0 = 1 / p, n1 = n0 + q (`shown`: n0 an output too); or n0 a table
// read of a declared tensor at p, n1 = n0 * q.
constexpr Graph knob_gain(bool shown) {
    Input x{"x", Signal{Length{.multiple_of = 1, .min = 1, .expect = 64}}};
    Param p{"p"};
    Param q{"q"};
    const Expr inverse = 1.0 / p;
    const Expr gain = inverse + q;
    Output y{"y"};
    y = x * gain;
    if (!shown) {
        return corpus::analysed(y);
    }
    Output shown_inverse{"inverse"};
    shown_inverse = inverse;
    return corpus::analysed(finish(y, shown_inverse));
}
constexpr Graph table_gain() {
    Input x{"x", Signal{Length{.multiple_of = 1, .min = 1, .expect = 64}}};
    Input table{"table", {8}};
    Param p{"p"};
    Param q{"q"};
    const Expr level = wavetable(table, p);
    const Expr gain = level * q;
    Output y{"y"};
    y = x * gain;
    return corpus::analysed(y);
}
constexpr Graph kKnobGain = knob_gain(false);
constexpr Graph kShownGain = knob_gain(true);
constexpr Graph kTableGain = table_gain();
constexpr Schedule three_stages(const Graph& g) { return stages(g, {stage({0}), stage({1}), stage({2})}); }

// Stage `at` of `d` kept or not, watching the inputs of `g` named.
constexpr Schedule kept_at(const Graph& g, Schedule d, std::size_t at, bool keep, std::initializer_list<std::string_view> watched) {
    d.stages[at].kept = keep;
    for (std::size_t i = 0; i < g.inputs.size(); ++i) {
        for (const std::string_view name : watched) {
            d.stages[at].watched[i / 64] |= g.inputs[i].name.view() == name ? std::uint64_t{1} << (i % 64) : 0;
        }
    }
    return d;
}
// Both knob stages kept, or the second alone: it watches its maker's Param too, kept or not.
constexpr Schedule kKnobStages = three_stages(kKnobGain);
static_assert(refusal(kKnobGain, kept_at(kKnobGain, kept_at(kKnobGain, kKnobStages, 0, true, {"p"}), 1, true, {"p", "q"})) == "lowered");
static_assert(refusal(kKnobGain, kept_at(kKnobGain, kKnobStages, 1, true, {"p", "q"})) == "lowered");
// Watching nothing (the review's case), a Param more, another Param, not its maker's; a stage not kept watching one.
constexpr std::string_view kWatches = "heinzel::compile: a kept stage watches the Params it and its makers read; one not kept, none";
static_assert(refusal(kKnobGain, kept_at(kKnobGain, kKnobStages, 0, true, {})) == kWatches);
static_assert(refusal(kKnobGain, kept_at(kKnobGain, kKnobStages, 0, true, {"p", "q"})) == kWatches);
static_assert(refusal(kKnobGain, kept_at(kKnobGain, kKnobStages, 0, true, {"q"})) == kWatches);
static_assert(refusal(kKnobGain, kept_at(kKnobGain, kKnobStages, 1, true, {"q"})) == kWatches);
static_assert(refusal(kKnobGain, kept_at(kKnobGain, kKnobStages, 1, false, {"p"})) == kWatches);
// An output may be written in the caller's memory, which a skipped run would leave unwritten.
static_assert(refusal(kShownGain, kept_at(kShownGain, three_stages(kShownGain), 0, true, {"p"})) == "heinzel::compile: a kept stage makes an output");
static_assert(refusal(kShownGain, three_stages(kShownGain)) == "lowered");
// A declared tensor is bound by pointer and may change with no call: a stage reading it, or what such a stage makes.
constexpr std::string_view kStale = "heinzel::compile: a kept stage reads what may change while no Param does";
static_assert(refusal(kTableGain, kept_at(kTableGain, three_stages(kTableGain), 0, true, {"p"})) == kStale);
static_assert(refusal(kTableGain, kept_at(kTableGain, three_stages(kTableGain), 1, true, {"p", "q"})) == kStale);
static_assert(refusal(kTableGain, three_stages(kTableGain)) == "lowered");

// A prefetch asks for lines of a known size.
static_assert(refusal(kMatmul, [] {
                  Schedule d = fetched();
                  d.line_bytes = 0;
                  return d;
              }()) == "heinzel::compile: a schedule fetches ahead by lines of no size");
static_assert(refusal(kMatmul, fetched()) == "lowered");

// A relaid operand.
static_assert(refusal(kMatmul, schedule({relaid_by(walked(kMatmul, stage({0})), 2, 16)})) =="heinzel::compile: a CacheRead relays operand 0 or 1 of its contraction");
static_assert(refusal(kMatmul, alone(relaid_by(register_tile(), 1, 12))) == "heinzel::compile: a relaid operand must be stored, in blocks of a whole number of lanes");
static_assert(refusal(kMatmul, alone(relaid_by(register_tile(), 1, 0))) == "heinzel::compile: a relaid operand must be stored, in blocks of a whole number of lanes");
// A block of whole lanes, but not the strip of two blocks of eight the tile reads it in; a copy no lane reads.
static_assert(refusal(kMatmul, alone(relaid_by(register_tile(), 1, 8))) == "heinzel::compile: a relaid operand is read in lanes, a block by each strip of them");
static_assert(refusal(kMatmul, schedule({relaid_by(walked(kMatmul, stage({0})), 1, 16)})) ==
              "heinzel::compile: a relaid operand is read in lanes, a block by each strip of them");
static_assert(refusal(kMatmul, relaid()) == "lowered");
// The first operand's blocked axis is the contracted one, not its lanes'; a block of one Vector, which the copy stage
// would walk as a whole block with a Ones tail.
static_assert(refusal(kMatmul, alone(relaid_by(register_tile(), 0, 16))) == "heinzel::compile: a relaid operand is blocked along the axis its lanes walk");
static_assert(refusal(kMatmul, schedule({relaid_by(planned(stage({0}), {serial(2), unrolled(0, 4), loops_schedules::lanes(1, 8), serial(3)}, 3), 1, 8)})) ==
              "heinzel::compile: a relaid block spans more than one Vector, under a Clip or Masked strip");
// A Tile over the lanes that is not a whole number of blocks, alone or split among workers; one of two blocks, split
// among workers, lowers (M3 review round 5).
constexpr Schedule tiled_lanes(std::int32_t chunk, std::int32_t split = -1) {
    Schedule::Stage ps = relaid_by(planned(stage({0}), {serial(2), unrolled(0, 4), tile(1, chunk), loops_schedules::lanes(1, 8, 2), serial(3)}, 4), 1, 16);
    ps.contract = true, ps.parallel = split;
    return schedule({ps});
}
constexpr std::string_view kWholeBlocks = "heinzel::compile: a relaid operand's lanes are tiled in whole blocks";
static_assert(refusal(kMatmul, tiled_lanes(24)) == kWholeBlocks && refusal(kMatmul, tiled_lanes(40)) == kWholeBlocks && refusal(kMatmul, tiled_lanes(24, 2)) == kWholeBlocks);
static_assert(refusal(kMatmul, tiled_lanes(32, 2)) == "lowered");
constexpr Graph kProductAfterAChain = product_after_a_chain();
constexpr Schedule product_after(std::int32_t stages_of_maps) { // the maps in `stages_of_maps` stages, then the contraction, relaid
    const loops::graph::Facts facts = loops::graph::facts_of(kProductAfterAChain);
    Schedule d{};
    for (std::int32_t k = 0; k + 1 < stages_of_maps; ++k) {
        d.stages.push_back(walked(kProductAfterAChain, facts, stage({k})));
    }
    fixed_vector<std::int32_t, loops::kMaxStageStmts> rest{};
    for (std::int32_t k = stages_of_maps - 1; k < 15; ++k) {
        rest.push_back(k);
    }
    d.stages.push_back(walked(kProductAfterAChain, facts, Schedule::Stage{.nodes = rest}));
    d.stages.push_back(columns_relaid(stage({15})));
    return d;
}
static_assert(refusal(kProductAfterAChain, product_after(15)) ==
              "heinzel::compile: more stages than kMaxStages; rebuild with -DHEINZEL_MAX_STAGES=N");
static_assert(refusal(kProductAfterAChain, product_after(14)) == "lowered");

// Bands.
constexpr Schedule blurxy_bands(std::initializer_list<Schedule::Band> bands) {
    Schedule d = stages(kBlurxy, {stage({1, 2}), stage({4, 5})});
    for (const Schedule::Band& b : bands) {
        d.bands.push_back(b);
    }
    return d;
}
constexpr std::string_view kGroupOrder = "heinzel::compile: bands are in group order, disjoint, and a chunk is one row or more";
static_assert(refusal(kBlurxy, blurxy_bands({band(0, 2, 0, 0)})) == kGroupOrder && refusal(kBlurxy, blurxy_bands({band(0, 2, 0, 2147483647)})) == kGroupOrder);
static_assert(refusal(kBlurxy, blurxy_bands({band(1, 2147483647, 0, 4), band(1, 2, 0, 4)})) == kGroupOrder);
constexpr Graph kMany = banded(Banded::Many);
constexpr Schedule many(std::initializer_list<Schedule::Band> bands) {
    Schedule d = stages(kMany, {stage({1, 2}), stage({3, 7, 12, 17, 22, 27, 32, 37, 42}),
                             stage({5, 6, 9, 10, 11, 14, 15, 16, 19, 20, 21, 24, 25, 26, 29, 30, 31, 34, 35, 36, 39, 40, 41, 44, 45, 46})});
    for (const Schedule::Band& b : bands) {
        d.bands.push_back(b);
    }
    return d;
}
static_assert(refusal(kMany, many({band(1, 2, 0, 4), band(2, 1, 0, 4)})) == kGroupOrder);
constexpr std::string_view kTwoOrMore = "heinzel::compile: a band runs two or more of the schedule's groups";
static_assert(refusal(kBlurxy, blurxy_bands({band(0, 1, 0, 4)})) == kTwoOrMore && refusal(kBlurxy, blurxy_bands({band(1, 2, 0, 4)})) == kTwoOrMore &&
              refusal(kBlurxy, blurxy_bands({band(1, 2147483647, 0, 4)})) == kTwoOrMore);
template <Banded What>
constexpr Graph kBanded = banded(What);
template <Banded What>
constexpr loops::Refusal banded_says(std::initializer_list<Schedule::Stage> members, Schedule::Band b) {
    Schedule d = stages(kBanded<What>, members);
    d.bands.push_back(b);
    return refusal(kBanded<What>, d);
}
static_assert(banded_says<Banded::Contraction>({stage({0}), stage({2, 3})}, band(0, 2, 0, 4)) == "heinzel::compile: a band holds no value made in parts and no contraction");
static_assert(refusal(kBlurxy, blurxy_bands({band(0, 2, 7, 4)})) == "heinzel::compile: a band walks an axis of each group's domain that no fold of it collapses");
static_assert(refusal(kBlurxy, blurxy_bands({band(0, 2, 1, 4)})) == "heinzel::compile: a band walks an axis of each group's domain that no fold of it collapses");
static_assert(banded_says<Banded::Hop>({stage({1, 2}), stage({4, 5})}, band(0, 2, 0, 4)) == "heinzel::compile: in a band, a later group reads a band value at a step other than one");
static_assert(banded_says<Banded::Offset>({stage({1, 2}), stage({5, 6})}, band(0, 2, 0, 4)) ==
              "heinzel::compile: in a band, a later group reads a band value from an offset, or off the band");
static_assert(banded_says<Banded::Unread>({stage({1, 2}), stage({3}), stage({5, 6})}, band(0, 2, 0, 4)) ==
              "heinzel::compile: in a band, every group but the last makes something a later one reads");
static_assert(banded_says<Banded::Shorter>({stage({1, 2}), stage({5, 6})}, band(0, 2, 0, 4)) ==
              "heinzel::compile: in a band, a group's extent along it is not the last group's plus its lead");
static_assert(banded_says<Banded::FeedPast>({stage({0, 1}), stage({3, 4})}, band(0, 2, 0, 4)) ==
              "heinzel::compile: in a band, no group reads a feed's past (its first samples run apart)");
static_assert(banded_says<Banded::Silence>({stage({0, 1, 2, 3}), stage({5, 6})}, band(0, 2, 0, 4)) ==
              "heinzel::compile: a band buffer keeps silence before its block only along time");
static_assert(banded_says<Banded::Wide>({stage({1, 2}), stage({4, 5})}, band(0, 2, 0, 4)) ==
              "heinzel::compile: a band buffer's rows must be contiguous: its outer axes all of extent one");
static_assert(refusal(kMany, many({band(1, 2, 0, 4)})) == "heinzel::compile: a band keeps more values than a band holds (kMaxBandBuffers)");
static_assert(banded_says<Banded::Lag>({stage({0}), stage({1, 2}), stage({4, 5})}, band(0, 3, 0, 4)) ==
              "heinzel::compile: in a band, a later group reads a band value through a lag or a gather");
constexpr Graph kTransposedBand = transposed_band();
static_assert(refusal(kTransposedBand, [] {
                  Schedule d = stages(kTransposedBand, {stage({0}), stage({1, 2}), stage({4, 5})});
                  d.bands.push_back(band(0, 2, 0, 2));
                  return d;
              }()) == "heinzel::compile: in a band, a group's band values disagree on the axis it walks");
static_assert(banded_says<Banded::Blurxy>({stage({1, 2}), stage({4, 5})}, band(0, 2, 0, 4)) == "lowered");
static_assert(refusal(kBlurxy, [] {
                  Schedule d = blurxy_bands({band(0, 2, 0, 2)});
                  d.stages[1].parallel = 0;
                  return d;
              }()) == "heinzel::compile: a group in a band is not split; its band is");
constexpr Schedule oscillator_band(bool parallel) {
    Schedule d = stages(kOscillator, {stage({0, 1, 2, 3, 4}), stage({5, 6, 7})});
    d.bands.push_back(band(0, 2, 0, 16, parallel));
    return d;
}
constexpr std::string_view kSplitBand = "heinzel::compile: a split band carries only band buffers, and what runs ahead writes only those";
static_assert(refusal(kOscillator, oscillator_band(true)) == kSplitBand);
static_assert(refusal(kOscillator, oscillator_band(false)) == "lowered");
constexpr Graph kBlurxyAndItsRows = blurxy_and_its_rows();
static_assert(refusal(kBlurxyAndItsRows, [] {
                  Schedule d = stages(kBlurxyAndItsRows, {stage({1, 2}), stage({4, 5})});
                  d.bands.push_back(band(0, 2, 0, 5, true));
                  return d;
              }()) == kSplitBand);

std::string pinned(const std::string& path) {
    std::ifstream in(path);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

} // namespace

int main() {
    const bool repin = std::getenv("HEINZEL_REPIN") != nullptr;
    int wrong = 0;
    each([&](const char* name, const std::expected<loops::Program, loops::Refusal>& p) {
        const std::string path = std::string{HEINZEL_LOOPS_PRIMITIVES_GOLDENS} + "/" + name + ".txt";
        const std::string got = loops::print(*p);
        if (repin) {
            std::ofstream(path) << got;
        } else if (got != pinned(path)) {
            ++wrong;
            std::printf("%s: the tree differs from %s:\n%s\n", name, path.c_str(), got.c_str());
        }
    });
    if (wrong != 0) {
        std::printf("%d trees differ from their listings\n", wrong);
        return 1;
    }
    return 0;
}
