// A table read whose phase lies in one turn reads with no wrap (Decision 78, after the close: the phase in one turn).
// The proof of ranges keeps a phase in [0, 1] at every precision only by what no rounding moves past 0 or 1
// (`ranges/in_every_rounding.hpp`): `frac`'s fact, `lookup`'s clamp by `min` and `max`, a selection of constants, a
// recurrence and its past. Compiling marks such a read, and the program's statement says so; not a phase that is an
// input, nor one past 1, nor one declared in [0, 1] (an input's, a Param's) or promised there, which release checks
// nowhere, though the proof keeps each of those in [0, 1]. On Executor at High, Normal and Fast, over f32
// and f64, on the default machine and zen2: `wavetable` of `frac(p)` and of `min(abs(p), 1)` (abs(-0) is +0),
// `lookup` (lo 0: a phase of -0 at x = -0), an oscillator and a lookup inside feedback; at High every number the
// walk's, bit for bit; at Normal and Fast every number that of the same read with its phase times 1, which the proof
// cannot see into, so read with the wrap. The phases reach 0, -0, 1 (frac just below 0), the numbers beside 0 and 1,
// every cell's place and NaN, through `frac`, `abs` and the clamp. A false fact a graph built by hand states reads a
// wrong number, never memory outside the table. An oscillator in blocks of 1 to 100 gives one run's numbers. `frac`'s
// fact holds of `x - floor(x)` as the runner computes it, in float, in float lanes and in double, over a million each.
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <limits>
#include <memory>
#include <type_traits>
#include <vector>

#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/ranges/in_every_rounding.hpp"
#include "heinzel/runtime/compute.hpp"
#include "heinzel/runtime/lane_value.hpp"
#include "heinzel/sema/analyze.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

constexpr Index n{};
constexpr std::size_t kPoints = 1024;
constexpr std::int32_t kCells = 64;
constexpr double kLo = 0.0; // so that x = -0 makes a phase of -0
constexpr double kHi = 3.0;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

// Numbers a float holds, none repeating soon, the first cell far from the others.
constexpr std::array<float, kCells> kWave = [] {
    std::array<float, kCells> v{};
    for (std::size_t i = 0; i < v.size(); ++i) {
        v[i] = 0.03125f * static_cast<float>(static_cast<std::int32_t>((i * 37 + 11) % 61) - 30);
    }
    v[0] = 7.5f;
    return v;
}();

// ---- the reads ------------------------------------------------------------------------------------------------------

enum class Phase { Frac, Abs, Lookup, Oscillator, Pole };

// `phase` as read: itself, or, `Hidden`, times 1, the same number to the bit, which the proof cannot see into.
template <bool Hidden>
constexpr Expr seen(Expr phase) {
    if constexpr (Hidden) {
        return phase * 1.0;
    } else {
        return phase;
    }
}

// `lookup(table, x, kLo, kHi)`, or, `Hidden`, its clamp written out and seen through a product.
template <bool Hidden>
constexpr Expr looked_up(Expr table, Expr x) {
    if constexpr (Hidden) {
        Expr shifted = x - kLo;
        Expr along = shifted * detail::lookup_scale(kCells, kLo, kHi);
        Expr above = max(0.0, along);
        Expr phase = min(detail::last_cell(kCells), above);
        return wavetable(table, seen<true>(phase));
    } else {
        return lookup(table, x, kLo, kHi);
    }
}

// One read over the input `p` of type T at a level: a wavetable of `frac(p)` and of `min(abs(p), 1)`, a lookup of
// x = p, a wavetable oscillator stepping by p, a one-pole whose input passes through a lookup of its own past (JUCE's
// ladder's saturator).
template <Phase P, ElemType T, Precision Level, bool Hidden>
constexpr Graph read_at() {
    Input p{"p", Signal{kPoints}, T};
    Output y{"y"};
    Expr table = constant(kWave);
    if constexpr (P == Phase::Frac) {
        Expr phase = frac(p);
        y = wavetable(table, seen<Hidden>(phase));
    } else if constexpr (P == Phase::Abs) {
        Expr magnitude = abs(p);
        Expr phase = min(magnitude, 1.0);
        y = wavetable(table, seen<Hidden>(phase));
    } else if constexpr (P == Phase::Lookup) {
        y = looked_up<Hidden>(table, p);
    } else if constexpr (P == Phase::Oscillator) {
        Variable phase;
        Expr stepped = phase[n - 1] + p;
        phase[n] = frac(stepped);
        y = wavetable(table, seen<Hidden>(phase));
    } else {
        Variable s;
        Expr fed_back = 0.9 * s[n - 1];
        Expr drive = p - fed_back;
        Expr warm = looked_up<Hidden>(table, drive);
        Expr half = 0.5 * warm;
        Expr kept = 0.3 * s[n - 1];
        s[n] = half + kept;
        y = s;
    }
    return finish(Level, y);
}

