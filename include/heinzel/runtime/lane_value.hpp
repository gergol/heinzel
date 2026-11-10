#pragma once

// `Lane<T, W>`: W numbers of type T computed at once, the vector type the loop tree runs its lanes in.
// It is the compiler's vector extension and nothing else (no intrinsic, no instruction-set macro), so any
// width compiles on any target. Lane k of every result is bit for bit what the scalar operation answers for
// lane k, or both are a NaN. That is why min, max and select are the scalar `a < b ? a : b`, not the
// processor's min/max, whose NaN rule differs between architectures.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

#include "heinzel/runtime/compute.hpp"

namespace heinzel {

template <class T, std::size_t W>
struct Mask {
    using element = std::conditional_t<sizeof(T) == 4, std::int32_t, std::int64_t>;
    using vec = element __attribute__((vector_size(sizeof(T) * W)));
    vec m; // a lane is all ones where the comparison held, zero where it did not

    friend Mask operator&(Mask a, Mask b) { return {a.m & b.m}; }
    friend Mask operator|(Mask a, Mask b) { return {a.m | b.m}; }
    friend Mask operator!(Mask a) { return {~a.m}; }
};

template <class T, std::size_t W>
bool any(Mask<T, W> a) { return __builtin_reduce_or(a.m) != 0; }

// Width one is not a Lane: it is plain `T`, and the same templates take either. Constant evaluation only
// runs width one, so nothing here needs to be a constant expression, and loads and stores may be `memcpy`,
// which is also safe on unaligned storage.
template <class T, std::size_t W>
struct Lane {
    using vec = T __attribute__((vector_size(sizeof(T) * W)));
    vec v;

    // A brace initialiser, not `vec{} + s`: under strict floating point that sum is a real addition
    // per broadcast (it has to turn -0.0 into +0.0), and it was a third of a GEMM inner loop.
    static Lane splat(T s) { return splat(s, std::make_index_sequence<W>{}); }
    static Lane load(const T* from) {
        Lane r;
        std::memcpy(&r.v, from, sizeof(vec));
        return r;
    }
    void store(T* to) const { std::memcpy(to, &v, sizeof(vec)); }
    // The first `count` lanes only — the ragged end of a row, run at full width: what is past the end
    // is read as zero and never written.
    static Lane load_first(const T* from, std::size_t count) {
        Lane r{};
        std::memcpy(&r.v, from, count * sizeof(T));
        return r;
    }
    void store_first(T* to, std::size_t count) const { std::memcpy(to, &v, count * sizeof(T)); }
    T operator[](std::size_t k) const { return v[k]; }

    friend Lane operator+(Lane a, Lane b) { return {a.v + b.v}; }
    friend Lane operator-(Lane a, Lane b) { return {a.v - b.v}; }
    friend Lane operator*(Lane a, Lane b) { return {a.v * b.v}; }
    friend Lane operator/(Lane a, Lane b) { return {a.v / b.v}; }
    friend Lane operator-(Lane a) { return {-a.v}; }

