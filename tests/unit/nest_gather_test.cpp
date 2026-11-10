// Reads whose ADDRESS is a signal, on the loop-nest path, against the reference walk — bit for bit, at High and at
// Normal: at High the blend is computed one lane at a time and in double, exactly as the walk computes it, and rounded
// once; at Normal the tap and the blend are taken in float over every lane at once (Decision 78's float reads), and
// over these numbers, sixteenths and thirty-seconds, the float blend is exact, so the walk's number to the bit too.
//
//   delay line   a = x/2 ; y[t] = a[t - p[t]], p a signal in samples, clamped to [0, 5] and landing
//                between two samples: a Gather out of `a`'s buffer, which keeps six samples of silence
//                before its block (the far tap of the deepest read) — mono in lanes along time, and
//                over three voices in lanes across them (a ragged end: three of eight)
//   table        y[t] = table[frac(phase[t]) * 8], blended between two cells of an eight-cell table that
//                is itself a signal (a row of cells per sample), so time walks it and the phase indexes it —
//                and a table constant over time (one row), read in lanes along time: every lane reads the one
//                row (a review found the lanes stepping through it and past it). The walk has no sample of
//                that table past its first, so it is checked against the blend written out. A phase that is
//                no number (a NaN, an infinity) reads a NaN, on both runners, in lanes and not
//   ramps        y = dline(r1, p) + dline(r2, p) + r1[t - 2], r1 and r2 two ramps over three voices: a
//                ramp is storage filled once, that no stage makes, and keeps silence before it as deep
//                as its readers reach — as a stage's buffer does (a review found the delay lines reading
//                the other ramp's samples, and the lag refused by lowering after the planner said yes)
//
// Refused by name, planned as written: a delay line over an input (a feed keeps no silence, so compiling gives the line a
// copy that keeps it, `rewrite/copied_inputs.hpp`, which a graph planned as written lacks; `delay_lines_test` runs it).

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/sema/analyze.hpp"

#include "canaried.hpp"
#include "interpret.hpp"
#include "planned.hpp"
#include "tree_facts.hpp"

#include <cstddef>
#include <cstdio>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>

using namespace heinzel;

