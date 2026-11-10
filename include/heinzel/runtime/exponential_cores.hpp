#pragma once

// The exponential family: `exp`, `log`, `exp2`, `log2`, `log10`, and `tanh`, a ratio of exponentials.
// One idea runs through them: take the exponent out and approximate what is left over a narrow interval.
// The three logarithms share the reduction outright, so they cannot disagree about the hard half.

#include "heinzel/runtime/exact_arithmetic.hpp"
#include "heinzel/runtime/float_bits.hpp"
#include "heinzel/runtime/rounding_cores.hpp"
#include "heinzel/runtime/series_length.hpp"

namespace heinzel {

// ---- the exponential family, and the reduction they share ----------------------------------------
//
// `Exact` here means these cores, not the platform's library, which a constant expression cannot call.
// How close they come to the platform is measured, recorded beside each core and gated by the sweeps in
// `tests/unit/transcendental_test.cpp`.
//
// Each core takes the tier it runs at, and the tier chooses how many terms of its series it evaluates.
// The reduction is shared by both tiers, so the coarse tier gives up polynomial degree and nothing
// else. Every bound is argued from the first dropped term where the length is declared, and measured
// against the exact core: `docs/measurements/tier-accuracy.md` is the table, `tests/unit/tier_bounds_test.cpp`
// the gate. The tier is a parameter rather than a second set of functions (`exp_approx` beside `exp`)
// because those would be a second writing of the reduction, the hard part both tiers agree about.

// ln 2, split so that a multiple of it can be subtracted from an argument without losing the low
// bits of the argument. `kNaturalLogOfTwoHigh` has its bottom 29 bits clear, so `k * high` is exact
// for every `k` a reduction produces, and the remainder is corrected by the second term. This is
// Cody and Waite's arrangement and the constants are fdlibm's.
inline constexpr double kNaturalLogOfTwoHigh = 0x1.62e42fee00000p-1;
inline constexpr double kNaturalLogOfTwoLow = 0x1.a39ef35793c76p-33;
inline constexpr double kOneOverNaturalLogOfTwo = 0x1.71547652b82fep0;
inline constexpr double kOneOverNaturalLogOfTen = 0x1.bcb7b1526e50ep-2;
// log10(2), split for the same reason `ln 2` is: the exponent's contribution to a base-ten
// logarithm is a multiple of it, and rounding that multiple once costs an ulp of the answer. As one
// number it measured 2 ulp against the platform, 78% of arguments bit-exact; split, 1 ulp and 99%.
inline constexpr double kLogBaseTenOfTwoHigh = 0x1.3441350000000p-2;
inline constexpr double kLogBaseTenOfTwoLow = 0x1.3ef3fde623e25p-31;

// Beyond these the answer is not a double: `exp` of anything above the first overflows, and of
// anything below the second underflows to zero. Compared before any arithmetic happens, so an
// infinite argument is answered rather than multiplied.
inline constexpr double kExpOverflowsAbove = 709.782712893384;
inline constexpr double kExpUnderflowsBelow = -745.1332191019411;

// How long the series below is at each tier, and what each length delivers. The interval is
// |r| <= ln(2)/2 = 0.34657, and the error of a truncation is its first dropped term, r^n/n!:
//
//                        dropped     predicted   measured (relative, over every argument whose
//                                                answer is a normal double)
//   exact  14 terms   r^14/14!    below 5e-18   2.2e-16 — a fortieth of an ulp at the value 1 the
//                                               series is centred on, so the Horner evaluation's
//                                               own rounding dominates it, which is what the
//                                               measurement says too
//   approx  6 terms   r^6/6!      2.4e-6        3.24e-6, against `_exp`'s approx bound of 3e-5
//                                               and `_exp2`'s of 4e-5
//
// The measured number is a third larger than the dropped term alone, which is the
// reduction's own rounding showing through once the series stops dominating it — worth saying,
// because it is the reason the prediction is an argument for the gate rather than the gate.
//
// `_exp` and `_exp2` measure identically at every tier, to every digit. They share this series over
// an interval of the same width, and their reductions are both exact, so there is nothing left for
// them to differ about.
inline constexpr SeriesLength kExponentialSeries{14, 6};
static_assert(is_a_ladder(kExponentialSeries));

// exp(r) for |r| <= ln(2)/2, by its Taylor series, to as many terms as the tier pays for.
constexpr double exponential_of_small(double r, int terms) {
    constexpr double kReciprocalFactorial[14] = {
        1.0,
        1.0,
        1.0 / 2.0,
        1.0 / 6.0,
        1.0 / 24.0,
        1.0 / 120.0,
        1.0 / 720.0,
        1.0 / 5040.0,
        1.0 / 40320.0,
        1.0 / 362880.0,
        1.0 / 3628800.0,
        1.0 / 39916800.0,
        1.0 / 479001600.0,
        1.0 / 6227020800.0,
    };
    // The length table against the array it indexes. Two numbers that have to agree and are written
    // in two places, so a table saying sixteen would read past the end — a compile error while a
    // graph is being built, and a silent read of whatever follows while one is running.
    static_assert(kExponentialSeries.exact == sizeof(kReciprocalFactorial) / sizeof(double));
    static_assert(kExponentialSeries.approx >= 1, "a Horner evaluation needs a first term");
    double accumulated = kReciprocalFactorial[terms - 1];
    for (int term = terms - 2; term >= 0; --term) {
        accumulated = accumulated * r + kReciprocalFactorial[term];
    }
    return accumulated;
}

// e to the x. The argument is written as `k * ln2 + r` with `|r| <= ln(2)/2`, so the answer is
// `2^k * exp(r)` — an exact scaling and a series over a narrow interval.
//
// Measured against `std::exp` over the whole domain in steps of 1/100 (141801 arguments):
// at most 1 ulp at `Exact`, and 127544 of them bit-identical. Never 2. The coarse tier's
// measured relative error over the same sweep is recorded with `kExponentialSeries` above.
//
// The reduction is the same at every tier, deliberately. It is exact — `k * ln2_high` has its low
// bits clear by construction — so a coarse tier that also cut it would be giving up the one part
// that costs nothing to keep and would stop being a comparison against the exact core.
constexpr double exponential(double x, FnAccuracy fn) {
    if (x != x) {
        return x;
    }
    if (x > kExpOverflowsAbove) {
        return kPositiveInfinity;                   // and this is the answer for +infinity too
    }
    if (x < kExpUnderflowsBelow) {
        return 0.0;
    }
    const double whole = round_to_nearest_even(x * kOneOverNaturalLogOfTwo);
    const double remainder = (x - whole * kNaturalLogOfTwoHigh) - whole * kNaturalLogOfTwoLow;
    return exponential_of_small(remainder, terms_at(kExponentialSeries, fn))
           * two_to_the(static_cast<int>(whole));
}

// Two to the x, and NOT `exp(x * ln2)` — that spelling answers something a hair away from a power of
// two at every integer, and a power of two is the one answer anybody checks. Splitting off the whole
// part first is exact, so an integral argument leaves a zero remainder and the series returns one.
//
// Measured against `std::exp2` over the whole domain in steps of 1/100 (204601 arguments):
// at most 1 ulp at `Exact`, 176510 bit-identical, and exact at every integer including -1074,
// where the answer is the smallest denormal.
//
// Exact at every integer at every tier: a coarse tier changes how many terms follow the series' first
// term, one, but not what the series answers at a zero remainder.
constexpr double two_to_the_power(double x, FnAccuracy fn) {
    if (x != x) {
        return x;
    }
    if (x >= 1024.0) {
        return kPositiveInfinity;
    }
    if (x < -1075.0) {
        return 0.0;
    }
    const double whole = round_to_nearest_even(x);
    const double remainder = x - whole;             // exact: both are doubles and |remainder| <= 1/2
    return exponential(remainder * (kNaturalLogOfTwoHigh + kNaturalLogOfTwoLow), fn)
           * two_to_the(static_cast<int>(whole));
}

// The natural log of a significand in [1/sqrt2, sqrt2), together with the exponent that was taken
// out. The three logarithms differ only in what they do with these two numbers, which is why they
// share this and cannot disagree about the hard half.
//
// The series is `log((1+s)/(1-s)) = 2(s + s^3/3 + s^5/5 + ...)` with `s = (m-1)/(m+1)`. Over that
// interval |s| <= 0.1716 and s^2 <= 0.0295, so the term after the last one kept is below 2e-17
// relative to the answer — which is the bound that matters, because the answer approaches zero as
// the significand approaches one and an absolute bound would say nothing there.
//
// The tier's lengths, and what each delivers. The first dropped term is `s^(2n)/(2n+1)` relative to
// the leading `2s`, and the absolute error in a natural logarithm is that times `2s <= 0.3432`:
//
//   exact  12 terms   below 2e-17 relative, as above
//   approx  3 terms   s^6/7   = 3.7e-6 relative -> 1.3e-6 absolute predicted; `_log` measures
//                     1.14e-6 over every binade, against an approx bound of 2e-5
//
// The three logarithms scale this error by their own base — `_log2` by 1/ln2 = 1.44, `_log10` by 1/ln10 = 0.434 —
// and the measurements follow that exactly: 1.65e-6 for `_log2` against a bound of 3e-5, 4.96e-7 for `_log10`
// against 1e-5. A scaling that did not follow would mean the three had stopped sharing the hard half.
inline constexpr SeriesLength kLogarithmSeries{12, 3};
static_assert(is_a_ladder(kLogarithmSeries));

struct LogarithmParts {
    double log_of_significand{0.0};
    int exponent{0};
};

constexpr LogarithmParts logarithm_parts(double x, int terms) {
    constexpr double kSquareRootOfTwo = 0x1.6a09e667f3bcdp0;
    constexpr double kReciprocalOdd[12] = {
        1.0,        1.0 / 3.0,  1.0 / 5.0,  1.0 / 7.0,  1.0 / 9.0,  1.0 / 11.0,
        1.0 / 13.0, 1.0 / 15.0, 1.0 / 17.0, 1.0 / 19.0, 1.0 / 21.0, 1.0 / 23.0,
    };
    static_assert(kLogarithmSeries.exact == sizeof(kReciprocalOdd) / sizeof(double));
    static_assert(kLogarithmSeries.approx >= 1, "a Horner evaluation needs a first term");

    SignificandAndExponent parts = as_significand_and_exponent(x);
    if (parts.significand > kSquareRootOfTwo) {
        parts.significand *= 0.5;                   // exact, and centres the interval on one
        parts.exponent += 1;
    }

    const double s = (parts.significand - 1.0) / (parts.significand + 1.0);
    const double s_squared = s * s;
    double accumulated = kReciprocalOdd[terms - 1];
    for (int term = terms - 2; term >= 0; --term) {
        accumulated = accumulated * s_squared + kReciprocalOdd[term];
    }
    return {2.0 * s * accumulated, parts.exponent};
}

// Whether a logarithm's argument has an answer that is not a logarithm at all, and what it is.
// Written once because all three ask it identically, and the answers are the platform's.
struct LogarithmSpecialCase {
    bool applies{false};
    double answer{0.0};
};

constexpr LogarithmSpecialCase logarithm_special_case(double x) {
    if (x != x) {
        return {true, x};
    }
    if (x < 0.0) {
        return {true, kQuietNaN};
    }
    if (x == 0.0) {
        return {true, -kPositiveInfinity};          // of either zero: log(-0) is -inf, as libm has it
    }
    if (x > kLargestFiniteDouble) {
        return {true, x};                           // +infinity
    }
    return {};
}

// The natural logarithm. The exponent's contribution is added through the same split `ln 2` the
// reduction uses, so an argument whose exponent is zero — everything in [1/sqrt2, sqrt2) — gets the
// series and nothing else, which is where the relative accuracy is hardest to hold.
//
// Measured against `std::log` over every binade, 32 significands each (67136 arguments):
// at most 1 ulp at `Exact`, and 67063 of them bit-identical — 73 one ulp out, none further.
// The coarse tier's bound is absolute rather than an ulp count, for the reason the series' own note
// gives: an ulp of a logarithm near an argument of one is not a useful unit.
constexpr double natural_logarithm(double x, FnAccuracy fn) {
    const LogarithmSpecialCase special = logarithm_special_case(x);
    if (special.applies) {
        return special.answer;
    }
    const LogarithmParts parts = logarithm_parts(x, terms_at(kLogarithmSeries, fn));
    const double exponent = static_cast<double>(parts.exponent);
    return exponent * kNaturalLogOfTwoHigh + (parts.log_of_significand + exponent * kNaturalLogOfTwoLow);
}

// Base two. Exact at every power of two, and that is the point of not writing it as `log(x)/ln2`:
// the significand is exactly one there, so the series returns zero and the exponent is the answer.
//
// Measured against `std::log2`, same sweep: at most 1 ulp at `Exact`, 67059 bit-identical,
// 77 one out.
constexpr double logarithm_base_two(double x, FnAccuracy fn) {
    const LogarithmSpecialCase special = logarithm_special_case(x);
    if (special.applies) {
        return special.answer;
    }
    const LogarithmParts parts = logarithm_parts(x, terms_at(kLogarithmSeries, fn));
    return static_cast<double>(parts.exponent) + parts.log_of_significand * kOneOverNaturalLogOfTwo;
}

// Base ten. No argument is a power of ten and also a power of two except one, so there is no
// exactness to preserve beyond `log10(1) == 0`, which falls out the same way.
//
// Measured against `std::log10`, same sweep: at most 2 ulp at `Exact`, 67001 bit-identical,
// 133 one out and two arguments at two. It is the one core of the five that reaches two, and the
// reason is structural rather than sloppy: near an argument of one the exponent's contribution and
// the significand's are of similar size and opposite sign, so they cancel and the relative error of
// what is left is amplified. The worst case measured is 0x1.2p-1. Recorded rather than tuned away:
// the fix is a different decomposition.
constexpr double logarithm_base_ten(double x, FnAccuracy fn) {
    const LogarithmSpecialCase special = logarithm_special_case(x);
    if (special.applies) {
        return special.answer;
    }
    const LogarithmParts parts = logarithm_parts(x, terms_at(kLogarithmSeries, fn));
    const double exponent = static_cast<double>(parts.exponent);
    return exponent * kLogBaseTenOfTwoHigh
           + (parts.log_of_significand * kOneOverNaturalLogOfTen + exponent * kLogBaseTenOfTwoLow);
}

// ---- the hyperbolic tangent, and the `exp(x) - 1` it is really made of -----------------------------
//
// `tanh(x) = (e^2x - 1)/(e^2x + 1)`, and written that way it is wrong near zero: `e^2x` is one
// there, the subtraction cancels totally, and the answer's own bits are the ones thrown away. The
// standard fix is a core for `e^x - 1` that never forms the one — the series has no leading one to
// cancel against — and that is what the two functions below are.
//
// The series bound is chosen by where the other spelling stops losing, not by where the series stops
// converging. Above the bound the answer falls back on `exponential(x) - 1.0`, whose error is one
// ulp of `e^x` spread over `e^x - 1` — so the fallback amplifies its own error by `e^x/(e^x - 1)`,
// and that factor is the whole argument for where the bound goes. At 1.5 it is 1.29; at 0.75 it is
// 1.90 and at 0.35 it is 3.39. The bound is where the amplification is still a fraction rather than
// a multiple, and the series is what covers everything below it.

// `exp(x) - 1`, REQUIRING a non-negative argument. `hyperbolic_tangent` is its only caller and only
// ever asks for one, because it takes the magnitude first and puts the sign back at the end; the
// series alternates for a negative argument and its accuracy there has not been measured.
//
// The first two terms are taken out of the Horner loop, which was measured: as one Horner evaluation
// over the whole series the answer is 3 ulps from the platform's `tanh`; with `x` and `x^2/2` written
// separately and only the rest accumulated it is 2.
// The leading `x` is exact and the rest is a correction to it, so the loop's accumulated rounding is
// weighed against a third of the answer instead of all of it.
inline constexpr double kExponentialMinusOneSeriesBound = 1.5;

// The tier's lengths. The interval is [0, 1.5] and the error of a truncation is its first dropped
// term, x^(k+1)/(k+1)!, in `e^x - 1` rather than in the answer — `hyperbolic_tangent` then divides
// by `r + 2`, which damps it by `2/(r+2)^2 <= 0.0667` at the top of the interval:
//
//   exact  23 terms   1.2e-19 times the answer at the bound — under an ulp of it
//   approx  9 terms   x^10/10! = 1.6e-5 in `r` -> about 1.1e-6 in a tangent predicted, 1.22e-6
//                     measured; `_tanh`'s approx bound is 1e-4
//
// The coarse maximum lands at an argument of -0.75, which is where the reduced argument reaches the
// series bound of 1.5 — the last point the series covers before the fallback takes over, and so the
// point at which the dropped term is largest. That the measurement lands exactly there is the
// evidence that what is being measured is the truncation and not something else.
//
// The fallback matters to those numbers because past 1.5 the answer comes from `exponential` at the
// same tier, so a coarse tangent is coarse for two reasons and both are counted.
inline constexpr SeriesLength kExponentialMinusOneSeries{23, 9};
static_assert(is_a_ladder(kExponentialMinusOneSeries));

constexpr double exponential_minus_one_of_nonnegative(double x, FnAccuracy fn) {
    // 1/k! for k = 1..23. At the bound above the last term is 1.2e-19 times the answer — under an
    // ulp of it — so a twenty-fourth would land below the final rounding and change nothing.
    constexpr double kReciprocalFactorial[23] = {
        0x1.0000000000000p+0,  0x1.0000000000000p-1,  0x1.5555555555555p-3,  0x1.5555555555555p-5,
        0x1.1111111111111p-7,  0x1.6c16c16c16c17p-10, 0x1.a01a01a01a01ap-13, 0x1.a01a01a01a01ap-16,
        0x1.71de3a556c734p-19, 0x1.27e4fb7789f5cp-22, 0x1.ae64567f544e4p-26, 0x1.1eed8eff8d898p-29,
        0x1.6124613a86d09p-33, 0x1.93974a8c07c9dp-37, 0x1.ae7f3e733b81fp-41, 0x1.ae7f3e733b81fp-45,
        0x1.952c77030ad4ap-49, 0x1.6827863b97d97p-53, 0x1.2f49b46814157p-57, 0x1.e542ba4020225p-62,
        0x1.71b8ef6dcf572p-66, 0x1.0ce396db7f853p-70, 0x1.761b413163819p-75,
    };
    static_assert(kExponentialMinusOneSeries.exact == sizeof(kReciprocalFactorial) / sizeof(double));
    // Three is this series' floor and not one, because its first two terms are taken out of the
    // Horner loop above. At two the loop would not run and the answer would carry `x^2/2` twice.
    static_assert(kExponentialMinusOneSeries.approx >= 3, "this series cannot be shortened below three");
    if (x > kExponentialMinusOneSeriesBound) {
        return exponential(x, fn) - 1.0;
    }
    const int terms = terms_at(kExponentialMinusOneSeries, fn);
    double accumulated = kReciprocalFactorial[terms - 1];
    for (int term = terms - 2; term >= 2; --term) {
        accumulated = accumulated * x + kReciprocalFactorial[term];
    }
    const double square = x * x;
    return x + (square * 0.5 + (square * x) * accumulated);
}

// Measured against the platform's `std::tanh` at `Exact`: at most 2 ulps, over five million
// arguments from -25 to 25 in steps of a hundred-thousandth (4884259 of them bit-identical) and over
// every binade below 16 at sixteen significands each.
//
// Taken on the magnitude and signed back, because `tanh` is odd and because the two directions are
// not equally kind. Call the reduced quantity `r = e^2x - 1`, so the answer is `r/(r+2)`: its
// relative sensitivity to `r` works out to `2/(r+2)`. Written on the argument as it stands, a
// negative one puts `r` in (-1, 0) and that factor between one and two — it AMPLIFIES whatever
// error the series left. Taken on the magnitude, `r` is never negative and the factor never above
// one.
//
// The quotient is compensated for the same reason `atan2`'s is: `r + 2` rounds, and half an ulp of
// four is a whole ulp of the answer. Measured over the same five million arguments, that step alone
// moves 79298 of them from one ulp out to bit-identical and takes the worst case from 3 ulps to 2,
// so the bound in `tests/unit/transcendental_test.cpp` rests on it.
//
// Past 20 the answer is one. That is not a shortcut: `tanh` reaches the largest double below one at
// an argument of about 19.07, so every argument past it has one as its correctly rounded answer, and
// the branch exists because `e^2x` overflows long before `tanh` stops being interesting.
inline constexpr double kHyperbolicTangentSaturatesAbove = 20.0;

constexpr double hyperbolic_tangent(double x, FnAccuracy fn) {
    if (x != x || x == 0.0) {
        return x;                                   // odd, so a zero's answer is that same zero
    }
    const bool negative = is_negative(x);
    const double magnitude = negative ? -x : x;
    if (magnitude >= kHyperbolicTangentSaturatesAbove) {
        return negative ? -1.0 : 1.0;
    }
    const double reduced = exponential_minus_one_of_nonnegative(magnitude + magnitude, fn);
    const TwoDoubles denominator = exact_sum(reduced, 2.0);
    const TwoDoubles quotient = careful_ratio(reduced, 0.0, denominator.high, denominator.low);
    const double answer = quotient.high + quotient.low;
    return negative ? -answer : answer;
}

} // namespace heinzel
