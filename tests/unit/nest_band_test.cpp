// Bands (`Schedule::Band`): consecutive stages under one loop over a shared axis, a value made and
// read only inside the band kept for one chunk and what the next reads again — carried to the buffer's
// front between chunks. Each case takes the planner's schedule of a machine that bands nothing and adds a band
// by hand (the planner's own bands are `nest_band_plan_test`'s; the cases are `tests/support/band_cases.hpp`). A band
// changes no arithmetic order, so each banded run is compared BIT FOR BIT with the same graph unbanded, and with a
// hand loop. Every run is canaried (`canaried.hpp`): a carry or a silence that writes outside its buffer ends the
// test. Here: what the lowering makes of every case (leads, band buffers, kept rows), blurxy run, and the refusals;
// the other workloads run in `nest_band_workloads_test`.
//
//   blurxy    a horizontal then a vertical 3-tap blur; chunks of 1 to more than the whole
//   output    blurxy whose intermediate is also a result: it stays whole, written chunk by chunk
//   reaches   a reader that reads the band value through a window and at the point: the larger reach
//   refusals  every band the lowering refuses, by name

#include "heinzel/notation/einsum.hpp"

#include "band_cases.hpp"
#include "canaried.hpp"
#include "planned.hpp"

#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>

using namespace heinzel;

