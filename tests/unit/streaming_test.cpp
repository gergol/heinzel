// State carried from one run to the next (Plan/archive/14-dsl/05, Decision 52): a signal run in blocks on one workspace gives
// the numbers of one run over the whole, bit for bit — every output, after every block — at `Normal` and at `High`.
// Each graph's stream is a `Length`, so one program serves every block size; the workspace is built for the largest
// block and each run sets its own (capacity mode). The sizes: 1, 2, an odd one, one past the largest, mixed ones, and
// from each program its deepest feed read − 1, its deepest prefix − 1 and its band's step ± 1. Split runs too — along
// time, along voices (each worker's first samples read the tail), a split band — in order and on a pool. One literal
// graph is built at a block and at a multiple of it. Then `reset` (back to silence, exactly the carried regions),
// `seed`, and `streams` both ways.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

#include "heinzel/compile.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/loops/streams.hpp"
#include "heinzel/loops/workspace.hpp"
#include "heinzel/sema/analyze.hpp"

#include "canaried.hpp"
#include "planned.hpp"
#include "split_runs.hpp"

using namespace heinzel;

namespace {

constexpr Index n{};
constexpr Length kT{};

// ---- the graphs -------------------------------------------------------------------------------------------

constexpr Graph bank(std::int32_t voices) { // state in registers (narrow) and through the result (wide)
    Input x{"x", Signal{voices, kT}};
    Output y{"y"};
    y[n] = x[n] + 0.9375 * y[n - 1];
    return y;
}
constexpr Graph two_poles() {
    Input x{"x", Signal{kT}};
    Output y{"y"};
    y[n] = x[n] + 1.2 * y[n - 1] - 0.5 * y[n - 2];
    return y;
}
constexpr Graph coupled() {
    Input x{"x", Signal{2, kT}};
    Variable a;
    Variable b;
    a[n] = x[n] + 0.5 * b[n - 1];
    b[n] = x[n] + 0.25 * a[n - 1];
    Output y{"y"};
    y = a + b;
    return y;
}
constexpr Graph echo(std::int32_t lag) { // 3 samples, and 40: deeper than most blocks
    Input x{"x", Signal{2, kT}};
    Output y{"y"};
    y[n] = x[n] + 0.5 * y[n - lag];
    return y;
}
constexpr Graph fir(std::int32_t voices) { // a feed read in its past: its tail
    Input x{"x", Signal{voices, kT}};
    Output y{"y"};
    y = 0.5 * x[n] + 0.3 * x[n - 1] + 0.2 * x[n - 5];
    return y;
}
constexpr Graph biquad() {
    Input x{"x", Signal{kT}};
    Output y{"y"};
    y[n] = 0.2 * x[n] + 0.4 * x[n - 1] + 0.2 * x[n - 2] + 1.1 * y[n - 1] - 0.6 * y[n - 2];
    return y;
}
constexpr Graph lagged_ramp() { // a ramp keeps a prefix too
    Input x{"x", Signal{4, kT}};
    Expr ramp = iota(Signal{4, Dim{kStreamLength}}); // a Dim declares nothing: x declares the stream
    Output y{"y"};
    y = x[n] + ramp[n - 2];
    return y;
}
constexpr Graph oscillator() { // in a stream band
    Input inc{"inc", Signal{kT}};
    Variable phase;
    phase[n] = frac(phase[n - 1] + inc[n]);
    Output y{"y"};
    y = sinpi(2.0 * phase) * 0.5;
    return y;
}
constexpr Graph delay_line() { // y[t] = a[t - p[t]], a = x / 2: a depth that is a signal, a Gather::Back
    Input x{"x", Signal{3, kT}};
    Input p{"p", Signal{3, kT}};
    Expr a = x * 0.5;
    Expr depth = p * 4.0;
    Arena* arena = bring_together(a, depth);
    BuildNode node{};
    node.op = op::DelayLine;
    node.inputs[0] = a.position();
    node.inputs[1] = depth.position();
    node.attr.kind = AttrKind::SignalledDelay;
    node.attr.signalled_delay.max_delay = 5;
    arena->nodes.push_back(node);
    Output y{"y"};
    y = Expr{arena, static_cast<std::int32_t>(arena->nodes.size() - 1)};
    return y;
}
constexpr Graph param_pole() { // a Param in the recurrence: the same number every block
    Input x{"x", Signal{2, kT}};
    Param pole{"pole"};
    Output y{"y"};
    y[n] = x[n] + 0.5 * pole * y[n - 1];
    return y;
}
constexpr Graph blurred_echo() { // two blurs across 37 voices, a split band's, then a recurrence outside it
    const auto blur = [](Expr v, Expr w) { return (v.unfold(0, 3) * w).sum(1); };
    Input x{"x", Signal{37, kT}};
    Input w{"w", {1, 3}};
    Expr b = blur(blur(x, w), w);
    Output y{"y"};
    y[n] = b[n] + 0.5 * y[n - 1];
    return y;
}
constexpr Graph knob_oscillator() { // no signal drives it: its length declared (Plan/archive/14-dsl/10), in a stream band
    Param f{"f"};
    Variable phase{Signal{kT}};
    phase[n] = frac(phase[n - 1] + f * 0.01);
    Output y{"y"};
    y = sinpi(2.0 * phase) * 0.5;
    return y;
}
constexpr Graph knob_beside_an_input() { // no signal drives it: x's Length taken (Plan/28-dsl-proposals/03 B1)
    Input x{"x", Signal{kT}};
    Param f{"f"};
    Variable phase;
    phase[n] = frac(phase[n - 1] + f * 0.01);
    Output y{"y"};
    y = x * sinpi(2.0 * phase);
    return y;
}
constexpr Graph deep_band() { // a stream band whose buffer keeps three rows: blocks of 1 and 2 are shorter
    Input inc{"inc", Signal{kT}};
    Variable phase;
    phase[n] = frac(phase[n - 3] + inc[n]);
    Output y{"y"};
    y = sinpi(2.0 * phase) * 0.5;
    return y;
}
constexpr Graph wide_f64() { // F64 throughout: a feed's tail and a prefix of 8-byte samples
    Input x{"x", Signal{3, kT}, f64};
    Output y{"y"};
    y[n] = x[n] + 0.25 * x[n - 2] + 0.5 * y[n - 1] - 0.125 * y[n - 5];
    return y;
}
constexpr Graph timeless_first() { // a timeless output listed before the streamed recurrence
    Input x{"x", Signal{kT}};
    Input w{"w", {3}};
    Output scaled{"scaled"};
    Output y{"y"};
    scaled = w * 2.0;
    y[n] = x[n] + 0.5 * y[n - 1];
    return finish(scaled, y);
}

template <Graph (*Build)(), numerics::Precision L>
constexpr Graph at_precision() {
    Graph g = Build();
    set_default_precision(g, L);
    return g;
}

// ---- one run against runs over blocks ----------------------------------------------------------------------

// A feed or an output with the stream as its last axis moves along it; any other is the whole of it every run.
bool streamed(const loops::Buffer& b) { return b.shape.rank >= 1 && is_symbol(extent_at(b.shape, b.shape.rank - 1)); }

struct Fed {
    std::vector<std::vector<std::byte>> storage;
    loops::Feeds feeds{};
};
Fed fed(const loops::Program& p, std::int64_t total) {
    Fed f{};
    f.storage.resize(kMaxIO);
    loops::Sizes sizes{};
    sizes.fill(total);
    for (const loops::Buffer& b : p.buffers) {
        if (b.home != loops::Home::Feed) {
            continue;
        }
        std::vector<std::byte>& held = f.storage[static_cast<std::size_t>(b.feed)];
        const std::int64_t count = loops::element_count(b, sizes);
        held.resize(static_cast<std::size_t>(count * loops::bytes_of(b.type)));
        for (std::int64_t k = 0; k < count; ++k) {
            const double v = 0.1 + static_cast<double>((static_cast<std::uint64_t>(k) * 2654435761u + static_cast<std::uint64_t>(b.feed) * 97u) % 1000u) / 1000.0;
            b.type == ElemType::F32 ? void(reinterpret_cast<float*>(held.data())[k] = static_cast<float>(v)) : void(reinterpret_cast<double*>(held.data())[k] = v);
        }
        f.feeds[static_cast<std::size_t>(b.feed)] = held.data();
    }
    return f;
}

// Every output's bytes of the run just made: a streamed one appended to `outs`, any other checked against `whole`.
template <const loops::Program& P>
bool collect(auto& work, std::vector<std::vector<std::byte>>& outs, const std::vector<std::vector<std::byte>>* whole) {
    bool same = true;
    for (std::size_t o = 0; o < P.outputs.size(); ++o) {
        const auto b = static_cast<std::size_t>(P.outputs[o]);
        const auto size = static_cast<std::size_t>(loops::element_count(P.buffers[b], work.sizes()) * loops::bytes_of(P.buffers[b].type));
        const std::byte* at = work.bytes_of(b);
        if (streamed(P.buffers[b]) || whole == nullptr) {
            outs[o].insert(outs[o].end(), at, at + size);
        } else {
            same = same && std::memcmp(at, (*whole)[o].data(), size) == 0;
        }
    }
    return same;
}

// One block's run — among in-order workers, its launches and ranges held to what its split marks call for at this
// block's length (`split_runs::predicted`): a runner that stopped splitting after a `set_length` would show here.
template <const loops::Program& P, class... Workers>
bool run_block(const loops::Feeds& feeds, auto& work, Workers&... workers) {
    canaried::run<P>(feeds, work, workers...);
    return true;
}
template <const loops::Program& P>
bool run_block(const loops::Feeds& feeds, auto& work, split_runs::InOrder& workers) {
    const split_runs::Launches before{workers.launches, workers.ranges};
    canaried::run<P>(feeds, work, workers);
    const split_runs::Launches made{workers.launches - before.launches, workers.ranges - before.ranges};
    return made == split_runs::predicted<P>(work.sizes(), workers.size(), work.copies());
}

// One serial run over `total` samples, then runs over blocks cycling through `blocks` — among `workers`, if given
// — on one workspace built for the largest (with a split band's copies): the same bytes of every output.
template <const loops::Program& P, class... Workers>
bool blocks_equal_one_run(std::int64_t total, const std::vector<std::int64_t>& blocks, Workers&... workers) {
    const Fed f = fed(P, total);
    loops::Sizes whole_sizes{};
    loops::Sizes largest{};
    whole_sizes.fill(total);
    largest.fill(*std::max_element(blocks.begin(), blocks.end()));
    canaried::Workspace<P> one(whole_sizes);
    canaried::Workspace<P, 16> work(largest);
    std::vector<std::vector<std::byte>> want(P.outputs.size());
    std::vector<std::vector<std::byte>> got(P.outputs.size());
    canaried::run<P>(f.feeds, one);
    collect<P>(one, want, nullptr);
    bool same = true;
    auto next = blocks.begin();
    for (std::int64_t at = 0; at < total; next = next + 1 == blocks.end() ? blocks.begin() : next + 1) {
        const std::int64_t here = std::min(*next, total - at);
        work.set_length(here);
        loops::Feeds feeds = f.feeds;
        for (const loops::Buffer& b : P.buffers) {
            if (b.home == loops::Home::Feed && streamed(b)) {
                feeds[static_cast<std::size_t>(b.feed)] = static_cast<const std::byte*>(f.feeds[static_cast<std::size_t>(b.feed)]) +
                                                          at * loops::elements_per_sample(b, work.sizes()) * loops::bytes_of(b.type);
            }
        }
        same = run_block<P>(feeds, work, workers...) && same;
        same = collect<P>(work, got, &want) && same;
        at += here;
    }
    for (std::size_t o = 0; o < P.outputs.size(); ++o) {
        same = same && (!streamed(P.buffers[static_cast<std::size_t>(P.outputs[o])]) || got[o] == want[o]);
    }
    return same;
}

int g_wrong = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        ++g_wrong;
        std::printf("  FAILED: %s\n", what);
    }
}

