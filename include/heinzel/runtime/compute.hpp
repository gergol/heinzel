#pragma once

// What one operation computes, for one sample: the single writing of each operation's arithmetic.
// The loop tree calls `compute_as` with the operation as a compile-time constant, so only the chosen branch
// is compiled; the reference walk (`tests/reference/compute_reference.hpp`) dispatches to the same families.
// A gate keeps this the only file in `include/` that writes an operation's arithmetic, so no copy can drift.
// Every other pass asks the rule table about an operation; arithmetic is the one thing a table cannot hold.

#include <cstdint>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/runtime/fold_arithmetic.hpp"
#include "heinzel/runtime/math_cores.hpp"
#include "heinzel/runtime/value_type.hpp"
#include "heinzel/support/fail.hpp"

// The loop tree calls `compute_as` with the operation a constant: inlined, the chain of families folds to that one
// operation. Left to the inliner's budget it once did not (one row more put the chain out of line, every family's cores
// with it, 5.6 KB in every binary), so where the build optimizes it is forced; at -O0 nothing folds either way.
#if defined(__OPTIMIZE__)
#define HEINZEL_COMPUTE_INLINE [[gnu::always_inline]] inline
#else
#define HEINZEL_COMPUTE_INLINE inline
#endif

namespace heinzel {

// What one operation computed, and whether a meaning is written for it at all. The flag lets the coverage
// checks ask at compile time whether every operation the table says owes a meaning has one.
template <class V>
struct ComputedAs {
    V value{};
    bool written{false};

