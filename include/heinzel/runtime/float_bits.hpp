#pragma once

// A double's parts, read and built through `bit_cast`: the only way to reach its exponent in a constant
// expression without `<cmath>`. Also the two values arithmetic cannot produce there: clang refuses a constant
// expression that computes a NaN or divides by zero, but it may name one, and an operation whose answer is a
// NaN or an infinity (the logarithm of a negative, one over the root of zero) must be able to give it.

#include <bit>
#include <cstdint>

namespace heinzel {

// ---- reading and building a double's parts -------------------------------------------------------

inline constexpr double kQuietNaN = std::bit_cast<double>(std::uint64_t{0x7ff8000000000000});
inline constexpr double kPositiveInfinity = std::bit_cast<double>(std::uint64_t{0x7ff0000000000000});

// ---- the sign, which is a bit and not a value ----------------------------------------------------
//
// A zero's sign cannot be read from its value: it is a question about the sign bit.

constexpr bool is_negative(double x) {
    return (std::bit_cast<std::uint64_t>(x) >> 63) == 1;
}

// The magnitude of one value carrying the sign bit of another — `copysign`, which is in `<cmath>`
// and therefore out of reach of a core that must run while a graph is being compiled.
constexpr double with_sign_of(double magnitude, double model) {
    constexpr std::uint64_t kSignBit = 0x8000000000000000ULL;
    const std::uint64_t sign = std::bit_cast<std::uint64_t>(model) & kSignBit;
    return std::bit_cast<double>((std::bit_cast<std::uint64_t>(magnitude) & ~kSignBit) | sign);
}

// 2^k, exactly, for every k a double can hold one for: normal down to 2^-1022, denormal to 2^-1074,
// zero below that, infinity above 2^1023.
constexpr double two_to_the(int k) {
    if (k > 1023) {
        return kPositiveInfinity;
    }
    if (k >= -1022) {
        return std::bit_cast<double>(static_cast<std::uint64_t>(k + 1023) << 52);
    }
    if (k >= -1074) {
        return std::bit_cast<double>(std::uint64_t{1} << static_cast<unsigned>(k + 1074));
    }
    return 0.0;
}

// A positive finite `x` as `significand * 2^exponent` with the significand in [1, 2). The scaling of
// a denormal is exact and is undone in the exponent, so this is a decomposition and not an
// approximation.
//
// REQUIRES `x` to be positive, finite and non-zero. Its callers check; there is no answer to give
// for a zero or an infinity, and inventing one would be a value a caller could accidentally use.
struct SignificandAndExponent {
    double significand{0.0};
    int exponent{0};
};

constexpr SignificandAndExponent as_significand_and_exponent(double x) {
    int exponent = 0;
    std::uint64_t bits = std::bit_cast<std::uint64_t>(x);
    if ((bits >> 52) == 0) {                       // denormal: normalise it and pay for it below
        bits = std::bit_cast<std::uint64_t>(x * 0x1p54);
        exponent -= 54;
    }
    exponent += static_cast<int>((bits >> 52) & 0x7ff) - 1023;
    const std::uint64_t one = std::uint64_t{1023} << 52;
    return {std::bit_cast<double>((bits & 0x000fffffffffffff) | one), exponent};
}

// The smallest double whose significand is full. Below it a value has fewer than 53 significant bits
// — the exponent has bottomed out and the leading bits are zeros — which is why `exact_product`
// cannot split one exactly and why anything relying on that splitting has to ask first.
inline constexpr double kSmallestNormalDouble = 0x1p-1022;

constexpr bool is_normal_magnitude(double x) {
    return (x < 0.0 ? -x : x) >= kSmallestNormalDouble;
}

// The largest finite double, written out so this file depends on nothing but bits, not even `<limits>`.
inline constexpr double kLargestFiniteDouble = 0x1.fffffffffffffp1023;

// Is this a real number rather than an infinity or a NaN? Comparisons only: `x - x == 0.0` computes a
// NaN for an infinity, which clang refuses in a constant expression, so `is_finite(inf)` would stop the
// compile instead of answering false. `x == x` is false for a NaN, and the two bounds for the infinities.
constexpr bool is_finite(double x) {
    return x == x && x <= kLargestFiniteDouble && x >= -kLargestFiniteDouble;
}

} // namespace heinzel
