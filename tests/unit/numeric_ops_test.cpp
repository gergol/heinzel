// What milestone M4's numeric operations compute, one sample at a time.
//
// The concern here is the ARITHMETIC and only the arithmetic: given operands, does the one writing
// in `runtime/compute.hpp` answer what this operation is supposed to answer, including at the inputs
// where "supposed to" is a decision somebody made rather than a thing everyone agrees on. Those
// decisions are what this file exists for — a comment claiming `sign(NaN)` is NaN is worth exactly
// as much as the check beside it.
//
// It is NOT where the runners are checked. That an operation is run end to end, on the walk and on the
// loop tree alike, is M4.1's condition and lives with the both-ways corpus (`both_ways_test.cpp`); that
// the table knows which rows owe a meaning is `meaning_test.cpp`. This file would pass on a tree with no
// runner at all, and saying so is what keeps the three from being read as one.
//
// Everything here answers EXACTLY, and is therefore checked bitwise: comparison and choice, the
// rounding four, `_fma`, and the two roots. M4's approximations — the exponential and periodic
// families — are `transcendental_test.cpp`, where the claim is a measured bound instead. The split
// happened when this file reached amendment 06's six hundred lines, along a seam already in it.
#include "heinzel/runtime/compute.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

using namespace heinzel;

#include "one_operation.hpp"

using heinzel::testing::apply;
using heinzel::testing::kInf;
using heinzel::testing::kNaN;
using heinzel::testing::negative_zero;

// ---- Min and Max ---------------------------------------------------------------------------------
static_assert(apply(op::Min, 2.0, 3.0) == 2.0 && apply(op::Min, 3.0, 2.0) == 2.0);
static_assert(apply(op::Max, 2.0, 3.0) == 3.0 && apply(op::Max, 3.0, 2.0) == 3.0);
static_assert(apply(op::Min, -kInf, 0.0) == -kInf && apply(op::Max, kInf, 0.0) == kInf);

// The NaN answer is a DECISION, not an accident, so it is pinned rather than left to whatever the
// comparison happens to do. Written as `a < b ? a : b`, a NaN in either operand makes the comparison
// false and the answer is operand 1.
//
// That is the old tree's rule, READ rather than assumed: `git show main:include/heinzel/ir/op_eval.hpp`
// at c4012b8 writes `Max` as `if_else_(a > b, a, b)` and `Min` as `if_else_(a < b, a, b)` — the same
// expression. numpy's `minimum` differs; it propagates NaN from either side. The old tree's rule is
// the one kept, because a graph ported across untouched must behave as it did, which is the same
// reason `Precise` was restored as the default tier (Decision 6).
static_assert(apply(op::Min, kNaN, 1.0) == 1.0, "NaN in operand 0 gives operand 1");
static_assert(apply(op::Max, kNaN, 1.0) == 1.0, "and the same for max, for the same reason");
static_assert(apply(op::Min, 1.0, kNaN) != apply(op::Min, 1.0, kNaN),
              "NaN in operand 1 propagates — the asymmetry is real and is recorded, not hidden");

// Zero has a sign and these two do not distinguish it, which is worth stating because a caller who
// needs `min(-0.0, 0.0)` to be `-0.0` is going to be surprised. `-0.0 < 0.0` is false, so the answer
// is operand 1.
static_assert(apply(op::Min, -0.0, 0.0) == 0.0 && apply(op::Max, -0.0, 0.0) == 0.0);

// ---- Le and Eq -----------------------------------------------------------------------------------
static_assert(apply(op::Le, 1.0, 2.0) == 1.0 && apply(op::Le, 2.0, 2.0) == 1.0 &&
              apply(op::Le, 3.0, 2.0) == 0.0);
static_assert(apply(op::Eq, 2.0, 2.0) == 1.0 && apply(op::Eq, 2.0, 3.0) == 0.0);

// `Le` is not `Lt` under another name, checked at the one input where they differ.
static_assert(apply(op::Le, 2.0, 2.0) != apply(op::Lt, 2.0, 2.0));

// Both answer false for NaN, including `NaN == NaN`, which is the floating-point rule and not a
// mistake to be corrected here.
static_assert(apply(op::Eq, kNaN, kNaN) == 0.0 && apply(op::Le, kNaN, kNaN) == 0.0);

// And `Eq` really is exact rather than tolerant. A tolerance is a decision about what "the same"
// means for a particular graph, and one buried in the writing would be inherited silently by every
// graph in the tree.
static_assert(apply(op::Eq, 0.1 + 0.2, 0.3) == 0.0,
              "the classic pair is not equal, and this operation says so");

