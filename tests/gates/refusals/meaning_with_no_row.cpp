// A DELIBERATE OFFENDER, and this one offends by FAILING TO COMPILE. The companion of
// `row_with_no_meaning.cpp`, for the opposite mistake.
//
// The half-done operation here is A MEANING WITH NO ROW: arithmetic written in `runtime/compute.hpp`
// or `tests/reference/compute_reference.hpp` for an operation code the table has no row for. Every
// question about such an operation — its shape rule, its timing, its arity — reads past the end of
// the table.
//
// It is spelled by asking the check about a table that ends at ten rows while eleven meanings are
// written. Codes 0 to 10 are `_add` through `_recur` and every one of them has arithmetic above, so
// a table claiming to stop at ten is a table one row short of its meanings. That is the same
// situation as adding `op::Softmax{13}`, writing its arithmetic, and forgetting the row — and it is
// reachable without editing the real table, which has to stay correct for every other translation
// unit.
#include "heinzel/runtime/compute.hpp"

#include "meaning_coverage.hpp"

using namespace heinzel;

static_assert(no_meaning_beyond(10),
              "arithmetic is written in runtime/compute.hpp or compute_reference.hpp for an operation "
              "code that has no row in kOpTable — add the row and update kOpCount, or delete the arithmetic");

int main() { return 0; }
