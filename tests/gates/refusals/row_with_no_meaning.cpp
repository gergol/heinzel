// A DELIBERATE OFFENDER, and this one offends by FAILING TO COMPILE.
//
// Milestone M3.4 asks that adding a half-done operation make the build refuse. `op_rules_test.cpp`
// records why that cannot be shown from an ordinary test — "a test that fails to compile is not a
// test" — so it is shown from here instead: this translation unit is excluded from the default
// build, and a ctest test marked WILL_FAIL tries to build it and requires the attempt to fail.
//
// It sits in `gates/refusals/` and not beside the source-scanning gates' offenders in
// `gates/fixtures/`: configuring a table means naming an operation, which is not an offence against
// anything, and leaving it there would make the one-writing gate report this file instead of the one
// it is pointed at.
//
// The half-done operation here is A ROW WITH NO MEANING WRITTEN: the rule table says `_reduce` has a
// per-sample meaning, and neither `runtime/compute.hpp` nor `tests/reference/compute_reference.hpp` has
// arithmetic for it. That is exactly what happens when somebody adds an operation, fills in the row,
// and stops.
//
// The half-done table is `tests/support/incomplete_operation.hpp`, shared with
// `unit/meaning_test.cpp` — which requires the CHECK to object where this file requires the BUILD to
// refuse. Two copies of it would let those two claims drift apart while both still passed.
#include "heinzel/runtime/compute.hpp"

#include "incomplete_operation.hpp"
#include "meaning_coverage.hpp"

using namespace heinzel;

static_assert(every_per_sample_operation_has_a_meaning(kRowSaysItHasAMeaning.data(), kOpCount),
              "an operation whose row says it has a per-sample meaning has none written in "
              "runtime/compute.hpp or compute_reference.hpp — write the arithmetic, or classify the row "
              "MeaningRule::AxisChanging");

int main() { return 0; }