// The answer is one or zero as a NUMBER, in whatever type the step rounds it to — there is no
// boolean in this tree's element types, and `Select` reading a comparison depends on that.
static_assert(apply(op::Select, apply(op::Le, 1.0, 2.0), 7.0, 9.0) == 7.0);
static_assert(apply(op::Select, apply(op::Le, 3.0, 2.0), 7.0, 9.0) == 9.0);

// ---- Sign ------------------------------------------------------------------------------------------
static_assert(apply(op::Sign, 3.5) == 1.0 && apply(op::Sign, -3.5) == -1.0);
static_assert(apply(op::Sign, kInf) == 1.0 && apply(op::Sign, -kInf) == -1.0);

// Zero keeps its own sign, and NaN stays NaN — numpy's rule, and the old tree's row says so in as
// many words. Both fall out of writing it as two comparisons and returning the operand otherwise;
// writing it as a difference of comparisons would answer 0.0 for NaN and lose the sign of zero.
static_assert(apply(op::Sign, 0.0) == 0.0 && apply(op::Sign, -0.0) == 0.0);
static_assert([] {
    // `-0.0 == 0.0` is true, so equality cannot see the sign, and dividing by it to find out is not
    // a constant expression. Reading the bit is, and it is the only thing here that has to look at a
    // number's representation rather than its value.
    return negative_zero(apply(op::Sign, -0.0)) && !negative_zero(apply(op::Sign, 0.0));
}(), "sign(0) keeps the zero's own sign, which comparing against 0.0 cannot show");
static_assert(apply(op::Sign, kNaN) != apply(op::Sign, kNaN), "sign(NaN) is NaN");

// ---- Abs -------------------------------------------------------------------------------------------
// The sign bit cleared: both zeros answer +0 (numpy's; `max(x, -x)` answered -0 for +0), an infinity its
// magnitude, a NaN a NaN.
static_assert(apply(op::Abs, -3.5) == 3.5 && apply(op::Abs, 3.5) == 3.5 && apply(op::Abs, -kInf) == kInf);
static_assert(!negative_zero(apply(op::Abs, -0.0)) && !negative_zero(apply(op::Abs, 0.0)), "abs(+-0) is +0");
static_assert(apply(op::Abs, -0x1p-1074) == 0x1p-1074, "a denormal keeps its magnitude");
static_assert(apply(op::Abs, kNaN) != apply(op::Abs, kNaN), "abs(NaN) is NaN");

// ---- Floor, Ceil, Round, Trunc -------------------------------------------------------------------
// Four directions, four cores in `math_cores.hpp`. None of them can be an expression: `std::floor`
// is not usable in a constant expression under the required toolchain (finding M4-A).
static_assert(apply(op::Floor, 1.7) == 1.0 && apply(op::Floor, -1.7) == -2.0);
static_assert(apply(op::Ceil, 1.2) == 2.0 && apply(op::Ceil, -1.2) == -1.0);
static_assert(apply(op::Trunc, 1.7) == 1.0 && apply(op::Trunc, -1.7) == -1.0);
static_assert(apply(op::Floor, 3.0) == 3.0 && apply(op::Ceil, 3.0) == 3.0 &&
              apply(op::Trunc, 3.0) == 3.0 && apply(op::Round, 3.0) == 3.0,
              "an integral value is left alone by all four");

// The four are four. `Floor` and `Trunc` agree on every positive number and disagree on every
// negative one that is not integral, which is the whole difference between them.
static_assert(apply(op::Floor, 2.5) == apply(op::Trunc, 2.5));
static_assert(apply(op::Floor, -2.5) != apply(op::Trunc, -2.5));

// ROUND BREAKS TIES TO EVEN. This is the old tree's rule — `op_eval.hpp` at c4012b8 names it
// `round_rn` and calls `__builtin_rint` — and it is NOT `std::round`, which breaks ties away from
// zero. The two differ on exactly these inputs, so they are the ones pinned.
static_assert(apply(op::Round, 2.5) == 2.0, "half to even, not half away from zero");
static_assert(apply(op::Round, 3.5) == 4.0);
static_assert(apply(op::Round, -2.5) == -2.0);
static_assert(apply(op::Round, -3.5) == -4.0);
static_assert(apply(op::Round, 0.5) == 0.0 && apply(op::Round, 1.5) == 2.0);

