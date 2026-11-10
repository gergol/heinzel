// Which stages the planner bands (`loops/planner/bands.hpp`), and that what it bands runs exactly as the
// same graph stored whole. A band changes no arithmetic order, so every banded run is compared BIT FOR BIT
// with the run planned by a machine that never bands (`band_bytes = 0`).
//
//   blurxy 8 MB      one band over its two stages, as high as half the L2 keeps, lowered to make the chunks a whole
//                    multiple of the cores
//   harris 25 MB     one band, three band buffers (the products)
//   256 KB           blurxy and harris: under `band_when_over` and the L2, stored whole
//   conv 4 MB        its working set under the threshold, but the square its taps read outgrows the second level:
//                    banded at the machine's height (`Plan/22-conv-bands`)
//   osc              no read through a window reaches ahead: no band
//   three samples    a window along rows over a value three samples wide: its rows are not contiguous, and
//                    the band that would be is refused by name — nothing banded, the graph still lowers
//   contractions     mlp2, linear_relu, attention: planned exactly as without bands
//   norms 1 MB       a cheap stage reading the row a fold read (rownorm, rmsnorm): a stream band of 16 rows, though
//                    under `band_when_over`; softmax reads it again behind an `exp`: stored whole (`Plan/21-row-bands`)
//   two chains       two blurxy pipelines side by side in one graph
//   from a knob      blurxy past the L3 behind a gain worked out from a knob, a stage with no loop first: its run still
//                    outgrows the last-level cache where a map runs in its reader (`docs/knob-math.md`)
//   edited           the planner's band edited by hand: another height, or time, lowers and runs as stored whole; an
//                    axis the band cannot walk is refused by name

#include "heinzel/dsp/oscillator.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/band_layout.hpp"
#include "heinzel/loops/planner/bands.hpp"
#include "heinzel/loops/planner/in_the_reader.hpp"
#include "heinzel/loops/planner/stages.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/notation/einsum.hpp"
#include "heinzel/sema/analyze.hpp"

#include "canaried.hpp"
#include "planned.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <cstdio>
#include <cstring>
#include <numbers>
#include <string_view>
#include <utility>
#include <vector>

using namespace heinzel;

