// Adversarial review of stages split among workers (P6 M2, `Plan/probes/review_p6_m2/`): the reviewer's cases
// kept as a test. Part A held from the start — every split run bit for bit the serial run
// (`tests/support/split_runs.hpp`); part B the finding, fixed.
//
//   A  split_of on its edges; a FIR over a feed split along voices (each worker runs its own first samples
//      from silence) and along time, with feedback, time outer and inner; views read by a split stage (a
//      transpose, a strided slice, a flip along the split axis, a producer inside its window through a flip, a
//      planar pack with a flipped part), a ramp, a window over two axes, an F64 intermediate, a contraction of a
//      transposed view; extents bound at run time down to 0 and below one unit; a fold over voices, a
//      per-sample contraction, two results of one stage, a delay line and a table read; one workspace split
//      among changing numbers of workers
//   B  a fold that keeps its axes, its own loop moved outside the inner loops a schedule names (`inner_from`) by
//      hand — once lowered, and stored past its buffer's end serially, raced when split: refused by name

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/notation/einsum.hpp"
#include "heinzel/sema/analyze.hpp"

#include "canaried.hpp"
#include "planned.hpp"
#include "split_runs.hpp"

#include <cstdint>
#include <cstdio>
#include <limits>
#include <string_view>

using namespace heinzel;
using split_runs::split;

