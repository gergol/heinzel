// Bands split among workers along their chunks (`Schedule::Band::parallel`): each worker runs a run of consecutive
// chunks with band buffers of its own (`canaried::Workspace` copies), its first chunk started as chunk 0 is — every
// stage from the last one's first row, making again what a carry would have held. The bands are added to the
// planner's schedule by hand, as `nest_band_test` adds them, and each banded program runs serially and split
// (`tests/support/split_runs.hpp`) — every whole buffer byte for byte; the gaps between a band buffer's copies stay
// untouched.
//
//   blurxy     a horizontal then a vertical blur: chunks of 1, 5, 7, 34 and more than the whole; 3 copies for
//              16 workers (the workspace caps the split)
//   harris     two stages, three band buffers, the first stage running ahead
//   chain      three vertical blurs: leads 4, 2, 0
//   conv       the band along the lanes of the window's reader
//   planned    blurxy and harris at 8 MB: the planner's own bands are split; the oscillator's is not
//   refusals   a stream band (feedback along it), and a band whose stage running ahead stores a result

#include "heinzel/dsp/oscillator.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"

#include "canaried.hpp"
#include "planned.hpp"
#include "split_runs.hpp"

#include <cstdio>
#include <string_view>

using namespace heinzel;

namespace {

namespace loops = heinzel::loops;

constexpr Machine kMachine = [] {
    Machine m = zen2;
    m.band_bytes = 0; // the bands are added by hand here
    return m;
}();
constexpr Machine kStoring = [] {
    Machine m = zen2;
    m.recompute_always = m.recompute_if_uncached = -1;
    m.band_bytes = 0;
    return m;
}();
constexpr std::int32_t kH = 37;
constexpr std::int32_t kW = 70;

constexpr Graph analysed(Graph g) {
    if (!infer(g).ok) {
        fail("nest_band_parallel_test: a graph did not resolve");
    }
    return g;
}

constexpr Expr vblur(Expr v, Expr w) { return (v.unfold(0, 3) * w).sum(1); }

// `result`: the horizontal blur is a second result (stored whole by the stage that runs ahead).
constexpr Graph blurxy(bool result, std::int32_t height = kH, std::int32_t width = kW) {
    Input x{"x", {height, width}};
    Input wh{"wh", {1, 1, 3}};
    Input wv{"wv", {1, 3, 1}};
    Expr bx = (x.unfold(1, 3) * wh).sum(2);
    Expr y = vblur(bx, wv);
    return result ? analysed(finish(Output{"y"} = y, Output{"bx"} = bx)) : analysed(finish(Output{"y"} = y));
}

constexpr Graph harris(std::int32_t height = 12, std::int32_t width = kW - 33) {
    Input x{"x", {height, width}};
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

constexpr Graph chain() {
    Input x{"x", {kH, kW}};
    Input w{"w", {1, 3, 1}};
    return analysed(finish(Output{"y"} = vblur(vblur(vblur(x, w), w), w)));
}

constexpr Graph conv() {
    Input x{"x", {1000}};
    Input h{"h", {1, 7}};
    return analysed(finish(Output{"y"} = ((x * x).unfold(0, 7) * h).sum(1)));
}

constexpr Graph oscillator() {
    Input inc{"inc", Signal{Length{.multiple_of = 1, .min = 1, .expect = 512}}};
    return analysed(finish(Output{"y"} = dsp::oscillator(inc, constant(0.5))));
}

// A band over all of a schedule's stages, split among workers (the stages keep no split of their own).
constexpr loops::Schedule split_band(loops::Schedule d, std::int32_t axis, std::int32_t step) {
    d.bands.push_back(loops::Schedule::Band{0, static_cast<std::int32_t>(d.stages.size()), axis, step, true});
    for (loops::Schedule::Stage& s : d.stages) {
        s.parallel = -1;
    }
    d.parallel_work = 1;
    return d;
}

constexpr Graph kBlurxy = blurxy(false);
constexpr Graph kBlurxyResult = blurxy(true);
constexpr Graph kHarris = harris();
constexpr Graph kChain = chain();
constexpr Graph kConv = conv();
constexpr Graph kOsc = oscillator();
constexpr Graph kBlurxy8M = blurxy(false, 1024, 2048);
constexpr Graph kHarris8M = harris(1024, 2048);
// Each graph planned once per machine; every band below is added to that schedule.
template <const Graph& G, const Machine& M = kMachine>
constexpr loops::Schedule kSchedule = planned::schedule_of(G, M);
template <const Graph& G, std::int32_t Axis, std::int32_t Step, const Machine& M = kMachine>
constexpr planned::Held kSplitBandHeld{planned::program_of(G, split_band(kSchedule<G, M>, Axis, Step))};
template <const Graph& G, std::int32_t Axis, std::int32_t Step, const Machine& M = kMachine>
constexpr const loops::Program& kSplitBand = kSplitBandHeld<G, Axis, Step, M>.program;

static_assert(kSplitBand<kBlurxy, 0, 5>.bands[0].parallel && loops::kBandBuffers<kSplitBand<kHarris, 0, 3>, 0>.size() == 3 &&
              kSplitBand<kChain, 0, 4>.stages[0].lead == 4);

// The planner splits the bands it makes where nothing but band buffers is carried; the oscillator's stream
// carries its phase from chunk to chunk.
constexpr Machine kOneCore = [] {
    Machine m = zen2;
    m.cores = 1;
    return m;
}();
static_assert(kSchedule<kBlurxy8M, zen2>.bands.size() == 1 && kSchedule<kBlurxy8M, zen2>.bands[0].parallel);
static_assert(kSchedule<kHarris8M, zen2>.bands.size() == 1 && kSchedule<kHarris8M, zen2>.bands[0].parallel);
static_assert(kSchedule<kOsc, zen2>.bands.size() == 1 && !kSchedule<kOsc, zen2>.bands[0].parallel);
// The planner's stream band of the oscillator, not split (its state runs from chunk to chunk), with a worker owed a
// single statement-point: run among workers it launches nothing — `split_runs::predicted` counts no launch for it.
constexpr loops::Schedule owed_one_point(loops::Schedule d) {
    d.parallel_work = 1;
    return d;
}
constexpr planned::Held kOscEagerHeld{planned::program_of(kOsc, owed_one_point(kSchedule<kOsc, zen2>))};
constexpr const loops::Program& kOscEager = kOscEagerHeld.program;
static_assert(kOscEager.bands.size() == 1 && !kOscEager.bands[0].parallel && kOscEager.parallel_work == 1);
static_assert(kSchedule<kBlurxy8M, kOneCore>.bands.size() == 1 && !kSchedule<kBlurxy8M, kOneCore>.bands[0].parallel, "a machine with one core splits no band");

int g_wrong = 0;

// Between a split band buffer's copies lies a gap nobody writes: run with as many workers as copies, the band split
// as its work pays for (two ways at least), and `canaried` holds every gap — every byte outside a copy's region — to
// its canary.
template <const loops::Program& P, unsigned copies>
void check_the_gaps(const char* what) {
    const loops::Sizes sizes{};
    const split_runs::Fed f = split_runs::fed(P, sizes);
    canaried::Workspace<P, copies> work(sizes);
    split_runs::InOrder workers{copies};
    canaried::run<P>(f.feeds, work, workers);
    const split_runs::Launches want = split_runs::predicted<P>(sizes, copies, copies);
    if (P.bands.size() != 1 || split_runs::Launches{workers.launches, workers.ranges} != want || want.launches != 1 || workers.most < 2) {
        ++g_wrong;
        std::printf("  %s: the band ran %u times split into %u ranges, %u into %u wanted\n", what, workers.launches, workers.ranges, want.launches,
                    want.ranges);
    }
}

void expect_refusal(const char* what, const Graph& g, const loops::Schedule& d, std::string_view says) {
    const loops::Refusal held = planned::refusal_of(g, d);
    const std::string_view said = held.view();
    if (said.substr(0, says.size()) != says && ++g_wrong <= 20) {
        const std::string_view shown = said.empty() ? std::string_view{"(nothing: it lowered)"} : said;
        std::printf("  refusal %s: said \"%.*s\"\n", what, static_cast<int>(shown.size()), shown.data());
    }
}

template <int... Steps>
void check_blurxy() {
    ((g_wrong += split_runs::check<kSplitBand<kBlurxy, 0, Steps>>("blurxy", loops::Sizes{}, Steps < kH - 2)), ...);
}

} // namespace

int main() {
    check_blurxy<1, 5, 7, 34, 35, 100>();
    g_wrong += split_runs::check<kSplitBand<kBlurxy, 0, 5>, 3>("blurxy, 3 copies", loops::Sizes{}, true);
    g_wrong += split_runs::check<kSplitBand<kHarris, 0, 1>>("harris, chunks of 1", loops::Sizes{}, true);
    g_wrong += split_runs::check<kSplitBand<kHarris, 0, 3>>("harris, chunks of 3", loops::Sizes{}, true);
    g_wrong += split_runs::check<kSplitBand<kChain, 0, 4>>("chain", loops::Sizes{}, true);
    g_wrong += split_runs::check<kSplitBand<kChain, 0, 1>>("chain, chunks of 1", loops::Sizes{}, true);
    g_wrong += split_runs::check<kSplitBand<kConv, 0, 50, kStoring>>("conv", loops::Sizes{}, true);
    check_the_gaps<kSplitBand<kBlurxy, 0, 5>, 7>("blurxy");
    check_the_gaps<kSplitBand<kHarris, 0, 1>, 7>("harris");
    loops::Sizes samples{};
    samples[0] = 1000;
    g_wrong += split_runs::check<kOscEager>("a stream band, never split", samples, false);
    expect_refusal("a stream band", kOsc, split_band(kSchedule<kOsc>, 0, 16),
                   "heinzel::compile: a split band carries only band buffers, and what runs ahead writes only those");
    expect_refusal("a result made ahead", kBlurxyResult, split_band(kSchedule<kBlurxyResult>, 0, 5),
                   "heinzel::compile: a split band carries only band buffers, and what runs ahead writes only those");
    if (g_wrong != 0) {
        std::printf("%d results are wrong\n", g_wrong);
        return 1;
    }
    std::printf("nest_band_parallel: every split band bit for bit the serial run, its copies kept apart, every refusal by name\n");
    return 0;
}