// And not by adding a half and truncating, which is the obvious implementation and is wrong here:
// the largest double below a half rounds UP to exactly a half when a half is added to it, and the
// truncation that follows then answers one. The core compares the fractional part instead.
static_assert(apply(op::Round, 0.49999999999999994) == 0.0,
              "the largest double below a half rounds to zero, not to one");

// Zeros keep their sign through all four, which a cast through an integer would not preserve.
static_assert(negative_zero(apply(op::Trunc, -0.0)) && negative_zero(apply(op::Floor, -0.0)));
static_assert(negative_zero(apply(op::Ceil, -0.5)), "ceil of a small negative is negative zero");
static_assert(negative_zero(apply(op::Round, -0.5)), "and so is rint of minus a half");
static_assert(!negative_zero(apply(op::Ceil, 0.5)) && apply(op::Ceil, 0.5) == 1.0);

// Above 2^52 every double is already an integer, and the four leave it alone rather than casting it
// through a `long long` — which past 2^63 would be undefined rather than merely wrong.
static_assert(apply(op::Floor, 9.0e18) == 9.0e18 && apply(op::Round, 9.0e18) == 9.0e18);
static_assert(apply(op::Trunc, kInf) == kInf && apply(op::Floor, -kInf) == -kInf);
static_assert(apply(op::Round, kNaN) != apply(op::Round, kNaN) &&
              apply(op::Floor, kNaN) != apply(op::Floor, kNaN), "NaN through all of them");

// ---- the five are five, not one ------------------------------------------------------------------
// A writing that fell through to a single arm would satisfy many of the checks above by accident.
// This says the five answer differently from each other on one input where they must.
static_assert(apply(op::Min, 4.0, 2.0) == 2.0 && apply(op::Max, 4.0, 2.0) == 4.0 &&
              apply(op::Le, 4.0, 2.0) == 0.0 && apply(op::Eq, 4.0, 2.0) == 0.0 &&
              apply(op::Sign, 4.0) == 1.0);

// ---- `_fma` rounds once, and the same way in both worlds (milestone M4) -------------------------
//
// The input is chosen so that rounding the product first is VISIBLE: `(1 + 2^-27)^2` is
// `1 + 2^-26 + 2^-54`, a double keeps the first two terms, and subtracting one leaves `2^-26` if the
// product was rounded and `2^-26 + 2^-54` if it was not. Everything below is a static_assert, so
// this is the answer during CONSTANT EVALUATION; `the_fma_matches_the_platform()` asks the same
// question at run time, and the two agreeing is the whole point of the operation.
constexpr double kOnePlusHalfAnUlp = 0x1.0000002p0;

static_assert(apply(op::Fma, kOnePlusHalfAnUlp, kOnePlusHalfAnUlp, -1.0) == 0x1.0000001p-26,
              "the product was rounded before the addition, which is the one thing _fma promises not to do");
static_assert(kOnePlusHalfAnUlp * kOnePlusHalfAnUlp - 1.0 == 0x1p-26,
              "...and this is what the ordinary expression gives, so the two really do differ here");

// The exact product is carried as a pair whose sum is the unrounded answer, and `low` is exactly
// what the rounding lost. Asserted directly, because every claim `_fma` makes rests on it.
static_assert([] {
    const TwoDoubles p = exact_product(kOnePlusHalfAnUlp, kOnePlusHalfAnUlp);
    return p.high == 0x1.0000004p0 && p.low == 0x1p-54;
}(), "the exact product does not agree with itself");

// Splitting an operand within 27 binades of overflow needs scaling, and the scaling is exact. Before
// that branch existed every one of these answered NaN.
static_assert(apply(op::Fma, 0x1p1000, 1.5, 0.0) == 0x1.8p1000, "the splitting overflowed");
static_assert(apply(op::Fma, 0x1.fffffffffffffp1023, 1.0, 0.0) == 0x1.fffffffffffffp1023);
static_assert(apply(op::Fma, 0x1p1000, 0x1p-1000, -1.0) == 0.0);

// A zero addend does not perturb the product, and a zero product does not perturb the addend.
static_assert(apply(op::Fma, 3.0, 4.0, 0.0) == 12.0 && apply(op::Fma, 0.0, 5.0, 7.0) == 7.0);

// ...and a zero answer keeps its own sign. `fma(0, -0, -0)` is negative zero, and the first version
// of the core answered positive: the final correction is zero here, and `-0.0 + 0.0` is `+0.0`.
// Checked by reading the sign bit, because nothing about a zero's VALUE distinguishes the two.
static_assert(negative_zero(apply(op::Fma, 0.0, -0.0, -0.0)),
              "the sign of a zero answer was thrown away by adding a zero correction");
