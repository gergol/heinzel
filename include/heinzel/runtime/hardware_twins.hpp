#pragma once

// The hardware twins: the one file that runs different code at run time than under constant evaluation.
// IEEE 754 requires the square root, the fused multiply-add and the four roundings to an integral value to
// be correctly rounded, so the processor's instruction computes the same bits as the software core. The
// core is the definition (a constant expression can evaluate it); the instruction is used at run time
// because it is faster, for the root by more than ten times. `tests/unit/hardware_twins_test.cpp` holds it.

#include "heinzel/runtime/float_bits.hpp"
#include "heinzel/runtime/instruction_set.hpp"

// Everywhere else `gate_no_constant_evaluation_branch` forbids the branch: two bodies chosen by whether a
// call is constant-evaluated cannot be compared with each other. These can, and are, bit for bit over every
// binade boundary, millions of random bit patterns and every special value.
//
// Whether the target has each instruction, and `HEINZEL_HARDWARE_TWINS=0`, are asked in
// `runtime/instruction_set.hpp`; the twins below exist only where it answers yes.

namespace heinzel {

// The definitions — `runtime/root_cores.hpp`, `runtime/rounding_cores.hpp`,
// `runtime/fused_multiply_add.hpp`. Declared here because the cores call each other through the names
// below, so those names have to exist before any core is defined.
constexpr double square_root_core(double x);
constexpr double truncate_toward_zero_core(double x);
constexpr double round_toward_negative_core(double x);
constexpr double round_toward_positive_core(double x);
constexpr double round_to_nearest_even_core(double x);
constexpr double multiply_add_rounding_once_core(double a, double b, double c);
constexpr float multiply_add_rounding_once_core(float a, float b, float c);

// Same bits, except which NaN. Where the answer is a NaN, core and instruction both answer one, but
// not the same one: x86 gives the root of a negative the negative quiet NaN, quiets a signalling NaN,
// and picks among two NaN operands by position. IEEE 754 leaves a NaN's sign and payload open, and the
// compiler already reorders operands freely, so no two builds of this tree ever agreed on them. Every
// comparison of results in this tree therefore reads "the same bits, or both a NaN" — and the twins
// need no guard, which keeps them a single instruction.
constexpr double square_root(double x) {
#if HEINZEL_HARDWARE_TWINS
    if !consteval {
        return __builtin_sqrt(x);
    }
#endif
    return square_root_core(x);
}

// The float root: the double root rounded is the float root (53 >= 2*24 + 2), so the core serves both.
constexpr float square_root(float x) {
#if HEINZEL_HARDWARE_TWINS
    if !consteval {
        return __builtin_sqrtf(x);
    }
#endif
    return static_cast<float>(square_root_core(static_cast<double>(x)));
}

constexpr double truncate_toward_zero(double x) {
#if HEINZEL_ROUNDING_TWIN
    if !consteval {
        return __builtin_trunc(x);
    }
#endif
    return truncate_toward_zero_core(x);
}

constexpr double round_toward_negative(double x) {
#if HEINZEL_ROUNDING_TWIN
    if !consteval {
        return __builtin_floor(x);
    }
#endif
    return round_toward_negative_core(x);
}

constexpr double round_toward_positive(double x) {
#if HEINZEL_ROUNDING_TWIN
    if !consteval {
        return __builtin_ceil(x);
    }
#endif
    return round_toward_positive_core(x);
}

// `roundeven`, not `nearbyint`: the latter follows the rounding mode, and the core does not.
constexpr double round_to_nearest_even(double x) {
#if HEINZEL_ROUNDING_TWIN
    if !consteval {
        return __builtin_roundeven(x);
    }
#endif
    return round_to_nearest_even_core(x);
}

// The float rounding, which needs no twin of its own: an integral value is exact in both types and
// every float is exact as a double, so the double answer above is the float one. Delegating to the
// double twin avoids a second `if !consteval` to hold to the instruction. A lane of floats has its
// own (`runtime/lane_value.hpp`, one `vroundps`).
constexpr float round_to_nearest_even(float x) {
    return static_cast<float>(round_to_nearest_even(static_cast<double>(x)));
}

constexpr double multiply_add_rounding_once(double a, double b, double c) {
#if HEINZEL_FMA_TWIN
    if !consteval {
        return __builtin_fma(a, b, c);
    }
#endif
    return multiply_add_rounding_once_core(a, b, c);
}

constexpr float multiply_add_rounding_once(float a, float b, float c) {
#if HEINZEL_FMA_TWIN
    if !consteval {
        return __builtin_fmaf(a, b, c);
    }
#endif
    return multiply_add_rounding_once_core(a, b, c);
}

// A multiply-add whose rounding is the machine's: one where it has the instruction, two where it does
// not. For arithmetic that owes no particular bits — a float core's polynomial, held to its bound
// either way (`tests/unit/float_cores_test.cpp`, built both ways) — and never for the operation above,
// which promises one rounding and pays for it in software where it must. `Normal` grants this freedom,
// the arm the benchmarks compare with takes it, and without it a core's Horner steps are two
// instructions each on its critical path: 6.7 ms against 5.7 for a channel strip.
//
// Where the machine fuses, it is the operation above — the instruction at run time, its correctly
// rounded core under constant evaluation, the two proven the same — so a core evaluated while the
// program compiles gives the bits it gives when it runs (`a * b + c` under constant evaluation would
// differ by an ulp in `sin` and `exp`).
// (`kMultiplyAddIsOneInstruction`, `runtime/instruction_set.hpp`, says which, for the lane form.)

constexpr float multiply_add_as_the_machine_rounds(float a, float b, float c) {
#if HEINZEL_FMA_TWIN
    return multiply_add_rounding_once(a, b, c);
#else
    return a * b + c;
#endif
}

// The same in double: the arithmetic of `_madd` (`runtime/compute.hpp`) in double, as a value kept in double at
// `Normal` computes it (`rewrite/double_state.hpp`), and in the walk.
constexpr double multiply_add_as_the_machine_rounds(double a, double b, double c) {
#if HEINZEL_FMA_TWIN
    return multiply_add_rounding_once(a, b, c);
#else
    return a * b + c;
#endif
}

} // namespace heinzel
