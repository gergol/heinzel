// The two table reads (Plan/28-dsl-proposals/05 D3): `wavetable(table, phase)` reads N cells as one cycle of a wave,
// the phase in turns and wrapped, and `lookup(table, x, lo, hi)` reads N cells as a function at N points from lo to hi,
// x clamped. Each spelling builds its graph, node for node: a `_tblread` of the table's N cells, after a clamp of x's
// place for `lookup`. On `Executor` at High, Normal and Fast, over F32 and F64, on the default machine and zen2,
// against the walk: at High and over F64 every number bit for bit (both share `table_tap` and the blend, and the
// clamp's arithmetic is exact or rounded once); an F32 read at Normal and Fast takes its tap and blend in float, so
// within Decision 78's bound of the walk (`float_reads.hpp`), over an F64 table in double and the walk's to the bit, a
// NaN exactly where the walk's is: a wavetable over a constant table and over an input, its phases sweeping, negative,
// past a whole turn, just below one, the floats beside 1 and beside every cell's place, a NaN and the infinities, each
// of which reads a NaN; lookup at its edges, x = lo, x = hi, below and above, the floats beside every cell's x, the
// infinities and a NaN, over a table of 64 cells (the last cell's place exact) and one of 100 (never the first at the
// top). The float tap's cell inside its table, alone and in lanes. A lookup inside feedback, a saturated one-pole, runs
// as the walk reads it at High, one voice and three. A wavetable oscillator run in blocks gives one run's numbers, at
// Normal on both machines. The proof of ranges: a read of a constant table is in its cells' hull, lookup's phase in [0,
// (N - 1)/N]. Refused by name, at run time: lookup's bounds, span and scale a double does not hold, a table of one
// cell. A computed table, lo at or above hi, an infinite bound and one cell are the build_refuses_table_* fixtures.
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/ranges/proven.hpp"
#include "heinzel/sema/analyze.hpp"

#include "executed.hpp"
#include "float_reads.hpp"
#include "interpret.hpp"
#include "ranged_graphs.hpp"

using namespace heinzel;