    constexpr bool operator==(const ComputedAs&) const = default;
};

// The families below are written over a value type `V` (`runtime/value_type.hpp`). Some transcendentals
// have a core in `V` itself, some only a `double` core run one number of `V` at a time, and the rest a
// `double` core reached only by the reference walk, whose graphs the loop tree refuses; the rule table
// records which is which. `a`, `b`, `c` are the operands, already fetched from whatever sample the operation
// reads. A family answers `{value, true}` for the operations it covers and `{}` for the rest.

// Arithmetic: the operations whose answer is their own algebra.
template <class V>
HEINZEL_COMPUTE_INLINE constexpr ComputedAs<V> arithmetic_of(OpCode op, V a, V b, V c) {
    if (op == op::Add) {
        return ComputedAs<V>{a + b, true};
    }
    if (op == op::Sub) {
        return ComputedAs<V>{a - b, true};
    }
    if (op == op::Mul) {
        return ComputedAs<V>{a * b, true};
    }
    if (op == op::Div) {
        return ComputedAs<V>{a / b, true};
    }
    if (op == op::Neg) {
        return ComputedAs<V>{-a, true};
    }
    if (op == op::Min) {
        // If either operand is NaN the comparison is false, so this answers `b`, as numpy does.
        return ComputedAs<V>{select(a < b, a, b), true};
    }
    if (op == op::Max) {
        return ComputedAs<V>{select(a > b, a, b), true};
    }
    if (op == op::Fma) {
        // Not `a * b + c`: whether that rounds the product before adding is the compiler's choice, and
        // this operation promises it does not. The product is exact and the sum rounded once, so the
        // result is correctly rounded and the processor's instruction is its twin
        // (`runtime/fused_multiply_add.hpp`).
        return ComputedAs<V>{static_cast<V>(multiply_add_rounding_once(a, b, c)), true};
    }
    if (op == op::MultiplyAdd) {
        // The rounding the build's instruction set gives, which only a precision that grants reordering accepts
        // (`NumericContract::MachineRounded`): never the software core.
        return ComputedAs<V>{multiply_add_as_the_machine_rounds(a, b, c), true};
    }
    if (op == op::Abs) {
        // The sign bit cleared, so +0 and -0 answer +0 and a NaN stays a NaN, as numpy's `abs`.
        return ComputedAs<V>{magnitude_of(a), true};
    }
    if (op == op::Sign) {
        // sign(0) is 0 with the zero's own sign, and sign(NaN) is NaN, as in numpy. Both fall out of
        // two comparisons; a subtraction of comparisons would answer 0 for NaN.
        const V one = Broadcast<V>::of(1.0);
        return ComputedAs<V>{select(a > Broadcast<V>::of(0.0), one, select(a < Broadcast<V>::of(0.0), -one, a)), true};
    }
    return ComputedAs<V>{};
}

// Roots and reciprocal roots: arithmetic that needs a method rather than an expression. The cores are
// in `math_cores.hpp`.
template <class V>
HEINZEL_COMPUTE_INLINE constexpr ComputedAs<V> root_of(OpCode op, V a) {
    if (op == op::Sqrt) {
        return ComputedAs<V>{static_cast<V>(square_root(a)), true};
    }
    if (op == op::Rsqrt) {
        return ComputedAs<V>{static_cast<V>(reciprocal_square_root(a)), true};
    }
    return ComputedAs<V>{};
}

// Rounding to an integral value, one core per direction in `math_cores.hpp`: `<cmath>` is not usable
// in a constant expression.
template <class V>
HEINZEL_COMPUTE_INLINE constexpr ComputedAs<V> rounding_of(OpCode op, V a) {
    if (op == op::Floor) {
        return ComputedAs<V>{static_cast<V>(round_toward_negative(a)), true};
    }
    if (op == op::Ceil) {
        return ComputedAs<V>{static_cast<V>(round_toward_positive(a)), true};
    }
    if (op == op::Round) {
        // Ties to even, unlike `std::round`.
        return ComputedAs<V>{static_cast<V>(round_to_nearest_even(a)), true};
    }
    if (op == op::Trunc) {
        return ComputedAs<V>{static_cast<V>(truncate_toward_zero(a)), true};
    }
    return ComputedAs<V>{};
}

// Comparison and choice: the operations that answer one or zero, or pick.
template <class V>
HEINZEL_COMPUTE_INLINE constexpr ComputedAs<V> comparison_of(OpCode op, V a, V b, V c) {
    if (op == op::Lt) {
        return ComputedAs<V>{select(a < b, Broadcast<V>::of(1.0), Broadcast<V>::of(0.0)), true};
    }
    if (op == op::Le) {
        return ComputedAs<V>{select(a <= b, Broadcast<V>::of(1.0), Broadcast<V>::of(0.0)), true};
    }
    if (op == op::Eq) {
        // Exact `==`, deliberately: a tolerance is a decision about one graph, and burying one here
        // would make every graph inherit it.
        return ComputedAs<V>{select(a == b, Broadcast<V>::of(1.0), Broadcast<V>::of(0.0)), true};
    }
    if (op == op::Select) {
        return ComputedAs<V>{select(a != Broadcast<V>::of(0.0), b, c), true};
    }
    return ComputedAs<V>{};
}

// The transcendentals with only a double core, run over a value type one number at a time: every number widened
// to double, its core run, the answer rounded back, as the walk computes it, so the two agree bit for bit at
// every precision. `_pow` takes two operands. The rest answer `{}` and stay off the nest path
// (`has_a_writing_over_the_value_type`, `ir/op_rules.hpp`).
template <class V>
HEINZEL_COMPUTE_INLINE constexpr ComputedAs<V> transcendental_in_double_as(OpCode op, FnAccuracy fn, V a, V b) {
    if (op == op::Tan) {
        return ComputedAs<V>{tangent_over(a, fn), true};
    }
    if (op == op::Exp2) {
        return ComputedAs<V>{two_to_the_power_over(a, fn), true};
    }
    if (op == op::Pow) {
        return ComputedAs<V>{power_over(a, b, fn), true};
    }
    return ComputedAs<V>{};
}

// The transcendentals with a writing over a value type: a core in the value's own type, a float core for a float
// (which core a type gets: `runtime/float_cores.hpp`; the table's list: `has_a_core_in_the_values_own_type`,
// `ir/op_rules.hpp`), else the double core, one number at a time. The second family is reached from here rather
// than called beside this one in `compute_as`: one more call there, folded away as it is, moved a FIR's code in
// every arm (W5 of `tests/zero_overhead/`: 3.5 % more instructions a run, 121 B more code).
template <class V>
HEINZEL_COMPUTE_INLINE constexpr ComputedAs<V> transcendental_as(OpCode op, FnAccuracy fn, V a, V b) {
    if (op == op::Exp) {
        return ComputedAs<V>{exponential_over(a, fn), true};
    }
    if (op == op::Tanh) {
        return ComputedAs<V>{hyperbolic_tangent_over(a, fn), true};
    }
    if (op == op::SinPi) {
        return ComputedAs<V>{sine_of_half_turns_over(a, fn), true};
    }
    if (op == op::Sin) {
        return ComputedAs<V>{sine_over(a, fn), true};
    }
    if (op == op::Cos) {
        return ComputedAs<V>{cosine_over(a, fn), true};
    }
    if (op == op::Log) {
        return ComputedAs<V>{logarithm_over(a, fn), true};
    }
    return transcendental_in_double_as(op, fn, a, b);
}

// The rule table says which `Approximate` rows have a writing over the value type, and which of those a core in
// the value's own type (`has_a_writing_over_the_value_type`, `has_a_core_in_the_values_own_type`,
// `ir/op_rules.hpp`), because the planner asks the table and may not include `runtime/`. These check each claim
// against the families at `float`, in every build that includes this file. Every other row's arithmetic is over the
// value type already.
constexpr bool is_written_over_float(OpCode op) {
    return transcendental_as<float>(op, FnAccuracy::Exact, 1.0f, 1.0f).written;
}

constexpr bool has_a_float_core(OpCode op) {
    return is_written_over_float(op) && !transcendental_in_double_as<float>(op, FnAccuracy::Exact, 1.0f, 1.0f).written;
}

constexpr bool every_own_type_core_the_table_claims_is_written(const OpTableRow* rows, std::uint8_t count) {
    for (std::uint8_t i = 0; i < count; ++i) {
        const bool approximate = rows[i].rules.numeric == NumericContract::Approximate;
        if (approximate && has_a_core_in_the_values_own_type(OpCode{i}) != has_a_float_core(OpCode{i})) {
            return false;
        }
    }
    return true;
}

constexpr bool every_writing_the_table_claims_is_written(const OpTableRow* rows, std::uint8_t count) {
    for (std::uint8_t i = 0; i < count; ++i) {
        const bool approximate = rows[i].rules.numeric == NumericContract::Approximate;
        if (approximate && has_a_writing_over_the_value_type(OpCode{i}) != is_written_over_float(OpCode{i})) {
            return false;
        }
    }
    return true;
}

static_assert(every_own_type_core_the_table_claims_is_written(kOpTable, kOpCount),
              "the rule table and runtime/compute.hpp disagree about which transcendental has a core "
              "in the value's own type — write the float core, or take the row off the list in "
              "has_a_core_in_the_values_own_type");
static_assert(every_writing_the_table_claims_is_written(kOpTable, kOpCount),
              "the rule table and runtime/compute.hpp disagree about which transcendental has a writing "
              "over the value type — write it (its double core one number at a time, transcendental_in_double_as), "
              "or take the row off the list in has_a_writing_over_the_value_type");

// What the loop tree calls: one operation over `float`, `double` or a `Lane` of either. It covers the
// families above and refuses the rest, whose graphs the planner refuses first (`has_a_writing_over_the_value_type`).
// `op` is a constant wherever this is instantiated, so the chain folds.
// `fn` is the resolved tier: a node's `Inherit` means its graph's default, which this function does not know.
template <class V>
HEINZEL_COMPUTE_INLINE constexpr V compute_as(OpCode op, FnAccuracy fn, V a, V b = V{}, V c = V{}) {
    if (const ComputedAs<V> r = arithmetic_of(op, a, b, c); r.written) {
        return r.value;
    }
    if (const ComputedAs<V> r = root_of(op, a); r.written) {
        return r.value;
    }
    if (const ComputedAs<V> r = rounding_of(op, a); r.written) {
        return r.value;
    }
    if (const ComputedAs<V> r = transcendental_as(op, fn, a, b); r.written) {
        return r.value;
    }
    if (const ComputedAs<V> r = comparison_of(op, a, b, c); r.written) {
        return r.value;
    }
    fail("heinzel::compute_as: this operation has no writing over this value type yet");
}

} // namespace heinzel
