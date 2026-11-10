#pragma once

// Arithmetic carried in more than one double, because one is not enough to hold the answer: a double
// product needs 106 bits of significand and a double has 53. The square root's final correction, the
// radian reduction and the power and inverse circular cores carry their intermediates as pairs whose sum
// is the unrounded value.

#include <bit>
#include <cstdint>

#include "heinzel/runtime/float_bits.hpp"

namespace heinzel {

// ---- an exact product -----------------------------------------------------------------------------
//
// The product is carried as a pair of doubles whose sum is the unrounded answer. Dekker's splitting
// gets it: cut each operand into two 26-bit halves whose partial products are each exact, then
// recover what the rounded product threw away.

// Two doubles whose exact mathematical sum is the value being carried. `high` is the value rounded
// to a double and `low` is everything that rounding lost, so `high + low` rounds back to `high`.
struct TwoDoubles {
    double high{0.0};
    double low{0.0};
};

// 2^27 + 1. Multiplying by it and subtracting is what isolates the top 26 bits.
inline constexpr double kSplittingConstant = 134217729.0;

// Above this the splitting constant would overflow the multiplication and the halves would come back
// as infinities. 2^996 leaves the 27 binades the constant needs.
inline constexpr double kSplittingNeedsScalingAbove = 0x1p996;

// One operand as two halves of 26 bits each, exactly: `high + low == x`, and each half's product
// with another half is exact because 26 + 26 fits in 53.
//
// REQUIRES `|x| <= kSplittingNeedsScalingAbove`, which its only caller guarantees. Past that the
// multiplication on the first line overflows and both halves come back as infinities.
constexpr TwoDoubles split_in_half(double x) {
    const double lifted = kSplittingConstant * x;
    const double high = lifted - (lifted - x);
    return {high, x - high};
}

// The product of two doubles, exactly, as a pair. `high` is the product as ordinary multiplication
// would give it; `low` is precisely the bits that multiplication rounded away.
//
// REQUIRES the rounded product to be finite, which its only caller checks.
//
// The scaling belongs here and not in the splitting. Halving a huge operand, splitting it and
// multiplying the halves back by the same power of two looks exact, except that Dekker's `high` is
// `x` rounded to 26 bits, and rounding goes up as readily as down: for `x` the largest finite double
// the rounded half is 2^1024, so scaling it back overflows to infinity and the product comes out NaN.
// Scaling the operand instead has no such edge: the answer is the scaled product's pair, put back by a power of two,
// and the caller has already established that that product is finite.
//
// The scaled pair cannot underflow either, and the bound is worth stating because it is what makes
// this safe rather than merely usually right: this branch needs `|a| > 2^996`, the smallest
// non-zero double is 2^-1074, so `|a*b|` here is never below about 2^-78 and its low half is never
// close to denormal.
constexpr TwoDoubles exact_product(double a, double b) {
    if (a > kSplittingNeedsScalingAbove || a < -kSplittingNeedsScalingAbove) {
        const TwoDoubles scaled = exact_product(a * 0x1p-28, b);
        return {scaled.high * 0x1p28, scaled.low * 0x1p28};
    }
    if (b > kSplittingNeedsScalingAbove || b < -kSplittingNeedsScalingAbove) {
        const TwoDoubles scaled = exact_product(a, b * 0x1p-28);
        return {scaled.high * 0x1p28, scaled.low * 0x1p28};
    }
    const double rounded = a * b;
    const TwoDoubles left = split_in_half(a);
    const TwoDoubles right = split_in_half(b);
    const double lost = ((left.high * right.high - rounded) + left.high * right.low
                         + left.low * right.high)
                        + left.low * right.low;
    return {rounded, lost};
}

// The sum of two doubles, exactly, as a pair (Knuth). Unlike the faster form that needs to know
// which operand is larger, this one is correct for any two finite doubles — and which of a product
// and an addend is larger is exactly what nobody here knows.
constexpr TwoDoubles exact_sum(double a, double b) {
    const double rounded = a + b;
    const double b_as_added = rounded - a;
    return {rounded, (a - (rounded - b_as_added)) + (b - b_as_added)};
}

// ---- a quotient, with the bits the division rounded away ------------------------------------------
//
// A division rounds like every other operation, and unlike a product there is no pair that holds
// what it lost — but there is a cheap way to recover it: multiply the quotient back by the divisor
// exactly and look at what is missing. That residual, divided again, is the correction.
//
// The two `_low` arguments are the corrections on operands that are themselves pairs. Each caller
// has a different one of them and passes zero for the rest.
//
// What it skips when it cannot compute it. Only the first of the three terms below needs
// `exact_product`, which needs normal operands: a denormal has fewer than 53 significant bits,
// Dekker's splitting does not separate it exactly, and the last line would divide that wrong residual
// by the denominator, multiplying the error by 10^300. Without the guard, `atan2` of two denormals is
// 9638 ulps out (`y = 0x0.000a666666666p-1022`, `x = 0x0.00d999999999ap-1022`), where the plain
// quotient alone is within one.
//
// The operands' own corrections are not part of that and are always applied. `arc_tangent_of_unit`
// reaches this with a numerator of exactly zero whenever the reduced argument lands on a table
// centre, and dropping `t_low` there would throw away the whole correction its caller computed: one
// ulp out at two of the sixteen centre cases, against a long-double reference. Random sweeps reach
// those arguments with probability 2^-50, so the accuracy gate would never find it.
//
// A non-finite quotient is handed back with nothing beside it: an infinite ratio is one the caller's
// `atan` answers as pi/2 regardless.
constexpr TwoDoubles careful_ratio(double numerator, double numerator_low, double denominator,
                                   double denominator_low) {
    const double quotient = numerator / denominator;
    if (!is_finite(quotient)) {
        return {quotient, 0.0};
    }
    double residual = numerator_low - quotient * denominator_low;
    if (is_normal_magnitude(quotient) && is_normal_magnitude(numerator)
        && is_normal_magnitude(denominator)) {
        const TwoDoubles back = exact_product(quotient, denominator);
        residual += (numerator - back.high) - back.low;
    }
    const double low = residual / denominator;
    return {quotient, is_finite(low) ? low : 0.0};
}

} // namespace heinzel