// 1, 2, an odd size, one past the whole run, mixed sizes (capacity mode); and each alone, the deepest read into a
// feed's past − 1 (the head), the deepest prefix − 1, and a band's step ± 1.
const std::vector<std::vector<std::int64_t>> kBlocks{{1}, {2}, {7}, {201}, {5, 1, 16, 3}};
std::vector<std::vector<std::int64_t>> blocks_of(const loops::Program& p) {
    std::vector<std::vector<std::int64_t>> blocks = kBlocks;
    std::int64_t head = 1;
    std::int64_t prefix = 1;
    for (const std::int32_t d : loops::feed_depths(p)) {
        head = std::max<std::int64_t>(head, d - 1);
    }
    for (const loops::Buffer& b : p.buffers) {
        prefix = std::max<std::int64_t>(prefix, b.prefix - 1);
    }
    for (const std::int64_t size : {head, prefix}) {
        if (size > 2) { // (1 and 2 are there)
            blocks.push_back({size});
        }
    }
    for (const loops::BandInfo& band : p.bands) {
        blocks.push_back({std::max(1, band.step - 1), band.step + 1});
    }
    return blocks;
}

template <const loops::Program& P, class... Workers>
bool every_block_size(Workers&... workers) {
    bool same = true;
    for (const std::vector<std::int64_t>& blocks : blocks_of(P)) {
        same = blocks_equal_one_run<P>(200, blocks, workers...) && same;
    }
    return same;
}