    friend Mask<T, W> operator<(Lane a, Lane b) { return {a.v < b.v}; }
    friend Mask<T, W> operator<=(Lane a, Lane b) { return {a.v <= b.v}; }
    friend Mask<T, W> operator>(Lane a, Lane b) { return {a.v > b.v}; }
    friend Mask<T, W> operator>=(Lane a, Lane b) { return {a.v >= b.v}; }
    friend Mask<T, W> operator==(Lane a, Lane b) { return {a.v == b.v}; }
    friend Mask<T, W> operator!=(Lane a, Lane b) { return {a.v != b.v}; }

private:
    template <std::size_t... I>
    static Lane splat(T s, std::index_sequence<I...>) { return {vec{((void)I, s)...}}; }
};

template <class T, std::size_t W>
Lane<T, W> select(Mask<T, W> where, Lane<T, W> a, Lane<T, W> b) { return {where.m ? a.v : b.v}; }

template <class T, std::size_t W>
Lane<T, W> min(Lane<T, W> a, Lane<T, W> b) { return select(a < b, a, b); }

template <class T, std::size_t W>
Lane<T, W> max(Lane<T, W> a, Lane<T, W> b) { return select(a > b, a, b); }

// The sign bit cleared in every lane: one `fabs` (`andpd` on x86), as the scalar `magnitude_of` is.
template <class T, std::size_t W>
Lane<T, W> magnitude_of(Lane<T, W> x) { return {__builtin_elementwise_abs(x.v)}; }

// The correctly-rounded operations: clang's elementwise builtins, which are the one instruction where
// the target has it and a per-lane library call where it has not — the same answer either way, and the
// same as the scalar function's (`runtime/hardware_twins.hpp`). On a float lane the root is the float
// root, which is the double root rounded (53 >= 2*24+2), and a rounding to an integral value is exact.
template <class T, std::size_t W>
Lane<T, W> square_root(Lane<T, W> x) { return {__builtin_elementwise_sqrt(x.v)}; }

template <class T, std::size_t W>
Lane<T, W> truncate_toward_zero(Lane<T, W> x) { return {__builtin_elementwise_trunc(x.v)}; }

template <class T, std::size_t W>
Lane<T, W> round_toward_negative(Lane<T, W> x) { return {__builtin_elementwise_floor(x.v)}; }

template <class T, std::size_t W>
Lane<T, W> round_toward_positive(Lane<T, W> x) { return {__builtin_elementwise_ceil(x.v)}; }

template <class T, std::size_t W>
Lane<T, W> round_to_nearest_even(Lane<T, W> x) { return {__builtin_elementwise_roundeven(x.v)}; }

// A lane folded into one number: halves combined pairwise, which is three steps for eight lanes
// where a walk along them would be seven — and the order a lane fold has already given up.
template <class T, std::size_t W, std::size_t... I>
Lane<T, sizeof...(I)> half_of(Lane<T, W> x, std::size_t from, std::index_sequence<I...>) {
    T out[sizeof...(I)] = {x.v[from + I]...};
    return Lane<T, sizeof...(I)>::load(out);
}

template <class T, std::size_t W>
T folded(ReduceOp op, Lane<T, W> x) {
    if constexpr (W == 2) {
        return fold_step(op, x.v[0], x.v[1]);
    } else {
        return folded(op, fold_step(op, half_of(x, 0, std::make_index_sequence<W / 2>{}),
                                    half_of(x, W / 2, std::make_index_sequence<W / 2>{})));
    }
}

template <class T, std::size_t W>
Lane<T, W> reciprocal_square_root(Lane<T, W> x) { return Lane<T, W>::splat(T{1}) / square_root(x); }

template <class T, std::size_t W>
struct Broadcast<Lane<T, W>> {
    static Lane<T, W> of(double x) { return Lane<T, W>::splat(static_cast<T>(x)); }
};

template <class T, std::size_t W>
struct NumberOf<Lane<T, W>> {
    using type = T;
};

// A core written for one number, over a lane of them, one lane at a time — which is what a `double`
// transcendental is on this path and all it needs to be (`runtime/value_type.hpp` says why). Written
// as a pack expansion rather than a loop for the reason `splat` is: the lane index is then a constant
// at every use, with no loop for the optimiser to have to unroll.
template <class T, std::size_t W>
struct EachNumber<Lane<T, W>> {
    static Lane<T, W> of(Lane<T, W> x, auto core) { return of(x, core, std::make_index_sequence<W>{}); }
    static Lane<T, W> of(Lane<T, W> x, Lane<T, W> y, auto core) { return of(x, y, core, std::make_index_sequence<W>{}); }

private:
    template <std::size_t... I>
    static Lane<T, W> of(Lane<T, W> x, auto core, std::index_sequence<I...>) {
        return {typename Lane<T, W>::vec{static_cast<T>(core(x.v[I]))...}};
    }
    template <std::size_t... I>
    static Lane<T, W> of(Lane<T, W> x, Lane<T, W> y, auto core, std::index_sequence<I...>) {
        return {typename Lane<T, W>::vec{static_cast<T>(core(x.v[I], y.v[I]))...}};
    }
};

// `x * 2^whole` for an integral-valued `whole`, in lanes: the exponent written into the bits, in the
// two halves and in the order `runtime/float_cores.hpp` argues for. The exponent field is shifted as an
// unsigned integer, as the scalar version shifts it, so the shift cannot overflow a signed integer: the
// sanitizer checks that in the scalar code but cannot see it in a vector.
template <std::size_t W>
Lane<float, W> scaled_by_two_to_the(Lane<float, W> x, Lane<float, W> whole) {
    using ints = std::int32_t __attribute__((vector_size(4 * W)));
    using bits = std::uint32_t __attribute__((vector_size(4 * W)));
    const ints n = __builtin_convertvector(whole.v, ints);
    const ints low = n >> 1;
    return {x.v * __builtin_bit_cast(typename Lane<float, W>::vec, __builtin_convertvector(n - low + 127, bits) << 23) *
            __builtin_bit_cast(typename Lane<float, W>::vec, __builtin_convertvector(low + 127, bits) << 23)};
}

// The same scaling where `whole` is known to be a small power: one normal float holds `2^whole`, so one
// insertion and one multiply do (`runtime/float_cores.hpp` says who may claim that).
template <std::size_t W>
Lane<float, W> scaled_by_a_small_power_of_two(Lane<float, W> x, Lane<float, W> whole) {
    using ints = std::int32_t __attribute__((vector_size(4 * W)));
    using bits = std::uint32_t __attribute__((vector_size(4 * W)));
    return {x.v * __builtin_bit_cast(typename Lane<float, W>::vec,
                                     __builtin_convertvector(__builtin_convertvector(whole.v, ints) + 127, bits) << 23)};
}

// The significand in [1/2, 1) and the exponent of positive normal floats, in lanes: read out of the bits
// where the two functions above write an exponent in (`runtime/float_cores.hpp`'s logarithm reads them).
template <std::size_t W>
Lane<float, W> significand_of(Lane<float, W> x) {
    using bits = std::uint32_t __attribute__((vector_size(4 * W)));
    return {__builtin_bit_cast(typename Lane<float, W>::vec, (__builtin_bit_cast(bits, x.v) & 0x807fffffu) | 0x3f000000u)};
}

template <std::size_t W>
Lane<float, W> exponent_of(Lane<float, W> x) {
    using ints = std::int32_t __attribute__((vector_size(4 * W)));
    using bits = std::uint32_t __attribute__((vector_size(4 * W)));
    const bits field = (__builtin_bit_cast(bits, x.v) >> 23) & 0xffu;
    return {__builtin_convertvector(__builtin_convertvector(field, ints) - 126, typename Lane<float, W>::vec)};
}

// A multiply-add whose rounding is the machine's, in lanes (`runtime/hardware_twins.hpp` says what for).
template <class T, std::size_t W>
Lane<T, W> multiply_add_as_the_machine_rounds(Lane<T, W> a, Lane<T, W> b, Lane<T, W> c) {
    if constexpr (kMultiplyAddIsOneInstruction) {
        return {__builtin_elementwise_fma(a.v, b.v, c.v)};
    } else {
        return {a.v * b.v + c.v};
    }
}

// `a * b + c` rounded once, IN THE LANE'S OWN TYPE. For double lanes that is the `_fma` operation. For
// float lanes it is not — `_fma` on an F32 value is the double result rounded, which is another number
// — it is what a granted contraction of a float multiply-add computes.
template <class T, std::size_t W>
Lane<T, W> multiply_add_rounding_once(Lane<T, W> a, Lane<T, W> b, Lane<T, W> c) {
    return {__builtin_elementwise_fma(a.v, b.v, c.v)};
}

} // namespace heinzel
