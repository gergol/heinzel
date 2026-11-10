#pragma once

// What the arithmetic in `runtime/compute.hpp` and `runtime/fold_arithmetic.hpp` asks of the value type
// `V` it is written over: `float`, `double`, or a `Lane` of either (`runtime/lane_value.hpp`). A plain number
// needs a `select` (which is `?:`), its magnitude and a way to make a `V` from a constant (a cast); a lane brings its
// own.

#include <bit>
#include <cstdint>

#include "heinzel/ir/vocab.hpp"

namespace heinzel {

constexpr double select(bool where, double a, double b) { return where ? a : b; }
constexpr float select(bool where, float a, float b) { return where ? a : b; }

// |x|, the sign bit cleared: `fabs`, which `<cmath>` does not offer to a constant expression, written so the compiler
// still sees one instruction (a select of x and -x answers -0 for +0, so it keeps its compare and its blend).
constexpr double magnitude_of(double x) {
    return std::bit_cast<double>(std::bit_cast<std::uint64_t>(x) & 0x7fffffffffffffffULL);
}
constexpr float magnitude_of(float x) {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(x) & 0x7fffffffU);
}

// Rounding to a value's own type, at every step rather than only at the end. Rounding once at the
// output would make a single-precision filter agree with a double-precision reference that it
// should not agree with.
constexpr double as_type(ElemType type, double value) {
    return type == ElemType::F32 ? static_cast<double>(static_cast<float>(value)) : value;
}

// `x` as a `V`: itself for a number; a Lane specialises it to a broadcast.
template <class V>
struct Broadcast {
    static constexpr V of(double x) { return static_cast<V>(x); }
};

// The number type a value type holds: itself for a plain number, the element type for a lane.
// `runtime/float_cores.hpp` chooses a core by it: a float runs the float cores, a double the double cores.
template <class V>
struct NumberOf {
    using type = V;
};
template <class V>
using number_of = typename NumberOf<V>::type;

// A core written for one number (or a pair), applied to every number of a value: the call itself for a
// plain number, one lane at a time for a Lane. The double cores are reached this way: at `High`, the
// bit-exact reference, for F64, and for a transcendental with no float core yet (`tan`, `pow`, `exp2`) at
// every precision, which is slow, and correct, rather than refused (Gerald: none of these
// capabilities may go, `Plan/16-simplify/08-Later.md`). Where speed has been measured to matter the value
// is a float, and a float core answers the whole lane at once.
template <class V>
struct EachNumber {
    static constexpr V of(V x, auto core) { return static_cast<V>(core(x)); }
    static constexpr V of(V x, V y, auto core) { return static_cast<V>(core(x, y)); }
};

} // namespace heinzel
