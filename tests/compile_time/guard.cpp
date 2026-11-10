// The compile-time regression guard.
//
// This is NOT part of the ordinary suite, and three separate things keep it out: the option that
// builds it is off by default, the target is excluded from the default build, and it is never
// registered with ctest. All three matter — dropping any one of them puts it back in the inner
// loop, where it would tax every run of every other test.
//
// Build it deliberately:
//     cmake -S . -B build -DHEINZEL_BUILD_COMPILE_TIME_GUARD=ON
//     cmake --build build --target heinzel_compile_time_guard
//
// WHAT IT IS FOR. The budget for constant-evaluation work is a dial and may be raised, so the cap
// being hit by accident is no longer a signal that anything went wrong. Guarding against SILENT
// growth therefore has to be an explicit, measured target rather than a limit someone bumps into.
//
// HOW TO READ A REGRESSION, and this is the part that matters more than the number. A cost that is
// linear in the size of the graph and merely larger may be paid for by raising the dial. A cost
// that grows FASTER than the graph may not, ever — raising a budget buys a constant factor and
// never an asymptotic class. The measured example behind that rule: a design whose cost was
// exponential in reuse depth took 1.2 seconds at depth 8 and 12.6 seconds at depth 12, and raising
// the budget moved that cliff by two steps and nothing else.
//
// So the ladder below is deliberately measured at several sizes rather than at one. One number
// tells you something got slower. Several tell you whether the SHAPE changed, which is the question
// that decides what to do about it.
//
// MEASURED WHEN THIS WAS WRITTEN (clang 22.1.8, this host, no cache, syntax-only, one rung per
// translation unit so the rungs do not hide in each other's shadow):
//
//     sections     3      6     12     24     30
//     compile    204ms  218ms  244ms  264ms  283ms
//
// About 200 ms of that is the fixed cost of the headers; the marginal cost is roughly 3 ms per
// section and does not increase along the chain. That flatness is the design's central claim
// arriving as a measurement — an arena already is the flat list of nodes, so composing is appending
// and finishing is copying, and neither gets more expensive as the graph grows.
//
// A future run that is uniformly slower is a constant factor and may simply be paid. A future run
// where the gap between rungs WIDENS is the other thing entirely, and no budget fixes it.

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"

#include <cstddef>

#include "cascade.hpp"
#include "solver_cascade.hpp"

using namespace heinzel;