template <Graph (*Build)(), const Machine& M = default_machine>
void both_precisions(const char* what) {
    static constexpr Compiled normal = compile(at_precision<Build, numerics::Precision::Normal>(), M);
    static constexpr Compiled high = compile(at_precision<Build, numerics::Precision::High>(), M);
    static_assert(loops::streams(normal.program) && loops::streams(high.program), "every graph run in blocks here streams");
    expect(every_block_size<normal.program>(), what);
    expect(every_block_size<high.program>(), what);
}

// ---- split runs: in order at 2, 3 and 7 workers (the last range first) and on a pool of 4 --------------------

constexpr Machine kUnbanded = [] {
    Machine m = zen2;
    m.band_bytes = 0; // no band the planner makes: the oscillator's recurrence unbanded, the split band by hand
    return m;
}();

template <Graph (*Build)(), numerics::Precision L>
constexpr Graph kAnalysed = [] {
    Graph g = at_precision<Build, L>();
    if (!infer(g).ok) {
        fail("streaming_test: a graph did not resolve");
    }
    return g;
}();
// Stage 0 split along domain axis `Axis`; or (`Axis` -1) stages 0 and 1 in a band along time, split.
template <Graph (*Build)(), numerics::Precision L, std::int32_t Axis>
constexpr planned::Held kSplitHeld{[] {
    loops::Schedule d = planned::schedule_of(kAnalysed<Build, L>, Axis >= 0 ? zen2 : kUnbanded);
    if constexpr (Axis >= 0) {
        d = split_runs::split(d, 0, Axis);
    } else {
        d.bands.push_back(loops::Schedule::Band{.first = 0, .count = 2, .axis = 2, .step = 4, .parallel = true});
        for (loops::Schedule::Stage& stage : d.stages) {
            stage.parallel = -1;
        }
        d.parallel_work = 1;
    }
    return planned::program_of(kAnalysed<Build, L>, d);
}()};
template <Graph (*Build)(), numerics::Precision L, std::int32_t Axis>
constexpr const loops::Program& kSplit = kSplitHeld<Build, L, Axis>.program;

