// One rung of the solver's cost ladder, chosen by the command line so that each rung is its own
// compiler invocation and no rung hides in another's shadow.
//
// Driven by `tools/measure_solver_cost.sh`, which compiles this once per (rung, regime) with
// -fsyntax-only and clang invoked directly — so no cache and no code generation are in the number.
//
//   -DHEINZEL_MEASURE_SECTIONS=N   how many filter sections
//   -DHEINZEL_MEASURE_REGIME=0     literal extents: the solver is constructed and never asked
//                              =1  symbolic extents, bound: seeding, one unify per section, resolve
//                              =2  symbolic extents, unbound: unify and canonicalise, nothing pinned

#include "solver_cascade.hpp"

using namespace heinzel;

#ifndef HEINZEL_MEASURE_SECTIONS
#define HEINZEL_MEASURE_SECTIONS 6
#endif
#ifndef HEINZEL_MEASURE_REGIME
#define HEINZEL_MEASURE_REGIME 0
#endif

namespace {

constexpr Extents kRegime = HEINZEL_MEASURE_REGIME == 0   ? Extents::Literal
                            : HEINZEL_MEASURE_REGIME == 1 ? Extents::SymbolicBound
                                                          : Extents::SymbolicUnbound;

constexpr Graph kRung = solver_cascade<HEINZEL_MEASURE_SECTIONS, kRegime>();

// The work must not be optimisable away as unused, and it must actually have finished: an
// unresolved value would make the pass do LESS work, not more, so a rung that quietly failed to
// resolve would report a cheerful number for a graph it never analysed.
static_assert(kRung.nodes.size() > 0);
static_assert([] {
    for (const Value& v : kRung.values) {
        if (v.type == ElemType::Unknown) {
            return false;
        }
    }
    return true;
}(), "the rung resolved completely");

// And the regimes really differ, so a measurement of "the solver's cost" is not three measurements
// of the same thing.
static_assert(is_static(kRung) == (kRegime != Extents::SymbolicUnbound),
              "bound and literal graphs have no symbol left; the unbound one does");

} // namespace

int main() { return kRung.nodes.size() > 0 ? 0 : 1; }
