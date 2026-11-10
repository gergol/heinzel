// Adversarial review of the recurrence slice (V5): lags, feedback grouping, and the silence before
// the block, held against the reference walk (`tests/reference/interpret.hpp`) and against loops by hand.
//
//   PART A — what held up. Cross-coupled variables (a reads b's past while b reads a's), a cycle
//            whose lag is itself lagged, feedback through a second variable, rank-3 feedback, banks
//            narrower than one lane and one voice wide, a value whose past AND present are read
//            while it is also a named output, the same workspace run on over the feed's next block
//            at five symbolic widths, four planned schedules edited by hand that are refused by name, one that is
//            accepted and run, and one legal merge that is accepted. All of these are right, and are kept.
//
//   PART B — five findings, each a check that FAILED when the review was written (at `ca7870a7`) and
//            holds now: a value of no axes and an algebraic loop are refused by name, by `loops::lowers`
//            first; a cycle is found by what reads what (`loops/graph/feedback.hpp` `find_feedback`), so
//            where an expression was written no longer decides whether a graph lowers or how much of it
//            runs a sample at a time; and a long cycle is refused for what it is. (A sixth, time walked in
//            chunks refused though it keeps its order, is in part A: accepted now, and run.) As found:
//     B1  a RANK-0 value's past lowered, and the runner then indexed `element_strides(...)[rank - 1]`
//     B2  `y[n - 0]` of a variable defined later — an algebraic loop — passed the "does it lower" predicate
//     B3  one filter, two spellings: refused when a broadcast was WRITTEN inside the definition
//     B4  nodes that do not depend on a cycle were dragged into its serial stage (measured 4.3x)
//     B5  a cycle longer than a stage was refused as "anything but maps of one shape"

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/sema/analyze.hpp"

#include "canaried.hpp"
#include "interpret.hpp"
#include "planned.hpp"
#include "tree_facts.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace heinzel;

