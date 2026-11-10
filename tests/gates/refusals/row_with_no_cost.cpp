// A DELIBERATE OFFENDER, and this one offends by FAILING TO COMPILE.
//
// The third of the half-done operations, added at milestone M4 and for a reason the first two did
// not have: this one USED TO BUILD. `CostClass` had no sentinel and `Cheap` was value zero, so a row
// that never stated its cost was accepted by the coverage check and read as "one multiply-add's
// worth". Thirteen rows all stated theirs, so nothing showed it. Twenty-nine more were about to
// arrive, sixteen of them the most expensive thing in the tree.
//
// The other two fixtures beside this one show a check objecting to something it always objected to.
// This one shows a check that has been taught to object. That is worth the extra file, because the
// hole it closes is the kind that stays invisible right up until the day it is expensive.
//
// The half-done table is `tests/support/incomplete_operation.hpp`, shared with
// `unit/op_rules_test.cpp` — which requires the CHECK to say no where this file requires the BUILD
// to refuse. Two copies would let those two claims drift apart while both still passed.
#include "heinzel/ir/op_rules.hpp"

#include "incomplete_operation.hpp"

using namespace heinzel;

static_assert(every_row_is_complete(kRowWithNoCost.data(), kOpCount),
              "a rule row that never states its cost is half-filled — name the class it belongs to, "
              "because until M4 this column had no sentinel and an unstated cost read as Cheap");

int main() { return 0; }