namespace {

namespace loops = heinzel::loops;
using loops::Schedule;

constexpr Machine kWhole = [] {
    Machine m = zen2;
    m.band_bytes = 0;
    return m;
}();

constexpr Graph analysed(Graph g) {
    if (!infer(g).ok) {
        fail("nest_band_plan_test: a graph did not resolve");
    }
    return g;
}

constexpr Expr blurxy_of(Expr x, const char* wh, const char* wv) {
    Input h{wh, {1, 1, 3}};
    Input v{wv, {1, 3, 1}};
    Expr bx = (x.unfold(1, 3) * h).sum(2);
    return (bx.unfold(0, 3) * v).sum(1);
}

constexpr Graph blurxy(int height, int samples = 1) {
    return analysed(finish(Output{"y"} = blurxy_of(Input{"x", Signal{height, 2048, samples}}, "wh", "wv")));
}

constexpr Graph two_chains() {
    Expr a = blurxy_of(Input{"a", {1024, 2048}}, "ah", "av");
    Expr b = blurxy_of(Input{"b", {1024, 2048}}, "bh", "bv");
    return analysed(finish(Output{"ya"} = a, Output{"yb"} = b));
}

constexpr Graph harris(int height) {
    Input x{"x", {height, 2048}};
    Input wx{"wx", {1, 3, 1, 3}};
    Input wy{"wy", {1, 3, 1, 3}};
    Expr taps = x.unfold2d(0, 3, 1, 3);
    Expr dx = (taps * wx).sum({1, 3});
    Expr dy = (taps * wy).sum({1, 3});
    const auto box = [](Expr v) { return v.unfold2d(0, 3, 1, 3).sum({1, 3}); };
    Expr sxx = box(dx * dx);
    Expr sxy = box(dx * dy);
    Expr syy = box(dy * dy);
    Expr tr = sxx + syy;
    return analysed(finish(Output{"y"} = sxx * syy - sxy * sxy - static_cast<double>(0.04f) * tr * tr));
}

constexpr Graph conv() {
    Input x{"x", {1048576}};
    Input h{"h", {1, 32}};
    return analysed(finish(Output{"y"} = ((x * x).unfold(0, 32) * h).sum(1)));
}

constexpr Graph oscillator() {
    Input inc{"inc", Signal{Length{.multiple_of = 1, .min = 1, .expect = 512}}};
    return analysed(finish(Output{"y"} = dsp::oscillator(inc, constant(0.5))));
}

constexpr Graph mlp2() {
    Input x{"x", {128, 512}};
    Input w1{"w1", {512, 256}};
    Input w2{"w2", {256, 512}};
    Expr h = max(einsum("ik,kj->ij", x, w1), 0.0);
    return analysed(finish(Output{"y"} = max(einsum("ik,kj->ij", h, w2), 0.0)));
}

constexpr Graph attention() {
    Input q{"Q", {512, 64}};
    Input k{"K", {512, 64}};
    Input v{"V", {512, 64}};
    Expr scores = einsum("qd,kd->qk", q, k) * (1.0 / 8.0);
    Expr e = exp(scores - scores.max(1, keepdims));
    return analysed(finish(Output{"out"} = einsum("qk,kd->qd", e / e.sum(1, keepdims), v)));
}

template <int Rows = 1024, int Columns = 256>
constexpr Graph rownorm() {
    Input x{"x", {Rows, Columns}};
    Output y{"y"};
    y = x - x.mean(1, keepdims);
    return analysed(y);
}
constexpr Graph rmsnorm() {
    Input x{"x", {1024, 256}};
    Output y{"y"};
    y = x / sqrt((x * x).mean(1, keepdims) + 1e-6f);
    return analysed(y);
}
constexpr Graph softmax() {
    Input x{"x", {1024, 256}};
    Expr e = exp(x - x.max(1, keepdims));
    Output y{"y"};
    y = e / e.sum(1, keepdims);
    return analysed(y);
}
constexpr Graph kRownorm = rownorm();
constexpr Graph kRmsnorm = rmsnorm();
constexpr Graph kSoftmax = softmax();

constexpr Graph kBlurxy = blurxy(1024);
constexpr Graph kBlurxySmall = blurxy(32);
constexpr Graph kBlurxyWide = blurxy(1024, 3);
constexpr Graph kHarris = harris(1024);
constexpr Graph kHarrisSmall = harris(32);
constexpr Graph kConv = conv();
constexpr Graph kOsc = oscillator();
constexpr Graph kMlp2 = mlp2();
constexpr Graph kAttention = attention();
constexpr Graph kTwoChains = two_chains();
// The planner's schedules — what it decides, bands included — with bands and without.
template <const Graph& G>
constexpr Schedule kPlan = planned::schedule_of(G, zen2);
template <const Graph& G>
constexpr Schedule kPlanWhole = planned::schedule_of(G, kWhole);
// ...and the programs they lower to: `planned::program_of` stops the build where the lowering refuses a schedule.
template <const Graph& G>
constexpr planned::Held kBandedHeld{planned::program_of(G, kPlan<G>)};
template <const Graph& G>
constexpr const loops::Program& kBanded = kBandedHeld<G>.program;
template <const Graph& G>
constexpr planned::Held kStoredWholeHeld{planned::program_of(G, kPlanWhole<G>)};
template <const Graph& G>
constexpr const loops::Program& kStoredWhole = kStoredWholeHeld<G>.program;
template <const Graph& G>
constexpr bool kLowers = !kBanded<G>.stages.empty();

// The height: half the L2 holds the band buffers' rows — a row of `bx` is 2046 floats, and a chunk keeps
// its rows and the two its window reaches ahead — then lowered until the chunks are a whole multiple of zen2's 16
// cores: blurxy's 1022 rows in 35 chunks of 30 leave the busiest of 16 workers 90 rows where an even share is 64;
// 47 chunks of 22, 66. harris's 1020 rows make 128 chunks of 8 already.
constexpr std::int32_t kRow = 2046 * 4;
constexpr std::int32_t kBlurxyStep = static_cast<std::int32_t>((zen2.band_bytes - 2 * kRow) / kRow);
constexpr std::int32_t kHarrisStep = static_cast<std::int32_t>((zen2.band_bytes - 3 * 2 * kRow) / (3 * kRow));
constexpr bool evened(const Schedule::Band& band, std::int32_t at_most, std::int64_t rows) { // the busiest within 10 %
    const std::int64_t chunks = (rows + band.step - 1) / band.step;
    const std::int64_t busiest = (chunks + zen2.cores - 1) / zen2.cores * band.step;
    return band.step <= at_most && busiest * 10 <= (rows + zen2.cores - 1) / zen2.cores * 11;
}
static_assert(kBlurxyStep == 30 && evened(kPlan<kBlurxy>.bands[0], kBlurxyStep, 1022) && kPlan<kBlurxy>.bands[0].step == 22);
static_assert(kPlan<kBlurxy>.bands.size() == 1 && kPlan<kBlurxy>.bands[0] == Schedule::Band{0, 2, 0, 22, true}, "blurxy at 8 MB: one band, split among workers");
static_assert(kPlan<kHarris>.bands.size() == 1 && kPlan<kHarris>.bands[0] == Schedule::Band{0, 2, 0, kHarrisStep, true}, "harris at 25 MB: one band, split among workers");
static_assert(loops::kBandBuffers<kBanded<kHarris>, 0>.size() == 3, "...keeping its three products per band");
static_assert(kPlan<kBlurxySmall>.bands.empty() && kPlan<kHarrisSmall>.bands.empty(), "256 KB: under band_when_over, stored whole");
// conv at 4 MB: its working set fits the last-level cache, but the square its taps read outgrows the second level, so
// both stages are banded at the machine's height (`Plan/22-conv-bands`).
static_assert(kPlan<kConv>.bands.size() == 1 && kPlan<kConv>.bands[0].first == 0 && kPlan<kConv>.bands[0].count == 2 && kPlan<kConv>.bands[0].axis == 0,
              "conv at 4 MB: the square its taps read banded");
static_assert(evened(kPlan<kConv>.bands[0], 65504, 1048545) && kPlan<kConv>.bands[0].step == 32768, "...in 32 chunks, two for each of 16 workers");
// A size no power of two (the second review): 999985 outputs make 16 chunks of 65504 already; the evened height,
// 62500, rounded DOWN to the lanes block (62496) would make 17 and idle 7 of 16 workers — rounded up (62528), 16.
constexpr Graph conv_of_a_million() {
    Input x{"x", {1000000}};
    Input h{"h", {1, 16}};
    return analysed(finish(Output{"y"} = ((x * x).unfold(0, 16) * h).sum(1)));
}
static_assert(evened(planned::schedule_of(conv_of_a_million(), zen2).bands[0], 65504, 999985) &&
              planned::schedule_of(conv_of_a_million(), zen2).bands[0].step == 62528);
// The oscillator's waveform reads its phase at the point, ahead of nothing — a stream: banded in chunks of
// `band_stream_rows` samples, so the waveform runs while the accumulator's chain still does.
static_assert(kPlan<kOsc>.bands.size() == 1 && kPlan<kOsc>.bands[0] == Schedule::Band{0, 2, 0, zen2.band_stream_rows});

// A band over a value three samples wide would copy rows that are not contiguous: the candidate is H, and
// the band over it is refused by name — which is why nothing is banded, not a height or a threshold.
constexpr std::int32_t wide_candidate() {
    loops::ReadsMemo memo{};
    return loops::planner::detail::candidate_axis(kBlurxyWide, loops::graph::facts_of(kBlurxyWide), kPlan<kBlurxyWide>.stages, 1, memo);
}
constexpr loops::Refusal wide_refusal() {
    loops::ReadsMemo memo{};
    const loops::graph::Facts facts = loops::graph::facts_of(kBlurxyWide);
    const loops::graph::ValuePlaces at = loops::graph::where_values_are_made(kBlurxyWide, facts, kPlan<kBlurxyWide>.stages);
    return loops::band_layout(kBlurxyWide, facts, at, kPlan<kBlurxyWide>.stages, Schedule::Band{0, 2, 0, 1}, memo).why_not;
}
static_assert(wide_candidate() == 0 && kPlan<kBlurxyWide>.bands.empty());
static_assert(wide_refusal() == "heinzel::compile: a band buffer's rows must be contiguous: its outer axes all of extent one");

// A machine that never bands plans every graph as before bands — but for the splits among workers, which a
// stage in a band does not get (its band is split); contractions never reach a band at all.
constexpr Schedule without_bands(Schedule d) {
    d.bands = {};
    for (loops::Schedule::Stage& s : d.stages) {
        s.parallel = -1;
    }
    return d;
}
static_assert(without_bands(kPlanWhole<kBlurxy>) == without_bands(kPlan<kBlurxy>) && without_bands(kPlanWhole<kHarris>) == without_bands(kPlan<kHarris>));
static_assert(kPlan<kMlp2> == kPlanWhole<kMlp2> && kPlan<kAttention> == kPlanWhole<kAttention> && kPlan<kMlp2>.bands.empty());

// A planner's band edited by hand is laid out by the lowering, from the graph and the schedule's decisions: another
// height, or another axis the band can walk (time, one sample long), lowers and runs as the run stored whole (`main`);
// an axis it cannot walk is refused by name. (Planned on a machine that bands even a small blurxy, six rows high.)
constexpr Machine kEager = [] {
    Machine m = zen2;
    m.band_when_over = 0;
    m.band_bytes = 8 * kRow;
    return m;
}();
constexpr Schedule kEagerPlan = planned::schedule_of(kBlurxySmall, kEager);
constexpr Schedule edited(std::int32_t axis, std::int32_t step) {
    Schedule d = kEagerPlan;
    d.bands[0].axis = axis;
    d.bands[0].step = step;
    return d;
}
template <std::int32_t Axis, std::int32_t Step>
constexpr planned::Held kEditedHeld{planned::program_of(kBlurxySmall, edited(Axis, Step))};
constexpr std::string_view kNoFoldAxis = "heinzel::compile: a band walks an axis of each group's domain that no fold of it collapses";
static_assert(kEagerPlan.bands.size() == 1 && kEagerPlan.bands[0].axis == 0 && kEagerPlan.bands[0].step == 6);
static_assert(planned::refusal_of(kBlurxySmall, edited(1, 6)) == kNoFoldAxis && planned::refusal_of(kBlurxySmall, edited(-1, 6)) == kNoFoldAxis &&
              planned::refusal_of(kBlurxySmall, edited(2, 6)) == "heinzel::compile: a band buffer's rows must be contiguous: its outer axes all of extent one");
static_assert(kEditedHeld<0, 7>.program.bands.size() == 1 && kEditedHeld<0, 7>.program.bands[0].step == 7 && kEditedHeld<3, 6>.program.bands.size() == 1 &&
              kEditedHeld<3, 6>.program.stages[1].band_axis == 3);

// The norms read each row again in a cheap stage, after the fold: a stream band of `band_stream_rows` rows over both
// stages; softmax reads it again behind an `exp`, where the read is hidden: stored whole.
constexpr bool row_band(const Schedule& d) {
    return d.bands.size() == 1 && d.bands[0].first == 0 && d.bands[0].count == 2 && d.bands[0].axis == 0 && d.bands[0].step == zen2.band_stream_rows;
}
static_assert(row_band(kPlan<kRownorm>) && row_band(kPlan<kRmsnorm>) && kPlan<kSoftmax>.bands.empty(),
              "a cheap stage reading a row again, over 512 KB of rows: banded");
// ...but not where 16 rows of it outgrow `band_bytes` (a row of 128 KB): nothing would stay near between the reads.
constexpr Graph kWideRownorm = rownorm<40, 32768>();
static_assert(kPlan<kWideRownorm>.bands.empty(), "16 rows of 128 KB: not banded");

// A value of 2^60 doubles: its bytes saturate as large, never overflow (the review of `Plan/21-row-bands`).
constexpr Graph huge_rownorm() {
    Expr x = detail::input("x", Shape{std::int32_t{1} << 30, std::int32_t{1} << 30, 1}, ElemType::F64);
    return analysed(finish(Output{"y"} = x - x.mean(1, keepdims)));
}
static_assert(!planned::schedule_of(huge_rownorm(), zen2).stages.empty());

// Two chains side by side: a band each, in stage order.
static_assert(kPlan<kTwoChains>.bands.size() == 2 && kPlan<kTwoChains>.bands[0].first == 0 && kPlan<kTwoChains>.bands[1].first == 2);

// Review of M2, F4 (older than bands): a cheap producer inlined into one window's stage, which a second
// window reads the result of, made the inline pass ask a lane question of the producer's operand over the
// second window's region — `walked_axis` failed, and the graph did not plan. "Maps only" is asked first.
constexpr Graph squared_blurxy() {
    Input x{"x", {37, 70}};
    return analysed(finish(Output{"y"} = blurxy_of(x * x, "wh", "wv")));
}
constexpr Graph median_of_medians() {
    Input x{"x", {1000}};
    return analysed(finish(Output{"y"} = (x * 2.0).unfold(0, 3).median(1).unfold(0, 3).median(1)));
}
constexpr Graph kSquaredBlurxy = squared_blurxy();
constexpr Graph kMedianOfMedians = median_of_medians();
static_assert(kLowers<kSquaredBlurxy> && kLowers<kMedianOfMedians> && kPlan<kSquaredBlurxy>.stages.size() == 2 &&
              kPlan<kSquaredBlurxy>.stages[0].through >= 0);

// Review of M3. F3: the threshold is the working set against the last-level cache — harris at 232 rows
// (9.6 MB, 5.6 of them kept) ran 2-3 % slower banded; blurxy at 690 rows (16.9 MB) 9 % faster. Since
// `Plan/22-conv-bands` a value two cheap stages hand on past the L2 bands it too: harris at 232 rows, measured +1.3 %
// banded (blurxy at 512 rows -20 to -27 %); the working set alone still decides on a machine whose L2 is as large. F1: a
// symbolic extent inside the rows made the size that decides it overflow; it counts as large, and such a
// band has no height (a row of unknown length), so nothing is banded — and the graph plans.
constexpr Graph two_kept_symbolic_width() {
    // a symbolic element axis, which no declaration spells (DS-18)
    const DimDecl w{0, DimConstraints{1, 16}};
    Expr x = detail::input("x", Shape{1024, Extent{Dim{w.id}}, 1}, ElemType::F32, w);
    Input v{"wv", {1, 3, 1}};
    const auto blur = [&](Expr a) { return (a.unfold(0, 3) * v).sum(1); };
    return analysed(finish(Output{"y"} = blur(tanh(x) * 1.5) + blur(tanh(x * 0.7) * 0.5)));
}
constexpr Graph kHarrisInL3 = harris(232);
constexpr Graph kBlurxyPastL3 = blurxy(690);
constexpr Graph kSymbolicWidth = two_kept_symbolic_width();
constexpr Machine kLargeL2 = [] {
    Machine m = zen2;
    m.l2_bytes = static_cast<std::int32_t>(m.l3_bytes);
    return m;
}();
static_assert(planned::schedule_of(kHarrisInL3, kLargeL2).bands.empty() && planned::schedule_of(kBlurxyPastL3, kLargeL2).bands.size() == 1,
              "banded where the working set outgrows the L3");
static_assert(kPlan<kHarrisInL3>.bands.size() == 1, "harris at 232 rows hands its products on past the L2: banded");
static_assert(kLowers<kSymbolicWidth> && kPlan<kSymbolicWidth>.bands.empty());

// The work ahead first: whether a run outgrows the last-level cache, which prices a map stored or in its reader
// (`planner/in_the_reader.hpp` `readers_of`), is asked of the run's first stage with a loop. blurxy past the L3 with its
// input scaled by a gain worked out from a knob in dB: the gain is work gathered ahead of every loop (`next_unit`), a
// stage with no loop first, and the run still spills.
constexpr Graph blurxy_past_l3_from_a_knob() {
    Param db{"db", ge(-24), le(24)};
    Input x{"x", Signal{690, 2048, 1}};
    const Expr gain = exp(db * (std::numbers::ln10 / 20.0));
    return analysed(finish(Output{"y"} = blurxy_of(x * gain, "wh", "wv")));
}
// The rank of the domain of `g`'s first stage as gathered, and whether its run spills.
constexpr std::pair<std::int32_t, bool> spills(const Graph& g) {
    const loops::graph::Facts facts = loops::graph::facts_of(g);
    const loops::graph::Feedback feedback = loops::graph::find_feedback(g, facts);
    loops::planner::Gathered gathered{};
    if (!loops::planner::gather_groups(g, facts, feedback, zen2, gathered)) {
        return {-1, false};
    }
    return {gathered.groups[0].shape.domain.rank, loops::planner::detail::readers_of(g, facts, gathered, zen2).spills};
}
constexpr std::pair<std::int32_t, bool> kBlurxyPastL3Spills = spills(kBlurxyPastL3);
constexpr std::pair<std::int32_t, bool> kFromAKnobSpills = spills(blurxy_past_l3_from_a_knob());
static_assert(kBlurxyPastL3Spills.first > 0 && kBlurxyPastL3Spills.second && kFromAKnobSpills.first == 0 && kFromAKnobSpills.second,
              "a run past the L3 spills, the work gathered ahead of its loops first or not");

// Review of M6. F2: a stream band over a bank of 3 voices (a sample narrower than a lane but more than one
// number) lost 2-7 %: not banded; 16 voices are. F4 (older than bands): a bank whose feedback reads only
// two samples back was planned time-innermost, which only a lag of ONE sample (state in a register) can
// run — the planner said yes and the lowering refused; now it plans time outermost and lowers.
constexpr Graph bank(int voices, int depth) {
    constexpr Index n{};
    Input x{"x", Signal{voices, Length{.multiple_of = 1, .min = 1, .expect = 512}}};
    Variable y;
    y[n] = tanh(x * 1.3) + 0.4 * y[n - depth];
    return analysed(finish(Output{"z"} = y * 0.5));
}
// ...and (re-review, F1) a lag of a lag hands nothing on either: what a one-sample lag reads has to be made
// LATER in the stage, as the lowering asks it.
constexpr Graph lag_of_lag() {
    constexpr Index n{};
    Input x{"x", Signal{3, Length{.multiple_of = 1, .min = 1, .expect = 512}}};
    Variable y;
    y[n] = x + 0.5 * y[n - 2][n - 1];
    return analysed(finish(Output{"z"} = y));
}
constexpr Graph kBank3 = bank(3, 1);
constexpr Graph kBank16 = bank(16, 1);
constexpr Graph kBankLag2 = bank(3, 2);
constexpr Graph kLagOfLag = lag_of_lag();
static_assert(kLowers<kLagOfLag> && kBanded<kLagOfLag>.stages.size() >= 1);
static_assert(kPlan<kBank3>.bands.empty() && kPlan<kBank16>.bands.size() == 1, "a stream band only where a sample is one number or whole lanes");
static_assert(kLowers<kBankLag2> && kBanded<kBankLag2>.stages.size() >= 1);

// `Plan/22-conv-bands`' review: in a stream only a value read twice bands it by the L2 rule. A value a recurrence hands
// a cheap map, chunked into 16 samples, ran 5-17 % slower banded (a bank of one voice, a phase and its wrap); each
// sample less its mean over 8 voices, its input read twice, -25 %.
constexpr Graph literal_bank() {
    constexpr Index n{};
    Input x{"x", Signal{1, 262147}};
    Variable y;
    y[n] = x * 1.3 + 0.4 * y[n - 1];
    return analysed(finish(Output{"z"} = y * 0.5));
}
constexpr Graph phase() {
    constexpr Index n{};
    Input x{"x", Signal{262149}};
    Variable ph;
    ph[n] = x + ph[n - 1];
    return analysed(finish(Output{"y"} = ph * 0.5 - floor(ph * 0.5)));
}
constexpr Graph centred() {
    Input x{"x", Signal{8, 65536}};
    return analysed(finish(Output{"y"} = x - x.mean(0, keepdims)));
}
static_assert(planned::schedule_of(literal_bank(), zen2).bands.empty() && planned::schedule_of(phase(), zen2).bands.empty() &&
              planned::schedule_of(centred(), zen2).bands.size() == 1);

// ...and its cap on a stream's chunk compares without overflowing: a value of symbolic extent (2^62 bytes, as large)
// over four rows, on a machine whose working-set rule never speaks.
constexpr Graph symbolic_rows() {
    const DimDecl w{0, DimConstraints{1, 16}};
    Expr x = detail::input("x", Shape{4, Extent{Dim{w.id}}, 1}, ElemType::F32, w);
    return analysed(finish(Output{"y"} = x - x.mean(1, keepdims)));
}
constexpr Machine kNoWorkingSetRule = [] {
    Machine m = zen2;
    m.band_when_over = std::numeric_limits<std::int64_t>::max();
    return m;
}();
static_assert(!planned::schedule_of(symbolic_rows(), kNoWorkingSetRule).stages.empty());

static_assert(kLowers<kBlurxy> && kLowers<kHarris> && kLowers<kBlurxyWide> && kLowers<kConv> && kLowers<kOsc> && kLowers<kMlp2> &&
              kLowers<kAttention> && kLowers<kTwoChains>);

int g_wrong = 0;

// Every output of the banded run (`Banded`, where its schedule was edited), bit for bit that of the run stored whole —
// each twice on one workspace, in lockstep: the second continues the first (Plan/archive/14-dsl/05).
template <const Graph& G, const loops::Program& Banded = kBanded<G>>
void same(const char* what, const loops::Sizes& sizes = loops::Sizes{}) {
    static constexpr const loops::Program& banded = Banded;
    static constexpr const loops::Program& whole = kStoredWhole<G>;
    loops::Feeds feeds{};
    std::vector<std::vector<float>> data(G.inputs.size());
    for (std::size_t i = 0; i < G.inputs.size(); ++i) {
        data[i].resize(static_cast<std::size_t>(loops::element_count(banded.buffers[i], sizes)));
        for (std::size_t k = 0; k < data[i].size(); ++k) {
            data[i][k] = 0.001f * static_cast<float>((k * 7 + i * 131) % 977) - 0.45f;
        }
        feeds[i] = data[i].data();
    }
    canaried::Workspace<banded> a(sizes);
    canaried::Workspace<whole> b(sizes);
    for (int run = 0; run < 2; ++run) {
        canaried::run<whole>(feeds, b);
        canaried::run<banded>(feeds, a);
        for (std::size_t o = 0; o < banded.outputs.size(); ++o) {
            const std::size_t x = static_cast<std::size_t>(banded.outputs[o]);
            const std::size_t y = static_cast<std::size_t>(whole.outputs[o]);
            const std::int64_t n = loops::element_count(whole.buffers[y], sizes);
            if (std::memcmp(a.bytes_of(x), b.bytes_of(y), static_cast<std::size_t>(n * 4)) != 0 && ++g_wrong <= 10) {
                std::printf("  %s: run %d, output %zu differs from the run stored whole\n", what, run, o);
            }
        }
    }
}

// The review cases the planner bands nothing of — `kBanded` is `kStoredWhole` for each, so `same` would hold a
// program to itself — against a hand loop over the same inputs: every output element bit for bit, or (through
// `tanh`, a float core within an ulp of `std::tanh`, its error fed back) within `within` of it.
std::vector<float> input_of(std::size_t elements, std::size_t which) {
    std::vector<float> v(elements);
    for (std::size_t k = 0; k < v.size(); ++k) {
        v[k] = 0.001f * static_cast<float>((k * 7 + which * 131) % 977) - 0.45f;
    }
    return v;
}
template <const Graph& G>
void against(const char* what, const loops::Sizes& sizes, const std::vector<std::vector<float>>& in, const std::vector<float>& want,
             const std::vector<float>& again, float within = 0.0f) {
    static constexpr const loops::Program& p = kStoredWhole<G>;
    static_assert(kBanded<G> == kStoredWhole<G>, "a case the planner bands is held to its run stored whole, by `same`");
    loops::Feeds feeds{};
    for (std::size_t i = 0; i < in.size(); ++i) {
        feeds[i] = in[i].data();
    }
    canaried::Workspace<p> work(sizes);
    int run = 0;
    for (const std::vector<float>* expected : {&want, &again}) { // the second run continues the first (Plan/archive/14-dsl/05)
        ++run;
        canaried::run<p>(feeds, work);
        const float* got = work.template data<float>(static_cast<std::size_t>(p.outputs[0]));
        int off = 0;
        for (std::size_t k = 0; k < expected->size(); ++k) {
            const float w = (*expected)[k];
            off += (within == 0.0f ? std::bit_cast<std::uint32_t>(got[k]) != std::bit_cast<std::uint32_t>(w) : !(std::fabs(got[k] - w) <= within)) ? 1 : 0;
        }
        if (static_cast<std::size_t>(loops::element_count(p.buffers[static_cast<std::size_t>(p.outputs[0])], sizes)) != expected->size() || off != 0) {
            ++g_wrong;
            std::printf("  %s, run %d: %d of %zu elements differ from the hand loop\n", what, run, off, expected->size());
        }
    }
}

void squared_blurxy_by_hand() {
    const std::vector<std::vector<float>> in{input_of(37 * 70, 0), input_of(3, 1), input_of(3, 2)}; // x, wh, wv
    static_assert(kSquaredBlurxy.inputs.size() == 3);
    const std::vector<float>&x = in[0];
    const std::vector<float>&wh = in[1];
    const std::vector<float>&wv = in[2];
    std::vector<float> bx(37 * 68);
    std::vector<float> y(35 * 68);
    for (int i = 0; i < 37; ++i) {
        for (int j = 0; j < 68; ++j) {
            float acc = (x[i * 70 + j] * x[i * 70 + j]) * wh[0];
            acc = acc + (x[i * 70 + j + 1] * x[i * 70 + j + 1]) * wh[1];
            bx[std::size_t(i) * 68 + j] = acc + (x[i * 70 + j + 2] * x[i * 70 + j + 2]) * wh[2];
        }
    }
    for (int i = 0; i < 35; ++i) {
        for (int j = 0; j < 68; ++j) {
            float acc = bx[std::size_t(i) * 68 + j] * wv[0];
            acc = acc + bx[std::size_t(i + 1) * 68 + j] * wv[1];
            y[std::size_t(i) * 68 + j] = acc + bx[std::size_t(i + 2) * 68 + j] * wv[2];
        }
    }
    against<kSquaredBlurxy>("blurxy of a square", loops::Sizes{}, in, y, y);
}

void median_of_medians_by_hand() {
    const std::vector<std::vector<float>> in{input_of(1000, 0)};
    const auto median3 = [](float a, float b, float c) { return std::max(std::min(a, b), std::min(std::max(a, b), c)); };
    std::vector<float> m(998);
    std::vector<float> y(996);
    for (std::size_t t = 0; t < m.size(); ++t) {
        m[t] = median3(in[0][t] * 2.0f, in[0][t + 1] * 2.0f, in[0][t + 2] * 2.0f);
    }
    for (std::size_t t = 0; t < y.size(); ++t) {
        y[t] = median3(m[t], m[t + 1], m[t + 2]);
    }
    against<kMedianOfMedians>("a median of medians", loops::Sizes{}, in, y, y);
}

// y[n] = tanh(x * 1.3) + 0.4 * y[n - 2], z = y / 2 over three voices; and y[n] = x + 0.5 * y[n - 2][n - 1]. Two runs
// of `length` samples each: the second continues the first, as one run over the input fed twice.
void feedback_by_hand(std::int64_t length) {
    loops::Sizes sizes{};
    sizes[0] = length;
    const auto n = static_cast<std::size_t>(length);
    const std::vector<std::vector<float>> in{input_of(3 * n, 0)};
    std::vector<float> y(6 * n);
    std::vector<float> z(6 * n);
    std::vector<float> w(6 * n);
    for (std::size_t t = 0; t < 2 * n; ++t) {
        for (std::size_t v = 0; v < 3; ++v) {
            const float x = in[0][(t % n) * 3 + v];
            y[t * 3 + v] = std::tanh(x * 1.3f) + 0.4f * (t >= 2 ? y[(t - 2) * 3 + v] : 0.0f);
            z[t * 3 + v] = y[t * 3 + v] * 0.5f;
            w[t * 3 + v] = x + 0.5f * (t >= 3 ? w[(t - 3) * 3 + v] : 0.0f);
        }
    }
    const auto first = [&](const std::vector<float>& all) { return std::vector<float>(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(3 * n)); };
    const auto second = [&](const std::vector<float>& all) { return std::vector<float>(all.begin() + static_cast<std::ptrdiff_t>(3 * n), all.end()); };
    against<kBankLag2>("a bank fed back two samples", sizes, in, first(z), second(z), 1e-6f);
    against<kLagOfLag>("a lag of a lag", sizes, in, first(w), second(w));
}

} // namespace

int main() {
    same<kBlurxy>("blurxy");
    same<kHarris>("harris");
    same<kTwoChains>("two chains");
    same<kRownorm>("rownorm");
    same<kRmsnorm>("rmsnorm");
    same<kConv>("conv");
    squared_blurxy_by_hand();
    median_of_medians_by_hand();
    for (const std::int64_t length : {1, 63, 64, 65, 1000}) {
        loops::Sizes sizes{};
        sizes[0] = length;
        same<kOsc>("the oscillator", sizes);
        same<kBank16>("a bank of sixteen voices", sizes);
        feedback_by_hand(length);
    }
    same<kBlurxySmall, kEditedHeld<0, 7>.program>("blurxy, its band edited to seven rows");
    same<kBlurxySmall, kEditedHeld<3, 6>.program>("blurxy, its band edited to walk time");
    if (g_wrong != 0) {
        std::printf("%d results are wrong\n", g_wrong);
        return 1;
    }
    std::printf("nest_band_plan: every band where the rule says, each run bit for bit as stored whole, the rest as by hand\n");
    return 0;
}
