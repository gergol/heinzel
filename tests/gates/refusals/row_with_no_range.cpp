// A DELIBERATE OFFENDER, and this one offends by FAILING TO COMPILE.
//
// The fourth half-done operation (Plan/26-value-ranges/03): a row that never states its range rule or its domain. The
// proof of ranges reads both columns for every operation it walks, so a row without them would leave a result's
// range unsaid or an operand's domain unchecked: `_div` with no domain is a division by 0 the proof cannot see.
//
// The half-done table is `tests/support/incomplete_operation.hpp`, shared with `unit/op_rules_test.cpp`, which
// requires the CHECK to say no where this file requires the BUILD to refuse.
#include "heinzel/ir/op_rules.hpp"

#include "incomplete_operation.hpp"

using namespace heinzel;

static_assert(every_row_is_complete(kRowWithNoRange.data(), kOpCount),
              "a rule row that never states its range rule and domain is half-filled — the proof of ranges reads "
              "both for every operation it walks");

int main() { return 0; }
