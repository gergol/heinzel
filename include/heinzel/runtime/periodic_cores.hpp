#pragma once

// The periodic family: `sin`, `cos`, `tan` and `sinpi`. Two reductions, which is why `sinpi` is its own
// operation rather than `sin(pi * x)`: the radian functions subtract a multiple of pi/2, which no double
// holds exactly, so their accuracy degrades as the argument grows; `sinpi` counts half-turns, so its
// reduction subtracts an integer and is exact at every double. `sinpi(1e15)` is meaningful; `sin(1e15)`
// is a question about the sixtieth digit of pi.

#include "heinzel/runtime/exact_arithmetic.hpp"
#include "heinzel/runtime/float_bits.hpp"
#include "heinzel/runtime/rounding_cores.hpp"
#include "heinzel/runtime/series_length.hpp"

namespace heinzel {

// ---- the shared polynomials, on a reduced argument -----------------------------------------------
//
// Taylor series in `z = r * r`, over `|r| <= pi/4`. The first dropped term of the sine is
// `r^19/19!`, which is below 1e-19 there — two orders under an ulp of the value the series is
// centred on, so the rounding of the evaluation dominates it and adding terms would buy nothing.
//
// The tier is spent here and nowhere else in this file. The reduction is exact by construction and is
// what makes an answer far from zero mean anything, so a coarse tier gives up polynomial degree only.
//
// Four tables for four polynomials, and they do not share lengths. `z <= (pi/4)^2 = 0.6169` for the
// radian pair and `z <= 1/16` for the half-turn pair, so the same number of terms buys wildly
// different accuracy in the two — and within each pair the sine's dropped term is divided by
// `(2n+1)!` against the cosine's `(2n)!`, which is a whole extra factor. One table for the family
// would have to be the worst of the four.
//
//   sine    z^n/(2n+1)! times |r| <= pi/4      approx 4 terms  3.1e-7
//   cosine  z^n/(2n)!                          approx 5 terms  2.5e-8
//
// The approx bound is 5e-6 for `_sin` and `_cos` alike.
//
// Neither operation is its own polynomial, which is why `_sin` and `_cos` measure the same to three
// digits at the approx tier. The quadrant switch below sends `sin` through the cosine polynomial on
// two quadrants of four and `cos` through the sine polynomial on the other two, so each operation's
// worst case is the worse of the two — which is the sine's, at 3.1e-7.
//
// The cosine's approx length is discretionary: four would be 3.6e-6, inside the bound, and five is
// chosen so that the sine stays the binding constraint. One polynomial defining the family's accuracy
// and the other comfortably clear of it is easier to reason about than two that are each nearly
// binding, and it is what makes the table above readable as "the sine's numbers are the family's".
inline constexpr SeriesLength kSineSeries{9, 4};
inline constexpr SeriesLength kCosineSeries{10, 5};
static_assert(is_a_ladder(kSineSeries) && is_a_ladder(kCosineSeries));

constexpr double sine_of_reduced(double r, int terms) {
    constexpr double kOddCoefficient[9] = {
        0x1.0000000000000p+0,   -0x1.5555555555555p-3,  0x1.1111111111111p-7,
        -0x1.a01a01a01a01ap-13, 0x1.71de3a556c734p-19,  -0x1.ae64567f544e4p-26,
        0x1.6124613a86d09p-33,  -0x1.ae7f3e733b81fp-41, 0x1.952c77030ad4ap-49,
    };
    static_assert(kSineSeries.exact == sizeof(kOddCoefficient) / sizeof(double));
    static_assert(kSineSeries.approx >= 1, "a Horner evaluation needs a first term");
    const double z = r * r;
    double accumulated = kOddCoefficient[terms - 1];
    for (int term = terms - 2; term >= 0; --term) {
        accumulated = accumulated * z + kOddCoefficient[term];
    }
    return r * accumulated;
}

constexpr double cosine_of_reduced(double r, int terms) {
    constexpr double kEvenCoefficient[10] = {
        0x1.0000000000000p+0,   -0x1.0000000000000p-1,  0x1.5555555555555p-5,
        -0x1.6c16c16c16c17p-10, 0x1.a01a01a01a01ap-16,  -0x1.27e4fb7789f5cp-22,
        0x1.1eed8eff8d898p-29,  -0x1.93974a8c07c9dp-37, 0x1.ae7f3e733b81fp-45,
        -0x1.6827863b97d97p-53,
    };
    static_assert(kCosineSeries.exact == sizeof(kEvenCoefficient) / sizeof(double));
    static_assert(kCosineSeries.approx >= 1, "a Horner evaluation needs a first term");
    const double z = r * r;
    double accumulated = kEvenCoefficient[terms - 1];
    for (int term = terms - 2; term >= 0; --term) {
        accumulated = accumulated * z + kEvenCoefficient[term];
    }
    return accumulated;
}

// ---- reducing radians by pi/2 --------------------------------------------------------------------
//
// `x - k*(pi/2)` where no double holds pi/2. The usual arrangement splits the constant into pieces
// whose low bits are clear, so that `k` times each piece is an exact product — Cody and Waite's. That
// is wrong above about 2^23: the leading piece carries 30 significant bits, `k` reaches 30 more, and
// 60 bits do not fit in a double. Measured, the error at an argument of 1e9 was five trillion ulps —
// not a degraded answer, a meaningless one.
//
// So the product is taken exactly instead (`runtime/exact_arithmetic.hpp`). `exact_product(k, pi/2)`
// carries the whole thing as a pair whatever size `k` is, and there is no piece-boundary to fall off.
// Two tail terms follow, which are the bits of pi/2 that the leading double does not hold.
inline constexpr double kHalfPi = 0x1.921fb54442d18p+0;
inline constexpr double kHalfPiTailFirst = 0x1.1a62633145c07p-54;
inline constexpr double kHalfPiTailSecond = -0x1.f1976b7ed8fbcp-110;
inline constexpr double kTwoOverPi = 0x1.45f306dc9c883p-1;

// Beyond this the reduction is not worth doing, and the number is measured rather than chosen. A
// double this large has an ulp comparable to the answer's own scale, so the reduction would be
// asking for bits of pi/2 that no finite expansion here carries. The platform's library reduces
// against a stored expansion of 2/pi (Payne and Hanek) and keeps going; this tree does not, and
// answers NaN rather than a number it cannot stand behind. The operation for that range is `_sinpi`,
// whose reduction is a subtraction of an integer and is exact at every argument a double can hold.
inline constexpr double kRadianReductionGivesUpAbove = 0x1p40;

struct ReducedAngle {
    double remainder{0.0};      // in [-pi/4, pi/4]
    int quarter_turns{0};       // how many quarter turns were taken out, modulo four
    bool usable{true};
};

constexpr ReducedAngle reduce_by_half_pi(double x) {
    if (x != x || !is_finite(x) || x > kRadianReductionGivesUpAbove
        || x < -kRadianReductionGivesUpAbove) {
        return {0.0, 0, false};
    }
    const double whole = round_to_nearest_even(x * kTwoOverPi);
    const TwoDoubles taken_out = exact_product(whole, kHalfPi);

    // `x - taken_out.high` is exact: the two are within a factor of two of each other by
    // construction, which is Sterbenz's condition. Everything after it is a correction.
    double remainder = (x - taken_out.high) - taken_out.low;
    remainder -= whole * kHalfPiTailFirst;
    remainder -= whole * kHalfPiTailSecond;

    // `whole` is integral and bounded by the check above, so this cast is exact and the mask is its
    // remainder modulo four — which is the quadrant, and all of the quadrant that is wanted.
    const long long turns = static_cast<long long>(whole);
    return {remainder, static_cast<int>(((turns % 4) + 4) % 4), true};
}

// ---- sine, cosine, tangent -----------------------------------------------------------------------
//
// Measured against the platform, over three ranges each, because the interesting question about a
// periodic function is not its accuracy near zero but whether the reduction still holds far out:
//
//     sin   [-pi, pi]     400001 arguments   <= 2 ulp     332614 bit-exact
//     sin   [-1e3, 1e3]   400001 arguments   <= 2 ulp     323140 bit-exact
//     sin   [-1e12, 1e12] 200001 arguments   <= 2 ulp     161665 bit-exact
//     cos   [-pi, pi]     400001 arguments   <= 2 ulp     324502 bit-exact
//     cos   [-1e12, 1e12] 200001 arguments   <= 2 ulp     161583 bit-exact
//     tan   [-1.5, 1.5]   400001 arguments   <= 3 ulp     231916 bit-exact
//     tan   [-1e9, 1e9]   200001 arguments   <= 3 ulp     111661 bit-exact
//
// The flatness of those columns across twelve orders of magnitude is the point: it is what the
// exact-product reduction bought, and what the Cody-Waite one did not have.
//
// The tangent is allowed one more ulp than the other two and earns it honestly — it is a quotient of
// two approximations, so its relative error is theirs added, and near an odd multiple of pi/2 the
// denominator is small and amplifies what is left.

constexpr double sine(double x, FnAccuracy fn) {
    if (x == 0.0) {
        return x;                                   // sin is odd, so sin(-0) is -0, and the
                                                    // reduction below cannot carry a zero's sign
    }
    const ReducedAngle angle = reduce_by_half_pi(x);
    if (!angle.usable) {
        return kQuietNaN;
    }
    switch (angle.quarter_turns) {
    case 0:
        return sine_of_reduced(angle.remainder, terms_at(kSineSeries, fn));
    case 1:
        return cosine_of_reduced(angle.remainder, terms_at(kCosineSeries, fn));
    case 2:
        return -sine_of_reduced(angle.remainder, terms_at(kSineSeries, fn));
    default:
        return -cosine_of_reduced(angle.remainder, terms_at(kCosineSeries, fn));
    }
}

constexpr double cosine(double x, FnAccuracy fn) {
    const ReducedAngle angle = reduce_by_half_pi(x);
    if (!angle.usable) {
        return kQuietNaN;
    }
    switch (angle.quarter_turns) {
    case 0:
        return cosine_of_reduced(angle.remainder, terms_at(kCosineSeries, fn));
    case 1:
        return -sine_of_reduced(angle.remainder, terms_at(kSineSeries, fn));
    case 2:
        return -cosine_of_reduced(angle.remainder, terms_at(kCosineSeries, fn));
    default:
        return sine_of_reduced(angle.remainder, terms_at(kSineSeries, fn));
    }
}

// The quotient, computed from one reduction rather than from two calls — so the numerator and the
// denominator are always the same angle, which two separate reductions of the same argument would
// also give but only by luck of them agreeing.
constexpr double tangent(double x, FnAccuracy fn) {
    if (x == 0.0) {
        return x;                                   // odd, like the sine
    }
    const ReducedAngle angle = reduce_by_half_pi(x);
    if (!angle.usable) {
        return kQuietNaN;
    }
    const double s = sine_of_reduced(angle.remainder, terms_at(kSineSeries, fn));
    const double c = cosine_of_reduced(angle.remainder, terms_at(kCosineSeries, fn));
    const bool quarter_turn_taken = (angle.quarter_turns % 2) != 0;
    const double numerator = quarter_turn_taken ? c : s;
    const double denominator = quarter_turn_taken ? -s : c;
    if (denominator == 0.0) {
        // The platform answers a very large number here rather than an infinity, because no double
        // argument is exactly pi/2. This tree cannot reach the case either — `denominator` is a
        // polynomial in a non-zero remainder — but a division that could be by zero is not something
        // to leave to an argument, since a constant expression may not perform one.
        return numerator < 0.0 ? -kPositiveInfinity : kPositiveInfinity;
    }
    return numerator / denominator;
}

// ---- half-turns, where the reduction is exact ----------------------------------------------------
//
// `sinpi(x)` is `sin(pi * x)`, and it is a different operation rather than a convenience. The
// coefficients below have pi folded in, so nothing multiplies the argument by pi at run time: the
// reduction is `x` minus an integer, exact for every double below 2^52, and past that every double
// is an integer and the answer is a signed zero.
//
// Measured with a reference built to be fair to it — the exact reduction done in long double, and
// the exact answer at integers, because `sin(pi * x)` in any finite precision is not the reference
// for this function and using it would have scored the core against its own advantage:
//
//     sinpi [-200, 200]    400001 arguments   <= 2 ulp   322483 bit-exact
//     sinpi [1e14, 2e15]   200000 arguments   =  0 ulp   200000 bit-exact — every one exact
//
// That second row is the operation's whole reason for existing. `sin` cannot be asked those
// arguments at all: they are past where any reduction here can answer. Written as `sin(pi * x)`,
// `sinpi(1)` would be 1.22e-16; here it is zero, which is the answer.

// The half-turn pair's lengths. The reduced argument is at most a quarter turn, so `z = f^2 <= 1/16`
// against the radian pair's 0.6169 — nearly ten times smaller, and it shows: the same term count
// buys about two more digits here. What it does not change is the shape of the argument, so the two
// tables below are the radian pair's with the interval substituted, and pi folded into each
// coefficient rather than multiplied in at run time.
//
//   sine of half turns    pi^(2n+1)/(2n+1)! * z^n * |f|   approx 4 terms 3.1e-7
//   cosine of half turns  pi^(2n)/(2n)! * z^n             approx 5 terms 2.5e-8
//
// `_sinpi` reaches both through the same quadrant switch, so its measured bound is the sine's, for
// the reason given above — and it measures identically to `_sin` at the approx tier.
//
// Identical to the radian pair's numbers, and not by coincidence: pi^(2n) against a `z` sixteen times
// smaller is `(pi^2/16)^n`, and `pi^2/16` is 0.617 — the radian pair's `z` bound exactly. The two
// polynomials are the same series in a rescaled variable, so the tier costs the same in both, and
// `_sinpi`'s bounds are `_sin`'s.
inline constexpr SeriesLength kSineOfHalfTurnsSeries{8, 4};
inline constexpr SeriesLength kCosineOfHalfTurnsSeries{9, 5};
static_assert(is_a_ladder(kSineOfHalfTurnsSeries) && is_a_ladder(kCosineOfHalfTurnsSeries));

constexpr double sine_of_half_turns_reduced(double f, int terms) {
    constexpr double kOddCoefficient[8] = {
        0x1.921fb54442d18p+1,  -0x1.4abbce625be53p+2,  0x1.466bc6775aae2p+1,  -0x1.32d2cce62bd86p-1,
        0x1.50783487ee782p-4,  -0x1.e3074fde8871fp-8,  0x1.e8f434d018d63p-12, -0x1.6fadb9f155744p-16,
    };
    static_assert(kSineOfHalfTurnsSeries.exact == sizeof(kOddCoefficient) / sizeof(double));
    static_assert(kSineOfHalfTurnsSeries.approx >= 1, "a Horner evaluation needs a first term");
    const double z = f * f;
    double accumulated = kOddCoefficient[terms - 1];
    for (int term = terms - 2; term >= 0; --term) {
        accumulated = accumulated * z + kOddCoefficient[term];
    }
    return f * accumulated;
}

constexpr double cosine_of_half_turns_reduced(double f, int terms) {
    constexpr double kEvenCoefficient[9] = {
        0x1.0000000000000p+0,   -0x1.3bd3cc9be45dep+2,  0x1.03c1f081b5ac4p+2,
        -0x1.55d3c7e3cbffap+0,  0x1.e1f506891babbp-3,   -0x1.a6d1f2a204a8cp-6,
        0x1.f9d38a3763cc3p-10,  -0x1.b6e24f44b128fp-14, 0x1.20c62c2f2d7f5p-18,
    };
    static_assert(kCosineOfHalfTurnsSeries.exact == sizeof(kEvenCoefficient) / sizeof(double));
    static_assert(kCosineOfHalfTurnsSeries.approx >= 1, "a Horner evaluation needs a first term");
    const double z = f * f;
    double accumulated = kEvenCoefficient[terms - 1];
    for (int term = terms - 2; term >= 0; --term) {
        accumulated = accumulated * z + kEvenCoefficient[term];
    }
    return accumulated;
}

constexpr double sine_of_half_turns(double x, FnAccuracy fn) {
    if (x != x || !is_finite(x)) {
        return x != x ? x : kQuietNaN;              // sin of an infinite angle has no value
    }
    if (has_no_fractional_part(x)) {
        // Every double this large is an integer, so the answer is a zero carrying the argument's
        // sign — the same rule the integer case below states, applied where doubling `x` would
        // overflow before the reduction could run.
        return with_sign_of(0.0, x);
    }
    const double twice = x * 2.0;                   // exact: multiplying by a power of two
    const double whole = round_to_nearest_even(twice);
    const double f = (twice - whole) * 0.5;         // exact, and |f| <= 1/4
    const long long taken = static_cast<long long>(whole);
    const int quarter_turns = static_cast<int>(((taken % 4) + 4) % 4);

    // An integer argument, where the answer is a zero and the only question is its sign. Taken here
    // rather than left to `-sine(0)` below, which answers negative zero for both `1` and `-1` and so
    // is not odd. The zero carries the sign of the argument, which makes `sinpi(-x) == -sinpi(x)` hold
    // at every integer including zero itself.
    if (f == 0.0 && quarter_turns % 2 == 0) {
        return with_sign_of(0.0, x);
    }

    switch (quarter_turns) {
    case 0:
        return sine_of_half_turns_reduced(f, terms_at(kSineOfHalfTurnsSeries, fn));
    case 1:
        return cosine_of_half_turns_reduced(f, terms_at(kCosineOfHalfTurnsSeries, fn));
    case 2:
        return -sine_of_half_turns_reduced(f, terms_at(kSineOfHalfTurnsSeries, fn));
    default:
        return -cosine_of_half_turns_reduced(f, terms_at(kCosineOfHalfTurnsSeries, fn));
    }
}

} // namespace heinzel
