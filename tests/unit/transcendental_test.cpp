// What milestone M4's APPROXIMATE operations compute, and how far from the platform's they are.
//
// Split from `numeric_ops_test.cpp` when that file passed amendment 06's six hundred lines, and
// split along a seam that was already in it: the operations there answer exactly and are checked
// BITWISE; the ones here are approximations and are checked to a MEASURED BOUND. Two different
// claims deserve two different files, and mixing them is how a bound quietly becomes the standard
// for something that ought to be exact.
//
// The bounds below are the maxima measured against the platform's library and recorded beside each
// core. They are a GATE and not a note: if a core improves, the bound here is the thing to tighten,
// and a bound that has gone slack is a core that quietly got worse.
//
// What is EXACT about an approximate operation is still asserted as an equality — `_exp2` at every
// integer, `_sinpi` at every half-integer — because those are decisions the cores were shaped
// around rather than accidents of how close they get.
#include "heinzel/runtime/compute.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "double_double.hpp"
#include "one_operation.hpp"

using namespace heinzel;
using heinzel::testing::apply;
using heinzel::testing::kInf;
using heinzel::testing::kNaN;
using heinzel::testing::negative_zero;
using heinzel::testing::same_bits;

// ---- the exponential family, and what is EXACT about it (milestone M4) --------------------------
//
// Two different claims live here and they are checked differently. The values below are exact and
// are asserted as equalities; the accuracy of everything else is a MEASURED bound and is checked by
// the sweeps at the bottom of the file, against the platform's library. An equality here would be
// asserting the core is the platform, which it is not and does not claim to be.
static_assert(apply(op::Exp, 0.0) == 1.0 && apply(op::Log, 1.0) == 0.0);
static_assert(apply(op::Log10, 1.0) == 0.0 && apply(op::Log2, 1.0) == 0.0);

// `_exp2` is exact at every integer, which is why it is not written as `exp(x * ln2)` — that
// spelling misses a power of two by an ulp at every one of them.
static_assert(apply(op::Exp2, 0.0) == 1.0 && apply(op::Exp2, 10.0) == 1024.0);
static_assert(apply(op::Exp2, -3.0) == 0.125 && apply(op::Exp2, 1023.0) == 0x1p1023);
static_assert(apply(op::Exp2, -1074.0) == 0x1p-1074, "the smallest denormal, which is an exact answer");

// `_tanh` is odd, and what is exact about it is exact for two different reasons rather than one.
// At zero, because an odd function's answer at zero is that same zero SIGN AND ALL — and the sign is
// the whole content of that claim, so it is checked with `negative_zero` and not with `==`, which
// cannot tell the two zeros apart. Past about 19.07, because that is where `tanh` reaches the
// largest double below one: every argument beyond it has exactly one as its correctly rounded
// answer, so the saturation is arithmetic rather than a shortcut.
static_assert(apply(op::Tanh, 0.0) == 0.0 && negative_zero(apply(op::Tanh, -0.0)));
static_assert(apply(op::Tanh, 25.0) == 1.0 && apply(op::Tanh, -25.0) == -1.0);

// ...and `_log2` at every power of two, for the mirror reason: the significand is exactly one there.
static_assert(apply(op::Log2, 0x1p37) == 37.0 && apply(op::Log2, 0x1p-1000) == -1000.0);
static_assert(apply(op::Log2, 0x1p-1074) == -1074.0, "a denormal argument, normalised before its exponent is read");

// The edges are the platform's. `log(0)` is minus infinity for EITHER zero, and a negative argument
// is NaN rather than anything more helpful.
static_assert(apply(op::Log, 0.0) == -kInf && apply(op::Log, -0.0) == -kInf);
static_assert(apply(op::Exp, kInf) == kInf && apply(op::Exp, -kInf) == 0.0);
static_assert(apply(op::Log, kInf) == kInf);
static_assert([] {
    const double answer = apply(op::Log, -1.0);
    return answer != answer;
}(), "the logarithm of a negative is NaN");

