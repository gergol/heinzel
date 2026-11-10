// What the analysis pass and the runners make of a WAVETABLE READ — its shape, its refusals, and the
// interpolated values a hand-filled walk reads (milestone M5, S6).
//
// `_tblread` is `out[e] = table[frac(phase[e])*N]`, linearly interpolated between the two bracketing samples
// of an `[N]` buffer read as a cycle (after the last sample, the first). The table is INDEXED by a
// data-dependent position rather than iterated, so `compute()` has no sample-at-a-time answer for it and it
// carries the element meaning `ElementKind::Gathered` — the same family as `_order`/`_ovadd`. This file
// checks the passthrough shape, the attribute refusals SHOWN REJECTING deliberately broken inputs, and the
// interpolation a hand-filled walk reads by hand, a NaN for a NaN phase. The loop tree's table reads
// are `nest_gather_test.cpp`'s.

#include <array>
#include <cstdio>
#include <limits>
#include <string_view>

#include "heinzel/ir/ir.hpp"
#include "heinzel/runtime/table_gather_element.hpp"
#include "heinzel/sema/analyze.hpp"

#include "hand_built_graph.hpp"
#include "interpret.hpp"

using namespace heinzel;

namespace {
constexpr OpAttr table_attr(std::int32_t size) {
    OpAttr a{};
    a.kind = AttrKind::Table;
    a.table.size = size;
    return a;
}
} // namespace

// ---- the shape (milestone M5, S6) -------------------------------------------------------------------
//
// A table read has the PHASE's shape — the phase is iterated, the table is a side lookup. A phase of `{8}`
// (one number per sample, a streamed axis of 8) reading a three-entry table `{3, 8}` stays `{8}`.
static_assert([] {
    Graph g{};
    const auto phase = add_value(g, ElemType::F64, Shape{8}, ValueCategory::Input);
    const auto table = add_value(g, ElemType::F64, Shape{3, 8}, ValueCategory::Input);
    const auto out = add_node(g, op::TableRead, phase, table, kNoValue, table_attr(3));
    return infer(g).ok && g.values[static_cast<std::size_t>(out)].shape == Shape{8};
}(), "a table read has the phase's shape");

// ---- the refusals, SHOWN REJECTING deliberately broken inputs ---------------------------------------

// A resolution below one is a table with nothing to interpolate between.
static_assert([] {
    Graph g{};
    const auto phase = add_value(g, ElemType::F64, Shape{8}, ValueCategory::Input);
    const auto table = add_value(g, ElemType::F64, Shape{3, 8}, ValueCategory::Input);
    add_node(g, op::TableRead, phase, table, kNoValue, table_attr(0));
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() == std::string_view{"_tblread has resolution 0; a table read's N is at least one"};
}(), "SHOWN REJECTING: a resolution below one is refused");

// A table that does not hold N samples: resolution 3 needs a three-entry table, and a four-entry one is refused with
// both numbers.
static_assert([] {
    Graph g{};
    const auto phase = add_value(g, ElemType::F64, Shape{8}, ValueCategory::Input);
    const auto table = add_value(g, ElemType::F64, Shape{4, 8}, ValueCategory::Input);
    add_node(g, op::TableRead, phase, table, kNoValue, table_attr(3));
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"_tblread of resolution 3 needs a table of 3 samples, but was given 4"};
}(), "SHOWN REJECTING: a table that is not N samples is refused");

// A table whose size is a symbol resolved later has no N+1 to check against — refused as that, not read into
// a wrong index. The phase may be symbolic; the table may not.
static_assert([] {
    Graph g{};
    const auto phase = add_value(g, ElemType::F64, Shape{8}, ValueCategory::Input);
    const auto table = add_value(g, ElemType::F64, Shape{Dim{0}, 8}, ValueCategory::Input);
    add_node(g, op::TableRead, phase, table, kNoValue, table_attr(3));
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"_tblread reads a table of unknown size; a wavetable's extent must be known now"};
}(), "SHOWN REJECTING: a table of unknown size is refused");