namespace {

constexpr Index n{};
constexpr std::size_t kPoints = 1024;
constexpr std::int32_t kWaveCells = 64;
constexpr std::int32_t kOddCells = 100;
constexpr double kLo = -1.0;
constexpr double kHi = 3.0;

// Numbers a float holds, none repeating soon; the first cell of the odd table far from the others, so a top read that
// blended it in would show.
template <std::size_t N>
constexpr std::array<float, N> cells(float scale, float first) {
    std::array<float, N> v{};
    for (std::size_t i = 0; i < N; ++i) {
        v[i] = scale * static_cast<float>(static_cast<std::int32_t>((i * 37 + 11) % 61) - 30);
    }
    v[0] = first;
    return v;
}
constexpr std::array<float, kWaveCells> kWave = cells<kWaveCells>(0.03125f, 0.5f);
constexpr std::array<float, kOddCells> kOdd = cells<kOddCells>(0.0625f, 1000.0f);

// ---- the spellings build their graphs ---------------------------------------------------------------------------

constexpr Expr read_by_hand(Expr phase, Expr table, std::int32_t cells) {
    Arena* arena = bring_together(phase, table);
    BuildNode read{};
    read.op = op::TableRead;
    read.inputs[0] = phase.position();
    read.inputs[1] = table.position();
    read.attr.kind = AttrKind::Table;
    read.attr.table.size = cells;
    arena->nodes.push_back(read);
    return Expr{arena, static_cast<std::int32_t>(arena->nodes.size() - 1)};
}

template <bool Spelled>
constexpr Graph spelled_or_built() {
    Input p{"p", Signal{8}};
    Input x{"x", Signal{8}};
    Input t{"t", {kOddCells}};
    Output w{"w"};
    Output k{"k"};
    if constexpr (Spelled) {
        w = wavetable(t, p);
        k = lookup(constant(kWave), x, kLo, kHi);
    } else {
        w = read_by_hand(p, t, kOddCells);
        Expr table = constant(kWave);
        constexpr double kLast = 63.0 / 64.0; // a power of two of cells: the last cell's place is exact
        Expr along = (x - kLo) * (kLast / (kHi - kLo));
        Expr phase = min(kLast, max(0.0, along));
        k = read_by_hand(phase, table, kWaveCells);
    }
    return finish(w, k);
}
static_assert(spelled_or_built<true>() == spelled_or_built<false>(), "wavetable and lookup build their graphs");

// ---- on Executor, as the walk reads ---------------------------------------------------------------------------------

enum class Read { WaveOfAConstant, WaveOfAnInput, Lookup, LookupOdd };

// One read at a level, its ports `p` (a phase, or lookup's x) and, for an input table, `t` of type T.
template <Read R, ElemType T, Precision Level, ElemType Table = T>
constexpr Graph one_at() {
    Input p{"p", Signal{kPoints}, T};
    Output y{"y"};
    if constexpr (R == Read::WaveOfAnInput) {
        Input t{"t", {kWaveCells}, Table};
        y = wavetable(t, p);
    } else if constexpr (R == Read::WaveOfAConstant) {
        y = wavetable(constant(kWave), p);
    } else if constexpr (R == Read::Lookup) {
        y = lookup(constant(kWave), p, kLo, kHi);
    } else {
        y = lookup(constant(kOdd), p, kLo, kHi);
    }
    return finish(Level, y);
}

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

// The floats beside `x`, one step each way.
void beside(std::vector<double>& p, float x) {
    p.push_back(static_cast<double>(std::nextafter(x, -std::numeric_limits<float>::infinity())));
    p.push_back(static_cast<double>(std::nextafter(x, std::numeric_limits<float>::infinity())));
}

// The points: the edges first (a NaN, the infinities; a phase at, just below and past a whole turn, negative, large;
// lookup's lo, hi and past them), the floats beside every cell's place (a phase k/64 and k/64 - 1; lookup's x), then a
// dense sweep.
std::vector<double> points(Read r) {
    std::vector<double> p{kNaN, kInf, -kInf, -kNaN};
    if (r == Read::Lookup || r == Read::LookupOdd) {
        p.insert(p.end(), {kLo, kHi, kLo - 0.5, kHi + 0.5, -1e30, 1e30, kLo + 1e-7, kHi - 1e-7, 1.0, 0.0});
        const std::int32_t cells = r == Read::Lookup ? kWaveCells : kOddCells;
        for (std::int32_t k = 0; k < cells; ++k) {
            beside(p, static_cast<float>(kLo + (kHi - kLo) * k / (cells - 1)));
        }
        while (p.size() < kPoints) {
            p.push_back(kLo - 0.5 + (kHi - kLo + 1.0) * static_cast<double>(p.size()) / kPoints);
        }
        return p;
    }
    p.insert(p.end(), {0.0, -0.0, 1.0, -1.0, 0.5, 1.5, -0.25, 0x1p-30, -1e-30, 1.0 - 0x1p-24, 0x1p30 + 0.5, -1e6 - 0.125});
    for (std::int32_t k = 0; k <= kWaveCells; ++k) {
        beside(p, static_cast<float>(k) / kWaveCells);
        beside(p, static_cast<float>(k) / kWaveCells - 1.0f);
    }
    while (p.size() < kPoints) {
        p.push_back(-3.0 + 6.0 * static_cast<double>(p.size()) / kPoints);
    }
    return p;
}

// What the walk reads: each number, and the phase the read was given.
struct Walked {
    std::vector<double> out = std::vector<double>(kPoints);
    std::vector<double> phase = std::vector<double>(kPoints);
};

// The walk of the graph `Build` makes, analysed at its own level, fed `p` (and `t`).
template <auto Build>
Walked walked(const std::vector<double>& p, const std::vector<double>& t) {
    Graph g = Build();
    if (!infer(g).ok) {
        fail("tables_test: a graph did not analyse");
    }
    Inputs feeds{};
    for (const NamedValue& in : g.inputs) {
        const bool table = in.name.view() == "t";
        feeds.push_back(table ? InputFeed{t.data(), kWaveCells} : InputFeed{p.data(), 1});
    }
    const auto signals = std::make_unique<Signals<kPoints, 256>>(); // a plane per number: the table's cells too
    run(g, feeds, *signals);
    std::int32_t phase = kNoValue;
    for (const Node& node : g.nodes) {
        phase = node.op == op::TableRead ? node.inputs[0] : phase;
    }
    Walked w;
    for (std::size_t s = 0; s < kPoints; ++s) {
        w.out[s] = sample_at(*signals, g.outputs[0].value, 0, s);
        w.phase[s] = sample_at(*signals, phase, 0, s);
    }
    return w;
}

template <class Number>
bool same(Number a, Number b) {
    using Bits = std::conditional_t<sizeof(Number) == 4, std::uint32_t, std::uint64_t>;
    return std::bit_cast<Bits>(a) == std::bit_cast<Bits>(b) || (a != a && b != b);
}

constexpr const char* level_name(Precision l) { return l == High ? "High" : l == Normal ? "Normal" : "Fast"; }
constexpr const char* read_name(Read r) {
    return r == Read::WaveOfAConstant ? "a wavetable of a constant" : r == Read::WaveOfAnInput ? "a wavetable of an input"
           : r == Read::Lookup                                       ? "a lookup of 64 cells"
                                                                     : "a lookup of 100 cells";
}

// One read at one level, in one type, on one machine: Executor's numbers against the walk's, every one; a point that
// is no number reads a NaN. Bit for bit, but for an F32 read of F32 cells at Normal and Fast, which is held to the float
// writing's bound around the phase the walk read (over an F64 table it computes in double, as a map would).
template <Read R, ElemType T, Precision Level, const Machine& M, ElemType Table = T>
bool agrees() {
    using Number = std::conditional_t<T == ElemType::F32, float, double>;
    using Cell = std::conditional_t<Table == ElemType::F32, float, double>;
    constexpr bool in_float = T == ElemType::F32 && Table == ElemType::F32 && Level != High;
    const std::vector<double> wide = points(R);
    const std::vector<Number> p(wide.begin(), wide.end());
    const std::vector<Cell> t(kWave.begin(), kWave.end());
    const std::vector<double> fed_p(p.begin(), p.end());
    std::vector<double> fed_t; // the walk reads a tensor input at every sample
    for (std::size_t s = 0; s < kPoints; ++s) {
        fed_t.insert(fed_t.end(), t.begin(), t.end());
    }
    std::vector<Number> y(kPoints);
    Executor<one_at<R, T, Level, Table>, M> ex;
    ex.input("p") = p.data();
    if constexpr (R == Read::WaveOfAnInput) {
        ex.input("t") = t.data();
    }
    ex.output("y") = y.data();
    ex.run();
    const Walked walk = walked<one_at<R, T, Level, Table>>(fed_p, fed_t);
    const float* cells = R == Read::LookupOdd ? kOdd.data() : kWave.data();
    const std::int32_t n_cells = R == Read::LookupOdd ? kOddCells : kWaveCells;
    std::size_t differ = 0;
    std::size_t past = 0;
    std::size_t nans = 0;
    double worst = 0.0; // the largest difference, as a share of its bound
    for (std::size_t i = 0; i < kPoints; ++i) {
        const auto want = static_cast<Number>(walk.out[i]);
        const double bound = testing::table_bound(cells, n_cells, walk.phase[i]);
        differ += same(y[i], want) ? 0 : 1;
        past += !in_float || testing::within(y[i], want, bound) ? 0 : 1;
        const double apart = std::fabs(static_cast<double>(y[i]) - static_cast<double>(want));
        worst = same(y[i], want) ? worst : std::max(worst, apart / bound);
        const bool wave = R == Read::WaveOfAConstant || R == Read::WaveOfAnInput;
        const bool a_number = p[i] - p[i] == 0 || (!wave && p[i] == p[i]);
        nans += (y[i] != y[i]) == !a_number ? 0 : 1;
    }
    std::printf("tables: %s, %s at %s on %s: %zu of %zu numbers differ from the walk (at most %.2f of the float bound), %zu "
                "past it, %zu NaN misplaced\n",
                read_name(R), T == Table ? (T == ElemType::F32 ? "f32" : "f64") : "f32 over f64 cells", level_name(Level),
                &M == &zen2 ? "zen2" : "the default machine",
                differ, kPoints, worst, past, nans);
    return (in_float ? past == 0 : differ == 0) && nans == 0;
}

template <Read R>
bool agrees_everywhere() {
    bool ok = agrees<R, ElemType::F32, High, default_machine>() && agrees<R, ElemType::F32, High, zen2>();
    ok = agrees<R, ElemType::F32, Normal, default_machine>() && agrees<R, ElemType::F32, Normal, zen2>() && ok;
    ok = agrees<R, ElemType::F32, Fast, default_machine>() && agrees<R, ElemType::F32, Fast, zen2>() && ok;
    ok = agrees<R, ElemType::F64, High, default_machine>() && agrees<R, ElemType::F64, High, zen2>() && ok;
    ok = agrees<R, ElemType::F64, Normal, default_machine>() && agrees<R, ElemType::F64, Normal, zen2>() && ok;
    return agrees<R, ElemType::F64, Fast, default_machine>() && agrees<R, ElemType::F64, Fast, zen2>() && ok;
}

// An F32 phase over an F64 input table: read in double at every level, the walk's number to the bit.
bool double_cells_agree() {
    constexpr Read kInput = Read::WaveOfAnInput;
    bool ok = agrees<kInput, ElemType::F32, Normal, default_machine, ElemType::F64>();
    ok = agrees<kInput, ElemType::F32, Normal, zen2, ElemType::F64>() && ok;
    return agrees<kInput, ElemType::F32, Fast, zen2, ElemType::F64>() && ok;
}

// ---- lookup's edges ---------------------------------------------------------------------------------------------------

// `lookup` at its edges, f32 at Normal and at High (the points' first ten: a NaN, +inf, -inf, -NaN, lo, hi, below lo,
// above hi, -1e30, 1e30): the first cell at and below lo, the last at and above hi, a NaN at a NaN. Over 64 cells the
// last cell's place, 63/64, is exact, and so is x = hi's phase (hi - lo = 4): the last cell itself. Over 100 it is 99/100
// rounded down to a float, so the top reads just short of the last cell, never past it into the first (1000).
template <Read R, Precision Level>
bool edges() {
    const std::vector<double> wide = points(R);
    const std::vector<float> x(wide.begin(), wide.end());
    std::vector<float> y(kPoints);
    Executor<one_at<R, ElemType::F32, Level>, zen2> ex;
    ex.input("p") = x.data();
    ex.output("y") = y.data();
    ex.run();
    const float first = R == Read::Lookup ? kWave[0] : kOdd[0];
    const float last = R == Read::Lookup ? kWave[kWaveCells - 1] : kOdd[kOddCells - 1];
    const float before = R == Read::Lookup ? kWave[kWaveCells - 2] : kOdd[kOddCells - 2];
    // Within (N - 1) 2^-24 of a cell before the last: a float phase's last steps.
    const auto at_the_top = [&](float v) {
        return R == Read::Lookup ? v == last : std::fabs(v - last) <= 1e-5f * std::fabs(last - before);
    };
    const bool below = y[2] == first && y[4] == first && y[6] == first && y[8] == first;
    const bool above = at_the_top(y[1]) && at_the_top(y[5]) && at_the_top(y[7]) && at_the_top(y[9]);
    const bool nans = y[0] != y[0] && y[3] != y[3];
    std::printf("tables: %s at %s: x <= lo %a, x = hi %a and x >= hi %a (the last cell %a), a NaN %a\n", read_name(R),
                level_name(Level), static_cast<double>(y[4]), static_cast<double>(y[5]), static_cast<double>(y[7]),
                static_cast<double>(last), static_cast<double>(y[0]));
    return below && above && nans;
}

// ---- a table read inside feedback ----------------------------------------------------------------------------------

// A one-pole whose input passes through a `lookup` saturator, as JUCE's ladder's does, its own past among what the
// lookup reads: the read runs inside the cycle, a map of its phase, the table in storage before it. Over three voices
// too, in lanes across them.
template <std::int32_t Voices>
constexpr Graph saturated_pole() {
    Input x{"x", Voices == 1 ? Signal{kPoints} : Signal{Voices, kPoints}};
    Variable s;
    Expr drive = x - 0.9 * s[n - 1];
    Expr warm = lookup(constant(kWave), drive, kLo, kHi);
    s[n] = 0.5 * warm + 0.3 * s[n - 1];
    Output y{"y"};
    y = s;
    return y;
}

// At High, on the default machine and zen2, against the walk bit for bit (`executed` exits on the first difference).
bool feedback_agrees() {
    std::vector<double> x(3 * kPoints);
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] = static_cast<double>(static_cast<float>(-3.0 + 6.0 * static_cast<double>((i * 37) % 1001) / 1000.0));
    }
    testing::executed<saturated_pole<1>>({{"x", x.data()}});
    testing::executed<saturated_pole<1>, 0, zen2>({{"x", x.data()}});
    testing::executed<saturated_pole<3>>({{"x", x.data(), 3}});
    const testing::Outputs bank = testing::executed<saturated_pole<3>, 0, zen2>({{"x", x.data(), 3}});
    return bank[0] != std::vector<double>(bank[0].size());
}

