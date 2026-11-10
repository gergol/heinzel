#pragma once

// The inverse circular family: `atan`, `atan2`, `asin` and `acos`. One core turns a ratio into an angle,
// and the other three spend their effort forming their ratio without cancelling. An `asin` written as
// `pi/2 - acos` (or the reverse) fails: both are near pi/2 over half the domain, and subtracting them
// throws away the bits the answer is made of. Measured against the platform's functions; the
// measurements are gated in `tests/unit/transcendental_test.cpp`.

#include "heinzel/runtime/exact_arithmetic.hpp"
#include "heinzel/runtime/float_bits.hpp"
#include "heinzel/runtime/periodic_cores.hpp"
#include "heinzel/runtime/root_cores.hpp"
#include "heinzel/runtime/series_length.hpp"

namespace heinzel {

// What each wrapper forms, and why that spelling and not the obvious one:
//
//     asin(x)  = atan( x / sqrt(1-x^2) )          both ends stay relative
//     acos(x)  = atan( sqrt(1-x^2) / x )     x>0  the answer vanishes with the numerator
//     acos(x)  = pi/2 + asin(|x|)            x<0  two positives added; nothing cancels
//     atan2    = atan(|y|/|x|), then the quadrant
//
// `1 - x*x` is the hard part of `asin` and `acos`, and it is carried in a pair: at an argument near one
// it cancels completely (`1 - x*x` in one double at `x = 1 - 2^-53` has no significant bits left).
// `one_minus_square` below keeps every one of them, which lets a single branch serve the whole domain
// instead of the half-angle identity the textbooks reach for.

// pi and its tail, derived from the half-turn constants rather than spelled again. Both products are
// by a power of two and therefore exact, so this tree holds one value of pi and no second spelling
// can drift away from it.
inline constexpr double kPi = kHalfPi * 2.0;
inline constexpr double kPiTailFirst = kHalfPiTailFirst * 2.0;

// ---- the table the argument is reduced against ---------------------------------------------------
//
// `atan` has no useful series at a general argument: the Taylor one converges like `x^n` and at an
// argument of 1 it does not converge usefully at all. So the argument is brought into [0, 1] by
// `atan(x) = pi/2 - atan(1/x)`, and then into a sixteenth of that by the addition formula
//
//     atan(t) = atan(c) + atan( (t - c) / (1 + t*c) )
//
// against one of eight centres, `c = (2j+1)/16`. Every centre is a multiple of 1/16, so `t - c` is
// exact in each interval by Sterbenz's condition, and the reduced argument is never above 1/16 —
// where nine Taylor terms are already below what a double can hold.
//
// The angles at the centres are the only constants here that had to be computed rather than written,
// and one double does not hold them well enough: the reduction leaves the answer as
// `atan(c) + something small`, so an error in `atan(c)` is an error in the answer with nothing to
// damp it. Each is carried as a pair, computed to eighty digits and split.
inline constexpr int kArcTangentIntervals = 8;

inline constexpr double kArcTangentCentre[kArcTangentIntervals] = {
    0x1.0000000000000p-4, 0x1.8000000000000p-3, 0x1.4000000000000p-2, 0x1.c000000000000p-2,
    0x1.2000000000000p-1, 0x1.6000000000000p-1, 0x1.a000000000000p-1, 0x1.e000000000000p-1,
};

inline constexpr double kArcTangentAtCentre[kArcTangentIntervals] = {
    0x1.ff55bb72cfdeap-5, 0x1.7b97b4bce5b02p-3, 0x1.362773707ebccp-2, 0x1.a64eec3cc23fdp-2,
    0x1.0657e94db30d0p-1, 0x1.345f01cce37bbp-1, 0x1.5d58987169b18p-1, 0x1.819d0b7158a4dp-1,
};

inline constexpr double kArcTangentAtCentreTail[kArcTangentIntervals] = {
    -0x1.c934d86d23f1dp-60, 0x1.347b0b4f881cap-58, -0x1.963a544b672d8p-57, -0x1.24dec1b50b7ffp-56,
    -0x1.d5b495f6349e6p-56, 0x1.1021137c71102p-55, 0x1.0028e4bc5e7cap-57, -0x1.bf76229d3b917p-56,
};

// The bound on the argument the series ever sees, in both of its roles: no interval produces a
// reduced argument this large, and below it no reduction happens at all and the argument goes to the
// series as it stands. One number, because it is one bound.
inline constexpr double kArcTangentReducedBound = 0x1p-4;

// An angle carried in two doubles, `high + low`. Not `TwoDoubles`: that one promises its pair is an
// exact splitting of a value, and this one does not — `low` here is an ordinary rounded correction.
struct AngleParts {
    double high{0.0};
    double low{0.0};
};

// `atan(u) - u`, for `|u| <= 1/16`. The series is odd, so the tail is `u * u^2 * Q(u^2)`; taking `u`
// out as its own term is what keeps the relative accuracy at a small argument, where the answer is
// `u` and everything else is a correction to it.
//
// Nine terms. The first one dropped is `u^19/19`, which at the bound above is 1.1e-23 times `u` —
// seven orders below what the final rounding can express, so a tenth would buy nothing.
//
// The tier's lengths: the ladder is short because the series already is. The reduction brings every
// argument to `|u| <= 1/16`, so `z = u^2 <= 1/256` and each further term is worth two and a half
// decimal digits. Measured as the difference from the full series over that interval:
//
//   exact  8 terms   below what a double holds
//   approx 2 terms   5.31e-10, against `_atan`'s approx bound of 2e-6
//
// The approx tier lands far inside its bound, by a factor of four thousand, because this family's
// cost is not in its polynomial. The eight-centre table with a pair-arithmetic reduction does the
// work, and shortening the series saves a few multiply-adds out of a core that spends most of its
// time in `exact_product` and `careful_ratio`.
//
// A coarse tier could do the reduction in plain doubles and skip the argument correction, which is
// where the cost is. It does not: the accuracy that would cost is about 1e-17, seven orders below the
// truncation above, so it would not show as a tier at all, and it would put a second arithmetic path
// through a core whose whole argument is the pair arithmetic.
inline constexpr SeriesLength kArcTangentSeries{8, 2};
static_assert(is_a_ladder(kArcTangentSeries));

constexpr double arc_tangent_series_tail(double u, int terms) {
    constexpr double kCoefficient[8] = {
        -0x1.5555555555555p-2, 0x1.999999999999ap-3,  -0x1.2492492492492p-3, 0x1.c71c71c71c71cp-4,
        -0x1.745d1745d1746p-4, 0x1.3b13b13b13b14p-4,  -0x1.1111111111111p-4, 0x1.e1e1e1e1e1e1ep-5,
    };
    static_assert(kArcTangentSeries.exact == sizeof(kCoefficient) / sizeof(double));
    static_assert(kArcTangentSeries.approx >= 1, "a Horner evaluation needs a first term");
    const double z = u * u;
    double accumulated = kCoefficient[terms - 1];
    for (int term = terms - 2; term >= 0; --term) {
        accumulated = accumulated * z + kCoefficient[term];
    }
    return u * z * accumulated;
}

// `atan(t + t_low)` for `0 <= t <= 1`, as a pair. `t_low` is the correction on the argument itself:
// two of the three callers arrive with a ratio they formed, and dropping its low half here would
// undo the care that formed it.
constexpr AngleParts arc_tangent_of_unit(double t, double t_low, FnAccuracy fn) {
    if (t < kArcTangentReducedBound) {
        return {t, arc_tangent_series_tail(t, terms_at(kArcTangentSeries, fn)) + t_low};
    }
    int interval = static_cast<int>(t * 8.0);
    if (interval > kArcTangentIntervals - 1) {
        interval = kArcTangentIntervals - 1;    // an argument of exactly 1 lands one past the last
    }
    const double centre = kArcTangentCentre[interval];
    const double numerator = t - centre;        // exact in every interval: Sterbenz's condition
    const TwoDoubles product = exact_product(t, centre);
    const TwoDoubles denominator = exact_sum(1.0, product.high);
    const double denominator_low = (denominator.low + product.low) + t_low * centre;
    const TwoDoubles reduced =
        careful_ratio(numerator, t_low, denominator.high, denominator_low);
    const double tail =
        (arc_tangent_series_tail(reduced.high, terms_at(kArcTangentSeries, fn)) + reduced.low)
        + kArcTangentAtCentreTail[interval];
    return {kArcTangentAtCentre[interval], reduced.high + tail};
}

// `atan(t + t_low)` for any `t >= 0`, as a pair. Above 1 the argument is inverted, and the
// reciprocal's own rounding is corrected — `1/t` is not exact, and at an argument just above 1 that
// error lands on an answer of 0.785 undamped.
// REQUIRES `t` to be a number rather than a NaN — every caller checks, and a NaN would take the
// infinite branch and come back as pi/2, which is an answer and not a refusal.
constexpr AngleParts arc_tangent_parts(double t, double t_low, FnAccuracy fn) {
    if (!is_finite(t)) {
        return {kHalfPi, kHalfPiTailFirst};     // including an infinite argument, whose angle is pi/2
    }
    if (t <= 1.0) {
        return arc_tangent_of_unit(t, t_low, fn);
    }
    const double reciprocal = 1.0 / t;
    const TwoDoubles back = exact_product(reciprocal, t);

    // `1 - back.high` is exact: the product of a number with its own reciprocal is within one ulp of
    // one, which is Sterbenz's condition. The second term is the argument's correction carried
    // through the inversion, whose derivative is minus the reciprocal squared.
    const double reciprocal_low =
        ((1.0 - back.high) - back.low) * reciprocal - (reciprocal * reciprocal) * t_low;
    const AngleParts inner = arc_tangent_of_unit(reciprocal, reciprocal_low, fn);
    const TwoDoubles difference = exact_sum(kHalfPi, -inner.high);
    return {difference.high, (difference.low - inner.low) + kHalfPiTailFirst};
}

// `y / x` as a pair, with neither operand's size in the way. REQUIRES both to be positive, finite
// and non-zero; `arc_tangent_of_ratio` has answered every other case before it reaches this.
//
// The quotient of two denormals is a perfectly ordinary number, and `careful_ratio` rightly
// declines to correct it — so the division is done on the significands, where both operands are
// normal by construction, and the exponent difference is put back afterwards.
//
// Putting it back is exact wherever the answer is a normal double: a significand quotient lies in
// (1/2, 2) and multiplying it by a power of two only moves the exponent. Where the answer is not a
// normal double the multiplication saturates, and both saturations are the right answer — an
// overflowed ratio is an angle of pi/2 and an underflowed one is an angle of zero.
constexpr TwoDoubles ratio_of_magnitudes(double y, double x) {
    const SignificandAndExponent numerator = as_significand_and_exponent(y);
    const SignificandAndExponent denominator = as_significand_and_exponent(x);
    const TwoDoubles reduced =
        careful_ratio(numerator.significand, 0.0, denominator.significand, 0.0);
    const double scale = two_to_the(numerator.exponent - denominator.exponent);
    const double high = reduced.high * scale;
    if (!is_finite(high)) {
        return {kPositiveInfinity, 0.0};
    }
    return {high, reduced.low * scale};
}

constexpr double arc_tangent(double x, FnAccuracy fn) {
    if (x != x || x == 0.0) {
        return x;                               // odd, so a zero's answer is that same zero
    }
    const bool negative = is_negative(x);
    const AngleParts parts = arc_tangent_parts(negative ? -x : x, 0.0, fn);
    const double answer = parts.high + parts.low;
    return negative ? -answer : answer;
}

// The angle of the point `(x, y)`, which is `atan(y/x)` plus a quadrant.
//
// The infinite cases are answered by the finite code, by replacing each infinite operand with a unit
// and each finite operand beside an infinity with a zero of its own sign. Every one of the standard's
// five infinite answers — ±pi/4, ±3pi/4, ±pi/2, ±0, ±pi — is the answer at those units, so this needs
// no constant of its own. A table of five more angles is five more chances to write one down wrong.
constexpr double arc_tangent_of_ratio(double y, double x, FnAccuracy fn) {
    if (y != y || x != x) {
        return kQuietNaN;
    }
    if (!is_finite(y) || !is_finite(x)) {
        // One level of recursion and no more: both arguments below are finite by construction.
        return arc_tangent_of_ratio(is_finite(y) ? with_sign_of(0.0, y) : with_sign_of(1.0, y),
                                    is_finite(x) ? with_sign_of(0.0, x) : with_sign_of(1.0, x), fn);
    }
    if (y == 0.0) {
        return is_negative(x) ? with_sign_of(kPi, y) : with_sign_of(0.0, y);
    }
    if (x == 0.0) {
        return with_sign_of(kHalfPi, y);
    }
    const TwoDoubles ratio = ratio_of_magnitudes(is_negative(y) ? -y : y, is_negative(x) ? -x : x);
    const AngleParts parts = arc_tangent_parts(ratio.high, ratio.low, fn);
    if (!is_negative(x)) {
        return with_sign_of(parts.high + parts.low, y);
    }
    const TwoDoubles difference = exact_sum(kPi, -parts.high);
    return with_sign_of(difference.high + ((difference.low - parts.low) + kPiTailFirst), y);
}

// ---- the two that need `1 - x^2` ------------------------------------------------------------------
//
// Carried as a pair, and that pair is the reason `asin` and `acos` need no half-angle identity. At
// `x = 1 - 2^-53` the ordinary subtraction has no significant bits left; here the square is exact and
// the subtraction of it from one is exact, so what comes out is the true value to about 106 bits.
constexpr TwoDoubles one_minus_square(double x) {
    const TwoDoubles square = exact_product(x, x);
    const TwoDoubles difference = exact_sum(1.0, -square.high);
    return {difference.high, difference.low - square.low};
}

// `sqrt(1 - x^2)` with its own correction, for `|x| < 1`. `square_root` is bitwise the platform's,
// which still leaves half an ulp; the Newton step here recovers it from the exact residual, and it
// is what carries the pair above through the root instead of throwing it away.
constexpr TwoDoubles unit_complement_root(double x) {
    const TwoDoubles complement = one_minus_square(x);
    if (complement.high <= 0.0) {
        return {0.0, 0.0};
    }
    const double root = square_root(complement.high);
    const TwoDoubles back = exact_product(root, root);
    const double residual = ((complement.high - back.high) - back.low) + complement.low;
    return {root, residual / (root + root)};
}

// `asin(x) = atan(x / sqrt(1-x^2))`, which stays relative at both ends: at a small argument the ratio
// is the argument, and at an argument near one the denominator is small and the ratio large, where
// `atan` answers pi/2 minus something small. `|x| > 1` is NaN, which is the platform's answer and
// numpy's.
constexpr double arc_sine(double x, FnAccuracy fn) {
    if (x != x || x == 0.0) {
        return x;                               // odd, so a zero's answer is that same zero
    }
    const bool negative = is_negative(x);
    const double magnitude = negative ? -x : x;
    if (magnitude > 1.0) {
        return kQuietNaN;
    }
    if (magnitude == 1.0) {
        return negative ? -kHalfPi : kHalfPi;
    }
    const TwoDoubles root = unit_complement_root(magnitude);
    const TwoDoubles ratio = careful_ratio(magnitude, 0.0, root.high, root.low);
    const AngleParts parts = arc_tangent_parts(ratio.high, ratio.low, fn);
    const double answer = parts.high + parts.low;
    return negative ? -answer : answer;
}

// `acos`, in two branches that are chosen for what does not cancel in each.
//
// On the positive half the answer goes to zero as the argument goes to one, and `atan` of the
// reciprocal ratio goes to zero with it — so the relative accuracy survives there, which
// `pi/2 - asin(x)` would not: those two agree to fifteen digits at an argument near one.
//
// On the negative half the answer is pi/2 plus a positive angle, and a sum of two positives cannot
// cancel at all. `pi - acos(|x|)` would have been the mirror of the positive branch and is not used,
// because it subtracts.
constexpr double arc_cosine(double x, FnAccuracy fn) {
    if (x != x) {
        return x;
    }
    if (x == 0.0) {
        return kHalfPi;                         // both zeros: the quarter turn has no sign
    }
    const bool negative = is_negative(x);
    const double magnitude = negative ? -x : x;
    if (magnitude > 1.0) {
        return kQuietNaN;
    }
    if (magnitude == 1.0) {
        return negative ? kPi : 0.0;
    }
    const TwoDoubles root = unit_complement_root(magnitude);
    if (!negative) {
        const TwoDoubles ratio = careful_ratio(root.high, root.low, magnitude, 0.0);
        const AngleParts parts = arc_tangent_parts(ratio.high, ratio.low, fn);
        return parts.high + parts.low;
    }
    const TwoDoubles ratio = careful_ratio(magnitude, 0.0, root.high, root.low);
    const AngleParts parts = arc_tangent_parts(ratio.high, ratio.low, fn);
    const TwoDoubles sum = exact_sum(kHalfPi, parts.high);
    return sum.high + ((sum.low + parts.low) + kHalfPiTailFirst);
}

} // namespace heinzel