// Whether the program `Executor<Build, M>` runs reads its table with the phase in one turn.
template <auto Build, const Machine& M = default_machine>
bool read_in_one_turn() {
    const std::unique_ptr<const CompileResult> compiled(new auto(try_compile(Build(), M)));
    if (!compiled->refusal.view().empty()) {
        fail("tables_in_one_turn_test: a read did not compile");
    }
    for (const loops::Stmt& st : compiled->compiled.program.stmts) {
        if (st.kind == loops::StmtKind::Gather && st.gather == loops::Gather::Table) {
            return st.phase_in_one_turn;
        }
    }
    fail("tables_in_one_turn_test: a program with no table read");
}

// ---- the phases -----------------------------------------------------------------------------------------------------

template <class Number>
void beside(std::vector<double>& p, Number x) {
    p.push_back(static_cast<double>(std::nextafter(x, -std::numeric_limits<Number>::infinity())));
    p.push_back(static_cast<double>(std::nextafter(x, std::numeric_limits<Number>::infinity())));
}

// The edges first, then every cell's place and the numbers beside it, then a sweep: for `frac(p)`, 0, -0, 1 (from
// just below 0: -1e-30 and the least number below 0), the numbers beside 0 and 1, a NaN and the infinities; for
// lookup's x, the same about lo and hi (-0 makes a phase of -0). For the oscillator steps, to 0, to 1 from just below
// 0, back to 0 and on; for the pole a sweep; each a NaN near the end, which the cycle keeps.
template <class Number>
std::vector<Number> points(Phase phase) {
    std::vector<double> p{};
    if (phase == Phase::Frac || phase == Phase::Abs || phase == Phase::Lookup) {
        p = {kNaN, -kNaN, kInf, -kInf, 0.0, -0.0, 1.0, -1.0, -1e-30, 0.5, -0.25, 2.75, 1e30, -1e30};
        beside(p, Number{0});
        beside(p, Number{1});
        beside(p, static_cast<Number>(kHi));
        for (std::int32_t k = 0; k <= kCells; ++k) {
            const double place = phase != Phase::Lookup ? static_cast<double>(k) / kCells : kHi * k / (kCells - 1);
            beside(p, static_cast<Number>(place));
            beside(p, static_cast<Number>(place - 1.0));
        }
    } else if (phase == Phase::Oscillator) {
        p = {0.0, -1e-30, 0.0, 0.25, 0.75, -0.0, 1.0 - 0x1p-24, 0x1p-24, -1.0};
    }
    while (p.size() < kPoints) {
        const double sweep = static_cast<double>((p.size() * 37) % 1001) / 1000.0;
        p.push_back(phase == Phase::Pole ? 6.0 * sweep - 3.0 : 4.0 * sweep - 1.0);
    }
    if (phase == Phase::Oscillator || phase == Phase::Pole) {
        p[kPoints - 2] = kNaN;
    }
    return std::vector<Number>(p.begin(), p.end());
}

// ---- the same numbers -----------------------------------------------------------------------------------------------

template <class Number>
bool same(Number a, Number b) {
    using Bits = std::conditional_t<sizeof(Number) == 4, std::uint32_t, std::uint64_t>;
    return std::bit_cast<Bits>(a) == std::bit_cast<Bits>(b) || (a != a && b != b);
}

template <auto Build, const Machine& M, class Number>
std::vector<Number> ran(const std::vector<Number>& p) {
    Executor<Build, M> ex;
    testing::held(ex);
    std::vector<Number> y(p.size());
    ex.input("p") = p.data();
    ex.output("y") = y.data();
    ex.run();
    return y;
}

constexpr const char* level_name(Precision l) { return l == High ? "High" : l == Normal ? "Normal" : "Fast"; }
constexpr const char* phase_name(Phase p) {
    return p == Phase::Frac         ? "a wavetable of frac(p)"
           : p == Phase::Abs        ? "a wavetable of min(abs(p), 1)"
           : p == Phase::Lookup     ? "a lookup"
           : p == Phase::Oscillator ? "an oscillator"
                                    : "a lookup in feedback";
}

