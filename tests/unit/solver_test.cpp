// The extent solver: which axes are the same axis, what size an axis has, and what it means for a
// graph to say two contradictory things about one axis.
//
// Two properties are checked here that no other file can check:
//
//   * TRANSITIVITY. `a` meets `b`, `b` meets `c`, nothing ever relates `a` and `c`. They are one
//     axis. A solver that only recorded direct meetings would pass every test written about pairs.
//   * ONE BODY. `find` and `unify` are ordinary constant-expression functions, so the same call
//     sequence must give the same answer while compiling and while running. The gate checks that no
//     source branches on which world it is in; this checks that the answers actually agree, which
//     is the property the gate exists to protect.
#include "heinzel/sema/solver.hpp"

#include <string_view>

using namespace heinzel;

namespace {

constexpr Extent sym(std::int32_t id) { return Extent{Dim{id}}; }
constexpr OpName kAdd{"_add"};
constexpr OpName kMul{"_mul"};

// ---- transitive equality (milestone M2.1's half that lives in the solver) ----------------------

// a == b at one operation, b == c at another, and nothing relates a and c.
constexpr ExtentSolver transitive() {
    ExtentSolver s{};
    Diagnostic why{};
    if (!s.unify(sym(0), sym(1), kAdd, why)) {
        fail("a and b must unify");
    }
    if (!s.unify(sym(1), sym(2), kMul, why)) {
        fail("b and c must unify");
    }
    return s;
}

static_assert(transitive().same_class(0, 2), "a and c are one axis though nothing related them");
static_assert(same_extent(transitive().resolve(sym(0)), transitive().resolve(sym(2))),
              "and they therefore resolve to the same extent");

// Unpinned, the class resolves to its representative — which is what makes transitivity VISIBLE in
// a resolved shape rather than only answerable by asking.
static_assert(same_extent(transitive().resolve(sym(2)), sym(0)));

// Pin any one member and every member takes that size.
constexpr ExtentSolver transitive_pinned() {
    ExtentSolver s = transitive();
    Diagnostic why{};
    if (!s.unify(sym(0), literal_extent(64), kAdd, why)) {
        fail("pinning a must succeed");
    }
    return s;
}

static_assert(literal_of(transitive_pinned().resolve(sym(2))) == 64, "c takes the size a was given");
static_assert(literal_of(transitive_pinned().resolve(sym(1))) == 64);

// A size resolves to itself, and an untouched symbol to itself.
static_assert(literal_of(ExtentSolver{}.resolve(literal_extent(8))) == 8);
static_assert(same_extent(ExtentSolver{}.resolve(sym(5)), sym(5)));

// Order of the merge does not change the classes: the lower-numbered representative always wins, so
// the resolved shapes two construction paths produce are comparable for equality.
constexpr ExtentSolver merged_the_other_way() {
    ExtentSolver s{};
    Diagnostic why{};
    (void)s.unify(sym(2), sym(1), kMul, why);
    (void)s.unify(sym(1), sym(0), kAdd, why);
    return s;
}
static_assert(same_extent(merged_the_other_way().resolve(sym(2)), sym(0)),
              "the class is named the same whichever order it was built in");

// ---- a contradiction is named, with both sizes and both operations -----------------------------
//
// Asserted LITERALLY. A message that names the disagreement is the whole user-visible point of the
// design, and a test that only checks `!ok` would pass on "something went wrong".
constexpr Diagnostic two_sizes_for_one_axis() {
    ExtentSolver s{};
    Diagnostic why{};
    (void)s.unify(sym(0), literal_extent(4), kAdd, why);
    if (s.unify(sym(0), literal_extent(8), kMul, why)) {
        fail("one axis given two sizes must be rejected");
    }
    return why;
}

static_assert(!two_sizes_for_one_axis().ok);
static_assert(two_sizes_for_one_axis().message.view() ==
                  std::string_view{"one axis is given two sizes: 4 at _add and 8 at _mul"},
              "both sizes, and the operation that introduced each");

// The same contradiction reached the other way: two classes each already pinned, then united.
constexpr Diagnostic two_pinned_classes_meet() {
    ExtentSolver s{};
    Diagnostic why{};
    (void)s.unify(sym(0), literal_extent(4), kAdd, why);
    (void)s.unify(sym(1), literal_extent(8), kMul, why);
    if (s.unify(sym(0), sym(1), kAdd, why)) {
        fail("uniting two differently-sized classes must be rejected");
    }
    return why;
}
static_assert(two_pinned_classes_meet().message.view() ==
              std::string_view{"one axis is given two sizes: 4 at _add and 8 at _mul"});

// Two sizes that agree are not a contradiction, and two literals are the pass's business, not the
// solver's: it answers whether they match and says nothing about shapes.
static_assert([] {
    ExtentSolver s{};
    Diagnostic why{};
    return s.unify(literal_extent(8), literal_extent(8), kAdd, why) &&
           !s.unify(literal_extent(8), literal_extent(4), kAdd, why);
}());

// ---- hints meet on union (M2.3) ----------------------------------------------------------------

// The tightest divisibility of the two is their least common multiple: a multiple of 4 and a
// multiple of 6 is a multiple of 12. Not 24 — that would promise more than either side said.
static_assert(meet(ExtentHint{.divisible_by = 4}, ExtentHint{.divisible_by = 6}).divisible_by == 12);
static_assert(meet(ExtentHint{.divisible_by = 4}, ExtentHint{.divisible_by = 8}).divisible_by == 8);
static_assert(meet(ExtentHint{}, ExtentHint{.divisible_by = 6}).divisible_by == 6);

// The tightest bounds: the larger lower bound and the smaller upper bound. An unstated bound does
// not tighten anything.
static_assert(meet(ExtentHint{.at_least = 16}, ExtentHint{.at_least = 64}).at_least == 64);
static_assert(meet(ExtentHint{.at_most = 256}, ExtentHint{.at_most = 128}).at_most == 128);
static_assert(meet(ExtentHint{.at_most = 256}, ExtentHint{}).at_most == 256);
static_assert(meet(ExtentHint{}, ExtentHint{}).at_most == 0, "nothing said stays nothing said");

// An expected value is a hint and not a promise, so two sides expecting different sizes is not a
// contradiction — it is a reason to stop expecting anything.
static_assert(meet(ExtentHint{.expected = 512}, ExtentHint{.expected = 512}).expected == 512);
static_assert(meet(ExtentHint{.expected = 512}, ExtentHint{}).expected == 512);
static_assert(meet(ExtentHint{.expected = 512}, ExtentHint{.expected = 256}).expected == 0);

// And an expectation only survives if it is still credible under the bounds the meet produced.
static_assert(meet(ExtentHint{.expected = 512}, ExtentHint{.at_most = 128}).expected == 0,
              "expecting 512 under a ceiling of 128 is not an expectation, it is a mistake");
static_assert(meet(ExtentHint{.expected = 500}, ExtentHint{.divisible_by = 8}).expected == 0);

// BOTH DIRECTIONS. The hint declared on the left-hand axis and the hint declared on the right-hand
// axis must give the same class. This is the assertion M2.3 asks for by name: a meet written with
// an accidental asymmetry passes every one-directional test.
constexpr ExtentHint kLeft{.divisible_by = 4, .at_least = 64, .at_most = 1024, .expected = 256};
constexpr ExtentHint kRight{.divisible_by = 6, .at_least = 128, .at_most = 512, .expected = 0};

static_assert(meet(kLeft, kRight) == meet(kRight, kLeft), "the meet is symmetric");

constexpr ExtentHint united(const ExtentHint& first, const ExtentHint& second) {
    ExtentSolver s{};
    Diagnostic why{};
    if (!s.declare(0, first, why) || !s.declare(1, second, why)) {
        fail("both declarations must be accepted");
    }
    if (!s.unify(sym(0), sym(1), kAdd, why)) {
        fail("the two axes must unify");
    }
    return s.hint_of(0);
}

static_assert(united(kLeft, kRight) == united(kRight, kLeft),
              "which axis the hint was declared on cannot change the class");
static_assert(united(kLeft, kRight).divisible_by == 12 && united(kLeft, kRight).at_least == 128 &&
                  united(kLeft, kRight).at_most == 512 && united(kLeft, kRight).expected == 0,
              "the class carries the tightest of each");

// SHOWN REJECTING a pair whose declared bounds cannot both hold — a floor above a ceiling.
constexpr Diagnostic bounds_that_exclude_each_other() {
    ExtentSolver s{};
    Diagnostic why{};
    (void)s.declare(0, ExtentHint{.at_least = 512}, why);
    (void)s.declare(1, ExtentHint{.at_most = 128}, why);
    if (s.unify(sym(0), sym(1), kAdd, why)) {
        fail("bounds that exclude each other must be rejected");
    }
    return why;
}
static_assert(!bounds_that_exclude_each_other().ok);
static_assert(bounds_that_exclude_each_other().message.view() ==
              std::string_view{"this axis is declared at least 512 and at most 128, which cannot both hold"});

// The same, declared on one axis rather than reached by union.
static_assert([] {
    ExtentSolver s{};
    Diagnostic why{};
    return !s.declare(0, ExtentHint{.at_least = 512, .at_most = 128}, why);
}());

// A hint that nothing ever checks is a hint that means nothing, so a size that breaks one is
// refused — and the message names the part that broke rather than saying only that something did.
constexpr Diagnostic size_against_divisibility() {
    ExtentSolver s{};
    Diagnostic why{};
    (void)s.declare(0, ExtentHint{.divisible_by = 8}, why);
    if (s.unify(sym(0), literal_extent(60), kAdd, why)) {
        fail("60 is not a multiple of 8");
    }
    return why;
}
// No operation named, and that is the pair worth reading beside the derived one further down: a
// hint declared through `declare` is one nobody signed, so the message says what broke and stops.
// The `_reshape` version of this same message is the M5.2 assertion, and the two together are what
// make "names the operation that recorded it" a claim rather than a coincidence of formatting.
static_assert(size_against_divisibility().message.view() ==
              std::string_view{"this axis is given the size 60, but it was declared a multiple of 8"});

static_assert([] {
    ExtentSolver s{};
    Diagnostic why{};
    (void)s.declare(0, ExtentHint{.at_least = 64}, why);
    return !s.unify(sym(0), literal_extent(16), kAdd, why);
}(), "a size below the declared floor is refused");

// A size that satisfies the hint is accepted, so the check is not simply refusing everything.
static_assert([] {
    ExtentSolver s{};
    Diagnostic why{};
    (void)s.declare(0, ExtentHint{.divisible_by = 8, .at_least = 32, .at_most = 128}, why);
    return s.unify(sym(0), literal_extent(64), kAdd, why) && literal_of(s.resolve(sym(0))) == 64;
}());

// ---- one body, two worlds (M2.2) ---------------------------------------------------------------
//
// The gate checks that no source asks which world it is in. This checks the consequence: the same
// sequence of calls gives the same answers in both. The compile-time answers are the static_asserts
// above; `main` runs the identical code as an ordinary program and compares.

constexpr ExtentSolver worked_example() {
    ExtentSolver s{};
    Diagnostic why{};
    (void)s.declare(1, ExtentHint{.divisible_by = 8, .at_most = 1024}, why);
    (void)s.unify(sym(0), sym(1), kAdd, why);
    (void)s.unify(sym(1), sym(2), kMul, why);
    (void)s.unify(sym(2), literal_extent(64), kAdd, why);
    return s;
}

constexpr ExtentSolver kWhileCompiling = worked_example();

// ---- what an axis is DERIVED from (milestone M5.2) ----------------------------------------------
//
// Written against the solver directly, because no operation records a relation yet: the algebra
// comes before the views that need it, so that each of them is written against something that
// exists. The three users below are the real ones, spelled the way `relate` takes them.

// The size an axis works out to, or ZERO when it does not work out to one yet. `literal_of` refuses
// a symbol — correctly, since asking one for a size is a caller mistake — but a `static_assert` that
// fails by refusing rather than by comparing reports "not a constant expression" and names nothing.
// An assertion is worth as much as the message it fails with.
constexpr std::int32_t resolved_size(const ExtentSolver& s, Extent e) {
    const Extent r = s.resolve(e);
    return is_symbol(r) ? 0 : literal_of(r);
}

constexpr OpName kUnfold{"_unfold"};
constexpr OpName kReshape{"_reshape"};
constexpr OpName kSlice{"_slice"};

// An unfold frames a length `L` into windows of `K` with hop `H`, and its frame count is
// `(L - K)/H + 1`, which is `(1*L + (H - K)) / H`. A FLOOR and not an obligation: a length that
// does not divide evenly yields fewer frames, and that is the operation's meaning.
constexpr ExtentSolver unfold_of(std::int32_t length, std::int32_t window, std::int32_t hop) {
    ExtentSolver s{};
    Diagnostic why{};
    if (!s.relate(1, 0, 1, hop - window, hop, false, kUnfold, why)) {
        fail("the unfold relation must record");
    }
    if (!s.unify(sym(0), literal_extent(length), OpName{"binding"}, why)) {
        fail("the length must bind");
    }
    return s;
}

// 64 framed by 16 with hop 8: (64-16)/8 + 1 = 7 frames, and the source is not a multiple of the hop
// in a way that matters, because nothing here asked it to be.
static_assert(resolved_size(unfold_of(64, 16, 8), sym(1)) == 7,
              "a derived axis does not work out to what its relation says");
// 65 framed the same way is still 7: the last three samples do not make a window. That the floor is
// the MEANING and not a rounding error is exactly why this relation is not exact.
static_assert(resolved_size(unfold_of(65, 16, 8), sym(1)) == 7,
              "a length that does not divide evenly must yield fewer frames, not an error");
static_assert(resolved_size(unfold_of(72, 16, 8), sym(1)) == 8);

// An UNBOUND source leaves the derived axis unresolved rather than guessing — and it comes back as
// its own class, exactly as an ordinary unpinned symbol does.
constexpr ExtentSolver unfold_unbound() {
    ExtentSolver s{};
    Diagnostic why{};
    (void)s.relate(1, 0, 1, -8, 8, false, kUnfold, why);
    return s;
}
static_assert(is_symbol(unfold_unbound().resolve(sym(1))),
              "a derived axis whose source has no size yet must not resolve to one");

// A CHAIN: unfold, then a reshape merging the frame axis with a literal 2. Nothing binds the middle
// axis — it has no size of its own, only a relation — so this is the case a solver that resolved
// one hop at a time would answer wrongly.
constexpr ExtentSolver chained(std::int32_t length) {
    ExtentSolver s{};
    Diagnostic why{};
    if (!s.relate(1, 0, 1, -8, 8, false, kUnfold, why)) {
        fail("the unfold relation must record");
    }
    if (!s.relate(2, 1, 2, 0, 1, false, kReshape, why)) {
        fail("the merge relation must record");
    }
    if (!s.unify(sym(0), literal_extent(length), OpName{"binding"}, why)) {
        fail("the length must bind");
    }
    return s;
}
static_assert(resolved_size(chained(64), sym(2)) == 14, "a chain of two relations must resolve");
static_assert(resolved_size(chained(64), sym(1)) == 7, "and the middle of the chain still resolves");

// ---- the obligation, which is the half M5.2 asks to be shown BOTH ways ---------------------------
//
// A reshape splitting an axis into groups of four must not lose elements, so its division is EXACT
// — and that is a divisibility obligation on the source, recorded now and checked when the source is
// bound. `ExtentHint::divisible_by` has been in this file since M2 with nothing reading it; this is
// its reader.
constexpr ExtentSolver split_by_four() {
    ExtentSolver s{};
    Diagnostic why{};
    if (!s.relate(1, 0, 1, 0, 4, true, kReshape, why)) {
        fail("the split relation must record");
    }
    return s;
}

static_assert(split_by_four().hint_of(0).divisible_by == 4,
              "an exact division must record a divisibility obligation on its source");

// The obligation HOLDS.
constexpr bool binding_holds(std::int32_t size) {
    ExtentSolver s = split_by_four();
    Diagnostic why{};
    return s.unify(sym(0), literal_extent(size), OpName{"binding"}, why);
}
static_assert(binding_holds(64), "64 is a multiple of four and the binding must be accepted");
constexpr std::int32_t split_bound_to(std::int32_t size) {
    ExtentSolver s = split_by_four();
    Diagnostic why{};
    (void)s.unify(sym(0), literal_extent(size), OpName{"binding"}, why);
    return resolved_size(s, sym(1));
}
static_assert(split_bound_to(64) == 16, "and the derived axis then works out to the quotient");

// The obligation DOES NOT hold, and the refusal names the operation that recorded it. That last
// clause is the condition's own, and it is the one worth having: a divisibility nobody wrote asking
// for is a puzzle without the operation's name beside it.
static_assert(!binding_holds(66), "66 is not a multiple of four and the binding must be refused");

constexpr Diagnostic refusal_for(std::int32_t size) {
    ExtentSolver s = split_by_four();
    Diagnostic why{};
    (void)s.unify(sym(0), literal_extent(size), OpName{"binding"}, why);
    return why;
}
static_assert(refusal_for(66).message.view()
                  == std::string_view{"this axis is given the size 66, but it was declared a multiple of 4 at _reshape"},
              "the bind-time refusal must name what broke AND the operation that recorded it");

// ---- three caller mistakes, each refused where it happens ----------------------------------------
constexpr Diagnostic relate_refusal(std::int32_t derived, std::int32_t source, std::int32_t mul,
                                    std::int32_t add, std::int32_t div) {
    ExtentSolver s{};
    Diagnostic why{};
    if (s.relate(derived, source, mul, add, div, false, kSlice, why)) {
        fail("this relation should not have been accepted");
    }
    return why;
}
static_assert(relate_refusal(1, 0, 1, 0, 0).message.view()
                  == std::string_view{"a derived extent needs a positive multiplier and divisor, at _slice"});
static_assert(relate_refusal(1, 1, 1, 0, 1).message.view()
                  == std::string_view{"an axis cannot be derived from itself, at _slice"});

// Derived from an axis already proven EQUAL to it, which is the same mistake wearing a disguise —
// and the one a check on the symbol ids alone would miss.
constexpr Diagnostic circular_through_equality() {
    ExtentSolver s{};
    Diagnostic why{};
    (void)s.unify(sym(0), sym(1), kAdd, why);
    if (s.relate(1, 0, 2, 0, 1, false, kSlice, why)) {
        fail("an axis derived from one it is already equal to should not have been accepted");
    }
    return why;
}
static_assert(circular_through_equality().message.view()
                  == std::string_view{"an axis cannot be derived from itself, at _slice"});

// Two operations deriving one axis differently. Recording the SAME relation twice is fine — the
// pass may reach a node by two paths — and recording a different one is a contradiction.
constexpr bool same_relation_twice() {
    ExtentSolver s{};
    Diagnostic why{};
    return s.relate(1, 0, 1, -8, 8, false, kUnfold, why) && s.relate(1, 0, 1, -8, 8, false, kUnfold, why);
}
static_assert(same_relation_twice(), "recording one relation twice is not a contradiction");

constexpr Diagnostic two_relations() {
    ExtentSolver s{};
    Diagnostic why{};
    (void)s.relate(1, 0, 1, -8, 8, false, kUnfold, why);
    if (s.relate(1, 0, 2, 0, 1, false, kReshape, why)) {
        fail("two different relations for one axis should not have been accepted");
    }
    return why;
}
static_assert(two_relations().message.view()
                  == std::string_view{"one axis is derived two different ways: at _unfold and at _reshape"});

} // namespace