static_assert(apply(op::Fma, 0.0, 0.0, -0.0) == 0.0 && !negative_zero(apply(op::Fma, 0.0, 0.0, -0.0)),
              "...and a positive zero product with a negative zero addend is still positive zero");

// THE ONE INPUT CLASS THAT DIFFERS FROM A HARDWARE FUSED MULTIPLY-ADD is checked at RUN TIME, in
// `the_stated_divergence_still_diverges()` below, and everything else about `_fma` on this page is a
// `static_assert`. The reason is not a preference and is worth reading: **a floating-point overflow
// is not a constant expression.** Producing a value outside the representable range is undefined
// behaviour, so a constant evaluator may not fold it — gcc 15.2 says so and refuses, clang 22.1.8
// folds it to infinity and says nothing.
//
// FINDING S5c-GCC, which is what amendment 01's boundary gcc run is for. This assertion was written
// as a `static_assert` and stood green for the whole of M4 because clang is the required toolchain
// and clang is lenient here. It was never a constant expression; it was a constant expression under
// one compiler. Moved rather than deleted — the property it pins is real and is stated beside the
// core — and the move costs nothing but the world it is checked in.

// ---- the square root, and the reciprocal one (milestone M4) --------------------------------------
//
// FINDING S4-A: `_sqrt` was wrong by orders of magnitude outside about [1e-10, 1e15] and had been
// since M1, because forty Newton steps from `guess = a` do not converge for an argument far from
// one. It had no value test at all — only a units test — which is how a defect that large sat in a
// green suite. These assertions are the hole being closed, not decoration.
static_assert(apply(op::Sqrt, 4.0) == 2.0 && apply(op::Sqrt, 0.25) == 0.5);
static_assert(apply(op::Sqrt, 0x1p100) == 0x1p50, "the large end, where forty steps never arrived");
static_assert(apply(op::Sqrt, 0x1p-100) == 0x1p-50, "and the small end");
static_assert(apply(op::Sqrt, 0x0.0000000000001p-1022) == 0x1p-537,
              "a denormal argument, which has to be normalised before its exponent can be halved");
static_assert(apply(op::Sqrt, 2.0) == 0x1.6a09e667f3bcdp0, "and the value everyone knows, to the bit");

// The top of a binade, which is where the correction step's own rounding falls exactly on a midpoint
// and ties to even carry it past the largest root a significand below four can have. Three arguments
// in 1.2 million reached it and all three are of this shape; `sqrt(max)` is one of them.
static_assert(apply(op::Sqrt, 0x1.fffffffffffffp1) == 0x1.fffffffffffffp0);
static_assert(apply(op::Sqrt, 0x1.fffffffffffffp-1) == 0x1.fffffffffffffp-1);
static_assert(apply(op::Sqrt, kLargestFiniteDouble) == 0x1.fffffffffffffp511,
              "the root of the largest double rounded up to a power of two");

// The edges, which are the platform's and the old tree's rather than this tree's invention.
static_assert(negative_zero(apply(op::Sqrt, -0.0)), "sqrt(-0) is -0, and a zero's sign is not a detail");
static_assert(apply(op::Sqrt, 0.0) == 0.0 && !negative_zero(apply(op::Sqrt, 0.0)));
static_assert([] {
    const double answer = apply(op::Sqrt, -1.0);
    return answer != answer;                        // the only way to ask "is this NaN" without <cmath>
}(), "sqrt of a negative is NaN — it answered ZERO before this milestone, silently");

static_assert(apply(op::Rsqrt, 4.0) == 0.5 && apply(op::Rsqrt, 0x1p100) == 0x1p-50);
static_assert(apply(op::Rsqrt, 0.0) == kInf && apply(op::Rsqrt, -0.0) == -kInf,
              "1/sqrt(-0) is -inf, because sqrt(-0) is -0 — the sign survives the division");
static_assert(apply(op::Rsqrt, kInf) == 0.0);
static_assert([] {
    const double answer = apply(op::Rsqrt, -1.0);
    return answer != answer;
}(), "1/sqrt of a negative is NaN");