// One read at one level, in one type, on one machine: in one turn, the hidden one not, and every number the walk's at
// High, the hidden read's at Normal and Fast.
template <Phase P, ElemType T, Precision Level, const Machine& M>
bool agrees() {
    using Number = std::conditional_t<T == ElemType::F32, float, double>;
    constexpr auto kRead = read_at<P, T, Level, false>;
    constexpr auto kHidden = read_at<P, T, Level, true>;
    const std::vector<Number> p = points<Number>(P);
    const std::vector<Number> y = ran<kRead, M>(p);
    std::vector<Number> want(p.size());
    if constexpr (Level == High) {
        const testing::Outputs walk = testing::walked<kRead>({{"p", p.data()}});
        want.assign(walk[0].begin(), walk[0].end());
    } else {
        want = ran<kHidden, M>(p);
    }
    std::size_t differ = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        differ += same(y[i], want[i]) ? 0 : 1;
    }
    const bool turn = read_in_one_turn<kRead, M>();
    const bool hidden = read_in_one_turn<kHidden, M>();
    std::printf("tables_in_one_turn: %s, %s at %s on %s: in one turn %s, hidden %s; %zu of %zu numbers differ from %s\n",
                phase_name(P), T == ElemType::F32 ? "f32" : "f64", level_name(Level), &M == &zen2 ? "zen2" : "the default machine",
                turn ? "yes" : "NO", hidden ? "YES" : "no", differ, p.size(), Level == High ? "the walk" : "the read with its wrap");
    return turn && !hidden && differ == 0;
}

// On both machines, each run whatever the other found.
template <Phase P, ElemType T, Precision Level>
bool on_both() {
    const bool here = agrees<P, T, Level, default_machine>();
    const bool there = agrees<P, T, Level, zen2>();
    return here && there;
}

template <Phase P>
bool agrees_everywhere() {
    const bool each[] = {on_both<P, ElemType::F32, High>(), on_both<P, ElemType::F32, Normal>(), on_both<P, ElemType::F32, Fast>(),
                         on_both<P, ElemType::F64, High>(), on_both<P, ElemType::F64, Normal>(), on_both<P, ElemType::F64, Fast>()};
    return std::all_of(std::begin(each), std::end(each), [](bool b) { return b; });
}

// ---- what the program may rest on -----------------------------------------------------------------------------------

constexpr Graph past_phase() { // the oscillator's past: its definition's fact, and silence
    Input step{"p", Signal{kPoints}};
    Variable phase;
    Expr stepped = phase[n - 1] + step;
    phase[n] = frac(stepped);
    Output y{"y"};
    Expr table = constant(kWave);
    y = wavetable(table, phase[n - 1]);
    return y;
}

constexpr Graph chosen_phase() { // a selection of two numbers in one turn
    Input p{"p", Signal{kPoints}};
    Output y{"y"};
    Expr low = p < 0.5;
    Expr phase = where(low, 0.25, 1.0);
    Expr table = constant(kWave);
    y = wavetable(table, phase);
    return y;
}

constexpr Graph clamped_phase() { // clamp(p, 0, 1): 1 itself reached
    Input p{"p", Signal{kPoints}};
    Output y{"y"};
    Expr phase = clamp(p, 0.0, 1.0);
    Expr table = constant(kWave);
    y = wavetable(table, phase);
    return y;
}

constexpr Graph input_phase() {
    Input p{"p", Signal{kPoints}};
    Output y{"y"};
    Expr table = constant(kWave);
    y = wavetable(table, p);
    return y;
}

constexpr Graph declared_phase() { // an input declared in one turn, which a debug build alone checks
    Input p{"p", Signal{kPoints}, ge(0), le(1)};
    Output y{"y"};
    Expr table = constant(kWave);
    y = wavetable(table, p);
    return y;
}

constexpr Graph past_one_phase() { // frac(p) * 1.5, which may pass 1
    Input p{"p", Signal{kPoints}};
    Output y{"y"};
    Expr turn = frac(p);
    Expr phase = turn * 1.5;
    Expr table = constant(kWave);
    y = wavetable(table, phase);
    return y;
}

