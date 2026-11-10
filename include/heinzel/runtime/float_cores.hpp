#pragma once

// `exp`, `tanh`, `sinpi`, `sin`, `cos` and `log` computed in the value's own type: a float, or a lane of floats.
// Under `Normal` an F32 value is computed in float. The double cores (`runtime/exponential_cores.hpp`) stay
// the reference; these are one straight line of arithmetic each, every special case a `select` at the end,
// held by `tests/unit/float_cores_test.cpp` to 2 f32 ulp of the double core rounded to float. All but the
// half-turn sine follow Cephes (Stephen L. Moshier, public domain), as the float arms benchmarked against do.

#include <bit>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "heinzel/runtime/exponential_cores.hpp" // the double cores these are measured against
#include "heinzel/runtime/hardware_twins.hpp"
#include "heinzel/runtime/periodic_cores.hpp" // and the circular functions'
#include "heinzel/runtime/power_cores.hpp"    // `pow`'s, which has no float core yet
#include "heinzel/runtime/value_type.hpp"

namespace heinzel {

// No branch on a value, because a lane cannot take one. The main path runs for every input, including the
// ones it has no answer for, and their answers are selected in at the end. It must therefore be defined for
// them, which is what the clamps are for: a reduced exponent is converted to an integer, and neither an
// infinity nor a NaN has one.

// `x * 2^whole` for an integral-valued `whole`, by writing the exponent into the bits — the scaling a
// reduced exponential puts back, which no power function should be called for.
//
// In two halves, because a float's exponent field holds 254 normal powers
// of two and an exponential needs 255 of them: down to 2^-126, which is the answer at an argument of
// -87.34, and up to 2^128, which is the scaling at 88.72 (the significand there is below one, so the
// product is still finite). Halving the exponent puts each half far inside the field. The order matters
// at both ends: `2^a * 2^b` formed on its own would be an infinity at the top and a zero
// at the bottom before it ever met `x`. Written this way the first multiply is exact — a normal power
// of two times a significand near one — so the second is the only rounding, which is what a scaled
// result owes, subnormal answers included.
constexpr float scaled_by_two_to_the(float x, float whole) {
    const std::int32_t n = static_cast<std::int32_t>(whole);
    const std::int32_t low = n >> 1;
    return x * std::bit_cast<float>(static_cast<std::uint32_t>(n - low + 127) << 23) *
           std::bit_cast<float>(static_cast<std::uint32_t>(low + 127) << 23);
}

// Past these an exponential has no float answer that is not a bound — an infinity above, a zero below
// — and clamping to them is ALSO what makes the integer conversion above defined: the comparisons
// below are written so that an infinity and a NaN are sent to a bound rather than through. They are
// where `x * log2(e)` reaches the ends of the range the two halves cover, 128 and -150; the answer at
// each is already the bound, so no select is needed to put one back.
inline constexpr float kExpInFloatHighest = 88.8f;
inline constexpr float kExpInFloatLowest = -104.0f;

// ln 2 split so that `whole * high` is exact — `high`'s bottom bits are clear, and `whole` is a small
// integer — and the low half corrects what the split left out. Cody and Waite's arrangement, at
// Cephes' constants for the float interval.
inline constexpr float kNaturalLogOfTwoHighInFloat = 0.693359375f;
inline constexpr float kNaturalLogOfTwoLowInFloat = -2.12194440e-4f;
inline constexpr float kOneOverNaturalLogOfTwoInFloat = 1.44269504088896341f;

// An argument written as `whole * ln2 + r` with |r| <= ln(2)/2: its exponential is `2^whole * exp(r)`,
// an exact scaling of a degree-6 minimax polynomial over an interval narrow enough that six terms reach
// a float's last bit. The two parts are handed back apart, because how the scaling is put in depends
// on how large `whole` can be, which the caller knows.
template <class V>
struct ReducedExponential {
    V of_the_remainder; // exp(r), |r| <= ln(2)/2
    V whole;            // the power of two it is still owed
};

template <class V>
constexpr ReducedExponential<V> reduced_exponential_in_float(V within) {
    using B = Broadcast<V>;
    const V whole = round_to_nearest_even(within * B::of(kOneOverNaturalLogOfTwoInFloat));
    const V r = (within - whole * B::of(kNaturalLogOfTwoHighInFloat)) - whole * B::of(kNaturalLogOfTwoLowInFloat);
    // Horner steps, each a multiply-add as the machine rounds it (`runtime/hardware_twins.hpp`): they
    // are this function's critical path, and one instruction each where the machine has it.
    V y = B::of(1.9875691500e-4f);
    y = multiply_add_as_the_machine_rounds(y, r, B::of(1.3981999507e-3f));
    y = multiply_add_as_the_machine_rounds(y, r, B::of(8.3334519073e-3f));
    y = multiply_add_as_the_machine_rounds(y, r, B::of(4.1665795894e-2f));
    y = multiply_add_as_the_machine_rounds(y, r, B::of(1.6666665459e-1f));
    y = multiply_add_as_the_machine_rounds(y, r, B::of(5.0000001201e-1f));
    return {multiply_add_as_the_machine_rounds(y, r * r, r) + B::of(1.0f), whole};
}

// e to the x, in float, over the whole range: clamped, reduced, scaled in two halves.
//
// Measured against the double core rounded to float over all 2^32 arguments, built with and without the
// fused multiply-add: at most 1 ulp (`tests/unit/float_cores_test.cpp`).
template <class V>
constexpr V exponential_in_float(V x) {
    using B = Broadcast<V>;
    const V within = select(x < B::of(kExpInFloatHighest),
                            select(x > B::of(kExpInFloatLowest), x, B::of(kExpInFloatLowest)),
                            B::of(kExpInFloatHighest));
    const ReducedExponential<V> e = reduced_exponential_in_float(within);
    // The one value the main path cannot answer for: the clamp sent it to a bound, so it is put back.
    return select(x == x, scaled_by_two_to_the(e.of_the_remainder, e.whole), x);
}

// `x * 2^whole` where `whole` is a SMALL power — one normal float holds it, so one insertion does.
constexpr float scaled_by_a_small_power_of_two(float x, float whole) {
    return x * std::bit_cast<float>(static_cast<std::uint32_t>(static_cast<std::int32_t>(whole) + 127) << 23);
}

// Below this magnitude a hyperbolic tangent is its own odd polynomial and above it a ratio of
// exponentials. Cephes' switch point, for the reason the double core gives at
// length: `(e^2x - 1)/(e^2x + 1)` cancels totally near zero, where the answer's own bits are the ones
// thrown away, so near zero the answer is computed and never subtracted.
inline constexpr float kTanhIsRationalBelow = 0.625f;
inline constexpr float kTanhIsOneAboveTwice = 20.0f; // 1 - 2/(e^20 + 1) is one to a float's last bit

// The hyperbolic tangent, in float. Both branches are always evaluated — a lane takes no branch — and
// the switch is the select at the end.
//
// Measured against the double core rounded to float, over every binade from 2^-30 to 2^7 of both
// signs at 4096 points each: at most 1 ulp.
//
// An infinity falls out rather than being selected for: twice its magnitude is held at twenty, where
// the answer is exactly one, with the argument's sign. A NaN and a zero do not, and are put back at the
// end — Cephes returns a zero early for the same reason, and a lane cannot return early.
template <class V>
constexpr V hyperbolic_tangent_in_float(V x) {
    using B = Broadcast<V>;
    const V zero = B::of(0.0f);
    const V magnitude = select(x < zero, -x, x);
    const V square = x * x;
    V p = B::of(-5.70498872745e-3f);
    p = multiply_add_as_the_machine_rounds(p, square, B::of(2.06390887954e-2f));
    p = multiply_add_as_the_machine_rounds(p, square, B::of(-5.37397155531e-2f));
    p = multiply_add_as_the_machine_rounds(p, square, B::of(1.33314422036e-1f));
    p = multiply_add_as_the_machine_rounds(p, square, B::of(-3.33332819422e-1f));
    const V near_zero = multiply_add_as_the_machine_rounds(x * square, p, x);
    // The exponential it needs is of an argument in [0, 20]: past ten the answer is one to a float's
    // last bit, so twice the magnitude is held at twenty — and there the power of two the exponential
    // is owed is between 0 and 29, which ONE insertion holds. The halved insertion a whole-range
    // exponential needs is a tenth of this function's work, three times per sample of a channel strip.
    const V twice = magnitude + magnitude;
    const ReducedExponential<V> e = reduced_exponential_in_float(select(twice < B::of(kTanhIsOneAboveTwice), twice, B::of(kTanhIsOneAboveTwice)));
    const V saturating = B::of(1.0f) - B::of(2.0f) / (scaled_by_a_small_power_of_two(e.of_the_remainder, e.whole) + B::of(1.0f));
    const V answer = select(magnitude < B::of(kTanhIsRationalBelow), near_zero, select(x < zero, -saturating, saturating));
    // Two arguments are their own answer: a NaN, which the hold sent to twenty, and a zero, whose sign
    // the polynomial loses (-0 * x^2 * p is +0, and -0 + +0 is +0). `x != x || x == 0` is `!(x < 0 || x > 0)`.
    return select(x < zero, answer, select(x > zero, answer, x));
}

// ---- the sine of half turns, in float --------------------------------------------------------------
//
// `sinpi(x)` is `sin(pi * x)` with the argument counted in HALF TURNS, and that is what makes it the
// one member of the periodic family worth writing at this width: its reduction is a subtraction of an
// integer, which is EXACT for every float there is, so the answer at 1e7 half-turns is as good as the
// answer at one (`runtime/periodic_cores.hpp` argues this at length for the double core). A radian
// sine at this width would have the opposite property.
//
// The reduction, and why it converts nothing to an integer. `r = x - roundeven(x)` is exact at every
// float — below 1/2 the rounding is zero and `r` is `x` itself; at or above it `r` is a multiple of
// `ulp(x) >= 2^-24` no larger than 1/2, which is at most 2^23 of them and so fits a significand with
// room to spare, subtraction and all. What is still owed is `(-1)^n` for
// `n = roundeven(x)`, and `n` reaches 2^128, which no integer here holds. So the parity is taken in
// floats: `n - 2*roundeven(n/2)` is zero when `n` is even and `+-1` when it is odd, both halvings and
// doublings being exact. A lane takes no branch, so it is a comparison against zero and a select.
//
// Two polynomials and not one, which was measured rather than assumed. A single odd polynomial on
// `|r| <= 1/2` is the cheaper shape — six terms reach 2e-11, so the fit is free — and it does not
// meet the bound: its Horner sum runs from pi down to 2.29 at the far end, and the cancellation
// carries each intermediate rounding into the answer amplified by 1.37. Measured that way the core
// was 3 f32 ulp. Folding `r` to a quarter turn instead — `sin(pi r)` on `|r| <= 1/4`, and
// `cos(pi g)` with `g = 1/2 - |r|` on the rest, which is the same curve a quarter turn along — cuts
// the amplification to 1.11 and 1.41, and the two polynomials are independent chains that a machine
// runs at once. `g` is exact where it is used: `|r| >= 1/4` there, which is Sterbenz's condition.
//
// The coefficients were fitted for this file, minimax in the relative error over `z = u*u` in
// [0, 1/16], and then driven onto the FLOAT grid one at a time — fit, round the constant term, fit
// the rest again, round the next — because rounding a finished fit to float afterwards costs more
// than the fit itself is worth: it was 1.3 ulp of the 2 here. Four terms for the sine (its own fit
// error is then 0.5 ulp, all of it the last bit of `pi`) and five for the cosine (0.01 ulp; four
// would be 1.2 and the budget does not have it). The cosine's constant term is pinned to exactly one,
// which is what makes a half-integer argument answer exactly +-1.
//
// Measured against the double core rounded to float, for every float from zero to 2^24 — the whole
// range in which a float is not already an integer — built with and without the fused multiply-add:
// at most 2 ulp either way, and over 1 ulp at 23021 of the 1.26e9 arguments. It is 2 rather than the
// exponential's 1 for a reason worth writing down: near a whole turn the answer is `r * pi`, `pi` in
// float is already 0.47 ulp off, and the polynomial's own correction to it is below half an ulp of
// `pi` and so is rounded away by the last Horner step. Both are floors that only a second float of
// `pi` — an extra multiply-add, and a fused one at that — would lift, and `Normal` is not owed it.
template <class V>
constexpr V sine_of_half_turns_in_float(V x) {
    using B = Broadcast<V>;
    const V zero = B::of(0.0f);
    const V whole = round_to_nearest_even(x);
    const V r = x - whole; // exact at every float, |r| <= 1/2
    const V magnitude = select(r < zero, -r, r);

    const V z = r * r; // sin(pi r), on the quarter turn around zero
    V s = B::of(-0x1.32ecc4p-1f);
    s = multiply_add_as_the_machine_rounds(s, z, B::of(0x1.467ff2p+1f));
    s = multiply_add_as_the_machine_rounds(s, z, B::of(-0x1.4abc24p+2f));
    s = multiply_add_as_the_machine_rounds(s, z, B::of(0x1.921fb6p+1f));

    const V g = B::of(0.5f) - magnitude; // cos(pi g), on the quarter turn around a half turn
    const V w = g * g;
    V c = B::of(0x1.d47dcep-3f);
    c = multiply_add_as_the_machine_rounds(c, w, B::of(-0x1.55b13cp+0f));
    c = multiply_add_as_the_machine_rounds(c, w, B::of(0x1.03c1aep+2f));
    c = multiply_add_as_the_machine_rounds(c, w, B::of(-0x1.3bd3ccp+2f));
    c = multiply_add_as_the_machine_rounds(c, w, B::of(1.0f));

    // The cosine half is even, so it is handed back the sign the sine half carries in `r` itself.
    const V quarter = select(magnitude <= B::of(0.25f), r * s, select(r < zero, -c, c));
    const V halved = round_to_nearest_even(whole * B::of(0.5f));
    const V answer = select(whole - (halved + halved) == zero, quarter, -quarter);
    // An INTEGER argument — every float past 2^23 included, and both zeros — is a zero carrying the
    // argument's sign, which is what makes `sinpi(-x) == -sinpi(x)` hold there too; the polynomial
    // loses that sign, and `x * 0` is it. An infinity and a NaN need no case of their own: the
    // reduction answers a NaN for both (`inf - inf`), and a NaN is not zero, so it falls through.
    return select(r == zero, x * zero, answer);
}

// ---- the sine and the cosine of an angle in radians, in float ------------------------------------
//
// Cephes' `sinf` polynomials (Moshier; Pommier's `sin_ps` runs the same), ported over the value type. The argument
// is reduced by the nearest multiple `m` of a quarter turn, `r = |x| - m pi/2`, and then `m`'s last two bits
// choose: the sine polynomial or the cosine one on `|r| <= pi/4`, and whether it is negated. The cosine is the
// sine a quarter turn along, `cos(x) = sin(|x| + pi/2)`: the same reduction, its quarter-turn count shifted by one.
//
// The reduction is in five parts, not Cephes' three, because three were measured wrong: over every float up
// to 8192 they came to 478 ulp (at 505.8, near 161 pi), where the answer is small and the reduction's error
// is not (a float comes within 4.2e-9 of a quarter turn there). Here pi/2 is five floats of at most eleven
// significant bits each, so every product `m * part` is exact while `m` is
// below 2^13 (|x| < 12868), and the parts hold pi/2 to 2^-64. The first two subtractions are exact (by
// Sterbenz's lemma); the later ones round only where the remainder is large enough that the rounding falls
// below the answer's last bit. Measured over all 2^32 floats, both with and without the fused
// multiply-add: at most 2 ulp for |x| <= 8192 (about 8.7 thousand arguments over 1), every finite
// argument's answer finite and within [-1, 1]; the cosine the same (8,600 and 8,434 over 1), and even to
// the bit; the logarithm at most 1 ulp over every positive float.
//
// The quadrant is taken in floats, as the half-turn sine takes its parity: `m` reaches past what an integer
// holds long before the argument runs out of floats, and a lane takes no branch. `m - 4 roundeven(m/4)` is
// 0, 1, +-2 or -1 for `m` = 0, 1, 2, 3 (mod 4), every step exact. (`main`'s float arm, Pommier's `sin_ps`,
// counts octants in an int32, which answers nonsense past 2^31 pi/4.)
//
// Past 2^24 the reduction has no bits left to be right with — a float is an integer there, its distance
// to the nearest quarter turn is below its own ulp — so the argument's magnitude is held at 2^24: what
// comes out is some sine or cosine, finite and in [-1, 1], and nothing past that is claimed. The hold also keeps
// `m` and `r * r` finite for the largest floats, where an unheld `|x| 2/pi` is an infinity and the
// cosine polynomial `inf - inf`. An infinity and a NaN are put back as a NaN at the end.
//
// Measured against the double core rounded to float (`tests/unit/float_cores_test.cpp`): see there for
// the domain claimed and the worst ulp found.
inline constexpr float kSineReducesUpTo = 16777216.0f; // 2^24

// `sin(a + QuarterTurns pi/2)` of a magnitude `a`: the sine at 0 quarter turns, the cosine at 1. The shift is added
// to `m` after the reduction, so `r` is the same for both, and `m + 1` is exact (`m` is below 2^24 2/pi).
template <int QuarterTurns, class V>
constexpr V sine_turned_in_float(V magnitude) {
    using B = Broadcast<V>;
    const V zero = B::of(0.0f);
    const V a = select(magnitude < B::of(kSineReducesUpTo), magnitude, B::of(kSineReducesUpTo));
    const V m = round_to_nearest_even(a * B::of(0.636619772367581343f)); // quarter turns: 2/pi
    const V r = ((((a - m * B::of(0x1.92p+0f)) - m * B::of(0x1.fb4p-12f)) - m * B::of(0x1.444p-24f)) - m * B::of(0x1.68cp-39f)) -
                m * B::of(0x1.1a8p-54f);
    const V turns = QuarterTurns == 0 ? m : m + B::of(static_cast<float>(QuarterTurns));
    const V quadrant = turns - B::of(4.0f) * round_to_nearest_even(turns * B::of(0.25f)); // 0, 1, +-2, -1

    const V z = r * r;
    V s = B::of(-1.9515295891e-4f);
    s = multiply_add_as_the_machine_rounds(s, z, B::of(8.3321608736e-3f));
    s = multiply_add_as_the_machine_rounds(s, z, B::of(-1.6666654611e-1f));
    s = multiply_add_as_the_machine_rounds(s * z, r, r);
    V c = B::of(2.443315711809948e-5f);
    c = multiply_add_as_the_machine_rounds(c, z, B::of(-1.388731625493765e-3f));
    c = multiply_add_as_the_machine_rounds(c, z, B::of(4.166664568298827e-2f));
    c = multiply_add_as_the_machine_rounds(c * z, z, -(B::of(0.5f) * z)) + B::of(1.0f);

    const V odd = select(quadrant == B::of(1.0f), B::of(1.0f), select(quadrant == B::of(-1.0f), B::of(1.0f), zero));
    const V p = select(odd == zero, s, c);
    return select(quadrant == B::of(1.0f), p, select(quadrant == zero, p, -p)); // turns = 2, 3 (mod 4)
}

template <class V>
constexpr V sine_in_float(V x) {
    using B = Broadcast<V>;
    const V zero = B::of(0.0f);
    const V magnitude = select(x < zero, -x, x);
    const V flipped = sine_turned_in_float<0>(magnitude);
    const V answer = select(x < zero, -flipped, flipped);
    // A zero is its own answer, sign and all (the polynomial loses it); an infinity and a NaN are `x - x`.
    return select(x == zero, x, select(x - x == zero, answer, x - x));
}

// Even, so the magnitude's answer is the answer, `cos(-x)` the same bits as `cos(x)`. A zero needs no case: the
// cosine polynomial at `r = 0` is exactly one. An infinity and a NaN are `x - x`.
template <class V>
constexpr V cosine_in_float(V x) {
    using B = Broadcast<V>;
    const V zero = B::of(0.0f);
    const V magnitude = select(x < zero, -x, x);
    const V answer = sine_turned_in_float<1>(magnitude);
    return select(x - x == zero, answer, x - x);
}

// ---- the natural logarithm, in float ----------------------------------------------------------------
//
// Cephes' `logf`, ported over the value type: `x = s 2^e` with `s` in [1/2, 1), read out of the bits
// (`significand_of`, `exponent_of` — the reading `scaled_by_two_to_the` writes); `s` folded to
// [sqrt(1/2), sqrt(2)) around one, so `log s` is `t - t^2/2 + t^3 P(t)` of the small `t`; and `e ln 2`
// added in Cody and Waite's two parts, the constants the exponential uses.
//
// Subnormals are answered, not flushed or clamped to the least normal: scaled by 2^23 first, the 23
// taken off again from the exponent.
//
// Measured against the double core rounded to float (`tests/unit/float_cores_test.cpp`): see there.
inline constexpr float kLeastNormalFloat = 1.17549435e-38f;

constexpr float significand_of(float x) { // in [1/2, 1), for a positive normal x
    return std::bit_cast<float>((std::bit_cast<std::uint32_t>(x) & 0x807fffffu) | 0x3f000000u);
}
constexpr float exponent_of(float x) { // x = significand_of(x) * 2^exponent_of(x)
    return static_cast<float>(static_cast<std::int32_t>((std::bit_cast<std::uint32_t>(x) >> 23) & 0xffu) - 126);
}

template <class V>
constexpr V logarithm_in_float(V x) {
    using B = Broadcast<V>;
    const V zero = B::of(0.0f);
    const V tiny = select(x < B::of(kLeastNormalFloat), B::of(1.0f), zero);
    const V normal = select(tiny == zero, x, x * B::of(8388608.0f)); // 2^23
    const V s = significand_of(normal);
    const V below = select(s < B::of(0.707106781186547524f), B::of(1.0f), zero); // sqrt(1/2)
    const V e = exponent_of(normal) - B::of(23.0f) * tiny - below;
    const V t = select(below == zero, s - B::of(1.0f), (s - B::of(1.0f)) + s);

    const V z = t * t;
    V y = B::of(7.0376836292e-2f);
    y = multiply_add_as_the_machine_rounds(y, t, B::of(-1.1514610310e-1f));
    y = multiply_add_as_the_machine_rounds(y, t, B::of(1.1676998740e-1f));
    y = multiply_add_as_the_machine_rounds(y, t, B::of(-1.2420140846e-1f));
    y = multiply_add_as_the_machine_rounds(y, t, B::of(1.4249322787e-1f));
    y = multiply_add_as_the_machine_rounds(y, t, B::of(-1.6668057665e-1f));
    y = multiply_add_as_the_machine_rounds(y, t, B::of(2.0000714765e-1f));
    y = multiply_add_as_the_machine_rounds(y, t, B::of(-2.4999993993e-1f));
    y = multiply_add_as_the_machine_rounds(y, t, B::of(3.3333331174e-1f));
    y = y * t * z;
    y = multiply_add_as_the_machine_rounds(e, B::of(kNaturalLogOfTwoLowInFloat), y);
    y = y - B::of(0.5f) * z;
    const V answer = multiply_add_as_the_machine_rounds(e, B::of(kNaturalLogOfTwoHighInFloat), t + y);
    // A positive infinity is its own answer; a zero of either sign has minus infinity, a negative
    // number or a NaN no answer at all.
    const V infinity = B::of(std::numeric_limits<float>::infinity());
    return select(x > zero, select(x < infinity, answer, x), select(x == zero, -infinity, B::of(std::numeric_limits<float>::quiet_NaN())));
}

// ---- which core a value type gets ----------------------------------------------------------------
//
// A float, or a lane of them, runs the cores above; anything else runs the double cores one number at a
// time, which is all the speed `High` and F64 are owed. The choice is here and not in `runtime/compute.hpp`,
// which maps an operation to its arithmetic: which core a type gets is arithmetic.
//
// The tier is a ceiling: the float cores do not read `fn`, so a `Fast` graph gets ~1e-7 where it asked for
// at most ~1e-4. Over-delivering accuracy is safe, and a coarse float core would be a third writing of each
// function with a bound of its own to measure, so none is written until a workload is measured to need one.
template <class V>
constexpr V exponential_over(V x, [[maybe_unused]] FnAccuracy fn) {
    if constexpr (std::is_same_v<number_of<V>, float>) {
        return exponential_in_float(x);
    } else {
        return EachNumber<V>::of(x, [fn](double v) { return exponential(v, fn); });
    }
}

template <class V>
constexpr V hyperbolic_tangent_over(V x, [[maybe_unused]] FnAccuracy fn) {
    if constexpr (std::is_same_v<number_of<V>, float>) {
        return hyperbolic_tangent_in_float(x);
    } else {
        return EachNumber<V>::of(x, [fn](double v) { return hyperbolic_tangent(v, fn); });
    }
}

template <class V>
constexpr V sine_of_half_turns_over(V x, [[maybe_unused]] FnAccuracy fn) {
    if constexpr (std::is_same_v<number_of<V>, float>) {
        return sine_of_half_turns_in_float(x);
    } else {
        return EachNumber<V>::of(x, [fn](double v) { return sine_of_half_turns(v, fn); });
    }
}

template <class V>
constexpr V sine_over(V x, [[maybe_unused]] FnAccuracy fn) {
    if constexpr (std::is_same_v<number_of<V>, float>) {
        return sine_in_float(x);
    } else {
        return EachNumber<V>::of(x, [fn](double v) { return sine(v, fn); });
    }
}

template <class V>
constexpr V cosine_over(V x, [[maybe_unused]] FnAccuracy fn) {
    if constexpr (std::is_same_v<number_of<V>, float>) {
        return cosine_in_float(x);
    } else {
        return EachNumber<V>::of(x, [fn](double v) { return cosine(v, fn); });
    }
}

template <class V>
constexpr V logarithm_over(V x, [[maybe_unused]] FnAccuracy fn) {
    if constexpr (std::is_same_v<number_of<V>, float>) {
        return logarithm_in_float(x);
    } else {
        return EachNumber<V>::of(x, [fn](double v) { return natural_logarithm(v, fn); });
    }
}

// The transcendentals with no float core yet: every value type, a float too, runs the double core one number at a
// time, so the loop tree answers what the walk answers, bit for bit, at every precision. Slow, and run rather than
// refused (Gerald: none of these capabilities may go, `Plan/16-simplify/08-Later.md`); a float core is written where
// a benchmark asks for one (`Plan/backlog.md`), as it asked for the cosine's.
template <class V>
constexpr V tangent_over(V x, FnAccuracy fn) {
    return EachNumber<V>::of(x, [fn](double v) { return tangent(v, fn); });
}

template <class V>
constexpr V two_to_the_power_over(V x, FnAccuracy fn) {
    return EachNumber<V>::of(x, [fn](double v) { return two_to_the_power(v, fn); });
}

template <class V>
constexpr V power_over(V x, V y, FnAccuracy fn) {
    return EachNumber<V>::of(x, y, [fn](double base, double exponent) { return power(base, exponent, fn); });
}

} // namespace heinzel