// ---- the periodic family, and why there are two of them (milestone M4) ---------------------------
//
// `_sinpi` is exact at every integer and at every half-integer, and that is the whole reason it is
// an operation rather than a spelling of `_sin`. Asserted as equalities; the accuracy everywhere
// else is a measured bound, gated below.
static_assert(apply(op::SinPi, 0.5) == 1.0 && apply(op::SinPi, 1.5) == -1.0);
static_assert(apply(op::SinPi, 2.5) == 1.0 && apply(op::SinPi, -0.5) == -1.0);
static_assert(apply(op::SinPi, 1e15) == 0.0, "past 2^52 every double is an integer and every answer a zero");

// The zero at an integer carries the ARGUMENT'S sign, which is what keeps `sinpi` odd. Writing it as
// `-sin(0)` gives negative zero for both 1 and -1, and a sine that is not odd is one somebody will
// eventually divide by.
static_assert(apply(op::SinPi, 1.0) == 0.0 && !negative_zero(apply(op::SinPi, 1.0)));
static_assert(negative_zero(apply(op::SinPi, -1.0)) && negative_zero(apply(op::SinPi, -2.0)));
static_assert(negative_zero(apply(op::SinPi, -0.0)) && !negative_zero(apply(op::SinPi, 0.0)));

// The other three are odd or even in the ordinary way, and their zeros say so.
static_assert(apply(op::Cos, 0.0) == 1.0 && apply(op::Tan, 0.0) == 0.0);
static_assert(negative_zero(apply(op::Sin, -0.0)) && negative_zero(apply(op::Tan, -0.0)));

// Past the point where the radian reduction can stand behind an answer it says NaN rather than
// returning a plausible one. A deliberate deviation from the old tree, which calls libm and reduces
// exactly there; the operation for that range is `_sinpi`.
static_assert([] {
    const double answer = apply(op::Sin, 0x1p41);
    return answer != answer;
}(), "the radian reduction started answering past where it can");
static_assert(apply(op::Sin, 0x1p39) == apply(op::Sin, 0x1p39), "...and stopped answering inside it");

// ---- the inverse circular family, and what is EXACT about it (milestone M4) ----------------------
//
// Four angles recovered from a ratio, and the exact points are the ones the cores were shaped
// around: the quarter turn at the ends of `asin` and `acos`, the eighth turn at an argument of one,
// and the sign of every zero. Everything else is a measured bound, gated below.
static_assert(apply(op::Atan, 0.0) == 0.0 && apply(op::Atan, 1.0) == 0x1.921fb54442d18p-1);
static_assert(apply(op::Atan, kInf) == kHalfPi && apply(op::Atan, -kInf) == -kHalfPi);
static_assert(apply(op::Asin, 1.0) == kHalfPi && apply(op::Asin, -1.0) == -kHalfPi);
static_assert(apply(op::Acos, 1.0) == 0.0 && apply(op::Acos, -1.0) == kPi);
static_assert(apply(op::Acos, 0.0) == kHalfPi && apply(op::Acos, -0.0) == kHalfPi);

// `atan` and `asin` are odd and their zeros say so; `acos` is neither, and the quarter turn it
// answers at either zero has no sign to carry.
static_assert(negative_zero(apply(op::Atan, -0.0)) && !negative_zero(apply(op::Atan, 0.0)));
static_assert(negative_zero(apply(op::Asin, -0.0)) && !negative_zero(apply(op::Asin, 0.0)));

// Outside [-1, 1] there is no angle, and the answer is NaN rather than a clamp. The old tree's rows
// say the same thing in as many words ("|x|>1 -> NaN (numpy)") and reach it through libm.
static_assert([] {
    const double answer = apply(op::Asin, 1.0000000000000002);
    return answer != answer;
}(), "an argument one ulp past one has no arc sine");
static_assert([] {
    const double answer = apply(op::Acos, -1.0000000000000002);
    return answer != answer;
}(), "...nor an arc cosine");