constexpr Graph promised_phase() { // in one turn by a promise, which the proof proves from p's declared range
    Input p{"p", Signal{kPoints}, ge(-1), le(1)};
    Output y{"y"};
    Expr half = 0.5 * p;
    Expr shifted = half + 0.5;
    Expr phase = shifted.promise(ge(0), le(1));
    Expr table = constant(kWave);
    y = wavetable(table, phase);
    return y;
}

constexpr Graph param_phase() { // a selection of a Param declared in one turn, which only a debug build checks when set
    Param turn{"turn", ge(0), le(1)};
    Input p{"p", Signal{kPoints}};
    Output y{"y"};
    Expr low = p < 0.5;
    Expr phase = where(low, turn, turn);
    Expr table = constant(kWave);
    y = wavetable(table, phase);
    return y;
}

// Whether the proof keeps `Build`'s table read's phase in [0, 1], and whether it does so at every precision.
struct Kept {
    bool proof{false};
    bool every_rounding{false};
};

template <auto Build>
constexpr Kept kept_in_one_turn() {
    Graph g = Build();
    if (!infer(g).ok) {
        fail("tables_in_one_turn_test: a graph did not analyse");
    }
    std::int32_t phase = kNoValue;
    for (const Node& node : g.nodes) {
        phase = node.op == op::TableRead ? node.inputs[0] : phase;
    }
    const auto at = static_cast<std::size_t>(phase);
    const Range turn = ranges::span(0, 1);
    return {ranges::inside(ranges::proof(g).values[at], turn), ranges::inside(ranges::in_every_rounding(g)[at], turn)};
}

static_assert(kept_in_one_turn<read_at<Phase::Frac, ElemType::F32, Normal, false>>().every_rounding);
static_assert(kept_in_one_turn<read_at<Phase::Abs, ElemType::F32, Normal, false>>().every_rounding);
static_assert(kept_in_one_turn<read_at<Phase::Lookup, ElemType::F32, Normal, false>>().every_rounding);
static_assert(kept_in_one_turn<read_at<Phase::Oscillator, ElemType::F32, Normal, false>>().every_rounding);
static_assert(kept_in_one_turn<read_at<Phase::Pole, ElemType::F32, Normal, false>>().every_rounding);
static_assert(kept_in_one_turn<past_phase>().every_rounding && kept_in_one_turn<chosen_phase>().every_rounding);
static_assert(kept_in_one_turn<clamped_phase>().every_rounding);
static_assert(!kept_in_one_turn<input_phase>().proof && !kept_in_one_turn<past_one_phase>().proof);
static_assert(kept_in_one_turn<declared_phase>().proof && !kept_in_one_turn<declared_phase>().every_rounding,
              "a declared range the proof trusts, the program not");
static_assert(kept_in_one_turn<promised_phase>().proof && !kept_in_one_turn<promised_phase>().every_rounding,
              "a promise the proof proves from a declared range, the program not");
static_assert(kept_in_one_turn<param_phase>().proof && !kept_in_one_turn<param_phase>().every_rounding,
              "a Param's declared range the proof trusts, the program not");

// The marks the programs carry, on both machines.
template <const Machine& M>
bool marked() {
    const bool past = read_in_one_turn<past_phase, M>();
    const bool taken = past && read_in_one_turn<chosen_phase, M>() && read_in_one_turn<clamped_phase, M>();
    const bool input = read_in_one_turn<input_phase, M>() || read_in_one_turn<declared_phase, M>();
    const bool stated = read_in_one_turn<promised_phase, M>() || read_in_one_turn<param_phase, M>();
    const bool not_taken = !input && !stated && !read_in_one_turn<past_one_phase, M>();
    std::printf("tables_in_one_turn: on %s, an oscillator's past, a choice of two numbers and a clamp read in one turn: %s; "
                "an input, declared or not, frac(p) * 1.5, a promise and a ranged Param with the wrap: %s\n",
                &M == &zen2 ? "zen2" : "the default machine", taken ? "yes" : "NO", not_taken ? "yes" : "NO");
    return taken && not_taken;
}

// ---- a false fact ---------------------------------------------------------------------------------------------------

// The tap with no wrap: a place below 0 reads the first cell, -0 as the wrap reads it (index 0, fraction +0).
static_assert(table_tap<false>(8, -0.25) == TableTap{0, 1, 0.0} && table_tap<false>(8, -1e300) == TableTap{0, 1, 0.0});
static_assert(table_tap<false>(8, -0.0) == table_tap(8, -0.0));
static_assert(std::bit_cast<std::uint64_t>(table_tap<false>(8, -0.0).fraction) == 0);

