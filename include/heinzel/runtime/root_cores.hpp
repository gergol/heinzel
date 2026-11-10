#pragma once

// The square root, and one over it: Newton's method on a significand whose exponent has been taken out
// first, then a single correction taken from the exact product, which makes the answer bitwise the
// platform's rather than within an ulp of it.

#include "heinzel/runtime/exact_arithmetic.hpp"
#include "heinzel/runtime/float_bits.hpp"
#include "heinzel/runtime/hardware_twins.hpp"

namespace heinzel {

// ---- the square root, and the reciprocal one ------------------------------------------------------
//
// The exponent is taken out first, so Newton always starts on a significand in [1, 4), where a linear
// guess is already within a quarter and each step doubles the correct digits. Six steps take that to
// below one ulp, and putting the halved exponent back is exact because it is a power of two. (Newton
// from `guess = a` converges only for arguments within about 2^40 of one: forty steps answer
// `sqrt(1e30)` with 9.09e17.)
constexpr double square_root_core(double x) {
    if (x != x || x == 0.0) {
        return x;                                   // NaN, and both zeros: sqrt(-0) is -0
    }
    if (x < 0.0) {
        return kQuietNaN;                           // what the platform answers
    }
    if (x > kLargestFiniteDouble) {
        return x;                                   // +infinity
    }

    SignificandAndExponent parts = as_significand_and_exponent(x);
    if (parts.exponent % 2 != 0) {
        // The exponent has to be even to be halved exactly, so an odd one lends its bit to the
        // significand — which stays in [1, 4), where the guess below is still good.
        parts.significand *= 2.0;
        parts.exponent -= 1;
    }

    double root = 0.5 * parts.significand + 0.5;    // within a quarter over [1, 4)
    for (int step = 0; step < 6; ++step) {
        root = 0.5 * (root + parts.significand / root);
    }

    // One correction, and it is what makes the answer bitwise the platform's rather than within an ulp of it.
    // Newton converges to a double next to the true root but has no reason to land on the nearer one; without this
    // step, 28350 of 134272 arguments across every binade are one ulp out. The residual `m - root^2` is below the
    // precision of a plain subtraction — root^2 has already been rounded — so it is taken from the exact product
    // (`runtime/exact_arithmetic.hpp`). Dividing that residual by twice the root is Newton's step written where it
    // can still be seen.
    const TwoDoubles squared = exact_product(root, root);
    const double residual = (parts.significand - squared.high) - squared.low;
    root += residual / (root + root);

    // ...and the correction itself rounds, which at one place matters. The largest double below four
    // is `4 - 2^-51`, and its root is `2 - 2^-53` less a little — strictly below the midpoint between
    // two and the double beneath it. So the correctly rounded root of any significand in [1, 4) is at
    // most that double and never two. The addition above can nonetheless land on two, because it is a
    // second rounding falling exactly on that midpoint and ties go to even. Three arguments out of
    // 1.2 million densely sampled reached it, every one of them the top of a binade — `sqrt(max)`
    // among them, which is why it is named in the test. This is the stated bound applied, not a fudge.
    if (root >= 2.0) {
        root = 0x1.fffffffffffffp0;
    }

    return root * two_to_the(parts.exponent / 2);
}

// One over the square root, written as a division by the root rather than as its own Newton iteration, so the two
// operations cannot drift apart — but the zeros and the negatives are named rather than divided, because a constant
// expression may not divide by zero even to reach the answer the platform gives.
constexpr double reciprocal_square_root(double x) {
    if (x != x) {
        return x;
    }
    if (x < 0.0) {
        return kQuietNaN;
    }
    if (x == 0.0) {
        // 1/sqrt(+0) is +inf and 1/sqrt(-0) is -inf, because sqrt(-0) is -0. Read the sign bit: a
        // zero's value cannot tell the two apart.
        return with_sign_of(kPositiveInfinity, x);
    }
    if (x > kLargestFiniteDouble) {
        return 0.0;
    }
    return 1.0 / square_root(x);
}

// In float, where an F32 value is computed in its own type: the float division of one by the float
// root. Not the double answer rounded — a sequence of two roundings is a different number in each
// type (`NumericContract::ExactInDouble`) — but what a float lane computes, which is the point: a
// loop's vector body and its scalar remainder must be the same function.
constexpr float reciprocal_square_root(float x) {
    if (x != x || x < 0.0f || x == 0.0f || x > 3.4028234663852886e38f) {
        return static_cast<float>(reciprocal_square_root(static_cast<double>(x))); // the special values
    }
    return 1.0f / square_root(x);
}
} // namespace heinzel