namespace {

using namespace band_cases;
namespace loops = heinzel::loops;
using loops::Schedule;

static_assert(kSchedule<kBlurxy>.stages.size() == 2 && kSchedule<kHarris>.stages.size() == 2 && kSchedule<kChain>.stages.size() == 3 &&
              kSchedule<kConv, kStoring>.stages.size() == 2 && kSchedule<kOsc>.stages.size() == 2 && kSchedule<kOnepole>.stages.size() == 2 &&
              kSchedule<kBlurxyOut>.stages.size() == 2 && kSchedule<kBlurxyReaches>.stages.size() == 2);
static_assert(kLowers<kBlurxy> && kLowers<kHarris> && kLowers<kChain> && kLowers<kConv, kStoring> && kLowers<kOsc> && kLowers<kOnepole> &&
              kLowers<kBlurxyOut> && kLowers<kBlurxyReaches>);

// What the lowering made of them: the leads, and a band buffer as many rows high as a chunk and its lead.
template <const loops::Program& P>
constexpr std::int32_t kept_rows(std::size_t which) {
    const loops::Buffer& b = P.buffers[loops::kBandBuffers<P, 0>[which]];
    return extent_at(b.shape, b.kept_axis).encoded;
}
static_assert(kBanded<kBlurxy, 0, 5>.stages[0].lead == 2 && kBanded<kBlurxy, 0, 5>.stages[1].lead == 0 &&
              loops::kBandBuffers<kBanded<kBlurxy, 0, 5>, 0>.size() == 1 && kept_rows<kBanded<kBlurxy, 0, 5>>(0) == 7);
static_assert(loops::kBandBuffers<kBanded<kHarris, 0, 3>, 0>.size() == 3 && kept_rows<kBanded<kHarris, 0, 3>>(2) == 5);
static_assert(kBanded<kChain, 0, 4>.stages[0].lead == 4 && kBanded<kChain, 0, 4>.stages[1].lead == 2 &&
              loops::kBandBuffers<kBanded<kChain, 0, 4>, 0>.size() == 2 && kept_rows<kBanded<kChain, 0, 4>>(0) == 8);
static_assert(kBanded<kConv, 0, 50, kStoring>.stages[0].lead == 6 && kept_rows<kBanded<kConv, 0, 50, kStoring>>(0) == 56);
static_assert(loops::kBandBuffers<kBanded<kOsc, 0, 16>, 0>.size() == 1 && kBanded<kOsc, 0, 16>.buffers[loops::kBandBuffers<kBanded<kOsc, 0, 16>, 0>[0]].prefix == 1);
static_assert(kBanded<kBlurxyOut, 0, 5>.bands.size() == 1 && loops::kBandBuffers<kBanded<kBlurxyOut, 0, 5>, 0>.empty(), "a band value that is also a result stays whole");
static_assert(kept_rows<kBanded<kBlurxy, 0, 100>>(0) == 35 + 2, "a chunk taller than the band is the band (review M2, F2)");
static_assert(kBanded<kBlurxyReaches, 0, 5>.stages[0].lead == 2);

// The hand loop of blurxy: each sum from its first term, in the graph's order.
std::vector<float> blurxy_by_hand(const std::vector<float>& x, const std::vector<float>& wh, const std::vector<float>& wv) {
    constexpr int Wo = kW - 2;
    std::vector<float> bx(std::size_t(kH) * Wo);
    std::vector<float> y(std::size_t(kH - 2) * Wo);
    for (int i = 0; i < kH; ++i) {
        for (int j = 0; j < Wo; ++j) {
            float acc = x[std::size_t(i) * kW + j] * wh[0];
            acc = acc + x[std::size_t(i) * kW + j + 1] * wh[1];
            bx[std::size_t(i) * Wo + j] = acc + x[std::size_t(i) * kW + j + 2] * wh[2];
        }
    }
    for (int i = 0; i + 2 < kH; ++i) {
        for (int j = 0; j < Wo; ++j) {
            float acc = bx[std::size_t(i) * Wo + j] * wv[0];
            acc = acc + bx[std::size_t(i + 1) * Wo + j] * wv[1];
            y[std::size_t(i) * Wo + j] = acc + bx[std::size_t(i + 2) * Wo + j] * wv[2];
        }
    }
    return y;
}

template <std::int32_t... Steps>
void check_blurxy() {
    const std::vector<float> x = noise(std::size_t(kH) * kW, 1);
    const std::vector<float> wh = noise(3, 2);
    const std::vector<float> wv = noise(3, 3);
    const loops::Feeds feeds = feeds_of(kBlurxy, {{"x", &x}, {"wh", &wh}, {"wv", &wv}});
    const std::vector<float> want = blurxy_by_hand(x, wh, wv);
    canaried::Workspace<kPlain<kBlurxy>> work(loops::Sizes{});
    canaried::run<kPlain<kBlurxy>>(feeds, work);
    if (std::memcmp(work.bytes_of(static_cast<std::size_t>(kPlain<kBlurxy>.outputs[0])), want.data(), want.size() * 4) != 0) {
        ++g_wrong;
        std::printf("  blurxy: the unbanded run differs from the hand loop\n");
    }
    (same<kPlain<kBlurxy>, kBanded<kBlurxy, 0, Steps>>("blurxy", feeds), ...);
    const loops::Feeds out_feeds = feeds_of(kBlurxyOut, {{"x", &x}, {"wh", &wh}, {"wv", &wv}});
    (same<kPlain<kBlurxyOut>, kBanded<kBlurxyOut, 0, Steps>>("blurxy, bx a result", out_feeds), ...);
    const loops::Feeds reach_feeds = feeds_of(kBlurxyReaches, {{"x", &x}, {"wh", &wh}, {"wv", &wv}});
    (same<kPlain<kBlurxyReaches>, kBanded<kBlurxyReaches, 0, Steps>>("blurxy, two reaches", reach_feeds), ...);
}

// ---- refusals: each a band the lowering refuses by name ---------------------------------------------------
// A graph for each; `what` picks it.
constexpr Graph refused_graph(int what) {
    Input w{"w", {1, 3, 1}};
    if (what == 0) { // a contraction, then a blur over its result
        Input a{"a", {kH, 5}};
        Input b{"b", {5, kW}};
        return analysed(finish(Output{"y"} = vblur(einsum("ik,kj->ij", a, b), w)));
    }
    if (what == 9 || what == 13 || what == 8) { // over voices and eight samples of time
        constexpr Index n{};
        Input x{"x", Signal{16, 8}};
        Input wv{"wv", {1, 3}};
        const auto voices_blur = [&](Expr v) { return (v.unfold(0, 3) * wv).sum(1); };
        if (what == 8) { // the past of the feed, in the band's first stage
            return analysed(finish(Output{"y"} = voices_blur(x + x[n - 1])));
        }
        if (what == 13) { // a later stage of the band reads the first one's value through a lag
            return analysed(finish(Output{"y"} = voices_blur((x * 2.0)[n - 1] * 3.0)));
        }
        Variable y; // a one-pole bank, blurred across its voices: its silence would be along voices
        y[n] = x + 0.9375 * y[n - 1];
        return analysed(finish(Output{"y"} = voices_blur(y)));
    }
    Input x{"x", Signal{kH, kW, what == 10 ? 3 : 1}};
    Expr bx = (x.unfold(1, 3) * Input{"wh", {1, 1, 3}}).sum(2);
    switch (what) {
    case 4: // a window that hops two rows
        return analysed(finish(Output{"y"} = (bx.unfold(0, 3, 2) * w).sum(1)));
    case 5: // through a slice one row in
        return analysed(finish(Output{"y"} = vblur(bx.slice(0, 1, kH - 1), w)));
    case 6: { // a first stage whose result nothing in the band reads
        Output doubled{"a"};
        Output blurred{"y"};
        doubled = x * 2.0;
        blurred = vblur(Input{"z", {kH, kW}}, w);
        return analysed(finish(doubled, blurred));
    }
    case 7: // the reader reads fewer rows than the maker makes
        return analysed(finish(Output{"y"} = vblur(bx.slice(0, 0, kH - 4), w)));
    case 11: { // nine values of the first stage, each blurred by the second (after a stage making `bx`, read by nobody)
        Expr made = x * 1.0;
        Expr sum = vblur(made, w);
        for (int k = 2; k <= 9; ++k) {
            made = made * static_cast<double>(k); // (each reads the one before: one stage of maps)
            sum = sum + vblur(made, w);
        }
        return analysed(finish(Output{"y"} = sum));
    }
    default:
        return analysed(finish(Output{"y"} = vblur(bx, w)));
    }
}

void expect_said(const char* what, std::string_view said, std::string_view says) {
    if (said.substr(0, says.size()) != says && ++g_wrong <= 20) {
        const std::string_view shown = said.empty() ? std::string_view{"(nothing: it lowered)"} : said;
        std::printf("  refusal %s: said \"%.*s\"\n", what, static_cast<int>(shown.size()), shown.data());
    }
}

// The planner's schedule of graph `graph` with `bands`, and — `relaid` 0 or 1 — its first stage planned to read that
// operand from a relaid copy.
void expect_refusal(const char* what, int graph, fixed_vector<Schedule::Band, loops::kMaxBands> bands, std::string_view says, std::int32_t relaid = -1) {
    const Graph g = refused_graph(graph);
    Schedule d = planned::schedule_of(g, kMachine);
    d.bands = bands;
    d.stages[0].relaid = relaid >= 0 ? relaid : d.stages[0].relaid;
    const loops::Refusal said = planned::refusal_of(g, d);
    expect_said(what, said.view(), says);
}

void check_refusals() {
    using B = fixed_vector<Schedule::Band, loops::kMaxBands>;
    const auto one = [](Schedule::Band b) { B bands{}; bands.push_back(b); return bands; };
    B overlapping{};
    overlapping.push_back(Schedule::Band{1, 2, 0, 4});
    overlapping.push_back(Schedule::Band{2, 1, 0, 4});
    B after_any_count{};
    after_any_count.push_back(Schedule::Band{1, 2147483647, 0, 4});
    after_any_count.push_back(Schedule::Band{1, 2, 0, 4});
    expect_refusal("1, one stage", 1, one(Schedule::Band{0, 1, 0, 4}), "heinzel::compile: a band runs two or more");
    expect_refusal("1, past the last stage", 1, one(Schedule::Band{1, 2, 0, 4}), "heinzel::compile: a band runs two or more");
    expect_refusal("1, a chunk of no rows", 1, one(Schedule::Band{0, 2, 0, 0}), "heinzel::compile: bands are in group order");
    expect_refusal("1, a chunk past any count", 1, one(Schedule::Band{0, 2, 0, 2147483647}), "heinzel::compile: bands are in group order");
    expect_refusal("1, stages past any count", 1, one(Schedule::Band{1, 2147483647, 0, 4}), "heinzel::compile: a band runs two or more");
    expect_refusal("1, overlapping", 11, overlapping, "heinzel::compile: bands are in group order");
    expect_refusal("1, after a band of any count", 11, after_any_count, "heinzel::compile: bands are in group order");
    expect_refusal("2, a contraction", 0, one(Schedule::Band{0, 2, 0, 4}), "heinzel::compile: a band holds no value made in parts");
    expect_refusal("2, a relaid copy", 1, one(Schedule::Band{0, 2, 0, 4}), "heinzel::compile: a band holds a stage planned to read a relaid or turned copy", 0);
    expect_refusal("3, no such axis", 1, one(Schedule::Band{0, 2, 7, 4}), "heinzel::compile: a band walks an axis of each group");
    expect_refusal("3, the fold's own axis", 1, one(Schedule::Band{0, 2, 1, 4}), "heinzel::compile: a band walks an axis of each group");
    expect_refusal("4, a hop", 4, one(Schedule::Band{0, 2, 0, 4}), "heinzel::compile: in a band, a later group reads a band value at a step");
    expect_refusal("5, an offset", 5, one(Schedule::Band{0, 2, 0, 4}), "heinzel::compile: in a band, a later group reads a band value from an");
    expect_refusal("6, nothing read", 6, one(Schedule::Band{0, 2, 0, 4}), "heinzel::compile: in a band, every group but the last makes");
    expect_refusal("7, extents", 7, one(Schedule::Band{0, 2, 0, 4}), "heinzel::compile: in a band, a group's extent along it");
    expect_refusal("8, a feed's past", 8, one(Schedule::Band{0, 2, 0, 4}), "heinzel::compile: in a band, no group reads a feed's past");
    expect_refusal("9, silence not along time", 9, one(Schedule::Band{0, 2, 0, 4}), "heinzel::compile: a band buffer keeps silence before its block only");
    expect_refusal("10, rows not contiguous", 10, one(Schedule::Band{0, 2, 0, 4}), "heinzel::compile: a band buffer's rows must be contiguous");
    expect_refusal("11, too many kept", 11, one(Schedule::Band{1, 2, 0, 4}), "heinzel::compile: a band keeps more values than a band holds");
    expect_refusal("13, a lag", 13, one(Schedule::Band{0, 3, 0, 4}), "heinzel::compile: in a band, a later group reads a band value through a lag");
}

} // namespace

int main() {
    check_blurxy<1, 2, 5, 8, 34, 35, 100>();
    check_refusals();
    if (g_wrong != 0) {
        std::printf("%d results are wrong\n", g_wrong);
        return 1;
    }
    std::printf("nest_band: every banded blurxy bit for bit the unbanded one, nothing written outside a buffer; every refusal by name\n");
    return 0;
}