// `wavetable(t, p * 1.0)` over an input table, a fact [0, 1] stated on the product as a graph built by hand may state a
// false one (`push_range` is public): the read is marked in one turn, so it reads a phase below 0 with no wrap.
template <ElemType T, Precision Level>
constexpr Graph forged() {
    Input p{"p", Signal{kPoints}, T};
    Input t{"t", {kCells}, T};
    Output y{"y"};
    Expr phase = p * 1.0;
    y = wavetable(t, phase);
    Graph g = finish(Level, y);
    for (const Node& node : g.nodes) {
        if (node.op == op::TableRead) {
            push_range(g, RangeMark{node.inputs[0], ranges::span(0, 1), false});
        }
    }
    return g;
}

// Phases below 0 read through the false fact: the table between NaNs, which a read touching one would hand on, so
// every number finite and within the table's cells is a read inside it (a wrong number, the first cell's).
template <ElemType T, Precision Level, const Machine& M>
bool stays_inside() {
    using Number = std::conditional_t<T == ElemType::F32, float, double>;
    constexpr std::size_t kGuard = 16;
    std::vector<Number> memory(kGuard + kCells + kGuard, static_cast<Number>(kNaN));
    std::copy(kWave.begin(), kWave.end(), memory.begin() + kGuard);
    std::vector<double> below{-1.0 / 128, -0.01, -0.25, -0.75};
    while (below.size() < kPoints) {
        below.push_back(-2.0 * static_cast<double>((below.size() * 37) % 1001) / 1000.0); // (-2, -0]
    }
    const std::vector<Number> p(below.begin(), below.end());
    std::vector<Number> y(p.size());
    Executor<forged<T, Level>, M> ex;
    testing::held(ex);
    ex.input("p") = p.data();
    ex.input("t") = memory.data() + kGuard;
    ex.output("y") = y.data();
    ex.run();
    const auto [least, largest] = std::minmax_element(kWave.begin(), kWave.end());
    const auto outside = std::count_if(y.begin(), y.end(), [&](Number v) { return !(v >= *least && v <= *largest); });
    const bool turn = read_in_one_turn<forged<T, Level>, M>();
    std::printf("tables_in_one_turn: a false fact, %s at %s on %s: in one turn %s; %td of %zu phases below 0 read outside "
                "the table\n",
                T == ElemType::F32 ? "f32" : "f64", level_name(Level), &M == &zen2 ? "zen2" : "the default machine",
                turn ? "yes" : "NO", outside, p.size());
    return turn && outside == 0;
}

template <ElemType T, Precision Level>
bool inside_on_both() {
    const bool here = stays_inside<T, Level, default_machine>();
    const bool there = stays_inside<T, Level, zen2>();
    return here && there;
}

bool false_facts_stay_inside() {
    const bool each[] = {inside_on_both<ElemType::F32, High>(), inside_on_both<ElemType::F32, Normal>(),
                         inside_on_both<ElemType::F32, Fast>(), inside_on_both<ElemType::F64, High>(),
                         inside_on_both<ElemType::F64, Normal>(), inside_on_both<ElemType::F64, Fast>()};
    return std::all_of(std::begin(each), std::end(each), [](bool b) { return b; });
}

// ---- blocks equal one run -------------------------------------------------------------------------------------------

constexpr Graph oscillator() {
    Input step{"step", Signal{Length{}}};
    Variable phase;
    Expr stepped = phase[n - 1] + step;
    phase[n] = frac(stepped);
    Output y{"y"};
    Expr table = constant(kWave);
    y = wavetable(table, phase);
    return finish(y);
}
template <Precision Level>
constexpr Graph oscillator_at() {
    Graph g = oscillator();
    set_default_precision(g, Level);
    return g;
}

