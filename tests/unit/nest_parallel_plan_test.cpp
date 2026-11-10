// Which stages the planner splits among workers (`loops/planner/parallel.hpp`), and along which loop: the
// outermost loop over an axis the rule allows — every value the stage makes moves along it, and no node reads
// the past of what the stage makes along it — skipping an axis of the literal extent one; never a stage in a
// band; nothing on a machine with one core. Then a few planned programs run split, bit for bit the serial run.
//
//   maps         an image along its rows; a mono chain along time; a producer inside its window along rows
//   folds        a row fold along its rows; a fold that keeps its axis over one row: nothing to split
//   contract     a tiled contraction along its row Tile; a small one along its unrolled rows
//   windows      a conv along its outputs
//   feedback     four voices with time innermost along the voices; a FIR over a feed along time; a bank of 64
//                voices not at all (time outermost is feedback, and a split inside it interleaves the writes)
//   none         the stages of a band (blurxy at 8 MB); a pad (a stage made in parts); every stage on a
//                machine with one core

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/notation/einsum.hpp"
#include "heinzel/sema/analyze.hpp"

#include "planned.hpp"
#include "split_runs.hpp"

#include <bit>
#include <cstdio>

using namespace heinzel;

namespace {

namespace loops = heinzel::loops;
using loops::LoopKind;

constexpr Machine kOneCore = [] {
    Machine m = zen2;
    m.cores = 1;
    return m;
}();

constexpr Graph analysed(Graph g) {
    if (!infer(g).ok) {
        fail("nest_parallel_plan_test: a graph did not resolve");
    }
    return g;
}

constexpr Graph image() {
    Input x{"x", {37, 101}};
    return analysed(finish(Output{"y"} = x * x + 0.5));
}

constexpr Graph mono() {
    Input x{"x", Signal{Length{.multiple_of = 1, .min = 1, .expect = 1 << 20}}};
    return analysed(finish(Output{"y"} = x * 2.0 + 1.0));
}

constexpr Graph blur() {
    Input x{"x", {41, 77}};
    Input w{"w", {1, 3, 1}};
    return analysed(finish(Output{"y"} = ((x * x).unfold(0, 3) * w).sum(1)));
}

constexpr Graph row_fold() {
    Input x{"x", {50, 67}};
    return analysed(finish(Output{"y"} = (x * x).sum(1)));
}

// One row, normalised over its columns: the fold keeps its axis, and that axis is the only one longer than one.
constexpr Graph one_row_norm() {
    Input x{"x", {1, 300}};
    return analysed(finish(Output{"y"} = x / (x * x).sum(1, keepdims)));
}

template <std::int32_t M, std::int32_t K, std::int32_t N>
constexpr Graph contraction() {
    Input a{"a", {M, K}};
    Input b{"b", {K, N}};
    return analysed(finish(Output{"y"} = max(einsum("ik,kj->ij", a, b), 0.0)));
}

constexpr Graph conv() {
    Input x{"x", {1000}};
    Input h{"h", {1, 7}};
    return analysed(finish(Output{"y"} = (x.unfold(0, 7) * h).sum(1)));
}

constexpr Graph bank(std::int32_t voices) {
    constexpr Index n{};
    Input x{"x", Signal{voices, Length{.multiple_of = 1, .min = 1, .expect = 4096}}};
    Output y{"y"};
    y[n] = x[n] + 0.9375 * y[n - 1];
    return analysed(y);
}

constexpr Graph fir() {
    constexpr Index n{};
    Input x{"x", Signal{Length{.multiple_of = 1, .min = 1, .expect = 4096}}};
    return analysed(finish(Output{"y"} = x + 0.5 * x[n - 1] + 0.25 * x[n - 3]));
}

constexpr Graph blurxy() {
    Input x{"x", {1024, 2048}};
    Input h{"wh", {1, 1, 3}};
    Input v{"wv", {1, 3, 1}};
    Expr bx = (x.unfold(1, 3) * h).sum(2);
    return analysed(finish(Output{"y"} = (bx.unfold(0, 3) * v).sum(1)));
}

constexpr Graph padded() {
    Input x{"x", Signal{13, 7, 24}};
    OpAttr attr{};
    attr.kind = AttrKind::View;
    attr.view.kind = ViewKind::Pad;
    attr.view.axis = 1;
    attr.view.pad_before = 2;
    attr.view.pad_after = 3;
    attr.view.pad_bits = std::bit_cast<std::uint64_t>(0.5);
    return analysed(finish(Output{"y"} = detail::append_unary(x, op::Pad, attr) * 2.0));
}

// A row RMS norm: a fold that keeps its axis, read back broadcast along it.
constexpr Graph rms_norm(numerics::Precision level) {
    Input x{"x", {50, 67}};
    Graph g = finish(Output{"y"} = x * rsqrt((x * x).mean(1, keepdims) + 1e-6));
    set_default_precision(g, level);
    return analysed(g);
}

constexpr Graph attention() {
    Input q{"Q", {37, 16}};
    Input k{"K", {37, 16}};
    Input v{"V", {37, 16}};
    Expr scores = einsum("qd,kd->qk", q, k) * 0.25;
    Expr e = exp(scores - scores.max(1, keepdims));
    return analysed(finish(Output{"out"} = einsum("qk,kd->qd", e / e.sum(1, keepdims), v)));
}

// A transposed read and two results.
constexpr Graph views_and_two_results() {
    Input x{"x", {29, 43}};
    Expr t = x.transpose({1, 0}) * 2.0;
    Output shifted{"t"};
    Output s{"s"};
    shifted = t + 1.0;
    s = t.max(0);
    return analysed(finish(shifted, s));
}

constexpr Graph kImage = image();
constexpr Graph kMono = mono();
constexpr Graph kBlur = blur();
constexpr Graph kRowFold = row_fold();
constexpr Graph kOneRow = one_row_norm();
constexpr Graph kBigGemm = contraction<512, 512, 512>();
constexpr Graph kSmallGemm = contraction<23, 17, 41>();
constexpr Graph kConv = conv();
constexpr Graph kBank = bank(64);
constexpr Graph kFewVoices = bank(4);
constexpr Graph kFir = fir();
constexpr Graph kBlurxy = blurxy();
constexpr Graph kPadded = padded();
constexpr Graph kRms = rms_norm(numerics::Precision::Normal);
constexpr Graph kRmsHigh = rms_norm(numerics::Precision::High);
constexpr Graph kAttention = attention();
constexpr Graph kViews = views_and_two_results();
// The planner's schedule: the stages, their loops, and which of them each stage is split along.
template <const Graph& G>
constexpr loops::Schedule kPlan = planned::schedule_of(G, zen2);

// The loop a stage is split along, and its kind and axis (-1: not split).
constexpr LoopKind kind_split(const loops::Schedule::Stage& s) { return s.loops[static_cast<std::size_t>(s.parallel)].kind; }
constexpr std::int32_t axis_split(const loops::Schedule::Stage& s) { return s.parallel < 0 ? -1 : s.loops[static_cast<std::size_t>(s.parallel)].axis; }

static_assert(kPlan<kImage>.parallel_work == zen2.parallel_work && kPlan<kImage>.line_bytes == zen2.line_bytes);
static_assert(axis_split(kPlan<kImage>.stages[0]) == 0 && kind_split(kPlan<kImage>.stages[0]) == LoopKind::Serial,
              "an image along its rows (the stream axis before them has the extent one)");
static_assert(axis_split(kPlan<kMono>.stages[0]) == 0 && kind_split(kPlan<kMono>.stages[0]) == LoopKind::Vector, "a mono chain along time");
static_assert(kPlan<kBlur>.stages.size() == 1 && kPlan<kBlur>.stages[0].through >= 0 && axis_split(kPlan<kBlur>.stages[0]) == 0,
              "a producer computed inside its window does not keep its stage from splitting");
static_assert(axis_split(kPlan<kRowFold>.stages[0]) == 0, "a row fold along its rows");
static_assert(kPlan<kOneRow>.stages[0].parallel == -1, "one row: the only axis longer than one is the fold's own");
static_assert(axis_split(kPlan<kBigGemm>.stages[0]) == 0 && kind_split(kPlan<kBigGemm>.stages[0]) == LoopKind::Tile,
              "a tiled contraction along its row Tile");
static_assert(axis_split(kPlan<kSmallGemm>.stages[0]) == 0 && kind_split(kPlan<kSmallGemm>.stages[0]) == LoopKind::Unroll,
              "a small contraction along its unrolled rows");
static_assert(axis_split(kPlan<kConv>.stages[0]) == 0, "a conv along its outputs");
static_assert(kPlan<kBank>.stages[0].loops[0].axis == 1 && kPlan<kBank>.stages[0].parallel == -1,
              "64 voices: time outermost, which is feedback — and split along the voices inside it, every worker would "
              "write a slice of every sample's row: not split");
static_assert(kPlan<kFewVoices>.stages[0].loops[0].axis == 0 && axis_split(kPlan<kFewVoices>.stages[0]) == 0,
              "four voices: time innermost with the state in registers — split along the voices");
static_assert(axis_split(kPlan<kFir>.stages[0]) == 0, "a FIR over a feed along time");
static_assert(kPlan<kBlurxy>.bands.size() == 1 && kPlan<kBlurxy>.stages[0].parallel == -1 && kPlan<kBlurxy>.stages[1].parallel == -1,
              "the stages of a band are not split on their own");
static_assert(kPlan<kPadded>.stages[0].parallel == -1 && axis_split(kPlan<kPadded>.stages[1]) >= 0, "a pad is not split; the map reading it is");

constexpr bool nothing_split(const loops::Schedule& d) {
    for (const loops::Schedule::Stage& s : d.stages) {
        if (s.parallel >= 0) {
            return false;
        }
    }
    return true;
}
constexpr loops::Schedule kImageOneCore = planned::schedule_of(kImage, kOneCore);
static_assert(nothing_split(kImageOneCore) && nothing_split(planned::schedule_of(kBigGemm, kOneCore)) && nothing_split(planned::schedule_of(kBank, kOneCore)) &&
                  nothing_split(planned::schedule_of(kConv, kOneCore)),
              "a machine with one core splits nothing");
static_assert(kImageOneCore.parallel_work == zen2.parallel_work, "...and still says how much work a worker is owed");

// Planned programs run split, with the machine's own threshold lowered so the small sizes here split at all.
template <const Graph& G>
constexpr planned::Held kEagerHeld{[] {
    loops::Schedule d = kPlan<G>;
    d.parallel_work = 1;
    return planned::program_of(G, d);
}()};
template <const Graph& G>
constexpr const loops::Program& kEager = kEagerHeld<G>.program;

int g_wrong = 0;

loops::Sizes length(std::int64_t n) {
    loops::Sizes sizes{};
    sizes[0] = n;
    return sizes;
}

} // namespace

