#pragma once

// A DELIBERATE OFFENDER, and nothing includes it.
//
// It exists so `gate_no_instruction_set_macros_would_object` can watch the gate object rather than
// only watch it pass. The shape is the honest one: nobody sets out to write an unmaintainable
// kernel. Somebody has a machine with AVX2, the multiply is the hot one, and a fast path behind an
// `#ifdef` is half an hour's work. Then the next operation gets one, and the two halves can never be
// run against each other in a single build, because only one of them exists in any given build.
//
// What this tree does instead is `codegen/generate.hpp`: a generator is a TYPE. Two of them coexist,
// take the same program, and are compared sample for sample.

namespace heinzel::fixture {

#ifdef __AVX2__
constexpr double scaled(double a, double b) { return a * b; } // the fast one
#else
constexpr double scaled(double a, double b) { return a * b; }
#endif

// And the other spelling, so the gate is shown catching both.
#if defined(__ARM_NEON)
constexpr bool has_a_vector_unit() { return true; }
#else
constexpr bool has_a_vector_unit() { return false; }
#endif

} // namespace heinzel::fixture
