#pragma once

// The power function, `2^(y * log2(x))`, with the logarithm carried in pairs. Its error is multiplied by
// `y`, and `y * log2(x)` may reach 1024: for the answer to be within an ulp, `y * log2(x)` must be within
// about 1.6e-16 absolutely, so `log2(x)` within 1.6e-19 relatively, about ten bits more than a double
// holds. Built from the plain cores (`logarithm_base_two`, right for `_log2`) it is wrong by hundreds of
// ulps. Everything here is private to `_pow`.

#include "heinzel/runtime/exact_arithmetic.hpp"
#include "heinzel/runtime/exponential_cores.hpp"
#include "heinzel/runtime/float_bits.hpp"
#include "heinzel/runtime/rounding_cores.hpp"

namespace heinzel {

// ---- just enough pair arithmetic, and no more ----------------------------------------------------
//
// Two helpers rather than a double-double library. Each is a couple of lines of Dekker's arithmetic
// that this file uses more than once; a general library would bring normalisation rules and edge
// cases nothing here exercises, and an unexercised rule is one nobody has checked.
//
// `exact_sum` does the renormalising in both: handed two doubles that overlap it returns the same
// value with them apart again, which keeps a pair a pair after its halves have been added to.

constexpr TwoDoubles pair_plus_pair(TwoDoubles a, TwoDoubles b) {
    const TwoDoubles sum = exact_sum(a.high, b.high);
    return exact_sum(sum.high, (sum.low + a.low) + b.low);
}

// The product of two pairs, keeping the three terms that can reach the answer's last bits. The
// fourth, `a.low * b.low`, is below them by 2^-106 and is the one term left out.
constexpr TwoDoubles pair_times_pair(TwoDoubles a, TwoDoubles b) {
    const TwoDoubles product = exact_product(a.high, b.high);
    return exact_sum(product.high, (product.low + a.high * b.low) + a.low * b.high);
}

// ---- log2 to ten bits more than a double holds ----------------------------------------------------

// 1/ln 2 and 1/3 as pairs: the nearest double, and what it missed by. Together each names its value
// to about 2^-106, which is the accuracy the amplification argument at the top of this file demands
// of everything on the path.
//
// Not the Cody-Waite arrangement `kNaturalLogOfTwo{High,Low}` uses next door. That split clears the low bits of the
// high half so that `k * high` is exact for an integer `k` — a property the argument reduction needs and nothing
// here does, since `pair_times_pair` gets its exactness from `exact_product` instead. Clearing bits it does not
// need would throw away 27 of them for nothing.
//
// Both were computed to sixty decimal digits and split, not written by hand: low halves off by 1e-11
// and 1e-9 are not a small error in a pair whose whole purpose is the bits below 2^-53, and they made
// `pow(3, 1)` answer 3.0000000000056768.
inline constexpr double kOneOverNaturalLogOfTwoHigh = 0x1.71547652b82fep+0;
inline constexpr double kOneOverNaturalLogOfTwoLow = 0x1.777d0ffda0d24p-56;
inline constexpr double kOneThirdHigh = 0x1.5555555555555p-2;
inline constexpr double kOneThirdLow = 0x1.5555555555555p-56;

// `log2(x)` as a pair, for a finite positive argument — every other case has been answered before
// this is reached.
//
// The reduction is `logarithm_base_two`'s and deliberately so: the same `x = 2^e * m` with `m` in
// [1/sqrt2, sqrt2), so the two cannot disagree about where the answer comes from. What differs is
// everything after it.
//
// The series argument is exact, which is the first of the two things that buy the extra bits.
// `s = (m-1)/(m+1)`: the numerator is exact by Sterbenz over that interval — a subtraction of two
// numbers within a factor of two of each other has an exact answer — and the denominator is carried
// as a pair, so `careful_ratio` returns `s` to about 2^-105 rather than 2^-53.
//
// The series is carried where it matters and no further, which is the second, and the seam is
// arithmetic rather than taste. Write `ln(m) = 2s(1 + Q)` with `Q = s^2/3 + s^4/5 + ...`: over this
// interval `Q` is just under 0.01, so an error in `Q` is worth a hundredth of itself in the answer,
// and the budget of 2^-63 on the answer is a budget of 2^-56 on `Q`.
//
// That is what decides which parts of `Q` are pairs. Its leading term `s^2/3` is nearly all of it, so
// both `s^2` and the third are carried as pairs. Everything past that leading term is under 2% of
// `Q`, so a double's accuracy there is 2^-64 of the answer and a double is what it gets. Written
// with `Q` entirely in one double the answer comes out at 2^-60 — short of what `pow` needs, and
// short in the way that never shows up until an exponent is large.
constexpr TwoDoubles logarithm_base_two_pair(double x) {
    constexpr double kSquareRootOfTwo = 0x1.6a09e667f3bcdp0;
    // 1/5 through 1/29, so the series keeps every term to `z^14/29`. Over this interval `z <= 0.0295`,
    // so the term `z^k/(2k+1)` is 2.4e-17 at k=10, 1.7e-20 at k=12 and 3.6e-25 at k=15 — the first
    // dropped one here, which is 2^-81 of the answer and cannot reach the pair.
    //
    // Stopping at k=9 would leave a truncation of 2^-55. That is far below a double's last bit, so
    // every ordinary sweep calls it exact; it is far above the 2^-63 this pair exists to deliver, and
    // it shows up as 146 ulps in `pow` at an exponent near a thousand — the one place an error in the
    // low half is amplified enough to be seen at all.
    constexpr double kReciprocalOdd[13] = {
        1.0 / 5.0,  1.0 / 7.0,  1.0 / 9.0,  1.0 / 11.0, 1.0 / 13.0,
        1.0 / 15.0, 1.0 / 17.0, 1.0 / 19.0, 1.0 / 21.0, 1.0 / 23.0,
        1.0 / 25.0, 1.0 / 27.0, 1.0 / 29.0,
    };

    SignificandAndExponent parts = as_significand_and_exponent(x);
    if (parts.significand > kSquareRootOfTwo) {
        parts.significand *= 0.5;                   // exact, and centres the interval on one
        parts.exponent += 1;
    }

    const double numerator = parts.significand - 1.0;   // exact: Sterbenz, over this interval
    const TwoDoubles denominator = exact_sum(parts.significand, 1.0);
    const TwoDoubles s = careful_ratio(numerator, 0.0, denominator.high, denominator.low);

    // `s^2` as a pair, and the cross term is not optional. Written `s.high * s.high` it drops
    // `2 * s.high * s.low`, which is 2^-53 of the square — and since the leading term of `Q` is
    // `z/3` and `Q` is a hundredth of the answer, that lands at 2^-60 of the answer where the
    // budget is 2^-63. Below, `z.high` alone is enough for the remainder, because the remainder is
    // under 2% of `Q` and a double's accuracy in it is 2^-64 of the answer.
    const TwoDoubles square = exact_product(s.high, s.high);
    const TwoDoubles z = exact_sum(square.high, square.low + (s.high + s.high) * s.low);

    double remainder = kReciprocalOdd[12];
    for (int term = 11; term >= 0; --term) {
        remainder = remainder * z.high + kReciprocalOdd[term];
    }
    const TwoDoubles q = pair_plus_pair(pair_times_pair(z, {kOneThirdHigh, kOneThirdLow}),
                                        {(z.high * z.high) * remainder, 0.0});

    const TwoDoubles twice_s{s.high + s.high, s.low + s.low};   // exact: a doubling
    const TwoDoubles natural = pair_plus_pair(twice_s, pair_times_pair(twice_s, q));
    const TwoDoubles in_base_two =
        pair_times_pair(natural, {kOneOverNaturalLogOfTwoHigh, kOneOverNaturalLogOfTwoLow});
    return pair_plus_pair({static_cast<double>(parts.exponent), 0.0}, in_base_two);
}

// `2^p` for a pair-valued exponent, which is the other half of what `pow` needs: the whole point of
// computing `y * log2(x)` in a pair is lost if the exponential then rounds it back to a double.
//
// The whole part comes out exactly and the fraction goes through `exponential_of_small`, whose
// interval is |r| <= ln(2)/2 — exactly what a fraction of at most a half becomes once multiplied by
// `ln 2`. That multiplication is done in a pair and only then rounded to the double the series
// takes: the pair is what makes the rounding happen once, at the end, instead of the fraction
// arriving already rounded and the product compounding it.
constexpr double two_to_the_power_of_pair(TwoDoubles p, FnAccuracy fn) {
    // Deliberately loose, and only here to keep the cast below in range. The boundary itself is left
    // to the arithmetic: whether an exponent of exactly 1024 overflows depends on the pair's low
    // half, and a guard tight enough to be interesting would be a guard answering a question the
    // arithmetic answers correctly.
    if (p.high > 1025.0) {
        return kPositiveInfinity;
    }
    if (p.high < -1080.0) {
        return 0.0;
    }
    const double whole = round_to_nearest_even(p.high);
    // Exact: `whole` is within a half of `p.high`, so Sterbenz applies to the subtraction.
    const TwoDoubles fraction = exact_sum(p.high - whole, p.low);
    const TwoDoubles r = pair_times_pair(fraction, {kNaturalLogOfTwoHigh, kNaturalLogOfTwoLow});
    // This is where `_pow`'s tier is spent, and the only place in this file it could be. A tier taken
    // on the logarithm would have an error amplified by the exponent, one that grows with an operand,
    // and a bound that depends on an operand is not a bound. Cutting the final series instead leaves
    // the amplified quantity exact and makes the answer's relative error the truncation of this one
    // series, uniformly over the whole domain: the number `_exp` records, at every exponent. A coarse
    // tier still pays for the whole pair; that is the price of a bound.
    const double scaled = exponential_of_small(r.high + r.low, terms_at(kExponentialSeries, fn));

    // The scaling is done in two halves. `scaled` lies between 0.707 and 1.414, so a single `two_to_the(steps)`
    // saturates at both ends while the answer is still an ordinary double: `pow(5.4e17, 17.375)` needs an exponent
    // of 1024 to reach 1.28e308, and one multiplication by an infinity throws the whole answer away (2.6e15 ulps
    // out: an infinity where a finite number belongs). Halving the exponent keeps both intermediates in range and
    // costs nothing, because scaling by a power of two is exact wherever it does not saturate, and the denormal end
    // still rounds exactly once.
    const int steps = static_cast<int>(whole);
    const int half = steps / 2;
    return (scaled * two_to_the(half)) * two_to_the(steps - half);
}

// ---- the operation ---------------------------------------------------------------------------------

// Is `y` an integer, and if so is it an odd one? `pow` needs the second question and not just the
// first: a negative base has an answer for an integral exponent and none otherwise, and the answer's
// sign is the exponent's parity. Past 2^53 every double is an even integer, which the magnitude test
// covers rather than special-cases.
struct IntegralExponent {
    bool integral{false};
    bool odd{false};
};

constexpr IntegralExponent integral_exponent(double y) {
    const double magnitude = y < 0.0 ? -y : y;
    if (magnitude >= 0x1p53) {
        return {true, false};                       // every double this large is an even integer
    }
    if (truncate_toward_zero(magnitude) != magnitude) {
        return {false, false};
    }
    // `is_odd_integer` states that it is only asked about a value that came out of a truncation.
    // This one did: the line above accepted it precisely because truncating left it alone.
    return {true, is_odd_integer(magnitude)};
}

// `x` to the `y`.
//
// The special cases are the C standard's, and the order below is the order they have to be asked in.
// `pow(anything, 0)` is one and `pow(1, anything)` is one — including when the other operand is NaN,
// which is why those two questions come before the NaN question and not after. That is not a
// curiosity: both are exact for every argument in a neighbourhood, so a NaN carries no information
// the answer depends on, and the standard says so.
//
// A negative base has an answer only for an integral exponent, and it is computed, not refused.
constexpr double power(double x, double y, FnAccuracy fn) {
    if (y == 0.0 || x == 1.0) {
        return 1.0;                                 // both exact for every other operand, NaN too
    }
    if (x != x || y != y) {
        return kQuietNaN;
    }

    const double magnitude = is_negative(x) ? -x : x;
    const IntegralExponent exponent = integral_exponent(y);
    // The only route by which an answer becomes negative: a negative base raised to an odd integer.
    const bool negate = is_negative(x) && exponent.odd;

    // An infinite exponent, where nothing about the base survives except how it compares with one —
    // and `-1` is the tie, answering one because every neighbourhood of it contains values on both
    // sides. `x == 1` was answered above; this is the other unit.
    if (y > kLargestFiniteDouble || y < -kLargestFiniteDouble) {
        if (magnitude == 1.0) {
            return 1.0;
        }
        return (magnitude < 1.0) == (y > 0.0) ? 0.0 : kPositiveInfinity;
    }

    // A zero or infinite base. Both answer a zero or an infinity and differ only in which way round,
    // so they are one branch: the answer runs to zero when a base under one is raised to a positive
    // exponent or a base over one to a negative one. The non-integral exponent is legal here, which
    // is why this comes before the check below — `pow(-inf, 0.5)` is `+inf` and not a complaint,
    // because an infinite magnitude has no last digit for the parity question to be about.
    if (magnitude == 0.0 || magnitude > kLargestFiniteDouble) {
        const bool to_zero = (magnitude == 0.0) == (y > 0.0);
        const double bare = to_zero ? 0.0 : kPositiveInfinity;
        return negate ? -bare : bare;
    }

    // A finite negative base raised to a non-integral exponent. There is no real answer, and this
    // tree says so rather than answering the magnitude's.
    if (is_negative(x) && !exponent.integral) {
        return kQuietNaN;
    }

    const TwoDoubles logarithm = logarithm_base_two_pair(magnitude);
    const TwoDoubles scaled = pair_times_pair({y, 0.0}, logarithm);
    const double answer = two_to_the_power_of_pair(scaled, fn);
    return negate ? -answer : answer;
}

} // namespace heinzel
