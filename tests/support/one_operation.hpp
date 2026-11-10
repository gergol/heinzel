#pragma once

// Applying one operation to operands, and the two questions about a double that `==` cannot answer.
//
// Scaffolding shared by the two test groups that ask what an operation COMPUTES. Deliberately not
// part of the library: it is how those tests are written, not what they check.

#include <bit>
#include <cstdint>
#include <limits>

#include "compute_reference.hpp"

namespace heinzel::testing {

// One operation, applied at a named tier. A node with nothing but its code is enough — none of
// these reads an attribute, and the one writing takes its operands as arguments rather than from the
// graph.
//
// The tier is FIRST rather than last because the two operands after it are optional: a default
// argument in front of one is how a caller ends up passing an operand where a tier goes.
constexpr double apply_at(FnAccuracy fn, OpCode op, double a, double b = 0.0, double c = 0.0) {
    Node n{};
    n.op = op;
    return compute(n, fn, a, b, c);
}

// The same, at the exact tier. Most of what these tests ask about is what an operation MEANS rather
// than how accurately it is computed, and for those the tier is noise; `apply_at` is for the
// assertions whose subject is the tier itself.
constexpr double apply(OpCode op, double a, double b = 0.0, double c = 0.0) {
    return apply_at(FnAccuracy::Exact, op, a, b, c);
}

// Is this value negative zero? The sign bit, because nothing about a zero's VALUE distinguishes it.
constexpr bool negative_zero(double x) {
    return x == 0.0 && is_negative(x);
}

// Bitwise, so a zero of the wrong sign is a difference rather than a match. Not for NaN: no two
// NaNs are required to share a bit pattern.
constexpr bool same_bits(double ours, double theirs) {
    return std::bit_cast<std::uint64_t>(ours) == std::bit_cast<std::uint64_t>(theirs);
}

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
inline constexpr double kInf = std::numeric_limits<double>::infinity();

} // namespace heinzel::testing