// ---- the run side: the interpolated values a hand-filled walk reads ---------------------------------
//
// Eight phases into a three-entry table `{10, 20, 30}` (N=3), read by a hand-filled walk. The phases are
// EXACT in double and `frac(phase)*3` lands on quarters and eighths, so the two samples the read brackets and
// the blend between them are both exact — a wrong index or a wrong blend moves the answer, a last-bit position
// does not. `0.75` reads the last cell toward the first; `1.0` and `-0.25` check the wrap: an integer phase
// reads cell zero, and a negative phase wraps into the table rather than off its front.
namespace {
constexpr std::array<double, 8> gathered(std::array<double, 8> phases) {
    Graph g{};
    const auto phase = add_value(g, ElemType::F64, Shape{8, 8}, ValueCategory::Input); // 8 phases, one sample used
    const auto table = add_value(g, ElemType::F64, Shape{3, 8}, ValueCategory::Input);
    const auto out = add_node(g, op::TableRead, phase, table, kNoValue, table_attr(3));
    if (!infer(g).ok) {
        fail("the table-read graph did not resolve");
    }
    Signals<8> signals{};
    lay_out(g, signals);
    for (std::int32_t e = 0; e < 8; ++e) {
        signals.at[static_cast<std::size_t>(signals.first[static_cast<std::size_t>(phase)] + e)][0] = phases[static_cast<std::size_t>(e)];
    }
    const double entries[3] = {10.0, 20.0, 30.0};
    for (std::int32_t k = 0; k < 3; ++k) {
        signals.at[static_cast<std::size_t>(signals.first[static_cast<std::size_t>(table)] + k)][0] = entries[k];
    }
    detail::walk_gather_step(g, g.nodes[0], 0, signals);
    std::array<double, 8> result{};
    for (std::int32_t e = 0; e < 8; ++e) {
        result[static_cast<std::size_t>(e)] = sample_at(signals, out, e, 0);
    }
    return result;
}
} // namespace

// 0.0 -> cell 0 exactly (10); 0.25 -> x=0.75, lerp(10,20,.75)=17.5; 0.5 -> x=1.5, lerp(20,30,.5)=25; 0.75 ->
// x=2.25, the last cell toward the first, lerp(30,10,.25)=25; 1.0 -> wraps to cell 0 (10); -0.25 -> frac .75, 25;
// 0.125 -> x=0.375, lerp(10,20,.375)=13.75; 1.5 -> frac .5, x=1.5, 25.
static_assert(gathered({0.0, 0.25, 0.5, 0.75, 1.0, -0.25, 0.125, 1.5}) ==
                  std::array<double, 8>{10.0, 17.5, 25.0, 25.0, 10.0, 25.0, 13.75, 25.0},
              "a table read interpolates between the two bracketing entries, wrapping the phase into the table");

// A NaN phase reads a NaN: its tap is the first cell's place with a NaN fraction, never an index made of a NaN (which
// the walk once cast, reading outside the table: Plan 26's oracle under ASan), and the phases beside it read as
// before. At run time: a constant expression refuses arithmetic that makes a NaN, and refuses one that is handed one
// too. An infinite phase, whose frac is inf - inf, is read by both runners in `nest_gather_test`.
int main() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const TableTap tap = table_tap(2, nan);
    const TableTap negative = table_tap(2, -nan);
    const std::array<double, 8> read = gathered({nan, 0.25, 1.0, 0.5, -nan, -0.25, nan, 1.5});
    const bool numbers = read[1] == 17.5 && read[2] == 10.0 && read[3] == 25.0 && read[5] == 25.0 && read[7] == 25.0;
    const bool nans = read[0] != read[0] && read[4] != read[4] && read[6] != read[6];
    if (tap.index != 0 || negative.index != 0 || tap.fraction == tap.fraction || !numbers || !nans) {
        std::printf("tblread_test: a NaN phase must read a NaN, from the first cell's place\n");
        return 1;
    }
    std::printf("tblread_test: a NaN phase reads a NaN, the phases beside it as before\n");
    return 0;
}