// Each run over blocks split, every block size, equal to one serial run; some block cut into two or more ranges.
template <const loops::Program& P>
bool split_every_block_size() {
    bool same = true;
    unsigned widest = 1;
    for (const unsigned n : {2u, 3u, 7u}) {
        split_runs::InOrder workers{n};
        same = every_block_size<P>(workers) && same;
        widest = std::max(widest, workers.most);
    }
#if defined(HEINZEL_HAVE_THREADS)
    same = every_block_size<P>(split_runs::pool_of(4)) && same;
#endif
    return same && widest >= 2;
}

template <Graph (*Build)(), std::int32_t Axis>
void split(const char* what) {
    static_assert(loops::streams(kSplit<Build, numerics::Precision::Normal, Axis>) && loops::streams(kSplit<Build, numerics::Precision::High, Axis>));
    expect(split_every_block_size<kSplit<Build, numerics::Precision::Normal, Axis>>(), what);
    expect(split_every_block_size<kSplit<Build, numerics::Precision::High, Axis>>(), what);
}

// ---- reset, seed, a literal stream, and what cannot stream --------------------------------------------------

// Runs over three blocks, `reset`, one block: the whole workspace byte for byte as a fresh one after that block —
// every carried region back to silence, and nothing else touched (the lagged ramp's ramp among them).
template <const loops::Program& P>
bool reset_is_a_fresh_start(std::int64_t block) {
    const Fed f = fed(P, 3 * block);
    loops::Sizes sizes{};
    sizes.fill(block);
    canaried::Workspace<P> used(sizes);
    canaried::Workspace<P> fresh(sizes);
    for (int k = 0; k < 3; ++k) {
        canaried::run<P>(f.feeds, used);
    }
    used.reset();
    canaried::run<P>(f.feeds, used);
    canaried::run<P>(f.feeds, fresh);
    return used.size_in_bytes() == fresh.size_in_bytes() && std::memcmp(used.arena(), fresh.arena(), used.size_in_bytes()) == 0;
}