// ---- blocks equal one run ---------------------------------------------------------------------------------------------

// An oscillator reading the wave, its phase a recurrence driven by its step.
constexpr Graph oscillator() {
    Input step{"step", Signal{Length{}}};
    Variable phase;
    phase[n] = frac(phase[n - 1] + step);
    Output y{"y"};
    y = wavetable(constant(kWave), phase);
    return y;
}

// Blocks of 8, 16, 12 and 4 samples, nothing reset between them: the numbers of one run over the 40, at Normal (the
// graph's level), on machine `M`.
template <const Machine& M>
bool blocks_equal_one_run() {
    std::vector<float> step(40);
    for (std::size_t i = 0; i < step.size(); ++i) {
        step[i] = 0.0371f + 0.001f * static_cast<float>(i % 7);
    }
    std::vector<float> whole(40);
    std::vector<float> blocks(40);
    Executor<oscillator, M> once;
    once.input("step") = step.data();
    once.output("y") = whole.data();
    once.prepare(40);
    once.run(40);
    Executor<oscillator, M> in_blocks;
    in_blocks.prepare(16);
    std::size_t at = 0;
    for (const std::int64_t frames : {8, 16, 12, 4}) {
        in_blocks.input("step") = step.data() + at;
        in_blocks.output("y") = blocks.data() + at;
        in_blocks.run(frames);
        at += static_cast<std::size_t>(frames);
    }
    return blocks == whole && whole != std::vector<float>(40);
}

