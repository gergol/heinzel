// The planner by itself (`Plan/archive/13-loop-tree/06` §5, M3.4): representative graphs — the corpus of `04` §4 but its edited
// schedules, and harris, attention, a chain of three blurs, an 8 MB unsharp, the oscillator's stream band — planned for
// zen2, the default machine (no fma, 16-byte lanes, one core) and a machine with tiny caches; then, per rule, a graph
// and machine where it takes nothing. Each schedule is printed and compared with its listing in
// `tests/goldens/loops_plan/`; each schedule also lowers (`plan_and_lower`) to a program the runner accepts
// (`well_formed`). Refusals are values while the compiler evaluates. Nothing but `loops/` and its allowlist is in
// sight (`loops_alone`); these listings hold the planner.
//
// A listing that changes is printed; `HEINZEL_REPIN=1 test_loops_plan` rewrites the files, and the change is reviewed
// in `git diff` like any other — never accepted blind.

#include "heinzel/loops/planner/plan.hpp"
#include "heinzel/loops/print.hpp"
#include "heinzel/schedule/machine.hpp"

#include "corpus_graphs.hpp"
#include "hand_built_graph.hpp"
#include "well_formed.hpp"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

namespace {

using namespace heinzel;
namespace loops = heinzel::loops;

constexpr Index n{};

// Caches small enough that a small graph is tiled as a large one would be.
constexpr Machine kTiny{.vector_bytes = 32, .registers = 16, .l1_bytes = 1024, .l2_bytes = 2048, .page_bytes = 256, .relay_pays_after = 1, .fma = true};
// zen2 with one fact changed: never banding, and the machines of the boundary cases below.
template <auto Edit>
constexpr Machine zen2_but() {
    Machine m = zen2;
    Edit(m);
    return m;
}
constexpr Machine kNoBands = zen2_but<[](Machine& m) { m.band_bytes = 0; }>();
constexpr Machine kRecomputing = zen2_but<[](Machine& m) { m.recompute_always = 1000; }>();
constexpr Machine kShortPages = zen2_but<[](Machine& m) { m.page_bytes = 6 * 64 * 4; }>();      // a block of rows of 64 columns
// blurxy<1024, 2048>'s working set, exactly (an L2 as large, so that its 8 MB `bx` alone does not band it)
constexpr Machine kWorkingSet = zen2_but<[](Machine& m) { m.band_when_over = 25133096, m.l2_bytes = 25133096; }>();
// blurxy<1024, 2048>'s `bx`, 1024 x 2046 floats, exactly the L2 (a working set no band is wanted for)
constexpr Machine kTouchedTwice = zen2_but<[](Machine& m) { m.band_when_over = std::int64_t{1} << 40, m.l2_bytes = 8380416; }>();
constexpr Machine kTwoRows = zen2_but<[](Machine& m) { m.band_bytes = 4 * 2046 * 4; }>();       // its reach and two rows of 2046
constexpr Machine kTwoCores = zen2_but<[](Machine& m) { m.cores = 2; }>();
constexpr Machine kSlabOfRows = zen2_but<[](Machine& m) { m.l1_bytes = 1 << 20, m.l2_bytes = 3072; }>(); // half the L2 is six rows of 64
constexpr Machine kEightRegisters = zen2_but<[](Machine& m) { m.registers = 8; }>();             // two rows a register tile
constexpr Machine kOneChunk = zen2_but<[](Machine& m) { m.band_when_over = 0, m.band_bytes = 10 * 2046 * 4; }>(); // 8 rows
constexpr Machine kStartsHidden = zen2_but<[](Machine& m) { m.stage_start = 0; }>(); // a chunk's starts: the state's store and load, 8
constexpr Machine kNoStarts = zen2_but<[](Machine& m) { m.stage_start = 0, m.forwarded = 0; }>(); // a chunk starts for nothing
constexpr Machine kHalfStarts = zen2_but<[](Machine& m) { m.stage_start = 0, m.forwarded = 4; }>(); // starts 4: under eight voices' gain, over one's
constexpr Machine kNoLineSize{.line_bytes = 0}; // the default machine, its line size unknown
constexpr Machine kSmallFirstLevel{.vector_bytes = 32, .registers = 16, .l1_bytes = 1024, .l2_bytes = 65536, .line_bytes = 128};

// ---- graphs for the declines ---------------------------------------------------------------------------------

constexpr Graph strict_rowsum() { // `corpus::rowsum<64>` at High: its fold owed its strict order
    // its length is symbol 1, which no declaration spells (DS-18)
    const DimDecl r{1, DimConstraints{1, 1}};
    Expr x = detail::input("x", Shape{64, Extent{Dim{r.id}}}, ElemType::F32, r);
    Output y{"y"};
    y = x.sum(0);
    Graph g = y;
    set_default_precision(g, numerics::Precision::High);
    return corpus::analysed(g);
}
constexpr Graph transposed_sum() { // y = x + zᵀ: the transpose reads its source across the lanes
    Input x{"x", {16, 16}};
    Input z{"z", {16, 16}};
    Output y{"y"};
    y = x + z.transpose({1, 0});
    return corpus::analysed(y);
}
constexpr Graph filtered(bool costly, bool also_a_result) { // an 8-tap filter over a producer of x
    Input x{"x", {4096}};
    Input h{"h", {1, 8}};
    Expr a = costly ? sqrt(x * x + 1.0) : x * x;
    Expr y = (a.unfold(0, 8) * h).sum(1);
    return corpus::analysed(also_a_result ? finish(Output{"a"} = a, Output{"y"} = y) : finish(Output{"y"} = y));
}
template <int Voices>
constexpr Graph oscillators() { // a bank of oscillators: a stream band's row is `Voices` numbers
    // its length is symbol 1, which no declaration spells (DS-18)
    const DimDecl samples{1, DimConstraints{1, 1}};
    Expr inc = detail::input("inc", Shape{Voices, Extent{Dim{samples.id}}}, ElemType::F32, samples);
    Output y{"y"};
    y = dsp::oscillator(inc, constant(0.5));
    return corpus::analysed(y);
}
constexpr Graph map_chain(std::int32_t maps, bool filtered) { // `maps` cheap maps of x — then, `filtered`, a 3-tap filter over them
    Expr a = Input{"x", {4096}};
    for (std::int32_t k = 0; k < maps; ++k) {
        a = a * 1.5;
    }
    Input h{"h", {1, 3}};
    Output y{"y"};
    y = filtered ? (a.unfold(0, 3) * h).sum(1) : a;
    return corpus::analysed(y);
}
constexpr Graph symbolic_product() { // 64 x k times k x 64, k bound at run time
    // a symbolic element axis, which no declaration spells (DS-18)
    const DimDecl k{0, DimConstraints{1, 1}};
    Expr a = detail::input("a", Shape{64, Extent{Dim{k.id}}, 1}, ElemType::F32, k);
    Expr b = detail::input("b", Shape{Extent{Dim{k.id}}, 64, 1}, ElemType::F32, k);
    Output y{"y"};
    y = einsum("ik,kj->ij", a, b);
    return corpus::analysed(y);
}
constexpr Graph double_product() { // 64 x 64 times 64 x 64 in F64: two lanes of 16 bytes
    Input a{"a", {64, 64}, ElemType::F64};
    Input b{"b", {64, 64}, ElemType::F64};
    Output y{"y"};
    y = einsum("ik,kj->ij", a, b);
    return corpus::analysed(y);
}
constexpr Graph double_bank() { // three one-poles in F64
    Output y{"y"};
    y[n] = Input{"x", Signal{3, Length{.multiple_of = 1, .min = 1, .expect = 64}}, ElemType::F64} + 0.9375 * y[n - 1];
    return corpus::analysed(y);
}
constexpr Graph column_sums() { // the sums of 16 rows of 8 columns: the fold's outputs one lane wide
    Output y{"y"};
    y = Input{"x", {16, 8}}.sum(0);
    return corpus::analysed(y);
}
constexpr Graph layers(std::int32_t count, std::int32_t width = 128) { // `nest_contract_test`'s: a chain of contractions, each with its own weight
    constexpr const char* names[] = {"w0", "w1", "w2", "w3", "w4", "w5", "w6", "w7", "w8", "w9", "w10", "w11", "w12", "w13", "w14", "w15", "w16"};
    Expr h = Input{"x", {width, width}};
    for (std::int32_t l = 0; l < count; ++l) {
        h = max(einsum("ik,kj->ij", h, Input{names[l], {width, width}}), 0.0);
    }
    Output y{"y"};
    y = h;
    return corpus::analysed(y);
}

// ---- refusals as values, at compile time -------------------------------------------------------------------------

constexpr loops::Refusal refusal(const Graph& g, const Machine& m) {
    const std::expected<loops::Planned, loops::Refusal> r = loops::plan_and_lower(g, m);
    const std::expected<void, loops::Refusal> w = r ? loops::well_formed(r->program) : std::unexpected(r.error());
    return w ? loops::Refusal{"planned"} : w.error();
}
constexpr Graph kElem = corpus::elem();
static_assert(refusal(kElem, zen2) == "planned" && refusal(kElem, kTiny) == "planned");
static_assert(refusal(Graph{}, zen2) == "heinzel::compile: a graph with no node has nothing to run");
static_assert(refusal(corpus::sliding_median(4, 4), zen2) == "heinzel::compile: median is not one exact rank, which is not lowered yet: [1, 4, 1]");
static_assert(refusal(layers(17), zen2) == "heinzel::compile: more loop nests than kMaxStages; rebuild with -DHEINZEL_MAX_STAGES=N");

// A node no stage runs is refused by its operation as written, the case, then its operands' shapes, last (Decision 77):
// an order statistic by its member's word, and a message cut at its 95 characters loses shapes, never the reason. An
// aligned `einsum` runs, a product and a sum (Decision 78); `_contr`, which only a graph built by hand holds, does not.
constexpr Graph aligned_product() {
    Input a{"a", {64, 32}};
    Input b{"b", {64, 32}};
    Output y{"y"};
    y = einsum("ij,ij->i", a, b);
    return corpus::analysed(y);
}
constexpr Graph contracted_by_hand() {
    Graph g{};
    const std::int32_t a = add_value(g, ElemType::F32, Shape{64, 32, 1}, ValueCategory::Input);
    const std::int32_t b = add_value(g, ElemType::F32, Shape{64, 32, 1}, ValueCategory::Input);
    OpAttr summed{};
    summed.kind = AttrKind::Contract;
    summed.contract.axes = Axes{1};
    const std::int32_t y = add_node(g, op::Contract, a, b, kNoValue, summed);
    g.inputs.push_back(NamedValue{Name{"a"}, a});
    g.inputs.push_back(NamedValue{Name{"b"}, b});
    g.outputs.push_back(NamedValue{Name{"y"}, y});
    return corpus::analysed(g);
}
constexpr Graph order_statistic(std::int32_t which) {
    Input x{"x", Signal{6, 64}};
    Output y{"y"};
    if (which == 0) {
        y = x.sort(0) + 0.0;
    } else if (which == 1) {
        y = x.topk(0, 3) + 0.0;
    } else {
        y = x.quantile(0, 0.25) + 0.0;
    }
    return corpus::analysed(y);
}
constexpr Graph quantile_of_five_axes() {
    Input x{"x", {4, 3, 3, 3, 3}};
    Output y{"y"};
    y = x.quantile(0, 0.25) + 0.0;
    return corpus::analysed(y);
}
static_assert(refusal(aligned_product(), zen2) == "planned");
// cos, tan, exp2 and pow plan, their double cores run one number at a time (Plan/28-dsl-proposals/05 D2); an operation
// with no writing over the value type is still refused (`refused<0>`, below).
constexpr Graph on_a_double_core(std::int32_t which) {
    Input x{"x", Signal{64}, gt(0), lt(1.5)};
    Output y{"y"};
    if (which == 0) {
        y = cos(x);
    } else if (which == 1) {
        y = tan(x);
    } else if (which == 2) {
        y = exp2(x);
    } else {
        y = pow(x, x);
    }
    return corpus::analysed(y);
}
static_assert(refusal(on_a_double_core(0), zen2) == "planned" && refusal(on_a_double_core(1), zen2) == "planned" &&
              refusal(on_a_double_core(2), zen2) == "planned" && refusal(on_a_double_core(3), zen2) == "planned");
static_assert(refusal(contracted_by_hand(), zen2) == "heinzel::compile: _contr is not lowered yet: [64, 32, 1] and [64, 32, 1]");
static_assert(refusal(order_statistic(0), zen2) == "heinzel::compile: sort is not one exact rank, which is not lowered yet: [6, 64]");
static_assert(refusal(order_statistic(1), zen2) == "heinzel::compile: topk 3 is not one exact rank, which is not lowered yet: [6, 64]");
static_assert(refusal(order_statistic(2), zen2) == "heinzel::compile: quantile 0.25 is not one exact rank, which is not lowered yet: [6, 64]");
constexpr std::string_view kCut = "heinzel::compile: quantile 0.25 is not one exact rank, which is not lowered yet: [4, 3, 3, 3, 3, 1]";
static_assert(kCut.size() > kMaxMessageLength - 1 && refusal(quantile_of_five_axes(), zen2).view() == kCut.substr(0, kMaxMessageLength - 1),
              "cut at a message's length, in the shapes");
static_assert(refusal(layers(12), zen2) == "planned"); // twelve layers and four copies: the other weights read as fed
static_assert(*loops::planner::schedule_of(kElem, zen2) == loops::plan_and_lower(kElem, zen2)->schedule &&
              loops::lower(kElem, *loops::planner::schedule_of(kElem, zen2)) == loops::plan_and_lower(kElem, zen2)->program);
// A machine at the ends of 32 bits plans without overflowing (M3 review rounds 5 and 6): a register tile of 2^30 rows
// or of none, lines of 2 GB, a stream band's rows at INT_MIN or INT_MAX (at most 2^30 of them, what a lowering takes),
// and a relay whose walks would pay after INT_MAX blocks of 2^30 rows, over a symbolic axis.
constexpr Graph kProduct = corpus::matmul<64, 64, 64>();
constexpr Graph batched_vector_products() {
    // b's symbolic element axis, which no declaration spells (DS-18)
    const DimDecl j{2, DimConstraints{1, 1}};
    Input a{"a", Signal{64, 1}}; // one sample, as b is: a tensor would not contract with it
    Expr b = detail::input("b", Shape{64, 64, Extent{Dim{j.id}}, 1}, ElemType::F32, j);
    Output y{"y"};
    y = einsum("k,kij->ij", a, b);
    return corpus::analysed(y);
}
static_assert(refusal(kProduct, Machine{.vector_bytes = 32, .registers = INT_MAX}) == "planned" &&
              refusal(kProduct, Machine{.vector_bytes = 32, .registers = INT_MIN}) == "planned" &&
              refusal(kProduct, Machine{.vector_bytes = 32, .line_bytes = INT_MAX}) == "planned");
static_assert(refusal(oscillators<8>(), Machine{.vector_bytes = 32, .band_when_over = 0, .band_stream_rows = INT_MIN}) == "planned" &&
              refusal(oscillators<8>(), Machine{.vector_bytes = 32, .band_when_over = 0, .band_stream_rows = INT_MAX}) == "planned");
static_assert(loops::planner::schedule_of(batched_vector_products(), Machine{.vector_bytes = 32, .registers = INT_MAX, .relay_pays_after = INT_MAX}).has_value());

// Every refusal an analysed graph reaches, by its text. The surface corpus (`agreement_graphs.hpp`) pins the order
// statistics, the constants and windows read from storage, the tables, a window as a result, an output no node makes
// and a window over time; `infer` refuses a view along time, a window's hop below one and a pad or join along a
// symbolic axis first; "no node is ready" is an invariant of the order.
constexpr Length kStream{.multiple_of = 1, .min = 1, .expect = 64};
constexpr Expr streamed(auto... e) { return Input{"x", Signal{e..., kStream}}; }
constexpr Expr flip(Expr x, std::int32_t axis) {
    OpAttr attr{};
    attr.kind = AttrKind::View;
    attr.view.kind = ViewKind::Flip;
    attr.view.axis = axis;
    return detail::append_unary(x, op::Flip, attr);
}
// Cases 14 and 15: a delay that changes, read in feedback (a flanger's) and of a constant (Plan/28-dsl-proposals/05 D4).
template <std::int32_t Case>
constexpr Graph delay_refused() {
    constexpr Index at{48'000};
    Input d{"d", Signal{64}};
    if constexpr (Case == 14) {
        Output y{"y"};
        y[at] = Input{"x", Signal{64}} + 0.7 * y[at - d.at_most(1.0) * ms];
        return y;
    } else {
        return finish(Output{"y"} = d + constant(0.5)[at - d.at_most(1.0) * ms]);
    }
}
// Case 10 as written: finishing moves its lag after y's definition (Plan/28-dsl-proposals/03 B4), so it goes back.
constexpr Graph lag_moved_back(Graph g) {
    std::rotate(g.nodes.begin(), g.nodes.begin() + 4, g.nodes.begin() + 5);
    return g;
}
template <std::int32_t Case>
constexpr Graph refused() {
    // cases 4, 6 and 12 say what no declaration can (DS-18)
    Variable y;
    if constexpr (Case == 0) {
        return finish(Output{"y"} = apply(op::Atan, streamed()));
    } else if constexpr (Case == 1) {
        return finish(Output{"y"} = streamed(4).sum(1));
    } else if constexpr (Case == 2) {
        return finish(Output{"y"} = streamed(4).sum(5));
    } else if constexpr (Case == 3) {
        return finish(Output{"y"} = streamed(4).unfold(0, 2).reshape({6}) * 2.0);
    } else if constexpr (Case == 4) {
        const DimDecl d{1, DimConstraints{1, 1}};
        return finish(Output{"y"} = flip(detail::input("x", Shape{Extent{Dim{d.id}}, 64}, ElemType::F32, d), 0) * 2.0);
    } else if constexpr (Case == 5) {
        y[n] = streamed() + 0.5 * y[n - 0];
    } else if constexpr (Case == 6) {
        y[n] = detail::input("x", Shape{}) + 0.5 * y[n - 1];
    } else if constexpr (Case == 7) {
        y[n] = streamed(4) + y[n - 1].sum(0, keepdims);
    } else if constexpr (Case == 8) {
        return finish(Output{"y"} = streamed(8).unfold(0, 3)[n - 1].sum(1));
    } else if constexpr (Case == 9) {
        Input x{"x", Signal{64}};
        Input p{"p", Signal{64}};
        Arena* arena = bring_together(x, p);
        BuildNode n{};
        n.op = op::DelayLine;
        n.inputs[0] = x.position();
        n.inputs[1] = p.position();
        n.attr.kind = AttrKind::SignalledDelay;
        n.attr.signalled_delay.max_delay = 5;
        arena->nodes.push_back(n);
        return finish(Output{"y"} = Expr{arena, static_cast<std::int32_t>(arena->nodes.size() - 1)});
    } else if constexpr (Case == 12) {
        const DimDecl s{0, DimConstraints{1, 1}};
        return finish(Output{"y"} = detail::input("x", Shape{2, 3, Extent{Dim{s.id}}, 1}, ElemType::F32, s).reshape({6, Extent{Dim{s.id}}}) * 2.0);
    } else if constexpr (Case == 13) {
        Input a{"a", {64, 32}};
        Input b{"b", {32, 48}};
        Input c{"c", {48, 64}};
        return finish(Output{"y"} = einsum("ik,kj->ij", a, b) + c.transpose({1, 0}));
    } else if constexpr (Case == 14 || Case == 15) {
        return delay_refused<Case>();
    } else if constexpr (Case == 10) {
        Variable z;
        Expr later = y[n - 1];
        z[n] = Input{"x", Signal{64}} * 2.0;
        y[n] = z + 1.0;
        return lag_moved_back(finish(Output{"y"} = y, Output{"l"} = later + 0.0));
    } else {
        Expr a = Input{"x", Signal{64}} + 0.5 * y[n - 1];
        for (std::int32_t k = 0; k < 100; ++k) {
            a = a * 1.0001;
        }
        y[n] = a;
    }
    return finish(Output{"y"} = y);
}
template <std::int32_t Case>
constexpr loops::Refusal refusal_of() {
    Graph g = refused<Case>();
    return infer(g).ok ? refusal(g, zen2) : loops::Refusal{"infer refused"};
}
// (a refusal compares equal to a text by the 95 characters it holds of it: cases 1, 3 and 4 lose the end of a shape)
static_assert(refusal_of<0>() == "heinzel::compile: _atan is not lowered yet: [?0]");
static_assert(refusal_of<1>() == "heinzel::compile: _reduce: this collapses the streamed axis, which a run here cannot do: [4, ?0]");
static_assert(refusal_of<2>() == "heinzel::compile: _reduce: this collapses an axis the operand does not have: [4, ?0]");
static_assert(refusal_of<3>() == "heinzel::compile: _reshape: new extents over a sliding window have no address here yet: [3, 2, ?0]");
static_assert(refusal_of<4>() == "heinzel::compile: _flip: an axis of symbolic extent, reversed, has no address here yet: [?1, 64]");
static_assert(refusal_of<5>() == "heinzel::compile: a value defined in terms of its own PRESENT is an algebraic loop");
static_assert(refusal_of<6>() == "heinzel::compile: a value of no axes has no time to be read back along");
static_assert(refusal_of<7>() == "heinzel::compile: feedback through anything but maps of one shape is not lowered yet");
static_assert(refusal_of<8>() == "heinzel::compile: the past of a window is not lowered yet");
static_assert(refusal_of<9>() == "heinzel::compile: a delay line over an input reads a copy of it, which this graph lacks");
static_assert(refusal_of<10>() == "heinzel::compile: a lag of a later value its definition does not read is not lowered yet");
static_assert(refusal_of<11>() == "heinzel::compile: a cycle over kMaxGroupStmts; rebuild with -DHEINZEL_MAX_STAGE_STMTS=N");
static_assert(refusal_of<14>() == "heinzel::compile: a delay line in feedback, or of a value defined later, is not lowered yet");
static_assert(refusal_of<15>() == "heinzel::compile: a delay line over a constant is not lowered yet");
static_assert(refusal_of<12>() == "heinzel::compile: _reshape: new extents, a symbolic one not outermost, have no address yet: [2, 3, ?0, 1]");
// Where the planner once said yes and the lowering refused (`06` §7), it now plans what the lowering accepts
// (`Plan/17-simplify-more/06` § A; `nest_planner_holes_test` runs each). (d): a product's lanes stay off its outputs
// where a transpose read after it would read across them.
static_assert(refusal_of<13>() == "planned");
// (f): a product per sample has its rows along time, tiled where there are more than one and a half slabs of them — 64
// samples on tiny caches, not 40 — but not where its stage reads a feed's past (the lag joins after the fold).
constexpr Graph per_sample(std::int32_t samples, bool lagged = true) {
    Input x{"x", Signal{8, samples}};
    Input w{"w", Signal{8, 8, samples}};
    Output y{"y"};
    y = einsum("k,ko->o", x, w) + 0.5 * (lagged ? x[n - 1] : x);
    return corpus::analysed(y);
}
constexpr bool tiles_time(const Graph& g) {
    for (const loops::Schedule::Loop& loop : loops::planner::schedule_of(g, kTiny)->stages[0].loops) {
        if (loop.kind == loops::LoopKind::Tile && loop.axis == 1) {
            return true;
        }
    }
    return false;
}
static_assert(refusal(per_sample(64), kTiny) == "planned" && refusal(per_sample(40), kTiny) == "planned");
static_assert(!tiles_time(per_sample(64)) && tiles_time(per_sample(64, false)));
// (b): a bank of one-poles over 256 voices, its feedback term read through a window over the voices, banded along them
// in chunks of 64: the cycle runs two voices ahead and stores its state whole, so the band is not split.
constexpr Graph bank_ahead() {
    Input x{"x", Signal{256, 1}};
    Variable y;
    Expr fed = 0.5 * y[n - 1];
    y[n] = x + fed;
    Output out{"y"};
    out = fed.unfold(0, 3).sum(1);
    return corpus::analysed(out);
}
constexpr Machine kBandsAhead = zen2_but<[](Machine& m) { m.band_when_over = 0, m.band_bytes = 66 * 4; }>();
static_assert(refusal(bank_ahead(), kBandsAhead) == "planned");
static_assert([] {
    const loops::Schedule d = *loops::planner::schedule_of(bank_ahead(), kBandsAhead);
    return d.bands.size() == 1 && d.bands[0].first == 0 && d.bands[0].count == 2 && !d.bands[0].parallel;
}());

// ---- the cases -------------------------------------------------------------------------------------------------

struct On {
    const char* label;
    const Machine* machine;
};
constexpr On kZen2{"zen2", &zen2};
constexpr On kDefault{"default", &default_machine};
constexpr On kTinyCaches{"tiny", &kTiny};
constexpr On kNoBanding{"zen2, no bands", &kNoBands};
constexpr On kRecomputes{"zen2, recompute_always 1000", &kRecomputing};
constexpr On kPages{"zen2, pages of 1536", &kShortPages};
constexpr On kSetOf{"zen2, band_when_over and l2 25133096", &kWorkingSet};
constexpr On kTouched{"zen2, l2 8380416, band_when_over 2^40", &kTouchedTwice};
constexpr On kRows{"zen2, band_bytes 32736", &kTwoRows};
constexpr On kCores{"zen2, two cores", &kTwoCores};
constexpr On kSlab{"zen2, l1 1 MB, l2 3072", &kSlabOfRows};
constexpr On kSmallL1{"l1 1024, lines of 128", &kSmallFirstLevel};
constexpr On kRegisters{"zen2, 8 registers", &kEightRegisters};
constexpr On kChunk{"zen2, band_when_over 0, band_bytes 81840", &kOneChunk};
constexpr On kNoLines{"default, line_bytes 0", &kNoLineSize};
constexpr On kStarts{"zen2, stage_start 0", &kStartsHidden};
constexpr On kFreeStarts{"zen2, stage_start 0, forwarded 0", &kNoStarts};
constexpr On kHalfForwarded{"zen2, stage_start 0, forwarded 4", &kHalfStarts};

struct Case {
    const char* name;
    Graph (*build)();
    On on[3];
    int machines;
};
constexpr Case every(const char* name, Graph (*build)()) { return Case{name, build, {kZen2, kDefault, kTinyCaches}, 3}; }
constexpr Case one(const char* name, Graph (*build)(), On on) { return Case{name, build, {on, on, on}, 1}; }

const Case kCases[] = {
    every("matmul_128", corpus::matmul<128, 128, 128>),
    every("matmul_512", corpus::matmul<512, 512, 512>),
    every("matmul_100x75x130", corpus::matmul<100, 75, 130>),
    every("linear_relu", corpus::linear_relu),
    every("tall", [] { return corpus::tall(true); }),
    every("blurxy_256k", corpus::blurxy<32, 2048, 3>),
    every("blurxy_8m", corpus::blurxy<1024, 2048, 3>),
    every("rowsum_1024", corpus::rowsum<1024>),
    every("rmsnorm", corpus::rmsnorm),
    every("softmax", corpus::softmax),
    every("sliding_median_5", [] { return corpus::sliding_median(37, 5); }),
    every("one_pole_bank", [] { return corpus::one_pole_bank(11, 0.9375, 64); }),
    every("ladder_bank", corpus::ladder_bank),
    every("fir_over_a_feed", corpus::fir_over_a_feed),
    every("delay_line", corpus::delay_line),
    every("padded", corpus::padded),
    every("elem", corpus::elem),
    every("lag_of_a_lag", corpus::lag_of_a_lag),
    every("harris_8m", corpus::harris<1024, 2048>),
    every("attention_64", corpus::attention<64, 64>),
    every("three_blurs_8m", corpus::three_blurs<1024, 2048>),
    every("unsharp_8m", corpus::unsharp<1024, 2048, 5>),
    every("fm_oscillator", corpus::fm_oscillator),
    // Each rule declining. (The default machine above is where `Contract` declines without fma and nothing splits on
    // one core; `blurxy_256k` where a working set that fits the last-level cache gets no band; `matmul_128` where
    // a product that fits the caches gets no tiles.)
    one("decline_lanes_short_axis", corpus::rowsum<4>, kZen2),      // an axis shorter than a lane carries none
    one("decline_lanes_strict_fold", strict_rowsum, kZen2),         // a fold owed its order keeps its lanes off its axis
    one("decline_lanes_strided", transposed_sum, kZen2),            // a read across the lanes keeps them off
    one("decline_relay_one_row", corpus::matmul<1, 256, 256>, kZen2), // a strip read by one row pays no copy
    one("decline_prefetch_short_rows", corpus::matmul<512, 1024, 16>, kZen2), // tiled, but its blocks' rows are near
    one("decline_time_innermost", [] { return corpus::one_pole_bank(64, 0.9375, 64); }, kZen2), // 64 voices: time outermost, no split
    one("decline_inline_costly", [] { return filtered(true, false); }, kZen2),   // a square root is no cheap map
    one("decline_inline_a_result", [] { return filtered(false, true); }, kZen2), // what a caller reads is stored anyway
    one("decline_inline_many_taps", corpus::conv<4096, 64>, kZen2),              // 63 more squares per output: stored
    one("decline_band_never", corpus::blurxy<1024, 2048, 3>, kNoBanding),        // a machine that never bands
    one("decline_band_narrow_stream", oscillators<4>, kZen2),                    // a stream's rows under a lane
    one("decline_band_block_starts", oscillators<8>, kZen2),                     // a block's stream: its gain hides no starts
    one("decline_copies_no_room", [] { return layers(12); }, kZen2),             // twelve layers, room for four copies
    // Each rule at its boundary — every comparison the review of M3 mutated one step that the cases above do not see
    // — so a rule moved by one is a listing that changes.
    one("boundary_inline_few", corpus::conv<4096, 9>, kZen2),                    // 8 more squares an output: inside, always
    one("boundary_inline_uncached", corpus::conv<2097152, 17>, kZen2),           // 16 more, x² exactly half the L3: inside
    one("boundary_inline_room", [] { return map_chain(94, true); }, kRecomputes), // 94 maps and a reader of 2: one stage
    one("boundary_stage_room", [] { return map_chain(100, false); }, kZen2),     // 100 maps: 96 in a stage, then 4
    one("boundary_fetch_page", corpus::matmul<512, 1024, 64>, kPages),           // six rows of a block a page apart: fetched
    one("boundary_relay_walks", corpus::matmul<72, 128, 128>, kZen2),            // strips read by 72 rows, twelve blocks: relaid
    one("boundary_band_working_set", corpus::blurxy<1024, 2048, 3>, kSetOf),     // a working set of exactly band_when_over
    one("boundary_band_touched_twice", corpus::blurxy<1024, 2048, 3>, kTouched), // a value handed on of exactly the L2: no band
    one("boundary_band_height", corpus::blurxy<1024, 2048, 3>, kRows),           // two rows a chunk, the reach two: no band
    one("boundary_stream_row", oscillators<8>, kFreeStarts),                     // a stream's row exactly one vector: banded
    one("boundary_band_starts", oscillators<8>, kStarts),                        // its gain, 8 cycles a chunk, its starts 8: no band
    one("boundary_band_voices", oscillators<8>, kHalfForwarded),                 // its gain 8 cycles a chunk (one voice's 1), starts 4: banded
    one("boundary_two_cores", corpus::elem, kCores),                             // two cores split
    one("boundary_fold_one_lane", corpus::rowsum<8>, kZen2),                     // a fold axis of exactly one lane: in lanes
    one("boundary_inline_recomputed", corpus::conv<2097152, 33>, kZen2),         // 32 more, x² half the L3: inside
    one("boundary_slab_of_rows", corpus::matmul<64, 64, 64>, kSlab),             // a slab of exactly one block of rows: tiled
    one("boundary_contracted_lanes", corpus::attention<16, 8>, kZen2),           // Q·Kᵀ over exactly one lane, K read 16 times: in lanes
    one("boundary_turned", corpus::attention<32, 8>, kZen2),                     // ...K read 32 times (`turn_pays_after`): K turned
    one("boundary_lanes_across", column_sums, kZen2),                            // eight outputs, one lane: across them
    one("boundary_nests", [] { return layers(16); }, kZen2),                     // sixteen stages, a program's all
    one("boundary_empty_contraction", corpus::matmul<64, 0, 64>, kZen2),         // a contracted axis of none: no tiles
    one("boundary_symbolic_contraction", symbolic_product, kSmallL1),            // no chunk of it fits the first level: no tiles
    one("boundary_no_voices", [] { return corpus::one_pole_bank_of_none(0.5); }, kZen2), // time outermost, not a block of no bodies
    one("boundary_contracted_whole", corpus::matmul<64, 552, 64>, kZen2),        // 552 = 1.5 chunks of 368: not chunked
    one("boundary_rows_whole", corpus::matmul<261, 1024, 64>, kZen2),            // 261 = 1.5 slabs of 174: not chunked
    one("boundary_strip_lines", corpus::matmul<64, 600, 64>, kDefault),          // a 32-byte strip takes a whole line: tiled
    one("boundary_two_lanes", double_product, kDefault),                         // F64 in 16 bytes: two lanes, in lanes
    one("boundary_two_rows", corpus::matmul<64, 64, 64>, kRegisters),            // eight registers: two rows, unrolled
    one("boundary_bank_two_lanes", double_bank, kDefault),                       // three F64 voices in two lanes
    one("boundary_no_band_bytes", oscillators<8>, kNoBanding),                   // no band bytes: no band, not even a stream's
    one("boundary_one_chunk", corpus::blurxy<10, 2048, 3>, kChunk),              // eight rows a chunk of eight: no band
    one("boundary_relay_strips", corpus::matmul<128, 64, 128>, kZen2),           // strips of exactly the first level: not relaid
    one("boundary_no_lines", corpus::matmul<64, 1024, 64>, kNoLines),            // no line size: a 32-byte strip, chunks of 584
    one("boundary_steps", [] { return layers(12, 512); }, kZen2),                // twelve layers, every one tiled
};

std::string listing(const Case& c) {
    const Graph g = c.build();
    std::string out;
    for (int k = 0; k < c.machines; ++k) {
        const std::unique_ptr<const std::expected<loops::Planned, loops::Refusal>> r(new auto(loops::plan_and_lower(g, *c.on[k].machine)));
        const std::expected<void, loops::Refusal> w = *r ? loops::well_formed((*r)->program) : std::unexpected(r->error());
        out += std::string{"-- "} + c.on[k].label + "\n" + (w ? loops::print((*r)->schedule, g) : std::string(w.error().view()) + "\n");
    }
    return out;
}

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
    for (const Case& c : kCases) {
        const std::string path = std::string{HEINZEL_LOOPS_PLAN_GOLDENS} + "/" + c.name + ".txt";
        const std::string got = listing(c);
        if (repin) {
            std::ofstream(path) << got;
        } else if (got != pinned(path)) {
            ++wrong;
            std::printf("%s: the schedule differs from %s:\n%s\n", c.name, path.c_str(), got.c_str());
        }
    }
    if (wrong != 0) {
        std::printf("%d schedules differ from their listings\n", wrong);
        return 1;
    }
    return 0;
}