// y[n] = x[n] + y[n - 3], its past seeded: the first three samples read the history, the rest silence where a
// shorter second seed left none. (A coefficient of one: nothing rounds but the sums.) At `High`, so that the state is
// the output's own: at `Normal` the cost model runs it in double (three back fills double's two lanes, not float's
// four), and the output is its conversion, which keeps no past.
constexpr Graph seeded() {
    Input x{"x", Signal{kT}};
    Output y{"y"};
    y[n] = x[n] + y[n - 3];
    return y;
}
constexpr Compiled kSeeded = compile(at_precision<seeded, numerics::Precision::High>());

bool seed_reaches_the_first_samples() {
    const std::vector<float> x{1.0f, 2.0f, 3.0f, 4.0f};
    loops::Sizes sizes{};
    sizes.fill(4);
    canaried::Workspace<kSeeded.program> work(sizes);
    const auto y = static_cast<std::size_t>(kSeeded.program.outputs[0]);
    const std::vector<float> history{10.0f, 20.0f, 30.0f}; // y[-1], y[-2], y[-3]
    work.seed(y, std::span<const float>(history));
    loops::Feeds feeds{};
    feeds[0] = x.data();
    canaried::run<kSeeded.program>(feeds, work);
    const float* out = work.data<float>(y);
    const bool first = out[0] == 31.0f && out[1] == 22.0f && out[2] == 13.0f && out[3] == 35.0f;
    const std::vector<float> shorter{7.0f}; // y[-1] only: y[-2] and y[-3] are silence again
    work.seed(y, std::span<const float>(shorter));
    canaried::run<kSeeded.program>(feeds, work);
    return first && out[0] == 1.0f && out[1] == 2.0f && out[2] == 10.0f && out[3] == 5.0f;
}

// A literal stream: one graph function at 16 samples and at 64, the first run four times on one workspace.
constexpr Graph literal_echo(std::int32_t samples) {
    Input x{"x", Signal{2, samples}};
    Output y{"y"};
    y[n] = x[n] + x[n - 1] + 0.5 * y[n - 3];
    return y;
}
constexpr Compiled kLiteral16 = compile(literal_echo(16));
constexpr Compiled kLiteral64 = compile(literal_echo(64));

bool literal_blocks_equal_one_run() {
    const Fed f = fed(kLiteral64.program, 64);
    canaried::Workspace<kLiteral64.program> one(loops::Sizes{});
    canaried::Workspace<kLiteral16.program> work(loops::Sizes{});
    canaried::run<kLiteral64.program>(f.feeds, one);
    const float* want = one.data<float>(static_cast<std::size_t>(kLiteral64.program.outputs[0]));
    bool same = true;
    for (std::int64_t k = 0; k < 4; ++k) {
        loops::Feeds feeds{};
        feeds[0] = static_cast<const std::byte*>(f.feeds[0]) + k * 16 * 2 * static_cast<std::int64_t>(sizeof(float));
        canaried::run<kLiteral16.program>(feeds, work);
        const float* got = work.data<float>(static_cast<std::size_t>(kLiteral16.program.outputs[0]));
        for (std::int64_t e = 0; e < 16 * 2; ++e) { // sample-major: element v of sample t at t * 2 + v
            same = same && got[e] == want[k * 16 * 2 + e];
        }
    }
    return same;
}

// A seeded and a literal stream stream (what cannot: `streams_test`).
static_assert(loops::streams(kSeeded.program) && loops::streams(kLiteral16.program));

