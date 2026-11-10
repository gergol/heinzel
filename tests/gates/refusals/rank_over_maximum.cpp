// A DELIBERATE OFFENDER, and this one offends by FAILING TO COMPILE.
//
// The exclusion board's row 16 (`git show main:docs/limitations.md`) — a rank over `kMaxRank` — is a
// refusal made by the `Shape` constructor itself: the fixed-capacity discipline is load-bearing, so a
// shape with more axes than the capacity has nowhere to put them and `fail()`s naming the override
// (`ir/vocab.hpp` `Shape` ctor). Because that refusal is `fail()` and not a `Diagnostic`, it cannot be
// shown rejecting from inside a `static_assert` that evaluates the pass — a throw in a constant
// expression is ill-formed, not a `false` value. So the row is shown rejecting the way the half-done
// operations are: a translation unit that must NOT compile, held out of the default build, its refusal
// observed as the build stopping at the wrong line and the compiler's note carrying the message.
//
// This is the SHOWN-FAILING check `exclusion_board_test.cpp`'s O9 gate points at for row 16. The board
// test asserts that the ceiling still exists (`kMaxRank == 6` and the ctor guards it); this fixture
// proves the guard actually stops a build, which no in-process assertion can.
#include "heinzel/ir/vocab.hpp"

using namespace heinzel;

// Seven axes; `kMaxRank` is six. The constructor `fail()`s while the compiler is evaluating this
// initializer, so the build stops here with a note naming `-DHEINZEL_MAX_RANK`.
constexpr Shape too_many_axes{1, 2, 3, 4, 5, 6, 7};

int main() { return 0; }