namespace {

// ---- the same operations against the platform's own, at run time ---------------------------------
//
// The static_asserts above pin the DECISIONS: the tie rule, the signs of zero, the boundary at 2^52,
// what `_fma` promises. What they cannot do is sweep, because a constant-evaluated loop over
// hundreds of thousands of values is a cost paid by every translation unit that includes this file.
// So the sweeps run at run time, where `<cmath>` is available — and it being available at run time
// and NOT while compiling is exactly finding M4-A, which is why these cores exist at all.
//
// Everything swept here is compared BITWISE, because everything here answers exactly. The
// approximations and their measured bounds are `transcendental_test.cpp`.
//
// WHAT RUNS HERE since the hardware twins (`runtime/hardware_twins.hpp`): at run time `apply` reaches
// the processor's instruction for the root (and, where the target has them, the roundings and the fused
// multiply-add), so these sweeps compare the twin with the platform. The software cores are swept
// against their twins in `hardware_twins_test.cpp`, and against the platform here whenever the build
// is configured with `HEINZEL_HARDWARE_TWINS=OFF`.
//
// `std::rint` and not `std::round` is the reference for `_round`, and that choice is the point: the
// default floating-point rounding mode is round-to-nearest-even, `rint` follows it, and `std::round`
// does not. A sweep against `std::round` would fail on every half-integer, which is how a wrong
// reference announces itself if anybody ever swaps it.
bool agrees_with_the_platform() {
    const auto same = [](double ours, double theirs) {
        // Bitwise, so a zero of the wrong sign is a failure rather than a pass. NaN is excluded here
        // and pinned by the assertions above, because no two NaNs compare equal by bits reliably.
        return std::bit_cast<std::uint64_t>(ours) == std::bit_cast<std::uint64_t>(theirs);
    };

    // A sweep dense enough to land on halves, quarters and the values just either side of them.
    for (int i = -400000; i <= 400000; ++i) {
        const double x = static_cast<double>(i) / 8.0;
        if (!same(apply(op::Floor, x), std::floor(x)) || !same(apply(op::Ceil, x), std::ceil(x)) ||
            !same(apply(op::Trunc, x), std::trunc(x)) || !same(apply(op::Round, x), std::rint(x))) {
            return false;
        }
    }

    // And the awkward ones by name, because a regular sweep steps straight over them.
    const double awkward[] = {0.0,
                              -0.0,
                              0.49999999999999994,
                              -0.49999999999999994,
                              0.5,
                              -0.5,
                              1.5,
                              -1.5,
                              2.5,
                              -2.5,
                              4503599627370495.5,
                              -4503599627370495.5,
                              4503599627370496.0,
                              9.0e18,
                              -9.0e18,
                              kInf,
                              -kInf};
    for (const double x : awkward) {
        if (!same(apply(op::Floor, x), std::floor(x)) || !same(apply(op::Ceil, x), std::ceil(x)) ||
            !same(apply(op::Trunc, x), std::trunc(x)) || !same(apply(op::Round, x), std::rint(x))) {
            return false;
        }
    }
    return true;
}

// The same question at RUN time, against the platform's `std::fma` — which is the hardware
// instruction, and the only reference available. What is being checked is not that the core is
// clever but that it is the same core in both worlds: every input below is also reachable by the
// constant evaluator, and the assertions above pin a representative of each family there.
//
// Three families, and the middle one is the one that matters. Random triples across many binades
// would let almost any emulation through; near-total cancellation, where the addend sits within a
// few ulps of minus the product, is where an implementation that rounded early is visibly wrong.
bool the_fma_matches_the_platform() {
    const auto same = [](double ours, double theirs) {
        return std::bit_cast<std::uint64_t>(ours) == std::bit_cast<std::uint64_t>(theirs);
    };

    // A deterministic spread of magnitudes and significands. No random engine: a test that picks
    // different inputs on different days reports a different thing on different days.
    for (int ea = -60; ea <= 60; ea += 3) {
        for (int eb = -60; eb <= 60; eb += 3) {
            for (int k = 1; k <= 40; ++k) {
                const double a = std::ldexp(1.0 + static_cast<double>(k) / 41.0, ea);
                const double b = std::ldexp(1.0 + static_cast<double>(41 - k) / 41.0, eb);
                for (const double c : {1.0, -1.0, std::ldexp(1.0, ea + eb), 0.0}) {
                    if (!same(apply(op::Fma, a, b, c), std::fma(a, b, c))) {
                        return false;
                    }
                }
            }
        }
    }

    // Near-total cancellation: the addend a few ulps either side of minus the rounded product.
    for (int ea = -40; ea <= 40; ea += 2) {
        for (int k = 1; k <= 60; ++k) {
            const double a = std::ldexp(1.0 + static_cast<double>(k) / 61.0, ea);
            const double b = 1.0 + static_cast<double>(61 - k) / 61.0;
            const std::uint64_t cancelling = std::bit_cast<std::uint64_t>(-(a * b));
            for (int step = -4; step <= 4; ++step) {
                const double c = std::bit_cast<double>(cancelling + static_cast<std::uint64_t>(step));
                if (!same(apply(op::Fma, a, b, c), std::fma(a, b, c))) {
                    return false;
                }
            }
        }
    }

    // And the ends of the range by name, where the splitting needs its scaling and where the
    // operands stop being finite. `std::fma` is the reference for all of them.
    const double edges[] = {0x1p996, 0x1p1000, 0x1.fffffffffffffp1023, 0x1p-1060, 0x1p-540, 0.0, -0.0};
    for (const double x : edges) {
        for (const double y : edges) {
            if (!same(apply(op::Fma, x, y, 1.0), std::fma(x, y, 1.0)) ||
                !same(apply(op::Fma, x, y, -x), std::fma(x, y, -x))) {
                return false;
            }
        }
    }

    // Not finite: an infinite or undefined operand answers what the rounded arithmetic answers,
    // which is what a fused multiply-add answers too. Compared by predicate rather than by bits,
    // because two NaNs are not required to share a bit pattern.
    const bool infinities = same(apply(op::Fma, kInf, 2.0, 1.0), std::fma(kInf, 2.0, 1.0)) &&
                            same(apply(op::Fma, -kInf, 2.0, 1.0), std::fma(-kInf, 2.0, 1.0));
    const bool undefined = std::isnan(apply(op::Fma, kNaN, 2.0, 1.0)) &&
                           std::isnan(apply(op::Fma, 0.0, kInf, 1.0));
    return infinities && undefined;
}

// The square root against the platform's, over EVERY binade a double has — which is the sweep that
// would have caught finding S4-A on the day it was written, and the reason it is written this way
// rather than at a handful of friendly arguments.
//
// Bitwise, not within a tolerance. Newton alone left 28350 of these 134272 arguments one ulp out;
// the correction step using the exact product closed every one, so a tolerance here would be a
// tolerance for a defect that no longer exists. `1.0 / std::sqrt(x)` is the reference for the
// reciprocal because it is what the old tree computes (`rsqrt_` at `c4012b8`), not an approximation
// of it.
bool the_roots_match_the_platform() {
    const auto same = [](double ours, double theirs) {
        return std::bit_cast<std::uint64_t>(ours) == std::bit_cast<std::uint64_t>(theirs);
    };

    for (int exponent = -1074; exponent <= 1023; ++exponent) {
        for (int k = 0; k < 16; ++k) {
            const double x = std::ldexp(1.0 + static_cast<double>(k) / 16.0, exponent);
            if (x == 0.0 || !std::isfinite(x)) {
                continue;                            // the ends of the range are named below instead
            }
            if (!same(apply(op::Sqrt, x), std::sqrt(x)) ||
                !same(apply(op::Rsqrt, x), 1.0 / std::sqrt(x))) {
                return false;
            }
        }
    }

    const double named[] = {0.0,          -0.0,        kInf,
                            kLargestFiniteDouble, 0x1p-1074,   0x1p-1022,
                            0x1.fffffffffffffp1,  0x1.fffffffffffffp-1};
    for (const double x : named) {
        if (!same(apply(op::Sqrt, x), std::sqrt(x))) {
            return false;
        }
    }
    return std::isnan(apply(op::Sqrt, -1.0)) && std::isnan(apply(op::Sqrt, kNaN)) &&
           std::isnan(apply(op::Rsqrt, -1.0)) && std::isnan(apply(op::Rsqrt, kNaN));
}

} // namespace

// The input class the first core got wrong by construction: the product overflows while the true
// `a*b + c` is finite, which needs an addend cancelling a product already past the largest double.
// Carrying the exponent as an integer (`runtime/fused_multiply_add.hpp`) recovers it, so
// `max * 2 - max` is `max` here as on hardware.
//
// At run time rather than while compiling, and not by choice: see the note above, and
// `docs/decisions.md`.
bool an_overflowing_product_is_recovered() {
    const double largest = 0x1.fffffffffffffp1023;
    return apply(op::Fma, largest, 2.0, -largest) == largest;
}

int main() {
    return agrees_with_the_platform() && the_fma_matches_the_platform()
                   && the_roots_match_the_platform() && an_overflowing_product_is_recovered()
               ? 0
               : 1;
}