// The workspace's own refusals (`Executor` checks before them, so only a direct caller meets them; a bad length
// would write out of bounds). A length's are debug-only, and `set_length` on a program with no stream does not
// compile (`build_refuses_workspace_*`).
template <class Build>
bool refused(Build build, std::string_view text) {
    try {
        build();
    } catch (const heinzel::error& e) {
        return e.message() == text;
    }
    return false;
}
template <const loops::Program& Seeded, const loops::Program& Literal>
bool workspace_refusals() {
    loops::Sizes sizes{};
    sizes.fill(8);
    const auto y = static_cast<std::size_t>(Seeded.outputs[0]);
    const auto two_voices = static_cast<std::size_t>(Literal.outputs[0]);
    std::size_t feed = 0;
    while (Seeded.buffers[feed].home != loops::Home::Feed) {
        ++feed;
    }
    constexpr std::string_view not_whole = "heinzel: seed: the history is not whole samples of the value's type";
    const std::vector<float> three{1, 2, 3};
    const std::vector<float> four{1, 2, 3, 4};
    const std::vector<double> one{1};
    bool all = true;
#ifndef NDEBUG
    constexpr std::string_view outside = "heinzel: set_length: the length is outside 1 and what the workspace was built for";
    all = refused([&] { canaried::Workspace<Seeded>(sizes).set_length(0); }, outside) &&
          refused([&] { canaried::Workspace<Seeded>(sizes).set_length(9); }, outside);
#endif
    return all && refused([&] { canaried::Workspace<Seeded>(sizes).seed(y, std::span<const double>(one)); }, not_whole) &&
           refused([&] { canaried::Workspace<Literal>(loops::Sizes{}).seed(two_voices, std::span<const float>(three)); }, not_whole) &&
           refused([&] { canaried::Workspace<Seeded>(sizes).seed(y, std::span<const float>(four)); },
                   "heinzel: seed: the history reaches deeper than the value keeps its past") &&
           refused([&] { canaried::Workspace<Seeded>(sizes).seed(feed, std::span<const float>(three)); }, "heinzel: seed: this value keeps no past to seed");
}

constexpr Graph fir3() { return fir(3); }
constexpr Graph fir13() { return fir(13); }
static_assert(kSplit<blurred_echo, numerics::Precision::Normal, -1>.bands[0].parallel &&
                  loops::kBandBuffers<kSplit<blurred_echo, numerics::Precision::Normal, -1>, 0>.size() == 1,
              "a split band, its one buffer between the blurs");
static_assert(compile(oscillator()).program.bands.size() == 1 && compile(oscillator(), kUnbanded).program.bands.empty(),
              "the oscillator in a stream band, and not");

constexpr Graph narrow_bank() { return bank(2); }
constexpr Graph wide_bank() { return bank(16); }
constexpr Graph echo3() { return echo(3); }
constexpr Graph echo40() { return echo(40); }

} // namespace

int main() {
    both_precisions<narrow_bank>("a narrow bank");
    both_precisions<wide_bank>("a wide bank");
    both_precisions<two_poles>("two poles");
    both_precisions<coupled>("a coupled pair");
    both_precisions<echo3>("an echo");
    both_precisions<echo40>("an echo deeper than the block");
    both_precisions<fir3>("a FIR over an input");
    both_precisions<biquad>("a biquad over an input");
    both_precisions<delay_line>("a delay line");
    both_precisions<param_pole>("a Param in a recurrence");
    both_precisions<lagged_ramp>("a lagged ramp");
    both_precisions<oscillator>("an oscillator in a stream band");
    both_precisions<oscillator, kUnbanded>("an oscillator, unbanded");
    both_precisions<knob_oscillator>("a knob-driven oscillator, its length declared");
    both_precisions<knob_beside_an_input>("a knob-driven oscillator beside an input, its length taken");
    both_precisions<deep_band>("a stream band keeping three rows");
    both_precisions<wide_f64>("F64: a feed's tail and a prefix");
    both_precisions<timeless_first>("a timeless output before a recurrence");
    split<fir3, 1>("a FIR over an input, split along time");
    split<fir13, 0>("a FIR over an input, split along voices");
    split<blurred_echo, -1>("a split band before a recurrence");
    static constexpr Compiled ramp = compile(lagged_ramp());
    static constexpr Compiled osc = compile(oscillator());
    static constexpr Compiled feed_past = compile(fir3());
    expect(reset_is_a_fresh_start<ramp.program>(8) && reset_is_a_fresh_start<osc.program>(40) && reset_is_a_fresh_start<feed_past.program>(3),
           "reset: back to silence, exactly the carried regions");
    expect(seed_reaches_the_first_samples(), "seed: the past before the first sample, the whole prefix");
    expect(literal_blocks_equal_one_run(), "a literal stream in blocks");
    expect(workspace_refusals<kSeeded.program, kLiteral16.program>(), "the workspace's refusals");
    if (g_wrong != 0) {
        std::printf("streaming_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("streaming_test: every signal run in blocks gives the numbers of one run\n");
    return 0;
}