// ---- the float tap stays in its table ----------------------------------------------------------------------------------

// The float writing's tap (`loops::table_tap_in_float`), of one number and of eight lanes, where its place rounds:
// beside every cell's place (or a few, past 2,048 cells) and beside a whole turn, tiny and huge, and no number. Its
// cell a whole number in [0, N - 1], so no read leaves the table, its fraction in [0, 1), a NaN where the phase is
// none; every lane the number's, as a stage's tail, run a sample at a time, needs.
bool float_taps_inside() {
    constexpr std::int32_t kSizes[] = {1, 2, 3, 7, 64, 100, 2048, (1 << 24) - 1, (1 << 24) + 1, (1 << 24) + 3, 1 << 30};
    constexpr float kTiny = 0x1p-149f;
    std::size_t wrong = 0;
    std::size_t taps = 0;
    for (const std::int32_t cells : kSizes) {
        std::vector<double> phases{kNaN, kInf, -kInf, 0.0, -0.0, 1.0, -1.0, 1e30, -1e30, kTiny, -kTiny, -1e-30, 0.5};
        const std::int32_t step = cells <= 2048 ? 1 : cells / 7;
        for (std::int64_t k = 0; k <= cells; k += step) {
            const auto place = static_cast<float>(static_cast<double>(k) / cells);
            phases.push_back(static_cast<double>(place));
            beside(phases, place);
            beside(phases, place - 1.0f);
        }
        beside(phases, 1.0f);
        while (phases.size() % 8 != 0) {
            phases.push_back(0.25);
        }
        const std::vector<float> p(phases.begin(), phases.end());
        for (std::size_t at = 0; at < p.size(); at += 8) {
            const auto lanes = loops::table_tap_in_float(cells, Lane<float, 8>::load(p.data() + at));
            for (std::size_t l = 0; l < 8; ++l) {
                const loops::FloatTap<float> tap = loops::table_tap_in_float(cells, p[at + l]);
                const bool a_number = p[at + l] - p[at + l] == 0.0f;
                const bool inside = tap.whole >= 0.0f && static_cast<double>(tap.whole) <= cells - 1.0 &&
                                    tap.whole == static_cast<float>(static_cast<std::int64_t>(tap.whole));
                const bool part = a_number ? tap.fraction >= 0.0f && tap.fraction < 1.0f : tap.fraction != tap.fraction;
                const bool lane = same(lanes.whole[l], tap.whole) && same(lanes.fraction[l], tap.fraction);
                wrong += inside && part && lane ? 0 : 1;
                ++taps;
            }
        }
    }
    std::printf("tables: the float tap of %zu phases over 11 sizes of table, in a lane and alone: %zu outside its table or "
                "its lane\n", taps, wrong);
    return wrong == 0;
}

