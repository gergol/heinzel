// Stages split among workers (`Schedule::Stage::parallel`): the axis of one loop is cut into
// ranges and each worker runs the WHOLE stage over one, the serial code unchanged. Each case takes the planner's
// schedule and splits one stage by hand along the outermost loop over a chosen axis, every split as wide as its units
// allow (`parallel_work = 1`), and runs it serially and split (`tests/support/split_runs.hpp`: in order at 1, 2, 3,
// 7 and 16 workers, twice on each workspace, and on pools of 4 and 16) — every workspace buffer byte for byte.
// Odd extents throughout, so every range but the last is whole units and the last meets the ragged end.
//
//   maps       an image chain along its rows (a serial loop) and along its lanes; a mono chain along time
//   folds      a row fold along its rows; a column fold (lanes across outputs, four bodies) along its outputs;
//              a strict High fold along its rows
//   contract   on a machine with tiny caches: along the Tile over rows (the relaid copy, the fetch-ahead, the
//              partial sums resumed per chunk), and along the column lanes (relaid blocks); untiled on zen2
//              along the unrolled rows
//   windows    a conv along its outputs; a blur whose square is computed inside the window (`through`); a
//              sliding median (a network) along its outputs
//   feedback   a bank of 64 voices (time outermost) along its voices; four voices with the state in registers
//              (time innermost) along its voices; a FIR over a feed along time — its first samples worker 0's
//   more       two split stages one after the other; a delay line over a buffer an earlier stage made; a pad
//              and a join (stages made in parts)
//   refusals   what the lowering refuses to split, by name

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/notation/einsum.hpp"
#include "heinzel/sema/analyze.hpp"

#include "planned.hpp"
#include "split_runs.hpp"
#include "tree_facts.hpp"

#include <bit>
#include <cstdio>
#include <string>
#include <string_view>

using namespace heinzel;
using split_runs::split;