namespace {

namespace loops = heinzel::loops;

constexpr Machine kOneLane{.vector_bytes = 4};
constexpr Machine kTiny{.vector_bytes = 32, .registers = 16, .l1_bytes = 1024, .l2_bytes = 2048, .page_bytes = 256, .relay_pays_after = 1, .fma = true};
constexpr std::int32_t kA = 4;
constexpr std::int32_t kB = 22;
constexpr std::int32_t kBlock = 19; // (kB 22: three lanes of eight along it, the last ragged)

constexpr Graph analysed(Graph g) {
    if (!infer(g).ok) {
        fail("nest_parallel_review_test: a graph did not resolve");
    }
    return g;
}

constexpr Expr two_operands(Expr a, Expr b, OpCode code, OpAttr attr) {
    Arena* arena = bring_together(a, b);
    BuildNode n{};
    n.op = code;
    n.inputs[0] = a.position();
    n.inputs[1] = b.position();
    n.attr = attr;
    arena->nodes.push_back(n);
    return Expr{arena, static_cast<std::int32_t>(arena->nodes.size() - 1)};
}

constexpr Expr view(Expr x, OpCode code, ViewKind kind, std::int32_t axis) {
    OpAttr attr{};
    attr.kind = AttrKind::View;
    attr.view.kind = kind;
    attr.view.axis = axis;
    return detail::append_unary(x, code, attr);
}
constexpr Expr flip(Expr x, std::int32_t axis) { return view(x, op::Flip, ViewKind::Flip, axis); }

constexpr Expr joined(Expr a, Expr b, OpCode code, ViewKind kind, std::int32_t axis) {
    Arena* arena = bring_together(a, b);
    BuildNode n{};
    n.op = code;
    n.inputs[0] = a.position();
    n.inputs[1] = b.position();
    n.attr.kind = AttrKind::View;
    n.attr.view.kind = kind;
    n.attr.view.axis = axis;
    arena->nodes.push_back(n);
    return Expr{arena, static_cast<std::int32_t>(arena->nodes.size() - 1)};
}

constexpr Expr cast(Expr x, ElemType to) {
    OpAttr attr{};
    attr.kind = AttrKind::Cast;
    attr.cast.to = to;
    return detail::append_unary(x, op::Cast, attr);
}


loops::Sizes length(std::int64_t n) {
    loops::Sizes sizes{};
    sizes[0] = n;
    return sizes;
}

int g_wrong = 0;

// ---- split_of on its edges ----------------------------------------------------------------------------------

void check_split_of() {
    const auto wrong = [](const char* what, std::int64_t n, std::int64_t unit) {
        if (++g_wrong <= 20) {
            std::printf("  split_of: %s (n %lld, unit %lld)\n", what, static_cast<long long>(n), static_cast<long long>(unit));
        }
    };
    for (std::int64_t n = 0; n <= 70; ++n) {
        for (const std::int64_t unit : {1, 2, 3, 8, 16, 24, 32, 48}) {
            for (const std::int64_t per_worker : {0, 1, 5, 64, 1000}) {
                for (const unsigned available : {0u, 1u, 2u, 3u, 7u, 16u, 100u}) {
                    const std::int64_t work = n * 13;
                    const loops::Split s = loops::split_of(n, unit, work, per_worker, available);
                    if (s.workers <= 1) {
                        continue;
                    }
                    if (s.per <= 0 || s.per % unit != 0) {
                        wrong("a range is not whole units", n, unit);
                    }
                    if (s.workers > available) {
                        wrong("more ranges than workers", n, unit);
                    }
                    if (static_cast<std::int64_t>(s.workers - 1) * s.per >= n || static_cast<std::int64_t>(s.workers) * s.per < n) {
                        wrong("the ranges do not tile [0, n)", n, unit);
                    }
                    if (per_worker > 0 && work / static_cast<std::int64_t>(s.workers) < per_worker) {
                        wrong("a worker is owed less than per_worker", n, unit);
                    }
                }
            }
        }
    }
    constexpr std::int64_t kBig = std::numeric_limits<std::int64_t>::max();
    const loops::Split huge_work = loops::split_of(1000, 8, kBig, 1, 16);
    const loops::Split huge_owed = loops::split_of(1000, 8, 1000, kBig, 16);
    if (huge_work.workers != 16 || huge_work.per != 64 || huge_owed.workers != 1) {
        wrong("huge work or a huge per_worker", 1000, 8);
    }
}

// ---- the past of a feed -------------------------------------------------------------------------------------

// y = x + x[t-1]/2 + x[t-3]/4 over `voices` voices of a run-time length.
constexpr Graph bank_fir(std::int32_t voices) {
    constexpr Index n{};
    Input x{"x", Signal{voices, Length{.multiple_of = 1, .min = 1, .expect = 4096}}};
    return analysed(finish(Output{"y"} = x + 0.5 * x[n - 1] + 0.25 * x[n - 3]));
}

// y = x[t-2] + 0.9375 y[t-1]: a feed's past and feedback in one stage. (Sixty-four voices, so that the voices loop runs
// more than once a sample: of sixteen, the cost model's stage takes all in one body of lanes, and nothing splits.)
constexpr Graph fed_back(std::int32_t voices) {
    constexpr Index n{};
    Input x{"x", Signal{voices, Length{.multiple_of = 1, .min = 1, .expect = 4096}}};
    Output y{"y"};
    y[n] = x[n - 2] + 0.9375 * y[n - 1];
    return analysed(y);
}

// Each graph planned once per machine; every split below edits that schedule.
template <const Graph& G, const Machine& M>
constexpr loops::Schedule kSchedule = planned::schedule_of(G, M);

constexpr Graph kFir13 = bank_fir(13);
constexpr Graph kFir64 = bank_fir(64);
constexpr Graph kFedBack = fed_back(64);
constexpr Graph kFedBackFew = fed_back(4);
constexpr planned::Held kFirVoicesHeld{planned::program_of(kFir13, split(kSchedule<kFir13, zen2>, 0, 0))};
constexpr const loops::Program& kFirVoices = kFirVoicesHeld.program;
constexpr planned::Held kFirTimeHeld{planned::program_of(kFir13, split(kSchedule<kFir13, zen2>, 0, 1))};
constexpr const loops::Program& kFirTime = kFirTimeHeld.program;
constexpr planned::Held kFir64LinesHeld{planned::program_of(kFir64, split(kSchedule<kFir64, zen2>, 0, 0, 64))};
constexpr const loops::Program& kFir64Lines = kFir64LinesHeld.program;
constexpr planned::Held kFedBackVoicesHeld{planned::program_of(kFedBack, split(kSchedule<kFedBack, zen2>, 0, 0))};
constexpr const loops::Program& kFedBackVoices = kFedBackVoicesHeld.program;
constexpr planned::Held kFedBackFewVoicesHeld{planned::program_of(kFedBackFew, split(kSchedule<kFedBackFew, kOneLane>, 0, 0))};
constexpr const loops::Program& kFedBackFewVoices = kFedBackFewVoicesHeld.program;
static_assert(kFirVoices.stages[0].head == 3 && kFedBackVoices.stages[0].head == 2);
static_assert(kFirTime.stages[0].time_loop == kFirTime.stages[0].split, "split along the time the feed's past is read on");
static_assert(kFirVoices.stages[0].time_loop != kFirVoices.stages[0].split, "split along the voices");

void check_feed_past() {
    for (const std::int64_t n : {1, 2, 3, 4, 5, 9, 17, 100}) {
        g_wrong += split_runs::check<kFirVoices>("fir over 13 voices, along voices", length(n), true);
        g_wrong += split_runs::check<kFirTime>("fir over 13 voices, along time", length(n), n > 4);
        g_wrong += split_runs::check<kFir64Lines>("fir over 64 voices, along voices in lines", length(n), true);
        g_wrong += split_runs::check<kFedBackVoices>("feed past and feedback, 64 voices", length(n), true);
        g_wrong += split_runs::check<kFedBackFewVoices>("feed past and feedback, 4 voices time innermost", length(n), true);
    }
}

// ---- views --------------------------------------------------------------------------------------------------

enum class Case { Turned, Part, Backwards, OfAMap, Product, Converted, Ramp, Packed, ThroughAFlip, TwoWindows };

constexpr Graph graph_of(Case which) {
    Input x{"x", Signal{kA, kB, kBlock}};
    Input w{"w", Signal{kA, 3, kBlock}};
    Input h{"h", Signal{3, kBlock}};
    Input k{"k", Signal{2, 1, 3, kBlock}};
    switch (which) {
    case Case::Turned: return analysed(finish(Output{"y"} = x.transpose({1, 0}) * 2.0));
    case Case::Part: return analysed(finish(Output{"y"} = x.slice(0, 1, 2, 2) + 1.0));
    case Case::Backwards: return analysed(finish(Output{"y"} = flip(x, 1) * 2.0));
    case Case::OfAMap: return analysed(finish(Output{"y"} = flip(x * 2.0, 0) + 1.0));
    case Case::Product: return analysed(finish(Output{"y"} = einsum("ba,ak->bk", x.transpose({1, 0}), w)));
    case Case::Converted: return analysed(finish(Output{"y"} = cast(cast(x, ElemType::F64) * 0.5, ElemType::F32).realize()));
    case Case::Ramp: return analysed(finish(Output{"y"} = x * iota({kB})));
    case Case::Packed: return analysed(finish(Output{"y"} = joined(x, flip(x, 1), op::Concat, ViewKind::Concat, 0)));
    case Case::ThroughAFlip: return analysed(finish(Output{"y"} = (flip(x * 2.0, 1).unfold(1, 3) * h).sum(2)));
    case Case::TwoWindows: return analysed(finish(Output{"y"} = (x.unfold2d(0, 2, 1, 3) * k).sum({1, 3})));
    }
    fail("nest_parallel_review_test: no such case");
}

// The schedule's last stage (the view's reader) split along `axis`, every other stage as planned.
template <Case Which, std::int32_t Axis>
struct ViewSplit {
    static constexpr Graph g = graph_of(Which);
    static constexpr loops::Schedule schedule = planned::schedule_of(g, zen2);
    static constexpr planned::Held pHeld{planned::program_of(g, split(schedule, schedule.stages.size() - 1, Axis))};
    static constexpr const loops::Program& p = pHeld.program;
};

template <Case Which, std::int32_t Axis>
void check_view(const char* what) {
    g_wrong += split_runs::check<ViewSplit<Which, Axis>::p>(what, loops::Sizes{}, true);
}

void check_views() {
    check_view<Case::Turned, 0>("transpose, axis 0");
    check_view<Case::Turned, 1>("transpose, axis 1");
    check_view<Case::Part, 0>("strided slice, axis 0");
    check_view<Case::Backwards, 1>("flip, along the flipped axis");
    check_view<Case::Backwards, 2>("flip, along time");
    check_view<Case::OfAMap, 0>("flip of an earlier stage's buffer, along the flipped axis");
    check_view<Case::Product, 0>("contraction of a transposed view, rows");
    check_view<Case::Product, 2>("contraction of a transposed view, time");
    check_view<Case::Converted, 0>("F64 intermediate, axis 0");
    check_view<Case::Converted, 2>("F64 intermediate, time");
    check_view<Case::Ramp, 1>("a ramp, axis 1");
    check_view<Case::Packed, 1>("pack with a flipped part, along the flipped axis");
    check_view<Case::ThroughAFlip, 1>("producer inside its window through a flip, along the window's frames");
    check_view<Case::TwoWindows, 0>("a window over two axes, axis 0");
}

// ---- extents bound at run time, and more kinds of stage ----------------------------------------------------

constexpr Graph stream_map(std::int32_t voices) { // n bound at run time with no floor: 0 allowed
    Input x{"x", Signal{voices, Length{}}};
    return analysed(finish(Output{"y"} = x * x + 0.5));
}

constexpr Graph voice_sum() {
    Input x{"x", Signal{21, Length{}}};
    return analysed(finish(Output{"y"} = (x * 2.0).sum(0) + 1.0));
}

constexpr Graph per_sample_product() {
    Input a{"a", Signal{13, 9, Length{}}};
    Input b{"b", Signal{9, 11, Length{}}};
    return analysed(finish(Output{"y"} = einsum("ik,kj->ij", a, b)));
}

constexpr Graph two_outputs() {
    Input x{"x", {29, 45}};
    const Expr sq = x * x;
    return analysed(finish(Output{"sq"} = sq, Output{"s"} = sq.sum(1)));
}

constexpr Graph delay_bank() {
    Input x{"x", Signal{37, 50}};
    Input p{"p", Signal{37, 50}};
    OpAttr attr{};
    attr.kind = AttrKind::SignalledDelay;
    attr.signalled_delay.max_delay = 5;
    return analysed(finish(Output{"y"} = two_operands(x * 0.5, p, op::DelayLine, attr)));
}

constexpr Graph table_read() {
    Input phase{"p", Signal{300}};
    Input table{"x", Signal{8, 300}};
    OpAttr attr{};
    attr.kind = AttrKind::Table;
    attr.table.size = 8;
    return analysed(finish(Output{"y"} = two_operands(phase, table, op::TableRead, attr) * 2.0));
}

template <const Graph& G, const Machine& M, std::size_t Stage, std::int32_t Axis>
constexpr planned::Held kSplitHeld{planned::program_of(G, split(kSchedule<G, M>, Stage, Axis))};
template <const Graph& G, const Machine& M, std::size_t Stage, std::int32_t Axis>
constexpr const loops::Program& kSplit = kSplitHeld<G, M, Stage, Axis>.program;

constexpr Graph kMap3 = stream_map(3);
constexpr Graph kMap40 = stream_map(40);
constexpr Graph kVoiceSum = voice_sum();
constexpr Graph kProduct = per_sample_product();
constexpr Graph kTwoOut = two_outputs();
constexpr Graph kDelayBank = delay_bank();
constexpr Graph kTable = table_read();

void check_run_time_extents() {
    for (const std::int64_t n : {0, 1, 2, 3, 7, 8, 9, 31, 100}) {
        g_wrong += split_runs::check<kSplit<kMap3, zen2, 0, 1>>("3 voices, time", length(n), n > 1);
        g_wrong += split_runs::check<kSplit<kMap40, zen2, 0, 0>>("40 voices, voices", length(n), n > 0);
        g_wrong += split_runs::check<kSplit<kMap40, zen2, 0, 1>>("40 voices, time", length(n), n > 1);
        g_wrong += split_runs::check<kSplit<kVoiceSum, zen2, 0, 1>>("sum over voices, time", length(n), false);
        g_wrong += split_runs::check<kSplit<kProduct, kTiny, 0, 2>>("per-sample product, time", length(n), false);
        g_wrong += split_runs::check<kSplit<kProduct, kTiny, 0, 0>>("per-sample product, rows", length(n), false);
        g_wrong += split_runs::check<kSplit<kProduct, zen2, 0, 0>>("per-sample product on zen2, rows", length(n), false);
    }
    g_wrong += split_runs::check<kSplit<kTwoOut, zen2, 0, 0>>("two outputs, rows", loops::Sizes{}, true);
    g_wrong += split_runs::check<kSplit<kDelayBank, zen2, 1, 0>>("delay line, voices", loops::Sizes{}, true);
    g_wrong += split_runs::check<kSplit<kTable, zen2, 0, 0>>("table read, time", loops::Sizes{}, true);
    // One workspace, split among changing numbers of workers — each run as often and as wide as its split mark says.
    static constexpr const loops::Program& P = kSplit<kMap40, zen2, 0, 1>;
    const loops::Sizes sizes = length(1000);
    const split_runs::Fed f = split_runs::fed(P, sizes);
    canaried::Workspace<P> serial(sizes);
    canaried::Workspace<P> reused(sizes);
    canaried::run<P>(f.feeds, serial);
    for (const unsigned n : {2u, 16u, 3u, 1u, 7u}) {
        split_runs::InOrder workers{n};
        canaried::run<P>(f.feeds, reused, workers);
        if (!split_runs::same_buffers(serial, reused) && ++g_wrong <= 20) {
            std::printf("  a workspace reused at %u workers differs\n", n);
        }
        const split_runs::Launches want = split_runs::predicted<P>(sizes, n, 1);
        if (split_runs::Launches{workers.launches, workers.ranges} != want || (n > 1 && want.ranges != n) || (n == 1 && want.launches != 0)) {
            ++g_wrong;
            std::printf("  a workspace reused at %u workers: %u launches into %u ranges, %u into %u wanted\n", n, workers.launches, workers.ranges,
                        want.launches, want.ranges);
        }
    }
}

// ---- part B: a fold's own loop outside the inner loops a schedule names ------------------------------------
// (The lowering holds the inner loops a schedule names to the ones its loops imply.)

constexpr Graph kept_fold() {
    Input x{"x", {50, 19}};
    return analysed(finish(Output{"y"} = (x * x).sum(1, keepdims)));
}
constexpr Graph kKeptFold = kept_fold();

constexpr loops::Refusal fold_loops_refused(bool also_split) {
    loops::Schedule d = kSchedule<kKeptFold, zen2>;
    loops::Schedule::Stage& stage = d.stages[0];
    for (std::size_t l = 0; l < stage.loops.size(); ++l) {
        stage.loops[l] = loops::Schedule::Loop{.axis = stage.loops[l].axis, .step = 1, .kind = loops::LoopKind::Serial};
        stage.parallel = stage.loops[l].axis == 1 ? static_cast<std::int32_t>(l) : stage.parallel;
    }
    stage.inner_from = static_cast<std::int32_t>(stage.loops.size()); // the fold's loop outside its own
    stage.parallel = also_split ? stage.parallel : -1;
    return planned::refusal_of(kKeptFold, d);
}
static_assert(fold_loops_refused(false) == "heinzel::compile: the inner loops a schedule names are not the ones its loops imply" &&
                  fold_loops_refused(true) == "heinzel::compile: the inner loops a schedule names are not the ones its loops imply",
              "a fold's loop outside the inner loops, serial and split: refused by name");

} // namespace

int main() {
    check_split_of();
    check_feed_past();
    check_views();
    check_run_time_extents();
    if (g_wrong != 0) {
        std::printf("%d results are wrong\n", g_wrong);
        return 1;
    }
    std::printf("nest_parallel_review: every split run bit for bit the serial run, a fold's loop outside its own refused\n");
    return 0;
}