namespace {

namespace loops = heinzel::loops;

constexpr std::int32_t kVoices = 11;
constexpr std::int32_t kBlock = 24;
constexpr Index n{};

constexpr Graph analysed(Graph g) {
    if (!infer(g).ok) {
        fail("nest_recurrence_review_test: a graph did not resolve");
    }
    return g;
}

int g_wrong = 0;

void expect(const char* what, std::size_t at, float got, float want) {
    if (!(got == want) && ++g_wrong <= 12) {
        std::printf("  %s [%zu]: got %a, want %a\n", what, at, static_cast<double>(got), static_cast<double>(want));
    }
}

std::vector<float> noise(std::size_t n, std::uint32_t seed) {
    std::vector<float> v(n);
    std::uint32_t state = seed;
    for (float& x : v) {
        state = state * 1664525u + 1013904223u;
        x = static_cast<float>(static_cast<std::int32_t>(state >> 8) % 2001 - 1000) * 0.00173f;
    }
    return v;
}

// The reference walk over one block, for a graph of one wide input.
std::unique_ptr<Signals<kBlock>> walked(const Graph& g, const std::vector<float>& x, std::int32_t width) {
    const std::vector<double> wide(x.begin(), x.end());
    Inputs feeds{};
    feeds.push_back(InputFeed{wide.data(), width});
    auto out = std::make_unique<Signals<kBlock>>();
    run(g, feeds, *out);
    return out;
}

// ================================ PART A — what held up ==========================================
// a[t] = x[t] + 0.5*b[t-1] ; b[t] = x[t] + 0.25*a[t-1] — two cycles that enclose one another.
constexpr Graph cross_coupled() {
    Input x{"x", Signal{kVoices, kBlock}};
    Variable a;
    Variable b;
    Expr la = a[n - 1];
    Expr lb = b[n - 1];
    a[n] = x + 0.5 * lb;
    b[n] = x + 0.25 * la;
    return analysed(finish(Output{"a"} = a, Output{"b"} = b));
}

// y[t] = x[t] + 0.5*y[t-1] - 0.25*y[t-3], spelled as a lag OF the cycle's own lag.
constexpr Graph lagged_lag() {
    Input x{"x", Signal{kVoices, kBlock}};
    Variable y;
    Expr l1 = y[n - 1];
    y[n] = x + 0.5 * l1 - 0.25 * l1[n - 2];
    return analysed(finish(Output{"y"} = y));
}

// b[t] = 0.5*a[t-1] ; a[t] = x[t] + b[t] — the cycle closes THROUGH a second variable.
constexpr Graph through_a_second_variable() {
    Input x{"x", Signal{kVoices, kBlock}};
    Variable a;
    Variable b;
    b[n] = a[n - 1] * 0.5;
    a[n] = x + b;
    return analysed(finish(Output{"y"} = a));
}

template <int A, int B, int T>
constexpr Graph rank3_bank() {
    Input x{"x", Signal{A, B, T}};
    Variable y;
    y[n] = x + 0.5 * y[n - 1];
    return analysed(finish(Output{"y"} = y));
}

template <int V>
constexpr Graph bank_of() {
    Input x{"x", Signal{V, kBlock}};
    Variable y;
    y[n] = x + 0.5 * y[n - 1];
    return analysed(finish(Output{"y"} = y));
}

// The result's past (a lag of two) AND its present, read by a group after the cycle; both named.
constexpr Graph past_and_present() {
    Input x{"x", Signal{kVoices, kBlock}};
    Variable y;
    y[n] = x + 0.5 * y[n - 1];
    return analysed(finish(Output{"y"} = y, Output{"z"} = y + Expr(y)[n - 2]));
}

constexpr Graph symbolic_bank() { // the voices axis a runtime-resolved extent (DS-18: no declaration spells it)
    const DimDecl c{0, DimConstraints{1, 1}};
    Expr x = detail::input("x", Shape{Extent{Dim{c.id}}, kBlock}, ElemType::F32, c);
    Variable y;
    y[n] = x + 0.5 * y[n - 1];
    return analysed(finish(Output{"y"} = y));
}

constexpr Graph echo_bank() { // a lag of a finished buffer: no cycle, both stages in lanes
    Input x{"x", Signal{kVoices, kBlock}};
    Expr a = x * 2.0;
    return analysed(finish(Output{"y"} = a + a[n - 3]));
}
constexpr Graph gCross = cross_coupled();
constexpr planned::Held kCrossHeld{planned::program_of(gCross, zen2)};
constexpr const loops::Program& kCross = kCrossHeld.program;
constexpr Graph gLagged = lagged_lag();
constexpr planned::Held kLaggedHeld{planned::program_of(gLagged, zen2)};
constexpr const loops::Program& kLagged = kLaggedHeld.program;
constexpr Graph gSecond = through_a_second_variable();
constexpr planned::Held kSecondHeld{planned::program_of(gSecond, zen2)};
constexpr const loops::Program& kSecond = kSecondHeld.program;
constexpr Graph gRank3 = rank3_bank<3, 5, kBlock>();
constexpr planned::Held kRank3Held{planned::program_of(gRank3, zen2)};
constexpr const loops::Program& kRank3 = kRank3Held.program;
constexpr Graph gThin = bank_of<3>();
constexpr planned::Held kThinHeld{planned::program_of(gThin, zen2)};
constexpr const loops::Program& kThin = kThinHeld.program;
constexpr Graph gMono = bank_of<1>();
constexpr planned::Held kMonoHeld{planned::program_of(gMono, zen2)};
constexpr const loops::Program& kMono = kMonoHeld.program;
constexpr Graph gBoth = past_and_present();
constexpr planned::Held kBothHeld{planned::program_of(gBoth, zen2)};
constexpr const loops::Program& kBoth = kBothHeld.program;
constexpr Graph gSym = symbolic_bank();
constexpr planned::Held kSymHeld{planned::program_of(gSym, zen2)};
constexpr const loops::Program& kSym = kSymHeld.program;
constexpr Graph gEcho = echo_bank();

// The innermost Loop of stage `s` (the Unrolls and Vectors inside it aside).
constexpr loops::Node innermost_loop(const loops::Program& p, std::size_t s) {
    loops::Node last{};
    for (const loops::Node& w : tree_facts::walks(p, s)) {
        last = w.kind == loops::NodeKind::Loop ? w : last;
    }
    return last;
}

// What was decided, where it is worth pinning. (A bank whose voices fit a block: a strip over the voices, the State
// registers in it, and under them the time loop with the lanes inside — the registers' open before it.)
static_assert(kCross.stages.size() == 1 && kCross.buffers.size() == 3 && kCross.buffers[1].prefix == 1 && kCross.buffers[2].prefix == 1 &&
                  tree_facts::serial(innermost_loop(kCross, 0)) && innermost_loop(kCross, 0).axis == 1,
              "two cycles that enclose one another are ONE stage, each value keeping one sample of silence");
constexpr auto kRank3Walks = tree_facts::walks(kRank3, 0);
static_assert(kRank3Walks.size() == 4 && tree_facts::serial(kRank3Walks[0]) && kRank3Walks[0].axis == 2 && loops::is_strip(kRank3Walks[2]) &&
                  kRank3Walks[2].axis == 1 && kRank3Walks[3].kind == loops::NodeKind::Vector && kRank3Walks[3].axis == 1,
              "rank 3: time outermost and serial, the lanes on the axis memory is contiguous in (rank-2)");
constexpr auto kThinWalks = tree_facts::walks(kThin, 0);
static_assert(kThinWalks.size() == 4 && loops::is_strip(kThinWalks[0]) && tree_facts::is(kThinWalks[0], loops::NodeKind::Loop, 0, 8) &&
                  tree_facts::is(kThinWalks[1], loops::NodeKind::Vector, 0, 8) && tree_facts::serial(kThinWalks[2]) && kThinWalks[2].axis == 1 &&
                  tree_facts::is(kThinWalks[3], loops::NodeKind::Vector, 0, 8) && loops::node_at(kThin, kThinWalks[2].parent).kind == loops::NodeKind::Registers &&
                  loops::node_at(kThin, kThinWalks[2].parent).role == loops::Role::State,
              "a bank of three voices still asks for a lane of eight — the ragged end carries it — with time inside, the state in a register");
static_assert(kBoth.stages.size() == 1 && kBoth.buffers[1].prefix == 2 && kBoth.outputs[0] == 1,
              "one buffer holds the value the cycle makes, its past, and the named output; what reads it two back runs in the "
              "cycle's stage (the trailing decision)");

template <const loops::Program& P>
void against_the_walk(const char* what, const Graph& g, std::uint32_t seed, std::size_t outputs) {
    const std::vector<float> x = noise(kVoices * kBlock, seed);
    const auto out = walked(g, x, kVoices);
    canaried::Workspace<P> work(loops::Sizes{});
    canaried::run<P>(loops::Feeds{x.data()}, work);
    for (std::size_t o = 0; o < outputs; ++o) {
        const float* y = work.template data<float>(static_cast<std::size_t>(P.outputs[o]));
        for (std::size_t t = 0; t < kBlock; ++t) {
            for (std::size_t c = 0; c < kVoices; ++c) {
                expect(what, t * kVoices + c, y[t * kVoices + c],
                       static_cast<float>(sample_at(*out, g.outputs[o].value, static_cast<std::int32_t>(c), t)));
            }
        }
    }
}

template <const loops::Program& P, int W>
void check_a_bank(const char* what, std::uint32_t seed) {
    const std::vector<float> x = noise(static_cast<std::size_t>(W) * kBlock, seed);
    canaried::Workspace<P> work(loops::Sizes{});
    canaried::run<P>(loops::Feeds{x.data()}, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(P.outputs[0]));
    std::vector<float> before(W, 0.0f);
    for (std::size_t t = 0; t < kBlock; ++t) {
        for (std::size_t e = 0; e < static_cast<std::size_t>(W); ++e) {
            before[e] = x[t * W + e] + 0.5f * before[e];
            expect(what, t * W + e, y[t * W + e], before[e]);
        }
    }
}

// The past AND the present of one value, out of one buffer, both named outputs.
void check_past_and_present() {
    const std::vector<float> x = noise(kVoices * kBlock, 31);
    canaried::Workspace<kBoth> work(loops::Sizes{});
    canaried::run<kBoth>(loops::Feeds{x.data()}, work);
    const float* y = work.data<float>(static_cast<std::size_t>(kBoth.outputs[0]));
    const float* z = work.data<float>(static_cast<std::size_t>(kBoth.outputs[1]));
    std::vector<float> made(kVoices * kBlock, 0.0f);
    for (std::size_t t = 0; t < kBlock; ++t) {
        for (std::size_t c = 0; c < kVoices; ++c) {
            made[t * kVoices + c] = x[t * kVoices + c] + 0.5f * (t > 0 ? made[(t - 1) * kVoices + c] : 0.0f);
            expect("A past-and-present y", t * kVoices + c, y[t * kVoices + c], made[t * kVoices + c]);
            expect("A past-and-present z", t * kVoices + c, z[t * kVoices + c],
                   made[t * kVoices + c] + (t >= 2 ? made[(t - 2) * kVoices + c] : 0.0f));
        }
    }
}

// The same workspace, the feed's next block: the second run continues from where the first stopped
// (Plan/archive/14-dsl/05), the recurrence as if over both blocks — at five widths of the symbolic voices axis.
void check_the_prefix_carries() {
    for (const std::int64_t width : {1, 3, 8, 11, 17}) {
        const auto w = static_cast<std::size_t>(width);
        const std::vector<float> x = noise(2 * w * kBlock, 32); // two blocks, one run each
        canaried::Workspace<kSym> work(loops::Sizes{width});
        canaried::run<kSym>(loops::Feeds{x.data()}, work);
        const float* y = work.data<float>(static_cast<std::size_t>(kSym.outputs[0]));
        const std::vector<float> first(y, y + w * kBlock); // the first block's, before the second run writes there
        canaried::run<kSym>(loops::Feeds{x.data() + w * kBlock}, work);
        std::vector<float> before(w, 0.0f);
        for (std::size_t t = 0; t < 2 * kBlock; ++t) {
            for (std::size_t c = 0; c < w; ++c) {
                before[c] = x[t * w + c] + 0.5f * before[c];
                expect("A the prefix carries", t * w + c, t < kBlock ? first[t * w + c] : y[(t - kBlock) * w + c], before[c]);
            }
        }
    }
}

// ---- planned schedules edited by hand --------------------------------------------------------------

constexpr loops::Schedule with_the_stages_swapped(const loops::Schedule& d) {
    loops::Schedule t{};
    t.stages.push_back(d.stages[1]);
    t.stages.push_back(d.stages[0]);
    return t;
}

// The members of a stage, spelled once: a schedule stage's node list, never a Graph's.
using Members = decltype(loops::Schedule::Stage::nodes);

constexpr loops::Schedule with_the_stages_merged(const loops::Schedule& d) {
    loops::Schedule t{};
    loops::Schedule::Stage one = d.stages[0];
    Members members = one.nodes;
    for (const std::int32_t n : d.stages[1].nodes) {
        members.push_back(n);
    }
    one.nodes = members;
    one.loops = d.stages[1].loops;
    one.inner_from = d.stages[1].inner_from;
    one.parallel = -1; // the loops are the other stage's: the split the planner gave stage 0 is not theirs
    t.stages.push_back(one);
    return t;
}
// What a run pays for the staging: the statements of every stage that carries no lane at all.
constexpr std::size_t serial_statements(const loops::Program& p) {
    std::size_t n = 0;
    for (std::size_t s = 0; s < p.stages.size(); ++s) {
        n += tree_facts::count(p, s, loops::NodeKind::Vector) > 0 ? 0 : static_cast<std::size_t>(p.stages[s].count);
    }
    return n;
}

constexpr Graph gBank = bank_of<kVoices>();
constexpr loops::Schedule kBankSchedule = planned::schedule_of(gBank, zen2);
constexpr loops::Schedule kEchoSchedule = planned::schedule_of(gEcho, zen2);

constexpr loops::Schedule time_in_chunks() {
    loops::Schedule tiled = kBankSchedule;
    tiled.stages[0].loops.clear();
    tiled.stages[0].loops.push_back(loops::Schedule::Loop{.axis = 1, .step = 8, .kind = loops::LoopKind::Tile});
    tiled.stages[0].loops.push_back(loops::Schedule::Loop{.axis = 1, .step = 1, .kind = loops::LoopKind::Serial});
    tiled.stages[0].loops.push_back(loops::Schedule::Loop{.axis = 0, .step = 8, .kind = loops::LoopKind::Vector});
    tiled.stages[0].inner_from = 3;
    tiled.stages[0].parallel = -1; // (the split the planner gave was along the loop these replace)
    return tiled;
}
constexpr planned::Held kChunkedHeld{planned::program_of(gBank, time_in_chunks())};
constexpr const loops::Program& kChunked = kChunkedHeld.program;
constexpr planned::Held kPlannedBankHeld{planned::program_of(gBank, kBankSchedule)};
constexpr const loops::Program& kPlannedBank = kPlannedBankHeld.program;

// Time in chunks keeps its order, so it is ACCEPTED (the review found it refused: over-strict) — and runs right,
// the state read back out of the result at every chunk's first sample.
void check_time_in_chunks() {
    const std::vector<float> x = noise(static_cast<std::size_t>(kVoices * kBlock), 41);
    canaried::Workspace<kChunked> one(loops::Sizes{});
    canaried::Workspace<kPlannedBank> two(loops::Sizes{});
    canaried::run<kChunked>(loops::Feeds{x.data()}, one);
    canaried::run<kPlannedBank>(loops::Feeds{x.data()}, two);
    const float* chunked = one.data<float>(static_cast<std::size_t>(kChunked.outputs[0]));
    const float* planned = two.data<float>(static_cast<std::size_t>(kPlannedBank.outputs[0]));
    for (std::size_t k = 0; k < x.size(); ++k) {
        expect("A time in chunks", k, chunked[k], planned[k]);
    }
}

// A schedule that breaks the feedback rule is refused by name — four of them; the fifth edit, time in chunks, above.
constexpr loops::Schedule time_unrolled() {
    loops::Schedule d = kBankSchedule;
    d.stages[0].loops[1] = loops::Schedule::Loop{.axis = 1, .step = 4, .kind = loops::LoopKind::Unroll}; // (time is the inner loop of a bank this narrow)
    d.stages[0].inner_from = 2;                                                                        // (unrolled, it is none)
    return d;
}
static_assert(planned::refusal_of(gBank, time_unrolled()) == "heinzel::compile: a stage that reads its own past plans time unrolled",
              "A time unrolled into four bodies");

constexpr loops::Schedule definition_first() { // the definition scheduled before the lag that reads its past
    loops::Schedule d = kBankSchedule;
    Members order{};
    order.push_back(3);
    order.push_back(0);
    order.push_back(1);
    order.push_back(2);
    d.stages[0].nodes = order;
    d.stages[0].inner_from = static_cast<std::int32_t>(d.stages[0].loops.size()); // (it hands no state on: no inner loops)
    return d;
}
static_assert(planned::refusal_of(gBank, definition_first()) ==
                  "heinzel::compile: a node reads a value its group has not made yet — the schedule's order is not the graph's",
              "A the definition scheduled before its lag");

constexpr loops::Schedule lag_split_off() { // the lag in a stage of its own, running before the stage that defines it
    loops::Schedule split{};
    loops::Schedule::Stage lag{};
    loops::Schedule::Stage rest{};
    Members only_the_lag{};
    Members all_the_rest{};
    only_the_lag.push_back(0);
    all_the_rest.push_back(1);
    all_the_rest.push_back(2);
    all_the_rest.push_back(3);
    lag.nodes = only_the_lag;
    rest.nodes = all_the_rest;
    lag.loops = rest.loops = kBankSchedule.stages[0].loops;
    lag.inner_from = rest.inner_from = static_cast<std::int32_t>(lag.loops.size()); // (neither hands state on: no inner loops)
    split.stages.push_back(lag);
    split.stages.push_back(rest);
    return split;
}
static_assert(planned::refusal_of(gBank, lag_split_off()) ==
                  "heinzel::compile: the past of a value that is neither fed, a ramp, nor made by a group has no address",
              "A the lag split into an earlier stage");

static_assert(planned::refusal_of(gEcho, with_the_stages_swapped(kEchoSchedule)) ==
                  "heinzel::compile: the past of a value that is neither fed, a ramp, nor made by a group has no address",
              "A a lag's stage moved before its source's");

// A schedule a hand may legally write: the echo's producer and its lag in ONE stage. The store of
// a[t] precedes the load of a[t-3] in the same body, three samples apart, so it is sound — and the
// numbers say so.
constexpr planned::Held kMergedEchoHeld{planned::program_of(gEcho, with_the_stages_merged(kEchoSchedule))};
constexpr const loops::Program& kMergedEcho = kMergedEchoHeld.program;

void check_the_merged_echo() {
    const std::vector<float> x = noise(kVoices * kBlock, 34);
    canaried::Workspace<kMergedEcho> work(loops::Sizes{});
    canaried::run<kMergedEcho>(loops::Feeds{x.data()}, work);
    const float* y = work.data<float>(static_cast<std::size_t>(kMergedEcho.outputs[0]));
    for (std::size_t t = 0; t < kBlock; ++t) {
        for (std::size_t c = 0; c < kVoices; ++c) {
            expect("A merged echo", t * kVoices + c, y[t * kVoices + c],
                   x[t * kVoices + c] * 2.0f + (t >= 3 ? x[(t - 3) * kVoices + c] * 2.0f : 0.0f));
        }
    }
}

// ================================ PART B — the findings ==========================================
// ---- B1: a rank-0 value's past ------------------------------------------------------------------
constexpr Graph scalar_feedback() {
    // a value of no axes whose past is read, which no declaration spells (DS-18)
    Expr x = detail::input("x", Shape{}, ElemType::F32);
    Variable y;
    y[n] = x + 0.5 * y[n - 1];
    return analysed(finish(Output{"y"} = y));
}
// A rank-0 value has no streamed axis to count silence along: its past is refused by name, and `loops::lowers`
// says no (the runner would read element_strides(...)[-1]).
static_assert(!loops::lowers(scalar_feedback(), zen2) &&
                  planned::refusal_of(scalar_feedback(), zen2) == "heinzel::compile: a value of no axes has no time to be read back along",
              "B1 rank-0 feedback is refused by name");

// ---- B2: an algebraic loop does not lower past `loops::lowers` ------------------------------------
constexpr Graph algebraic_loop() {
    Input x{"x", Signal{kVoices, kBlock}};
    Variable y;
    y[n] = x + 0.5 * y[n - 0];
    return analysed(finish(Output{"y"} = y));
}
// Refused by name, for what it is — not a hard failure of the lowering (`loops::lowers` is that same answer since M4).
static_assert(planned::refusal_of(algebraic_loop(), zen2) == "heinzel::compile: a value defined in terms of its own PRESENT is an algebraic loop",
              "B2 y[t] = x[t] + 0.5*y[t] is an algebraic loop, refused for what it is");

// ---- B3: the same filter, two spellings ----------------------------------------------------------
constexpr Graph broadcast_inside_the_definition() {
    Input x{"x", Signal{kVoices, kBlock}};
    Input b{"b", Signal{1, kBlock}};
    Variable y;
    Expr lag = y[n - 1];
    y[n] = x + 0.5 * lag + b * 2.0;
    return analysed(finish(Output{"y"} = y));
}
constexpr Graph broadcast_before_the_variable() {
    Input x{"x", Signal{kVoices, kBlock}};
    Input b{"b", Signal{1, kBlock}};
    Expr sum = x + b * 2.0;
    Variable y;
    y[n] = sum + 0.5 * y[n - 1];
    return analysed(finish(Output{"y"} = y));
}
// Both are the same graph; node order is evaluation order (graph_builder.hpp) — a compiler's choice — so a cycle is
// found by what reads what, not by a node index range.
static_assert(planned::refusal_of(broadcast_inside_the_definition(), zen2).empty() ==
                  planned::refusal_of(broadcast_before_the_variable(), zen2).empty(),
              "B3 one filter, two spellings: where the broadcast is written does not decide whether it lowers");

// ---- B4: the serial stage swallows what does not belong to the cycle -----------------------------
constexpr std::int32_t kLong = 4096;
constexpr int kTerms = 10;

constexpr Graph terms_after_the_lag() {
    Input x{"x", Signal{kLong}};
    Input z{"z", Signal{kLong}};
    Variable y;
    Expr acc = 0.5 * y[n - 1];
    for (int i = 0; i < kTerms; ++i) {
        acc = acc + sqrt(z * (0.25 + 0.5 * i));
    }
    y[n] = x + acc;
    return analysed(finish(Output{"y"} = y));
}
constexpr Graph terms_before_the_lag() {
    Input x{"x", Signal{kLong}};
    Input z{"z", Signal{kLong}};
    Expr sum = x;
    for (int i = 0; i < kTerms; ++i) {
        sum = sum + sqrt(z * (0.25 + 0.5 * i));
    }
    Variable y;
    y[n] = sum + 0.5 * y[n - 1];
    return analysed(finish(Output{"y"} = y));
}
constexpr Graph gAfter = terms_after_the_lag();
constexpr planned::Held kAfterHeld{planned::program_of(gAfter, zen2)};
constexpr const loops::Program& kAfter = kAfterHeld.program;
constexpr Graph gBefore = terms_before_the_lag();
constexpr planned::Held kBeforeHeld{planned::program_of(gBefore, zen2)};
constexpr const loops::Program& kBefore = kBeforeHeld.program;

void check_only_the_cycle_is_serial() {
    const std::size_t after = serial_statements(kAfter);
    const std::size_t before = serial_statements(kBefore);
    // Both feeds non-negative: a square root wants one, and which named input is which depends on the
    // arena order, which is the very thing under review.
    std::vector<float> x = noise(kLong, 35);
    std::vector<float> z = noise(kLong, 36);
    for (std::size_t i = 0; i < kLong; ++i) {
        x[i] = x[i] < 0.0f ? -x[i] : x[i];
        z[i] = z[i] < 0.0f ? -z[i] : z[i];
    }
    float last[2]{};
    canaried::Workspace<kAfter> one(loops::Sizes{});
    canaried::Workspace<kBefore> two(loops::Sizes{});
    canaried::run<kAfter>(loops::Feeds{x.data(), z.data()}, one);
    canaried::run<kBefore>(loops::Feeds{x.data(), z.data()}, two);
    last[0] = one.data<float>(static_cast<std::size_t>(kAfter.outputs[0]))[kLong - 1];
    last[1] = two.data<float>(static_cast<std::size_t>(kBefore.outputs[0]))[kLong - 1];
    expect("B4 the two spellings agree", 0, last[0], last[1]);
    // Written onto the lag, the ten ADDITIONS chain from it and are the cycle's — a Load of a root and an
    // addition each; the ten roots themselves (and their products) depend on nothing and are not.
    if (after == before + 2 * kTerms) {
        return;
    }
    ++g_wrong;
    std::printf("  FAILING (B4) the same mono pole plus ten unrelated square roots: %zu statements run\n"
                "              one sample at a time when they are written onto the lag, %zu when they are\n"
                "              written before the variable (same answers: %a vs %a). Measured 4.3x.\n"
                "              Only the nodes that REACH the lag belong in the serial stage.\n",
                after, before, static_cast<double>(last[0]), static_cast<double>(last[1]));
}

// ---- B5: a long cycle is refused for the wrong reason ---------------------------------------------
Graph a_cycle_of(int maps) {
    Input x{"x", Signal{8, 64}};
    Variable y;
    Expr acc = y[n - 1] * 0.5;
    for (int i = 0; i < maps; ++i) {
        acc = acc + 0.0;
    }
    y[n] = x + acc;
    return analysed(finish(Output{"y"} = y));
}

// (`graph::kMaxGroupStmts` is the planner's limit on one stage's graph nodes, which a cycle keeps.)
void check_the_long_cycle_is_named() {
    const Graph fits = a_cycle_of(static_cast<int>(loops::graph::kMaxGroupStmts) - 4); // the limit's nodes exactly
    const Graph over = a_cycle_of(static_cast<int>(loops::graph::kMaxGroupStmts) - 3); // one more
    const std::string_view why = loops::graph::find_feedback(over, loops::graph::facts_of(over)).why_not;
    if (!loops::graph::find_feedback(fits, loops::graph::facts_of(fits)).why_not.empty()) {
        ++g_wrong;
        std::printf("  (B5) a cycle of exactly the stage's limit of nodes was expected to fit and did not\n");
    }
    if (why.find("shape") == std::string_view::npos) {
        return;
    }
    ++g_wrong;
    std::printf("  FAILING (B5) a cycle of %zu nodes fits; %zu — every one a map of the SAME shape — is\n"
                "              refused as \"%.*s\":\n"
                "              the real reason is `++held > kMaxGroupStmts` (loops/graph/feedback.hpp), which the\n"
                "              message does not say. Give the count its own message.\n",
                fits.nodes.size(), over.nodes.size(), static_cast<int>(why.size()), why.data());
}

} // namespace

int main() {
    // ---- part A ----
    against_the_walk<kCross>("A cross-coupled", gCross, 25, 2);
    against_the_walk<kLagged>("A a lag of the cycle's lag", gLagged, 26, 1);
    against_the_walk<kSecond>("A through a second variable", gSecond, 27, 1);
    check_a_bank<kRank3, 15>("A rank-3 feedback", 28);
    check_a_bank<kThin, 3>("A three voices", 29);
    check_a_bank<kMono, 1>("A one voice", 30);
    check_past_and_present();
    check_the_prefix_carries();
    check_time_in_chunks();
    check_the_merged_echo();

    // ---- part B (B1-B3 are the static_asserts above) ----
    check_only_the_cycle_is_serial();
    check_the_long_cycle_is_named();

    if (g_wrong != 0) {
        std::printf("nest_recurrence_review_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("nest_recurrence_review_test: every finding is fixed\n");
    return 0;
}