int main() {
    g_wrong += split_runs::check<kEager<kImage>>("image", loops::Sizes{}, true);
    g_wrong += split_runs::check<kEager<kBlur>>("blur", loops::Sizes{}, true);
    g_wrong += split_runs::check<kEager<kSmallGemm>>("small contraction", loops::Sizes{}, true);
    g_wrong += split_runs::check<kEager<kConv>>("conv", loops::Sizes{}, true);
    g_wrong += split_runs::check<kEager<kBank>>("bank of 64", length(300), false);
    g_wrong += split_runs::check<kEager<kFir>>("fir", length(1000), true);
    g_wrong += split_runs::check<kEager<kPadded>>("pad", loops::Sizes{}, true);
    g_wrong += split_runs::check<kEager<kFewVoices>>("four voices in registers", length(300), false);
    g_wrong += split_runs::check<kEager<kRowFold>>("row fold", loops::Sizes{}, true);
    g_wrong += split_runs::check<kEager<kOneRow>>("one-row norm", loops::Sizes{}, false);
    g_wrong += split_runs::check<kEager<kRms>>("rms norm", loops::Sizes{}, true);
    g_wrong += split_runs::check<kEager<kRmsHigh>>("rms norm, High", loops::Sizes{}, true);
    g_wrong += split_runs::check<kEager<kAttention>>("attention", loops::Sizes{}, true);
    g_wrong += split_runs::check<kEager<kViews>>("a transposed read, two results", loops::Sizes{}, true);
    g_wrong += split_runs::check<kEager<kMono>>("mono chain", length(1000), true);
    if (g_wrong != 0) {
        std::printf("%d results are wrong\n", g_wrong);
        return 1;
    }
    std::printf("nest_parallel_plan: every stage split where the rule allows, each planned split bit for bit the serial run\n");
    return 0;
}
