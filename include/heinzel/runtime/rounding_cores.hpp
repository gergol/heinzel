#pragma once

// Rounding a double to an integral value, in the four directions an operation asks for. `std::floor`
// and its companions are not callable in a constant expression under clang. These are the only cores
// whose exact answer is representable, so they are checked against the platform bitwise, not to a bound.

#include "heinzel/runtime/float_bits.hpp"
#include "heinzel/runtime/hardware_twins.hpp"

namespace heinzel {

// Every double at or above this magnitude is already an integer, so the four functions below have
// nothing to do to it, and past 2^63 the integer cast they use would be undefined anyway. The
// comparison is written so that NaN takes this path too, because NaN compares false against
// everything and every one of these operations answers NaN for NaN.
inline constexpr double kEveryDoubleAboveThisIsIntegral = 4503599627370496.0; // 2^52

constexpr bool has_no_fractional_part(double x) {
    return !(x > -kEveryDoubleAboveThisIsIntegral && x < kEveryDoubleAboveThisIsIntegral);
}

// Toward zero, and it preserves the sign of zero, which the three below then inherit rather than
// each patching for itself.
//
// A cast through `long long` and back cannot carry that sign: every value in (-1, 0) casts to the
// integer 0 and comes back as positive zero, where `std::trunc` gives negative zero. The early
// return covers the two zeros themselves; the fix-up at the end covers the whole open interval
// between minus one and zero (`trunc(-0.5)` is `-0.0`).
constexpr double truncate_toward_zero_core(double x) {
    if (x == 0.0 || has_no_fractional_part(x)) {
        return x;
    }
    const double toward_zero = static_cast<double>(static_cast<long long>(x));
    return toward_zero == 0.0 && x < 0.0 ? -0.0 : toward_zero;
}

// Toward negative infinity. `t > x` is true exactly when truncation rounded a negative number up.
constexpr double round_toward_negative_core(double x) {
    const double t = truncate_toward_zero_core(x);
    return t > x ? t - 1.0 : t;
}

// Toward positive infinity. `ceil(-0.5)` is negative zero and this says so without a fix-up of its
// own: for a negative `x` the truncation never rounded down, so the answer is the truncation, and
// truncation already carries the sign.
constexpr double round_toward_positive_core(double x) {
    const double t = truncate_toward_zero_core(x);
    return t < x ? t + 1.0 : t;
}

// Is this integral-valued double an odd integer? Halving and truncating is exact for anything this
// is asked about, because it is only ever asked about a value that came out of truncation.
constexpr bool is_odd_integer(double t) {
    return truncate_toward_zero_core(t * 0.5) * 2.0 != t;
}

// To the nearest, ties to even: `rint` under the default rounding mode. Not `std::round`, which
// breaks ties away from zero; the two differ on exactly the inputs a test is most likely to reach for.
//
// Written as a comparison of the fractional part against a half rather than as
// `truncate(x + copysign(0.5, x))`, because that form answers 1 for the largest double below a half
// — the addition rounds it up to exactly a half before the truncation ever sees it.
constexpr double round_to_nearest_even_core(double x) {
    if (x == 0.0 || has_no_fractional_part(x)) {
        return x;
    }
    const double t = truncate_toward_zero_core(x);
    const double fraction = x - t;
    double nearest = t;
    if (fraction > 0.5 || (fraction == 0.5 && is_odd_integer(t))) {
        nearest = t + 1.0;
    } else if (fraction < -0.5 || (fraction == -0.5 && is_odd_integer(t))) {
        nearest = t - 1.0;
    }
    // No zero fix-up here either, and for the same reason: `rint(-0.5)` is negative zero, and the
    // path that reaches it leaves `nearest` as the truncation, which already has the sign.
    return nearest;
}
} // namespace heinzel