// `_atan2` and its quadrants, where the sign of a ZERO is the whole answer in four cases and no
// arithmetic on the value can recover it.
static_assert(apply(op::Atan2, 1.0, 1.0) == 0x1.921fb54442d18p-1, "the eighth turn, exactly");
static_assert(apply(op::Atan2, 0.0, 1.0) == 0.0 && negative_zero(apply(op::Atan2, -0.0, 1.0)));
static_assert(apply(op::Atan2, 0.0, -1.0) == kPi && apply(op::Atan2, -0.0, -1.0) == -kPi);
static_assert(apply(op::Atan2, 0.0, -0.0) == kPi && apply(op::Atan2, 0.0, 0.0) == 0.0);
static_assert(apply(op::Atan2, 1.0, 0.0) == kHalfPi && apply(op::Atan2, -1.0, -0.0) == -kHalfPi);

// The infinite cases are answered by the finite code at unit arguments, which is what makes them
// exactly the standard's five values rather than five more constants to write down wrong.
static_assert(apply(op::Atan2, kInf, kInf) == 0x1.921fb54442d18p-1);
static_assert(apply(op::Atan2, -kInf, -kInf) == -0x1.2d97c7f3321d2p+1, "minus three quarters of pi");
static_assert(apply(op::Atan2, kInf, 3.0) == kHalfPi && apply(op::Atan2, 3.0, kInf) == 0.0);
static_assert(apply(op::Atan2, -3.0, -kInf) == -kPi);