// ---- the proof of ranges ------------------------------------------------------------------------------------------------

constexpr Graph proven_reads() {
    Input p{"p", Signal{8}};
    Output w{"w"};
    Output k{"k"};
    w = wavetable(constant(kWave), p);
    k = lookup(constant(kWave), p, kLo, kHi);
    Graph g = finish(w, k);
    if (!infer(g).ok) {
        fail("tables_test: the proven reads did not analyse");
    }
    return g;
}

// A read of a constant table lies in its cells' hull, whatever the phase; lookup's phase in [0, 63/64].
constexpr bool ranges_proven() {
    const Graph g = proven_reads();
    const ranges::Proof p = ranges::proof(g);
    float lo = kWave[0];
    float hi = kWave[0];
    for (const float c : kWave) {
        lo = c < lo ? c : lo;
        hi = c > hi ? c : hi;
    }
    const Range hull = testing::made_of(testing::closed(lo, hi));
    const std::int32_t read = g.outputs[1].value;
    std::int32_t phase = kNoValue;
    for (const Node& node : g.nodes) {
        phase = node.output == read ? node.inputs[0] : phase;
    }
    return p.findings.empty() && p.values[static_cast<std::size_t>(g.outputs[0].value)] == hull &&
           p.values[static_cast<std::size_t>(read)] == hull &&
           p.values[static_cast<std::size_t>(phase)] == testing::made_of(testing::closed(0, 63.0 / 64.0));
}
static_assert(ranges_proven(), "a read of a constant table lies in its cells' hull, lookup's phase in [0, 63/64]");