// Blocks of 1, 7, 64, 3, 100, 1 and 24 samples, nothing reset between them: the numbers of one run over the 200.
template <Precision Level, const Machine& M>
bool blocks_equal_one_run() {
    std::vector<float> step(200);
    for (std::size_t i = 0; i < step.size(); ++i) {
        step[i] = 0.0371f + 0.11f * static_cast<float>(i % 7) - (i % 5 == 0 ? 0.6f : 0.0f);
    }
    std::vector<float> whole(step.size());
    std::vector<float> blocks(step.size());
    Executor<oscillator_at<Level>, M> once;
    once.input("step") = step.data();
    once.output("y") = whole.data();
    once.prepare(200);
    once.run(200);
    Executor<oscillator_at<Level>, M> in_blocks;
    in_blocks.prepare(100);
    std::size_t at = 0;
    for (const std::int64_t frames : {1, 7, 64, 3, 100, 1, 24}) {
        in_blocks.input("step") = step.data() + at;
        in_blocks.output("y") = blocks.data() + at;
        in_blocks.run(frames);
        at += static_cast<std::size_t>(frames);
    }
    const bool turn = read_in_one_turn<oscillator_at<Level>, M>();
    return turn && blocks == whole && whole != std::vector<float>(whole.size());
}

// ---- frac's fact as the runner computes it --------------------------------------------------------------------------

// x - floor(x), by the runner's one writing of each, in `V`.
template <class V>
V frac_of(V x) {
    const V whole = compute_as<V>(op::Floor, FnAccuracy::Exact, x);
    return compute_as<V>(op::Sub, FnAccuracy::Exact, x, whole);
}

template <class Number>
bool in_one_turn(Number x, Number f) {
    return f != f ? x - x != 0 : f >= 0 && f <= 1;
}

// Over the numbers beside every integer from -64 to 64 and a million bit patterns, of floats and of doubles, every frac
// in [0, 1], a NaN only of a NaN or an infinity; in float lanes too.
bool frac_kept() {
    std::vector<float> f32{0.0f, -0.0f, -1e-30f, std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), kNaN};
    std::vector<double> f64{0.0, -0.0, -1e-30, -0x1p-1074, kInf, -kInf, kNaN};
    for (std::int32_t k = -64; k <= 64; ++k) {
        std::vector<double> near{};
        beside(near, static_cast<float>(k));
        f32.insert(f32.end(), near.begin(), near.end());
        near.clear();
        beside(near, static_cast<double>(k));
        f64.insert(f64.end(), near.begin(), near.end());
    }
    std::uint64_t state = 0x2545f4914f6cdd1dull;
    while (f32.size() < (1u << 20)) {
        state ^= state << 13; // xorshift64: every bit pattern, of every exponent
        state ^= state >> 7;
        state ^= state << 17;
        f32.push_back(std::bit_cast<float>(static_cast<std::uint32_t>(state >> 32)));
        f64.push_back(std::bit_cast<double>(state));
    }
    std::size_t outside = 0;
    for (std::size_t i = 0; i + 8 <= f32.size(); i += 8) {
        const Lane<float, 8> lanes = frac_of(Lane<float, 8>::load(f32.data() + i));
        for (std::size_t l = 0; l < 8; ++l) {
            const float x = f32[i + l];
            outside += in_one_turn(x, frac_of(x)) && in_one_turn(x, lanes[l]) ? 0 : 1;
        }
    }
    for (const double x : f64) {
        outside += in_one_turn(x, frac_of(x)) ? 0 : 1;
    }
    std::printf("tables_in_one_turn: frac's fact over %zu floats, alone and in lanes, and %zu doubles: %zu outside [0, 1]\n",
                f32.size(), f64.size(), outside);
    return outside == 0;
}

} // namespace

int main() {
    bool ok = agrees_everywhere<Phase::Frac>();
    ok = agrees_everywhere<Phase::Abs>() && ok;
    ok = agrees_everywhere<Phase::Lookup>() && ok;
    ok = agrees_everywhere<Phase::Oscillator>() && ok;
    ok = agrees_everywhere<Phase::Pole>() && ok;
    ok = marked<default_machine>() && marked<zen2>() && ok;
    const bool blocks = blocks_equal_one_run<Normal, default_machine>() && blocks_equal_one_run<Normal, zen2>() &&
                        blocks_equal_one_run<Fast, zen2>() && blocks_equal_one_run<High, zen2>();
    std::printf("tables_in_one_turn: an oscillator in blocks of 1, 7, 64, 3, 100, 1 and 24 %s one run, in one turn\n",
                blocks ? "equals" : "DIFFERS from");
    ok = false_facts_stay_inside() && ok;
    ok = frac_kept() && blocks && ok;
    if (!ok) {
        std::printf("tables_in_one_turn_test: FAILED\n");
        return 1;
    }
    std::printf("tables_in_one_turn_test: the reads in one turn take no wrap, the same numbers to the bit\n");
    return 0;
}