namespace {

// The cascade itself is `cascade.hpp`.

// Each rung is built and analysed during compilation. Comparing how long the rungs take to each
// other is the measurement; the assertions are only there to make sure the work is not optimised
// away as unused.
// The top rung is 24 sections and not more, and the reason is a real limit rather than caution: a
// graph is a fixed-size value, so it holds at most kMaxValues of them. A section costs ten values —
// seven nodes, two coefficients and the variable itself — which puts the ceiling near thirty-one
// sections at 320, the value this target keeps through its own -DHEINZEL_MAX_VALUES (tests/CMakeLists.txt).
// Worth knowing, and worth not discovering as a mysterious build failure.
constexpr Graph k3 = cascade<3>();
constexpr Graph k6 = cascade<6>();
constexpr Graph k12 = cascade<12>();
constexpr Graph k24 = cascade<24>();

// A section costs the same everywhere in the chain. If a later section ever cost more than an
// earlier one, the cost of building would be growing faster than the graph — which is the one kind
// of regression that raising the budget cannot fix, so it is checked structurally and not left to
// be inferred from a clock.
constexpr std::size_t per_section(const Graph& small, const Graph& large, std::size_t sections) {
    return (large.nodes.size() - small.nodes.size()) / sections;
}

static_assert(per_section(k3, k6, 3) == per_section(k6, k12, 6));
static_assert(per_section(k6, k12, 6) == per_section(k12, k24, 12),
              "sections cost the same at the end of a long chain as at the start of a short one");

// Everything resolved. An unresolved value would make the analysis pass do LESS work, not more, so
// a guard that did not check this could report a cheerful number for a graph it never finished.
static_assert([] {
    for (const Value& v : k24.values) {
        if (v.type == ElemType::Unknown) {
            return false;
        }
    }
    return true;
}(), "the largest rung resolved completely");

// ---- the ladder, extended to cover the extent solver (milestone M2.8) --------------------------
//
// The cascade above never asks the solver anything: its only shapes are one block and some scalars,
// and a scalar broadcasts against anything without two axes ever meeting. So the solver needs a
// ladder of its own, and it is `solver_cascade.hpp` — the same shape of chain, but each section
// takes a coefficient signal with an extent of its own, so every section makes exactly one pair of
// axes meet. One unification per section is what makes "the marginal cost per section" a question
// about the solver rather than about the pass around it.
//
// Three regimes, which are the solver's three call sites in the order a graph reaches them:
// constructed-and-never-asked (what almost every graph in this tree does, so the cost everybody
// pays), symbolic-and-bound, and symbolic-and-unbound.
//
// MEASURED WHEN THIS WAS WRITTEN (clang 22.1.8, this host, no cache, syntax-only, one rung per
// translation unit, nine runs per point with the minimum reported, rungs equally spaced so the
// marginal columns are comparable — `tools/measure_solver_cost.sh`):
//
//     sections            4     8    12    16    20    24     marginal per section
//     literal           212   231   240   256   276   290     3.9 ms, no trend
//     bound             216   228   255   270   290   311     4.8 ms, no trend
//     unbound           217   234   256   269   287   304     4.4 ms, no trend
//
// About 196 ms of every column is the fixed cost of the headers. THE MARGINAL COST PER SECTION IS
// FLAT in all three regimes: the last interval is no larger than the first, and the scatter is
// about +/- 1.5 ms with no trend along the chain. The solver's own share is the gap between the
// regimes — roughly 0.9 ms per section to seed, unify and substitute, and 0.5 ms to unify and
// canonicalise. Its CONSTRUCTION, which every graph pays whether it asks anything or not, does not
// separate from the noise: the intercepts are 196 and 197 ms.
//
// So the solver is a linear cost that is small, which is the case a dial pays for if it ever needs
// paying for. A future run where the gap between rungs WIDENS is the other thing entirely.

constexpr Graph kSolver3Literal = solver_cascade<3, Extents::Literal>();
constexpr Graph kSolver6Literal = solver_cascade<6, Extents::Literal>();
constexpr Graph kSolver12Literal = solver_cascade<12, Extents::Literal>();
constexpr Graph kSolver3Bound = solver_cascade<3, Extents::SymbolicBound>();
constexpr Graph kSolver6Bound = solver_cascade<6, Extents::SymbolicBound>();
constexpr Graph kSolver12Bound = solver_cascade<12, Extents::SymbolicBound>();
constexpr Graph kSolver12Unbound = solver_cascade<12, Extents::SymbolicUnbound>();

// A section costs the same number of nodes everywhere in the chain, in every regime. This is the
// structural half of the flatness claim, and it is the half that does not depend on a clock: if a
// later section ever cost more than an earlier one, the graph itself would be growing faster than
// the chain, and no budget fixes that.
static_assert(per_section(kSolver3Literal, kSolver6Literal, 3) == per_section(kSolver6Literal, kSolver12Literal, 6));
static_assert(per_section(kSolver3Bound, kSolver6Bound, 3) == per_section(kSolver6Bound, kSolver12Bound, 6),
              "a section costs the same at the end of a long symbolic chain as at the start of a short one");

// And a symbolic chain produces exactly the graph its literal twin produces, at every rung — which
// is oracle O7 restated at the sizes the guard measures, so a divergence that only appeared on a
// large graph would show up here rather than nowhere.
static_assert(kSolver3Bound == kSolver3Literal);
static_assert(kSolver6Bound == kSolver6Literal);
static_assert(kSolver12Bound == kSolver12Literal);

// The three regimes really are different work, so the three columns above are not three
// measurements of one thing.
static_assert(is_static(kSolver12Bound) && !is_static(kSolver12Unbound));

// Twelve sections declare thirteen symbols, and every one of them ends up in ONE class — each
// section unifies its coefficient's axis with the running signal's. Asserted because it is the
// property that makes the ladder a measurement of the solver: if the sections stopped meeting, the
// numbers above would be measuring the pass around it.
static_assert([] {
    const Extent first = extent_at(kSolver12Unbound.values[0].shape, 0);
    for (const Value& v : kSolver12Unbound.values) {
        if (v.shape.rank == 1 && !same_extent(extent_at(v.shape, 0), first)) {
            return false;
        }
    }
    return is_symbol(first);
}(), "every section's axis was proven to be the same axis");

} // namespace

int main() { return k24.nodes.size() > 0 ? 0 : 1; }
