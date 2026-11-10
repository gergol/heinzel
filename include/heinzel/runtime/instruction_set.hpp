#pragma once

// What this build's processor computes in one instruction, as the hardware twins (`runtime/hardware_twins.hpp`)
// need to know it: a header of macros and one constant, declaring no function.

// `HEINZEL_HARDWARE_TWINS=0` turns every twin off, and each operation runs its core. A freestanding build
// sets it: where the instruction set lacks the operation the builtin becomes a libm call, which such a
// build may not be able to link.

#if !defined(HEINZEL_HARDWARE_TWINS)
#define HEINZEL_HARDWARE_TWINS 1
#endif

// Does the target have the instruction? The one question about the instruction set asked anywhere
// under include/, and this file is the one place `gate_no_instruction_set_macros` lets it be asked (and
// `runtime/hardware_twins.hpp` the one that may read the answers it defines).
// Without the instruction a builtin becomes a libm call, which is correct but — measured on baseline
// x86-64 — up to twice slower than the core for trunc/floor/ceil, and for the fused multiply-add not
// promised to be correctly rounded on every platform. So those twins exist only where the instruction
// does. The square root needs no question: every target this is on for has one.
// (`__FP_FAST_FMA` is the portable spelling and gcc defines it; clang does not.)
#if HEINZEL_HARDWARE_TWINS && (defined(__FP_FAST_FMA) || defined(__FMA__) || defined(__ARM_FEATURE_FMA))
#define HEINZEL_FMA_TWIN 1
#else
#define HEINZEL_FMA_TWIN 0
#endif
#if HEINZEL_HARDWARE_TWINS && (defined(__SSE4_1__) || defined(__ARM_FEATURE_DIRECTED_ROUNDING))
#define HEINZEL_ROUNDING_TWIN 1
#else
#define HEINZEL_ROUNDING_TWIN 0
#endif

namespace heinzel {

// A multiply-add rounded once is one instruction here (and the hardware twins are on): the lane form of a
// multiply-add rounded as the machine rounds (`runtime/lane_value.hpp`), which may ask no macro, reads it.
inline constexpr bool kMultiplyAddIsOneInstruction = HEINZEL_FMA_TWIN;

} // namespace heinzel