namespace {

namespace loops = heinzel::loops;

constexpr std::int32_t kBlock = 21;
constexpr std::int32_t kVoices = 3;
constexpr std::int32_t kReach = 5;
constexpr std::int32_t kCells = 8;

constexpr Graph analysed(Graph g) {
    if (!infer(g).ok) {
        fail("nest_gather_test: a graph did not resolve");
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

constexpr Expr delay_line(Expr signal, Expr position) {
    OpAttr attr{};
    attr.kind = AttrKind::SignalledDelay;
    attr.signalled_delay.max_delay = kReach;
    return two_operands(signal, position, op::DelayLine, attr);
}

constexpr Graph delayed_by_a_signal(bool bank, bool of_the_input, Precision level = Normal) {
    const Shape shape = bank ? Shape{kVoices, kBlock} : Shape{kBlock};
    Input x{"x", Signal{shape}};
    Input p{"p", Signal{shape}};
    return analysed(finish(level, Output{"y"} = delay_line(of_the_input ? x : x * 0.5, p)));
}

constexpr Graph table_read(bool constant, Precision level) {
    Input phase{"p", Signal{kBlock}};
    Input table{"x", Signal{kCells, constant ? 1 : kBlock}};
    OpAttr attr{};
    attr.kind = AttrKind::Table;
    attr.table.size = kCells;
    return analysed(finish(level, Output{"y"} = two_operands(phase, table, op::TableRead, attr) * 2.0));
}

constexpr Graph delayed_ramps(Precision level) {
    constexpr Index n{};
    Input p{"p", Signal{kVoices, kBlock}};
    const Expr first = iota(Signal{kVoices, kBlock});
    const Expr second = iota(Signal{kVoices, kBlock});
    return analysed(finish(level, Output{"y"} = delay_line(first, p) + delay_line(second, p) + first[n - 2]));
}

enum class Case { Mono, Bank, Table, ConstantTable, Ramps };
constexpr Graph graph_of(Case which, Precision level) {
    return which == Case::Table || which == Case::ConstantTable ? table_read(which == Case::ConstantTable, level)
           : which == Case::Ramps ? delayed_ramps(level)
                                  : delayed_by_a_signal(which == Case::Bank, false, level);
}
template <Case Which, Precision Level = Normal>
constexpr planned::Held kNestHeld{planned::program_of(graph_of(Which, Level), zen2)};
template <Case Which, Precision Level = Normal>
constexpr const loops::Program& kNest = kNestHeld<Which, Level>.program;

// Stage `s` walks time first, in lanes: a strip over it with a Vector inside.
constexpr bool in_lanes_along_time(const loops::Program& p, std::size_t s) {
    const auto w = tree_facts::walks(p, s);
    return w.size() >= 2 && loops::is_strip(w[0]) && w[0].axis == 0 && w[1].kind == loops::NodeKind::Vector && w[1].axis == 0;
}

static_assert(kNest<Case::Mono>.stages.size() == 2 && kNest<Case::Mono>.buffers[2].prefix == kReach + 1 && in_lanes_along_time(kNest<Case::Mono>, 1) &&
                  tree_facts::stmt(kNest<Case::Mono>, 1, 1).kind == loops::StmtKind::Gather &&
                  tree_facts::stmt(kNest<Case::Mono>, 1, 1).precision == Normal && tree_facts::stmt(kNest<Case::Mono, High>, 1, 1).precision == High,
              "the halved signal is stored with six samples of silence before it; the read runs in lanes along time, at the "
              "graph's level");
// Each ramp keeps silence as deep as its delay line reaches, and a sample more for the far tap.
constexpr bool ramps_keep_their_silence() {
    std::int32_t ramps = 0;
    for (const loops::Buffer& b : kNest<Case::Ramps>.buffers) {
        ramps += b.ramp && b.prefix == kReach + 1 ? 1 : 0;
    }
    return ramps == 2;
}
static_assert(ramps_keep_their_silence(), "both ramps keep six samples of silence before them");
// A phase just below a whole one: frac rounds to 1.0, and the taps must still lie in the table (the review of L12).
static_assert(table_tap(8, -1e-30) == TableTap{0, 1, 0.0} && table_tap(8, -0x1p-60) == TableTap{0, 1, 0.0} &&
                  table_tap(8, -0.0625) == TableTap{7, 0, 0.5},
              "a phase just below a whole one reads the table's first sample, in range");
// (time walked one sample at a time: a Loop of step 1 that is no strip)
static_assert(kNest<Case::Table>.stages.size() == 1 && tree_facts::walk(kNest<Case::Table>, 0, 0).kind == loops::NodeKind::Loop &&
                  tree_facts::walk(kNest<Case::Table>, 0, 0).step == 1 && !loops::is_strip(tree_facts::walk(kNest<Case::Table>, 0, 0)),
              "a table that is a row of cells per sample strides along time: no lanes there");
static_assert(kNest<Case::ConstantTable>.stages.size() == 1 && in_lanes_along_time(kNest<Case::ConstantTable>, 0),
              "a table constant over time stands still while the phase runs in lanes along time");

int g_wrong = 0;

// Positions from below zero to past the reach, most of them between two samples; phases of any sign.
std::vector<float> noise(std::size_t n, std::uint32_t seed, float scale) {
    std::vector<float> v(n);
    std::uint32_t state = seed;
    for (float& x : v) {
        state = state * 1664525u + 1013904223u;
        x = static_cast<float>(static_cast<std::int32_t>(state >> 8) % 129 - 16) * scale;
    }
    return v;
}

// Every fourth phase no number, in turn a NaN, an infinity and a negative one: each reads a NaN, on both runners.
void no_numbers_among(std::vector<float>& p) {
    constexpr float kNot[] = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity()};
    for (std::size_t t = 1; t < p.size(); t += 4) {
        p[t] = kNot[(t / 4) % 3];
    }
}

template <Case Which, Precision Level>
void check(const char* what, bool with_no_numbers = false) {
    static constexpr Graph g = graph_of(Which, Level);
    const std::size_t wide = Which == Case::Bank || Which == Case::Ramps ? kVoices : 1;
    constexpr bool table = Which == Case::Table || Which == Case::ConstantTable;
    const std::vector<float> x = noise(Which == Case::ConstantTable ? kCells : (table ? kCells : wide) * kBlock, 1, 0.0625f);
    std::vector<float> p = noise(wide * kBlock, 2, table ? 0.03125f : 0.0625f);
    if (with_no_numbers) {
        no_numbers_among(p);
    }
    std::vector<std::vector<double>> widened;
    Inputs feeds{};
    loops::Feeds nest_feeds{};
    for (std::size_t i = 0; i < g.inputs.size(); ++i) { // found by name: a graph's inputs stand in the order its arenas merged
        const std::vector<float>& from = g.inputs[i].name.data_[0] == 'x' ? x : p;
        widened.emplace_back(from.begin(), from.end());
        nest_feeds[i] = from.data();
    }
    for (std::size_t i = 0; i < g.inputs.size(); ++i) {
        feeds.push_back(InputFeed{widened[i].data(), elements_of(g.values[static_cast<std::size_t>(g.inputs[i].value)].shape).count});
    }
    const auto out = std::make_unique<Signals<kBlock>>();
    if constexpr (Which != Case::ConstantTable) {
        run(g, feeds, *out);
    }
    canaried::Workspace<kNest<Which, Level>> work(loops::Sizes{});
    canaried::run<kNest<Which, Level>>(nest_feeds, work);
    work.reset(); // twice, the second from silence too: without it, it would continue the first (Plan/archive/14-dsl/05)
    canaried::run<kNest<Which, Level>>(nest_feeds, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(kNest<Which, Level>.outputs[0]));
    for (std::size_t t = 0; t < kBlock; ++t) {
        for (std::size_t e = 0; e < wide; ++e) {
            const TableTap tap = table_tap(kCells, p[t]);
            const float want = Which == Case::ConstantTable
                                   ? static_cast<float>(2.0 * static_cast<float>(lerp2(x[static_cast<std::size_t>(tap.index)], x[static_cast<std::size_t>(tap.next)], tap.fraction)))
                                   : static_cast<float>(sample_at(*out, g.outputs[0].value, static_cast<std::int32_t>(e), t));
            const float got = y[t * wide + e];
            const bool a_number = p[t] - p[t] == 0.0f;
            const bool agrees = a_number ? got == want : got != got && want != want;
            if (!agrees && ++g_wrong <= 10) {
                std::printf("  %s at %s [sample %zu, element %zu]: got %a, want %a\n", what, Level == High ? "High" : "Normal", t, e,
                            static_cast<double>(got), static_cast<double>(want));
            }
        }
    }
}

bool refuses(const Graph& g) {
    const loops::Refusal held = planned::refusal_of(g, zen2);
    const std::string_view why = held.view();
    if (!why.empty()) {
        std::printf("  (refused: \"%.*s\")\n", static_cast<int>(why.size()), why.data());
    }
    return why.starts_with("heinzel::compile: ");
}

void check_refusals() {
    const Graph over_an_input = delayed_by_a_signal(false, true);
    if (!refuses(over_an_input) || loops::lowers(over_an_input, Machine{})) {
        ++g_wrong;
        std::printf("  a delay line over an input must be refused by name\n");
    }
}

template <Precision Level>
void check_all() {
    check<Case::Mono, Level>("delay line");
    check<Case::Bank, Level>("delay lines over voices");
    check<Case::Table, Level>("table");
    check<Case::ConstantTable, Level>("a table constant over time");
    check<Case::Table, Level>("table, phases no number among them", true);
    check<Case::ConstantTable, Level>("a table constant over time, phases no number among them", true);
    check<Case::Ramps, Level>("delay lines and a lag over ramps");
}

} // namespace

int main() {
    check_all<High>();
    check_all<Normal>();
    check_refusals();
    if (g_wrong != 0) {
        std::printf("nest_gather_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("nest_gather_test: reads whose address is a signal agree with the walk, bit for bit, at High and Normal\n");
    return 0;
}