// ---- lookup's bounds and cells, refused ---------------------------------------------------------------------------------

// What `lookup` over `cells` cells from lo to hi refuses, built at run time, where `fail` throws; empty: it builds.
std::string lookup_refusal(std::int32_t cells, double lo, double hi) {
    try {
        Input p{"p", Signal{8}};
        Input t{"t", {cells}};
        (void)lookup(t, p, lo, hi);
    } catch (const error& e) {
        return std::string(e.message());
    }
    return "";
}

// Each bound or span a double does not hold, which would read a NaN for some x or every one: a NaN, an infinity, a span
// past the largest double, one whose scale is past it; lo at +inf is crossed; a table of one cell. The widest and the
// narrowest spans a double holds build. (An infinite bound and one cell are fixtures too, as is lo above hi.)
bool lookups_refused() {
    constexpr double kMostDouble = std::numeric_limits<double>::max();
    const std::string finite = "heinzel: lookup(table, x, lo, hi) needs lo, hi and hi - lo finite";
    bool ok = lookup_refusal(64, kNaN, 1.0) == finite && lookup_refusal(64, 0.0, kNaN) == finite;
    ok = lookup_refusal(64, -kInf, 1.0) == finite && lookup_refusal(64, 0.0, kInf) == finite && ok;
    ok = lookup_refusal(64, -kMostDouble, kMostDouble) == finite && ok;
    ok = lookup_refusal(64, kInf, 1.0) == "heinzel: lookup(table, x, lo, hi) has lo at or above hi" && ok;
    ok = lookup_refusal(64, 0.0, 1e-320) == "heinzel: lookup(table, x, lo, hi) has hi - lo too small to space its cells" && ok;
    ok = lookup_refusal(1, 0.0, 1.0) == "heinzel: lookup(table, x, lo, hi) needs a table of at least 2 cells" && ok;
    return lookup_refusal(64, -kMostDouble / 2, kMostDouble / 2).empty() && lookup_refusal(2, 0.0, 1e-300).empty() && ok;
}

} // namespace

int main() {
    bool ok = agrees_everywhere<Read::WaveOfAConstant>();
    ok = agrees_everywhere<Read::WaveOfAnInput>() && ok;
    ok = double_cells_agree() && ok;
    ok = agrees_everywhere<Read::Lookup>() && ok;
    ok = agrees_everywhere<Read::LookupOdd>() && ok;
    ok = edges<Read::Lookup, Normal>() && edges<Read::Lookup, High>() && ok;
    ok = edges<Read::LookupOdd, Normal>() && edges<Read::LookupOdd, High>() && ok;
    const bool cycle = feedback_agrees();
    std::printf("tables: a lookup inside feedback, one voice and three, at High as the walk reads it: %s\n", cycle ? "yes" : "NO");
    ok = cycle && ok;
    const bool blocks = blocks_equal_one_run<default_machine>() && blocks_equal_one_run<zen2>();
    std::printf("tables: an oscillator in blocks of 8, 16, 12 and 4 %s one run, on both machines\n", blocks ? "equals" : "DIFFERS from");
    ok = float_taps_inside() && ok;
    const bool bounds = lookups_refused();
    std::printf("tables: lookup's bounds a double does not hold, and one cell, refused by name: %s\n", bounds ? "yes" : "NO");
    if (!ok || !blocks || !bounds) {
        std::printf("tables_test: FAILED\n");
        return 1;
    }
    std::printf("tables_test: wavetable and lookup read as the walk does, lookup's edges as stated\n");
    return 0;
}
