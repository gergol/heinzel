#pragma once

// `a * b + c` rounded once: a correctly rounded fused multiply-add, in integers. The 106-bit product is
// formed exactly, `c` aligned under it in 128 bits with what is shifted out kept as a sticky bit, and the
// sum rounded to nearest-even once, at the position its exponent dictates (subnormals included). IEEE 754
// requires this to be correctly rounded, so the processor's instruction computes the same bits: it is the
// twin (`runtime/hardware_twins.hpp`), and `tests/unit/hardware_twins_test.cpp` compares the two.

#include <bit>
#include <cstdint>

#include "heinzel/runtime/float_bits.hpp"
#include "heinzel/runtime/hardware_twins.hpp"

namespace heinzel {

// 128 bits, and only the operations the algorithm needs. Not `unsigned __int128`: a 32-bit
// target has none.
struct Wide {
    std::uint64_t high{0};
    std::uint64_t low{0};

    constexpr bool operator==(const Wide&) const = default;
};

constexpr bool is_less(Wide a, Wide b) { return a.high != b.high ? a.high < b.high : a.low < b.low; }

constexpr Wide plus(Wide a, Wide b) {
    const std::uint64_t low = a.low + b.low;
    return {a.high + b.high + (low < a.low ? 1u : 0u), low};
}

constexpr Wide minus(Wide a, Wide b) { // a >= b
    return {a.high - b.high - (a.low < b.low ? 1u : 0u), a.low - b.low};
}

constexpr Wide shifted_left(Wide a, int by) { // 0 <= by < 128, nothing shifted out
    if (by == 0) {
        return a;
    }
    if (by >= 64) {
        return {a.low << (by - 64), 0};
    }
    return {(a.high << by) | (a.low >> (64 - by)), a.low << by};
}

// Shifted right, with every bit that falls off the end OR-ed into the last one kept. That sticky bit
// is all a later rounding needs to know about them: that they were not all zero.
constexpr Wide shifted_right_sticky(Wide a, int by) {
    if (by == 0) {
        return a;
    }
    if (by >= 128) {
        return {0, (a.high | a.low) != 0 ? 1u : 0u};
    }
    const Wide lost = shifted_left(a, 128 - by);
    const Wide kept = by >= 64 ? Wide{0, a.high >> (by - 64)}
                               : Wide{a.high >> by, (a.low >> by) | (a.high << (64 - by))};
    return {kept.high, kept.low | ((lost.high | lost.low) != 0 ? 1u : 0u)};
}

constexpr int highest_set_bit(Wide a) { // a != 0
    return a.high != 0 ? 127 - std::countl_zero(a.high) : 63 - std::countl_zero(a.low);
}

// The exact product of two integers below 2^53, from 32-bit halves so no partial product overflows.
constexpr Wide product_of_significands(std::uint64_t a, std::uint64_t b) {
    const std::uint64_t a_low = a & 0xffffffffu;
    const std::uint64_t a_high = a >> 32;
    const std::uint64_t b_low = b & 0xffffffffu;
    const std::uint64_t b_high = b >> 32;
    const std::uint64_t middle = a_high * b_low + a_low * b_high; // below 2^54
    return plus({a_high * b_high + (middle >> 32), 0}, plus({0, a_low * b_low}, {0, middle << 32}));
}

// A finite, non-zero double as an integer significand with its top bit at 52, and the power of two
// its lowest bit is worth. A subnormal is normalised, so the top bit is always there.
struct Decomposed {
    std::uint64_t significand;
    int exponent;
    bool negative;
};

constexpr Decomposed decompose(double x) {
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(x);
    const std::uint64_t fraction = bits & 0x000fffffffffffffull;
    const int field = static_cast<int>((bits >> 52) & 0x7ff);
    const bool negative = (bits >> 63) != 0;
    if (field != 0) {
        return {fraction | (std::uint64_t{1} << 52), field - 1075, negative};
    }
    const int lift = std::countl_zero(fraction) - 11;
    return {fraction << lift, -1074 - lift, negative};
}

// A magnitude `bits * 2^exponent` rounded to nearest-even into a double's bit pattern, once. The two
// bits below the rounding position decide — a guard and the sticky — and the position moves up when
// the result is subnormal, so that case rounds once as well. A carry out of the significand lands in
// the exponent field by plain addition, which is exactly the double it should become.
constexpr double rounded_once(Wide bits, int exponent, bool negative) {
    const int top = highest_set_bit(bits);
    const int field = exponent + top + 1023; // the biased exponent, if the result is normal
    if (field >= 2047) {
        return with_sign_of(kPositiveInfinity, negative ? -1.0 : 1.0);
    }
    const int drop = (top - 52) + (field < 1 ? 1 - field : 0); // bits below the rounding position
    const Wide with_two = drop >= 2 ? shifted_right_sticky(bits, drop - 2) : shifted_left(bits, 2 - drop);
    std::uint64_t kept = with_two.low >> 2;
    const std::uint64_t below = with_two.low & 3u;
    if (below == 3u || (below == 2u && (kept & 1u) != 0)) {
        kept += 1;
    }
    // (A carry out of the largest finite exponent packs to 0x7ff with a zero fraction: infinity.)
    const std::uint64_t packed = (static_cast<std::uint64_t>((field < 1 ? 1 : field) - 1) << 52) + kept;
    return std::bit_cast<double>(packed | (negative ? std::uint64_t{1} << 63 : 0));
}

constexpr double multiply_add_rounding_once_core(double a, double b, double c) {
    if (is_finite(a) && is_finite(b) && !is_finite(c)) {
        // The true product is finite however large, so an infinite `c` (or a NaN one) is the answer.
        // Not `a * b + c`: a product that overflows the type would meet the opposite infinity as NaN.
        return c;
    }
    if (!is_finite(a) || !is_finite(b) || a == 0.0 || b == 0.0) {
        // An infinity or a NaN decides the answer by the ordinary rules, and an exactly zero product
        // leaves `c` (or the sum of two zeros, whose sign the ordinary sum gets right).
        return a * b + c;
    }
    // Both terms with their top bit at 126, leaving 127 for a carry. The low bits of each are zero,
    // which is what lets a sticky bit on the smaller one stand in for everything shifted out of it.
    const Decomposed da = decompose(a);
    const Decomposed db = decompose(b);
    const Wide exact = product_of_significands(da.significand, db.significand);
    const int lift = 126 - highest_set_bit(exact);
    Wide product = shifted_left(exact, lift);
    int product_exponent = da.exponent + db.exponent - lift;
    const bool product_negative = da.negative != db.negative;
    if (c == 0.0) {
        return rounded_once(product, product_exponent, product_negative);
    }
    const Decomposed dc = decompose(c);
    Wide addend = shifted_left({0, dc.significand}, 74);
    int addend_exponent = dc.exponent - 74;

    const bool product_is_larger = product_exponent != addend_exponent ? product_exponent > addend_exponent
                                                                       : !is_less(product, addend);
    Wide larger = product_is_larger ? product : addend;
    Wide smaller = product_is_larger ? addend : product;
    const int exponent = product_is_larger ? product_exponent : addend_exponent;
    const int apart = product_is_larger ? product_exponent - addend_exponent : addend_exponent - product_exponent;
    const bool negative = product_is_larger ? product_negative : dc.negative;
    smaller = shifted_right_sticky(smaller, apart);

    if (product_negative == dc.negative) {
        return rounded_once(plus(larger, smaller), exponent, negative);
    }
    const Wide difference = minus(larger, smaller);
    if (difference == Wide{}) {
        return 0.0; // exact cancellation is +0 under round-to-nearest
    }
    return rounded_once(difference, exponent, negative);
}

// The same in float: `a * b + c` rounded once to float, which is what a fused float multiply-add
// computes (it is not the `_fma` operation on an F32 value — that is the double result rounded,
// another number). The product of two floats is exact in double, so only the sum rounds: it is
// rounded to odd in double — the sticky bit kept in the last place — and then once more, to float,
// which with 29 spare bits is the correctly rounded float of the exact value.
constexpr float multiply_add_rounding_once_core(float a, float b, float c) {
    const double product = static_cast<double>(a) * static_cast<double>(b);
    const double cd = static_cast<double>(c);
    const double sum = product + cd;
    if (!is_finite(sum) || sum == 0.0) {
        return static_cast<float>(sum);
    }
    const double as_added = sum - product; // two-sum: what the rounding of `sum` lost
    const double lost = (product - (sum - as_added)) + (cd - as_added);
    std::uint64_t bits = std::bit_cast<std::uint64_t>(sum);
    if (lost != 0.0 && (bits & 1u) == 0) {
        // Step to the odd neighbour on the side the lost part lies: away from zero when it has the
        // sum's sign, toward zero otherwise.
        bits += (lost > 0.0) == (sum > 0.0) ? 1u : ~std::uint64_t{0};
    }
    return static_cast<float>(std::bit_cast<double>(bits));
}

} // namespace heinzel