int main() {
    const ExtentSolver while_running = worked_example();
    if (!(while_running == kWhileCompiling)) return 1;

    // Spelled out as well as compared whole, so a failure says which answer moved.
    if (while_running.find(0) != kWhileCompiling.find(0)) return 2;
    if (!same_extent(while_running.resolve(sym(2)), kWhileCompiling.resolve(sym(2)))) return 3;
    if (literal_of(while_running.resolve(sym(0))) != 64) return 4;
    if (!(while_running.hint_of(0) == kWhileCompiling.hint_of(0))) return 5;

    // And the refusals agree too: a rejection that only happened in one world would be the same
    // defect as an answer that only happened in one world.
    ExtentSolver s = while_running;
    Diagnostic why{};
    if (s.unify(sym(0), literal_extent(128), kMul, why)) return 6;
    if (why.message.view() != std::string_view{"one axis is given two sizes: 64 at _add and 128 at _mul"}) return 7;

    // Two nearly-coprime divisibility claims multiply out of range. Under constant evaluation that
    // is a compile error by construction — which is why it cannot be a static_assert — so this is
    // the half that can be observed, and it is observed refusing rather than wrapping.
    try {
        (void)meet(ExtentHint{.divisible_by = 65536}, ExtentHint{.divisible_by = 65535});
        return 8;
    } catch (const error& e) {
        if (e.message() != std::string_view{"heinzel::meet: two divisibility hints multiply out of range for an axis size"}) {
            return 9;
        }
    }

    // And an ordinary pair still meets, so the guard is not simply refusing everything.
    if (meet(ExtentHint{.divisible_by = 4}, ExtentHint{.divisible_by = 6}).divisible_by != 12) return 10;

    // A DERIVED AXIS THAT WORKS OUT TO NOTHING, observed refusing. Framing a length of 8 into
    // windows of 16 is a graph with no frames in it, and an empty axis is a graph that cannot run —
    // so the arithmetic stops where it happened rather than handing back a zero somebody allocates
    // a buffer from. Here rather than as a `static_assert` for the same reason the divisibility
    // overflow above is here: under constant evaluation a `fail()` IS the compile error, so the
    // half that can be observed is this one.
    try {
        (void)unfold_of(8, 16, 8).resolve(sym(1));
        return 11;
    } catch (const error& e) {
        if (e.message() != std::string_view{"heinzel::ExtentSolver: a derived extent works out to zero or fewer elements"}) {
            return 12;
        }
    }

    // And the same derivation at a length that DOES frame still answers, so the guard is not
    // refusing every unfold.
    if (literal_of(unfold_of(24, 16, 8).resolve(sym(1))) != 2) return 13;
    return 0;
}