namespace {

namespace loops = heinzel::loops;
using loops::NodeKind;
using loops::LoopKind;

constexpr Machine kTiny{.vector_bytes = 32, .registers = 16, .l1_bytes = 1024, .l2_bytes = 2048, .page_bytes = 256, .relay_pays_after = 1, .fma = true};
constexpr Machine kOneLane{.vector_bytes = 4}; // a lane of one float: four voices fit one block of bodies
constexpr Machine kStoring = [] {
    Machine m = zen2;
    m.recompute_always = m.recompute_if_uncached = -1; // a producer stored, never computed inside its window
    return m;
}();

constexpr Graph analysed(Graph g) {
    if (!infer(g).ok) {
        fail("nest_parallel_test: a graph did not resolve");
    }
    return g;
}

// The loop the program splits in its first split stage, in the planner's words: a Loop of single steps (Serial) or
// of chunks (Tile), or a strip whose first enumerator over its axis is lanes — alone, or bodies of them — (Vector)
// or bodies alone (Unroll).
constexpr LoopKind split_kind(const loops::Program& p) {
    for (const loops::Stage& stage : p.stages) {
        if (stage.split < 0) {
            continue;
        }
        const loops::Node& loop = loops::node_at(p, stage.split);
        if (!loops::is_strip(loop)) {
            return loop.step == 1 ? LoopKind::Serial : LoopKind::Tile;
        }
        for (std::int32_t n = stage.split + 1; static_cast<std::size_t>(n) < p.nodes.size() && loops::encloses(p, stage.split, n); ++n) {
            const loops::Node& e = loops::node_at(p, n);
            if (loops::is_enumerator(e) && e.axis == loop.axis) {
                const bool lanes = e.kind == NodeKind::Vector || (e.first_kid >= 0 && loops::node_at(p, e.first_kid).kind == NodeKind::Vector &&
                                                                  loops::node_at(p, e.first_kid).axis == loop.axis);
                return lanes ? LoopKind::Vector : LoopKind::Unroll;
            }
        }
        fail("nest_parallel_test: a split strip with no enumerator over its axis");
    }
    fail("nest_parallel_test: the program splits no stage");
}

// How many bodies of lanes the split strip of stage `s` runs per chunk: its chunk over the lanes of its Vector.
constexpr std::int32_t bodies_of_lanes(const loops::Program& p, std::size_t s) {
    const loops::Node& loop = loops::node_at(p, p.stages[s].split);
    for (std::int32_t n = p.stages[s].split + 1; n < tree_facts::subtree_end(p, s) && loops::encloses(p, p.stages[s].split, n); ++n) {
        if (loops::node_at(p, n).kind == NodeKind::Vector && loops::node_at(p, n).axis == loop.axis) {
            return loop.step / loops::node_at(p, n).step;
        }
    }
    return 0;
}

// How many walking nodes of stage `s` stand outside its first Registers node — the loops outside a fold's or a
// feedback's own (-1: none).
constexpr std::int32_t walks_outside_registers(const loops::Program& p, std::size_t s) {
    for (std::int32_t n = p.stages[s].root + 1; n < tree_facts::subtree_end(p, s); ++n) {
        if (loops::node_at(p, n).kind != NodeKind::Registers) {
            continue;
        }
        std::int32_t outside = 0;
        for (std::int32_t at = loops::node_at(p, n).parent; at != p.stages[s].root; at = loops::node_at(p, at).parent) {
            const NodeKind k = loops::node_at(p, at).kind;
            outside += k == NodeKind::Loop || k == NodeKind::Unroll || k == NodeKind::Vector ? 1 : 0;
        }
        return outside;
    }
    return -1;
}

// ---- the graphs --------------------------------------------------------------------------------------------

constexpr Graph image() {
    Input x{"x", {37, 101}};
    Input y{"y", {37, 101}};
    return analysed(finish(Output{"z"} = x * y + y * 0.5 - x));
}

constexpr Graph mono(std::int32_t n) {
    Input x{"x", Signal{n}};
    Input y{"y", Signal{n}};
    return analysed(finish(Output{"z"} = x * y + y));
}

constexpr Graph row_fold(numerics::Precision level) {
    Input x{"x", {50, 67}};
    Graph g = finish(Output{"y"} = (x * x).sum(1) * 0.5);
    set_default_precision(g, level);
    return analysed(g);
}

// A fold that keeps its axis: its result is stored along it, at extent one.
constexpr Graph kept_fold() {
    Input x{"x", {50, 19}};
    return analysed(finish(Output{"y"} = (x * x).sum(1, keepdims)));
}

constexpr Graph column_fold() {
    Input x{"x", {67, 150}};
    return analysed(finish(Output{"y"} = (x * 2.0).sum(0)));
}

template <std::int32_t M, std::int32_t K, std::int32_t N>
constexpr Graph linear_relu() {
    Input a{"a", {M, K}};
    Input b{"b", {K, N}};
    Input bias{"bias", {N}};
    return analysed(finish(Output{"y"} = max(einsum("ik,kj->ij", a, b) + bias, 0.0)));
}

constexpr Graph conv() {
    Input x{"x", {1000}};
    Input h{"h", {1, 7}};
    return analysed(finish(Output{"y"} = (x.unfold(0, 7) * h).sum(1)));
}

constexpr Graph blur() {
    Input x{"x", {41, 77}};
    Input w{"w", {1, 3, 1}};
    return analysed(finish(Output{"y"} = ((x * x).unfold(0, 3) * w).sum(1)));
}

constexpr Graph median() {
    Input x{"x", {1000}};
    return analysed(finish(Output{"y"} = x.unfold(0, 5).median(1) * 2.0));
}

// Over a run-time length: y = x + 0.9375 y[t-1], per voice.
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

constexpr Graph two_stages() {
    Input x{"x", {50, 67}};
    return analysed(finish(Output{"y"} = x * (x * x).sum(1, keepdims)));
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

constexpr Graph delay_line() {
    Input x{"x", Signal{11, 200}};
    Input p{"p", Signal{11, 200}};
    OpAttr attr{};
    attr.kind = AttrKind::SignalledDelay;
    attr.signalled_delay.max_delay = 5;
    return analysed(finish(Output{"y"} = two_operands(x * 0.5, p, op::DelayLine, attr)));
}

constexpr Graph in_parts(bool pad) {
    Input x{"x", Signal{13, 7, 24}};
    Input w{"w", Signal{13, 3, 24}};
    OpAttr attr{};
    attr.kind = AttrKind::View;
    attr.view.kind = pad ? ViewKind::Pad : ViewKind::Concat;
    attr.view.axis = 1;
    attr.view.pad_before = pad ? 2 : 0;
    attr.view.pad_after = pad ? 3 : 0;
    attr.view.pad_bits = std::bit_cast<std::uint64_t>(0.5);
    const Expr made = pad ? detail::append_unary(x, op::Pad, attr) : two_operands(x, w, op::Concat, attr);
    return analysed(finish(Output{"y"} = made * 2.0));
}

// ---- the programs ------------------------------------------------------------------------------------------

// Each graph planned once per machine; every split below edits that schedule.
template <const Graph& G, const Machine& M>
constexpr loops::Schedule kSchedule = planned::schedule_of(G, M);
template <const Graph& G, const Machine& M, std::size_t Stage, std::int32_t Axis>
constexpr planned::Held kSplitHeld{planned::program_of(G, split(kSchedule<G, M>, Stage, Axis))};
template <const Graph& G, const Machine& M, std::size_t Stage, std::int32_t Axis>
constexpr const loops::Program& kSplit = kSplitHeld<G, M, Stage, Axis>.program;

constexpr Graph kImage = image();
constexpr Graph kMono = mono(1000);
constexpr Graph kMonoFive = mono(5);
constexpr Graph kRowFold = row_fold(numerics::Precision::Normal);
constexpr Graph kRowFoldHigh = row_fold(numerics::Precision::High);
constexpr Graph kColumnFold = column_fold();
constexpr Graph kGemm = linear_relu<97, 29, 45>();
constexpr Graph kSmallGemm = linear_relu<23, 17, 41>();
constexpr Graph kConv = conv();
constexpr Graph kBlur = blur();
constexpr Graph kMedian = median();
constexpr Graph kBank = bank(64);
constexpr Graph kFewVoices = bank(4);
constexpr Graph kFir = fir();
constexpr Graph kTwoStages = two_stages();
constexpr Graph kDelay = delay_line();
constexpr Graph kPad = in_parts(true);
constexpr Graph kJoin = in_parts(false);
constexpr Graph kKeptFold = kept_fold();

// The bank split along its voices with ranges of whole cache lines: 16 voices, where the lanes alone make 8.
constexpr planned::Held kBankLinesHeld{planned::program_of(kBank, split(kSchedule<kBank, zen2>, 0, 0, 64))};
constexpr const loops::Program& kBankLines = kBankLinesHeld.program;
static_assert(loops::split_unit<kSplit<kBank, zen2, 0, 0>, 0>() == 8 && loops::split_unit<kBankLines, 0>() == 16);
// Line rounding on (128-byte lines): an image's rows are no store's contiguous axis — a row a range; its lanes
// are — thirty-two floats a range, where the lanes alone (a map's two registers on zen2) would make sixteen.
constexpr planned::Held kImageRowsLinesHeld{planned::program_of(kImage, split(kSchedule<kImage, zen2>, 0, 0, 128))};
constexpr const loops::Program& kImageRowsLines = kImageRowsLinesHeld.program;
constexpr planned::Held kImageLanesLinesHeld{planned::program_of(kImage, split(kSchedule<kImage, zen2>, 0, 1, 128))};
constexpr const loops::Program& kImageLanesLines = kImageLanesLinesHeld.program;
static_assert(loops::split_unit<kImageRowsLines, 0>() == 1 && loops::split_unit<kImageLanesLines, 0>() == 32 &&
              loops::split_unit<kSplit<kImage, zen2, 0, 1>, 0>() == 16);

// The contraction on the tiny machine fetches its results ahead, a line at a time: it is split with that line.
template <std::int32_t Axis>
constexpr planned::Held kGemmSplitHeld{planned::program_of(kGemm, split(kSchedule<kGemm, kTiny>, 0, Axis, kTiny.line_bytes))};
template <std::int32_t Axis>
constexpr const loops::Program& kGemmSplit = kGemmSplitHeld<Axis>.program;

constexpr planned::Held kTwoSplitHeld{planned::program_of(kTwoStages, split(split(kSchedule<kTwoStages, zen2>, 0, 0), 1, 0))};
constexpr const loops::Program& kTwoSplit = kTwoSplitHeld.program;

static_assert(split_kind(kSplit<kImage, zen2, 0, 0>) == LoopKind::Serial);
static_assert(kTwoSplit.stages.size() == 2 && kTwoSplit.stages[0].split >= 0 && kTwoSplit.stages[1].split >= 0, "a fold, then a map reading it broadcast");
static_assert(split_kind(kSplit<kImage, zen2, 0, 1>) == LoopKind::Vector);
static_assert(split_kind(kSplit<kColumnFold, zen2, 0, 1>) == LoopKind::Vector && bodies_of_lanes(kSplit<kColumnFold, zen2, 0, 1>, 0) == 4 &&
                  loops::split_unit<kSplit<kColumnFold, zen2, 0, 1>, 0>() == 32,
              "lanes across a fold's outputs, four bodies: a unit of 32 outputs");
static_assert(split_kind(kGemmSplit<0>) == LoopKind::Tile, "the rows of a tiled contraction: its Tile");
static_assert(kGemmSplit<0>.stages.size() == 2 && tree_facts::count(kGemmSplit<0>, 1, NodeKind::Prefetch) > 0,
              "on the tiny machine: a relaid copy stage first, and the contraction fetches its results ahead");
static_assert(split_kind(kSplit<kSmallGemm, zen2, 0, 0>) == LoopKind::Unroll, "the rows of an untiled contraction: unrolled");
static_assert(kSchedule<kBlur, zen2>.stages[0].through >= 0, "the square is computed inside the blur's window");
// (the voices' loop, the split one, outside the state's registers; time inside them)
static_assert(split_kind(kSplit<kFewVoices, kOneLane, 0, 0>) == LoopKind::Serial && walks_outside_registers(kSplit<kFewVoices, kOneLane, 0, 0>, 0) == 1 &&
                  tree_facts::count(kSplit<kFewVoices, kOneLane, 0, 0>, 0, NodeKind::Registers) == 1,
              "four voices on one lane: time innermost, the state handed on in registers, the voices outside it");

int g_wrong = 0;

template <const loops::Program& P>
void check(const char* what, const loops::Sizes& sizes = loops::Sizes{}, bool must_split = true) {
    g_wrong += split_runs::check<P>(what, sizes, must_split);
}

loops::Sizes length(std::int64_t n) {
    loops::Sizes sizes{};
    sizes[0] = n;
    return sizes;
}

// ---- refusals ----------------------------------------------------------------------------------------------

void expect_refusal(const char* what, const Graph& g, const loops::Schedule& d, std::string_view says) {
    const loops::Refusal held = planned::refusal_of(g, d);
    const std::string_view said = held.view();
    if (said.substr(0, says.size()) != says && ++g_wrong <= 40) {
        const std::string_view shown = said.empty() ? std::string_view{"(nothing: it lowered)"} : said;
        std::printf("  refusal %s: said \"%.*s\"\n", what, static_cast<int>(shown.size()), shown.data());
    }
}

// Stage `s` of a lowered program as the lowering formed it before checking its split along `axis`: its domain, its
// statements, its walking nodes as its skeleton, the split on the outermost loop over that axis, no fold.
loops::detail::Formed formed_of(const loops::Program& p, std::size_t s, std::int32_t axis) {
    loops::detail::Formed f{};
    f.axes = p.stages[s].axes;
    for (std::int32_t k = 0; k < p.stages[s].count; ++k) {
        f.stmts.push_back(tree_facts::stmt(p, s, k));
    }
    for (const loops::Node& n : tree_facts::walks(p, s)) {
        f.skeleton.walk.push_back(n);
    }
    for (std::size_t k = f.skeleton.walk.size(); k-- > 0;) {
        f.split = f.skeleton.walk[k].axis == axis ? static_cast<std::int32_t>(k) : f.split;
    }
    return f;
}

void check_refusals() {
    expect_refusal("a fold's own axis", kRowFold, split(kSchedule<kRowFold, zen2>, 0, 1), "heinzel::compile: a group is not split along an axis its fold collapses");
    expect_refusal("a fold's own axis, kept at extent one", kKeptFold, split(kSchedule<kKeptFold, zen2>, 0, 1),
                   "heinzel::compile: a group is not split along an axis its fold collapses");
    expect_refusal("a contraction's contracted axis", kSmallGemm, split(kSchedule<kSmallGemm, zen2>, 0, 3),
                   "heinzel::compile: a group is not split along an axis its fold collapses");
    // No schedule lowers to a store that stands still along a non-fold axis, or that a second axis walks too (a
    // scatter would): the check itself, on a lowered stage edited by hand.
    const auto check_edited = [](const char* what, auto edit) {
        loops::detail::Formed f = formed_of(kSplit<kImage, zen2, 0, 0>, 0, 0);
        edit(f.stmts[f.stmts.size() - 1].store);
        const std::expected<void, loops::Refusal> r = loops::detail::check_split(kSplit<kImage, zen2, 0, 0>, f, loops::detail::Implied{});
        const std::string said = r ? "(nothing: it passed)" : std::string(r.error().view());
        if (said.find("heinzel::compile: every buffer a split group writes moves along the split axis, alone") != 0 && ++g_wrong <= 40) {
            std::printf("  refusal %s: said \"%s\"\n", what, said.c_str());
        }
    };
    if (!loops::detail::check_split(kSplit<kImage, zen2, 0, 0>, formed_of(kSplit<kImage, zen2, 0, 0>, 0, 0), loops::detail::Implied{}) &&
        ++g_wrong <= 40) {
        std::printf("  refusal check_split: the stage as lowered, unedited, is refused — only an edit may make it refuse\n");
    }
    check_edited("a store standing still", [](loops::Access& store) { store.buffer_axis[0] = -1; });
    check_edited("a store two axes walk", [](loops::Access& store) { store.buffer_axis[2] = store.buffer_axis[0]; });
    expect_refusal("time in a feedback stage", kBank, split(kSchedule<kBank, zen2>, 0, 1),
                   "heinzel::compile: a split group reads what it writes only at the point");
    // A loop the stage does not have: refused as the schedule's split is put on the stage.
    loops::Schedule past_the_end = kSchedule<kImage, zen2>;
    past_the_end.stages[0].parallel = 9;
    expect_refusal("a loop past the end", kImage, past_the_end, "heinzel::compile: a schedule's split names a loop its stage does not have");
    loops::Schedule below = kSchedule<kImage, zen2>;
    below.stages[0].parallel = -2;
    expect_refusal("a loop below -1", kImage, below, "heinzel::compile: a schedule's split names a loop its stage does not have");
    loops::Schedule inner = kSchedule<kGemm, kTiny>; // the rows: a Tile, then the unrolled loop within its chunks
    for (std::size_t l = inner.stages[0].loops.size(); l-- > 0;) {
        inner.stages[0].parallel = inner.stages[0].loops[l].axis == 0 && inner.stages[0].parallel < 0 ? static_cast<std::int32_t>(l) : inner.stages[0].parallel;
    }
    expect_refusal("the inner loop of a tiled axis", kGemm, inner, "heinzel::compile: a group is split along the outermost loop over an axis");
    loops::Schedule banded = kSchedule<kBlur, kStoring>; // the square stored: two stages
    banded.bands.push_back(loops::Schedule::Band{0, 2, 0, 4});
    banded.stages[0].parallel = 0;
    expect_refusal("a stage in a band", kBlur, banded, "heinzel::compile: a group in a band is not split; its band is");
}

} // namespace

int main() {
    check<kSplit<kImage, zen2, 0, 0>>("image, rows");
    check<kSplit<kImage, zen2, 0, 1>>("image, lanes");
    check<kImageLanesLines>("image, lanes in whole lines");
    check<kSplit<kMono, zen2, 0, 0>>("mono, time");
    check<kSplit<kMonoFive, zen2, 0, 0>>("mono of five, one unit", loops::Sizes{}, false);
    check<kSplit<kRowFold, zen2, 0, 0>>("row fold, rows");
    check<kSplit<kRowFoldHigh, zen2, 0, 0>>("strict row fold, rows");
    check<kSplit<kColumnFold, zen2, 0, 1>>("column fold, outputs");
    check<kGemmSplit<0>>("contraction, Tile over rows");
    check<kGemmSplit<1>>("contraction, column lanes over relaid blocks");
    check<kSplit<kSmallGemm, zen2, 0, 0>>("contraction, unrolled rows");
    check<kSplit<kConv, zen2, 0, 0>>("conv, outputs");
    check<kSplit<kBlur, zen2, 0, 0>>("blur computing its square inside the window, rows");
    check<kSplit<kMedian, zen2, 0, 0>>("sliding median, outputs");
    for (const std::int64_t n : {1, 2, 3, 4, 9, 67, 100, 131, 1000}) {
        check<kSplit<kBank, zen2, 0, 0>>("bank of 64, voices", length(n));
        check<kBankLines>("bank of 64, voices in whole lines", length(n));
        check<kSplit<kFewVoices, kOneLane, 0, 0>>("four voices in registers, voices", length(n));
        check<kSplit<kFir, zen2, 0, 0>>("fir over a feed, time", length(n), n > 16);
    }
    check<kTwoSplit>("two stages, both split");
    check<kSplit<kDelay, zen2, 1, 1>>("a delay line, time");
    check<kSplit<kPad, zen2, 0, 0>>("a pad, its parts split");
    check<kSplit<kJoin, zen2, 0, 2>>("a join, its parts split along time");
    check_refusals();
    if (g_wrong != 0) {
        std::printf("%d results are wrong\n", g_wrong);
        return 1;
    }
    std::printf("nest_parallel: every split run bit for bit the serial run, every refusal by name\n");
    return 0;
}