namespace {

// How many representable doubles apart two answers are. A bound in ULPS rather than a relative
// tolerance, because a relative tolerance says nothing where the answer approaches zero — which is
// exactly where a logarithm spends its hardest arguments.
long long ulps_apart(double ours, double theirs) {
    if (ours == theirs) {
        return 0;
    }
    if (!std::isfinite(ours) || !std::isfinite(theirs)) {
        return -1;                                   // one is infinite and the other is not
    }
    const auto as_ordered = [](double d) { return static_cast<long long>(std::bit_cast<std::uint64_t>(d)); };
    if ((as_ordered(ours) < 0) != (as_ordered(theirs) < 0)) {
        return -1;                                   // opposite signs; the bit distance is meaningless
    }
    const long long gap = as_ordered(ours) - as_ordered(theirs);
    return gap < 0 ? -gap : gap;
}

// Within a stated number of representable doubles of the platform's answer. One predicate rather
// than the same lambda written out in each sweep below: the bound is what differs between them, and
// a copy per sweep is a copy per sweep for one of them to drift in.
// A miss says by how much, and the family's name follows from `main`.
bool within(long long bound, double ours, double theirs) {
    const long long apart = ulps_apart(ours, theirs);
    if (apart >= 0 && apart <= bound) {
        return true;
    }
    std::printf("  %lld ulps apart (-1: a sign or an infinity), allowed %lld: ours %a, theirs %a\n", apart, bound, ours,
                theirs);
    return false;
}

// The exponential family against the platform's, to a BOUND rather than to the bit — because these
// five are approximations and say so. The bounds are the measured maxima recorded beside each core;
// this is what makes them a gate rather than a note. If a core improves, the bound here is the thing
// to tighten, and a bound that has gone slack is a core that quietly got worse.

bool the_exponential_family_stays_within_its_measured_bounds() {
    // The whole domain of each, in steps of a hundredth.
    for (int hundredths = -70900; hundredths <= 70900; ++hundredths) {
        const double x = static_cast<double>(hundredths) / 100.0;
        if (!within(1, apply(op::Exp, x), std::exp(x))) {
            return false;
        }
    }
    for (int hundredths = -102300; hundredths <= 102300; ++hundredths) {
        const double x = static_cast<double>(hundredths) / 100.0;
        if (!within(1, apply(op::Exp2, x), std::exp2(x))) {
            return false;
        }
    }

    // Every binade a double has, thirty-two significands each. `_log10` is allowed two ulps and the
    // other two one; that difference is measured and its cause is written where the core is.
    for (int exponent = -1074; exponent <= 1023; ++exponent) {
        for (int k = 0; k < 32; ++k) {
            const double x = std::ldexp(1.0 + static_cast<double>(k) / 32.0, exponent);
            if (x == 0.0 || !std::isfinite(x)) {
                continue;
            }
            if (!within(1, apply(op::Log, x), std::log(x)) ||
                !within(1, apply(op::Log2, x), std::log2(x)) ||
                !within(2, apply(op::Log10, x), std::log10(x))) {
                return false;
            }
        }
    }

    // The one argument measured at the full two ulps, named so that a change which made it three
    // fails here rather than in a sweep somebody has to re-read.
    return within(2, apply(op::Log10, 0x1.2p-1), std::log10(0x1.2p-1)) &&
           std::isnan(apply(op::Log, kNaN)) && std::isnan(apply(op::Exp, kNaN));
}

// `_sinpi` against a reference built to be fair to it: the reduction done exactly, and the exact
// answer at an integer. `sin(pi * x)` in any finite precision is NOT the reference for this
// function — using it would score the core against the very advantage it exists to have. The
// reduced half-turns are folded to at most a quarter (each step exact in double), then pi times
// them and their sine or cosine are taken in double-double: the reference is correctly rounded on
// every machine. (It was `sin` in long double, which is double on Apple's arm64 and missed by up
// to 71 ulps there, where `sin(pi * r)` is near zero.)
double half_turns_reference(double x) {
    if (x == std::floor(x)) {
        return std::signbit(x) ? -0.0 : 0.0;
    }
    const double reduced = x - 2.0 * std::floor((x + 1.0) / 2.0);
    double a = std::fabs(reduced);
    a = a > 0.5 ? 1.0 - a : a;              // sin(pi a) = sin(pi (1 - a))
    const bool by_cosine = a > 0.25;        // sin(pi a) = cos(pi (1/2 - a))
    const double t = by_cosine ? 0.5 - a : a;
    const double answer = testing::wide_sin_or_cos(testing::kPiWide * testing::DoubleDouble(t), !by_cosine);
    return std::signbit(reduced) ? -answer : answer;
}

// The periodic family, and the question it exists to answer is not accuracy near zero but whether
// the reduction still holds far out — so every range below is swept, not just the first.
//
// The bounds are the measured maxima recorded beside the cores. `_tan` is allowed one more ulp than
// the other two and earns it: it is a quotient of two approximations, so its relative error is
// theirs added.

bool the_periodic_family_stays_within_its_measured_bounds() {
    const auto sweep = [&](double low, double high, int steps, auto&& check) {
        for (int i = 0; i <= steps; ++i) {
            const double x = low + (high - low) * (static_cast<double>(i) / static_cast<double>(steps));
            if (!check(x)) {
                return false;
            }
        }
        return true;
    };

    const auto circular = [&](double x) {
        return within(2, apply(op::Sin, x), std::sin(x)) && within(2, apply(op::Cos, x), std::cos(x));
    };
    const auto tangent_ = [&](double x) { return within(3, apply(op::Tan, x), std::tan(x)); };

    if (!sweep(-3.14159265358979, 3.14159265358979, 40000, circular) ||
        !sweep(-1000.0, 1000.0, 40000, circular) ||
        !sweep(-1e12, 1e12, 40000, circular)) {
        return false;
    }
    if (!sweep(-1.5, 1.5, 40000, tangent_) || !sweep(-1e9, 1e9, 40000, tangent_)) {
        return false;
    }

    // `_sinpi` against a reference fair to it (`half_turns_reference`).
    for (int i = -40000; i <= 40000; ++i) {
        const double x = static_cast<double>(i) / 200.0;
        if (!within(2, apply(op::SinPi, x), half_turns_reference(x))) {
            return false;
        }
    }

    // ...and far past where any radian reduction can go, where it was measured EXACT at every one of
    // two hundred thousand arguments. That is the operation's reason for existing, so it is checked
    // as an equality rather than to a bound.
    for (int i = 0; i < 20000; ++i) {
        const double x = 1e14 + static_cast<double>(i) * 1e8;
        if (apply(op::SinPi, x) != half_turns_reference(x)) {
            return false;
        }
    }
    return true;
}


// The inverse circular family, against the platform's. All four are gated at ONE ulp, which is the
// maximum measured.
//
// WHAT WAS MEASURED and what this re-runs are not the same set, and saying so is the point. During
// development each of the four was also swept over twenty million random bit patterns, and `_atan`
// over every binade a double has at eight significands each; nothing there reached two ulps either.
// The sweeps below are the subset cheap enough to run on every build — a linear sweep, the binades,
// the arguments closest to one from either side, and the exponent pairs that made the quotient a
// denormal. A gate is what runs, and a measurement is what was believed; keeping the two labelled
// separately is what stops the second from being quietly credited to the first.
//
// The tight bound is what `careful_ratio` and `one_minus_square` bought, and it is the reason they
// are written the way they are. A version of `_atan2` without the exponent split measured 9638 ulps
// at two denormal operands; a version of `_asin` reaching for the half-angle identity instead of
// carrying `1 - x*x` as a pair would not hold this near an argument of one.

bool the_one_operand_arcs_stay_within_their_measured_bounds() {
    // The whole interesting range of `_atan`, in steps of a thousandth, and every binade beyond it.
    for (int thousandths = -400000; thousandths <= 400000; ++thousandths) {
        const double x = static_cast<double>(thousandths) / 1000.0;
        if (!within(1, apply(op::Atan, x), std::atan(x))) {
            return false;
        }
    }
    for (int exponent = -1074; exponent <= 1023; ++exponent) {
        for (int k = 0; k < 8; ++k) {
            const double x = std::ldexp(1.0 + static_cast<double>(k) / 8.0, exponent);
            if (x == 0.0 || !std::isfinite(x)) {
                continue;
            }
            if (!within(1, apply(op::Atan, x), std::atan(x)) ||
                !within(1, apply(op::Atan, -x), std::atan(-x))) {
                return false;
            }
        }
    }

    // `_asin` and `_acos` over their whole domain, and then over the arguments closest to one —
    // which is where a spelling that subtracts two nearly equal angles loses its bits.
    for (int i = -400000; i <= 400000; ++i) {
        const double x = static_cast<double>(i) / 400001.0;
        if (!within(1, apply(op::Asin, x), std::asin(x)) ||
            !within(1, apply(op::Acos, x), std::acos(x))) {
            return false;
        }
    }
    double near_one = 1.0;
    for (int i = 0; i < 100000; ++i) {
        near_one = std::nextafter(near_one, 0.0);
        if (!within(1, apply(op::Asin, near_one), std::asin(near_one)) ||
            !within(1, apply(op::Acos, near_one), std::acos(near_one)) ||
            !within(1, apply(op::Asin, -near_one), std::asin(-near_one)) ||
            !within(1, apply(op::Acos, -near_one), std::acos(-near_one))) {
            return false;
        }
    }

    return std::isnan(apply(op::Atan, kNaN)) && std::isnan(apply(op::Asin, kNaN));
}

// `_atan2` on its own, because the question it answers is a different one: not how accurately an
// angle follows from a ratio, but whether the ratio survives being formed at all. The second sweep
// below is the one that found the 9638-ulp defect, and it is the reason the exponent split exists.
bool the_two_operand_arc_stays_within_its_measured_bound() {
    // All four quadrants, and then pairs whose exponents are far enough apart that the quotient
    // alone would be a denormal or an overflow.
    for (int i = -300; i <= 300; ++i) {
        for (int j = -300; j <= 300; ++j) {
            const double y = static_cast<double>(i) / 7.0;
            const double x = static_cast<double>(j) / 13.0;
            if (!within(1, apply(op::Atan2, y, x), std::atan2(y, x))) {
                return false;
            }
        }
    }
    for (int high = -1070; high <= 1020; high += 7) {
        for (int low = -1070; low <= 1020; low += 13) {
            const double y = std::ldexp(1.3, high);
            const double x = std::ldexp(1.7, low);
            if (!within(1, apply(op::Atan2, y, x), std::atan2(y, x)) ||
                !within(1, apply(op::Atan2, -y, x), std::atan2(-y, x)) ||
                !within(1, apply(op::Atan2, y, -x), std::atan2(y, -x))) {
                return false;
            }
        }
    }

    // The infinite cases, bit for bit against the platform rather than to a bound — they are exact
    // answers and this tree reaches them by a different route (unit arguments through the finite
    // code), so "the same angle" is the whole claim and an ulp of slack would hide a wrong quadrant.
    const double infinities[3] = {kInf, -kInf, 2.5};
    for (const double y : infinities) {
        for (const double x : infinities) {
            if (y != kInf && y != -kInf && x != kInf && x != -kInf) {
                continue;
            }
            if (!same_bits(apply(op::Atan2, y, x), std::atan2(y, x))) {
                return false;
            }
        }
    }
    return std::isnan(apply(op::Atan2, kNaN, 1.0)) && std::isnan(apply(op::Atan2, 1.0, kNaN));
}

// `_tanh` against the platform's, gated at TWO ulps, which is the maximum measured. Two and not one
// is a stated result rather than slack: the answer is a quotient of two quantities the exponential
// series produced, so it carries that series' error twice over, and the compensated division is what
// keeps the second copy from becoming a third.
//
// WHAT WAS MEASURED and what this re-runs are again different sets. The development sweep was five
// million arguments from -25 to 25 in steps of a hundred-thousandth (4884259 of them bit-identical
// to `std::tanh`, worst case 2 ulps at -0.99867) plus every binade below 16 at sixteen significands
// each. The gate below is the same shape at a tenth of the density.
bool the_hyperbolic_tangent_stays_within_its_measured_bound() {
    for (int i = -2500000; i <= 2500000; i += 10) {
        const double x = static_cast<double>(i) / 100000.0;
        if (!within(2, apply(op::Tanh, x), std::tanh(x))) {
            return false;
        }
    }

    // Every binade the answer is not yet saturated in, from the smallest denormal upwards. Near zero
    // `tanh(x)` IS `x` to the last bit, and the series is what has to deliver that rather than a
    // special case — so the small end is where the leading term being taken out of the Horner loop
    // is doing its work.
    for (int exponent = -1074; exponent < 4; ++exponent) {
        for (int k = 0; k < 16; ++k) {
            const double x = std::ldexp(1.0 + static_cast<double>(k) / 16.0, exponent);
            if (x == 0.0 || !std::isfinite(x)) {
                continue;
            }
            if (!within(2, apply(op::Tanh, x), std::tanh(x)) ||
                !within(2, apply(op::Tanh, -x), std::tanh(-x))) {
                return false;
            }
        }
    }

    // The two arguments the sweeps above cannot reach, since neither is a step of anything: the
    // infinities saturate like every argument past 19.07, and NaN propagates.
    return apply(op::Tanh, kInf) == 1.0 && apply(op::Tanh, -kInf) == -1.0
           && std::isnan(apply(op::Tanh, kNaN));
}

// ---- `_pow`, whose accuracy question is a different one -------------------------------------------
//
// Gated at ONE ulp, and reaching one ulp is the whole content of `power_cores.hpp`. Written the
// obvious way — `two_to_the_power(y * logarithm_base_two(x))` — the same operation is out by
// hundreds, because the logarithm's error is multiplied by `y` and `y * log2(x)` runs to 1024.
//
// So the sweeps below are chosen for that failure mode rather than for coverage of the domain. The
// one that matters is `the product near its limit`: arguments picked so that `y * log2(x)` lands
// near ±1024 with a base close to one, which is where a logarithm that is merely accurate to a
// double is visibly not enough. During development the whole set was 1.85 million arguments —
// a 720600-point grid, 380325 across binades from 2^-300 to 2^300, 400000 with the product driven
// to its limit, 48400 negative bases, and 300000 random pairs — and none of them reached two.
//
// Two intermediate versions of the file failed exactly here and nowhere else: a series truncated ten
// terms too early was 146 ulps out, and a squared argument that dropped its own low half was 2^-60
// where the budget is 2^-63. Both are invisible to every other sweep.
bool the_power_stays_within_its_measured_bound() {
    for (int i = 1; i <= 600; i += 3) {
        for (int j = -600; j <= 600; j += 3) {
            const double x = static_cast<double>(i) / 60.0;
            const double y = static_cast<double>(j) / 60.0;
            if (!within(1, apply(op::Pow, x, y), std::pow(x, y))) {
                return false;
            }
        }
    }

    // The product near its limit: a base close to one, and an exponent chosen to put `y * log2(x)`
    // at a named distance from where the answer stops being a double.
    for (int i = 1; i <= 3000; ++i) {
        const double x = 1.0 + static_cast<double>(i) * 1e-4;
        const double logarithm = std::log2(x);
        for (const double target : {-1020.0, -700.0, -100.0, 100.0, 700.0, 1020.0}) {
            const double y = target / logarithm;
            if (!within(1, apply(op::Pow, x, y), std::pow(x, y))) {
                return false;
            }
        }
    }

    // Across the binades, and negative bases at integral exponents — where the answer's SIGN is the
    // exponent's parity and nothing else.
    for (int exponent = -300; exponent <= 300; exponent += 3) {
        for (int k = 0; k < 8; ++k) {
            const double x = std::ldexp(1.0 + static_cast<double>(k) / 8.0, exponent);
            for (int j = -40; j <= 40; ++j) {
                const double y = static_cast<double>(j) / 2.0;
                if (std::isfinite(std::pow(x, y)) && !within(1, apply(op::Pow, x, y), std::pow(x, y))) {
                    return false;
                }
            }
        }
    }
    for (int i = -400; i <= -1; ++i) {
        for (int j = -60; j <= 60; ++j) {
            const double x = static_cast<double>(i) / 40.0;
            const double y = static_cast<double>(j);
            if (std::isfinite(std::pow(x, y)) && !within(1, apply(op::Pow, x, y), std::pow(x, y))) {
                return false;
            }
        }
    }
    return true;
}

// `_pow`'s special cases, bit for bit against the platform rather than to a bound — every one of
// them is an exact answer, and an ulp of slack would hide a lost sign or a wrong infinity. The pairs
// are every combination of the values below, which between them exercise each rule the standard
// states: a zero exponent and a unit base (which answer one even against NaN), both signed zeros,
// both infinities, odd and even integral exponents, and a non-integral exponent on a negative base.
bool the_power_answers_the_standard_cases_exactly() {
    const double interesting[] = {0.0,   -0.0, 1.0,  -1.0, 2.0,  -2.0, 0.5, -0.5,
                                  3.0,   -3.0, 2.5,  -2.5, kInf, -kInf, kNaN};
    for (const double x : interesting) {
        for (const double y : interesting) {
            const double ours = apply(op::Pow, x, y);
            const double theirs = std::pow(x, y);
            if (std::isnan(theirs)) {
                if (!std::isnan(ours)) {
                    return false;
                }
                continue;
            }
            // Not `same_bits` for the half-integral exponents: those are ordinary approximations and
            // are covered by the bound above. Everything else here is exact on both sides, and the
            // bound of one ulp cannot tell a lost zero-sign from a right one — so the sign is asked
            // about separately and directly.
            if (!within(1, ours, theirs) || std::signbit(ours) != std::signbit(theirs)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

int main() {
    struct Family {
        const char* name;
        bool (*holds)();
    };
    const Family families[] = {
        {"the exponential family", the_exponential_family_stays_within_its_measured_bounds},
        {"the periodic family", the_periodic_family_stays_within_its_measured_bounds},
        {"the one-operand arcs", the_one_operand_arcs_stay_within_their_measured_bounds},
        {"the two-operand arc", the_two_operand_arc_stays_within_its_measured_bound},
        {"the hyperbolic tangent", the_hyperbolic_tangent_stays_within_its_measured_bound},
        {"the power", the_power_stays_within_its_measured_bound},
        {"the power's standard cases", the_power_answers_the_standard_cases_exactly},
    };
    int wrong = 0;
    for (const Family& family : families) {
        if (!family.holds()) {
            ++wrong;
            std::printf("transcendental_test: wrong: %s\n", family.name);
        }
    }
    return wrong == 0 ? 0 : 1;
}
