// The float cores against the double ones: `exp`, `tanh`, `sinpi`, `sin`, `cos` and `log` computed in float
// (`runtime/float_cores.hpp`) versus the double core's answer rounded to float
// (`runtime/exponential_cores.hpp`, `runtime/periodic_cores.hpp`), to a bound of TWO f32 ulp — the
// accuracy class `main`'s benchmarked `Precise` arm is in, which is what makes the comparison like
// with like (Plan/archive/00 D-7, Plan/archive/03 §3).
//
// The sweep is dense and it is by BINADE, because an ulp is a relative unit: every binade of
// magnitude that matters, both signs, thousands of arguments in each, plus the arguments no binade
// sweep reaches — the zeros, the subnormals, NaN, both infinities, the thresholds where an exponential
// overflows and where it underflows, and the 0.625 where the hyperbolic tangent changes method.
//
// AND THE HALF-TURN SINE IS SWEPT FURTHER, because its whole claim is that the argument's size does
// not matter: every binade to 2^24 and past it, where a float is already an integer and the answer is
// a signed zero; the quarter turn where it changes polynomial; the integers and half-integers, which
// must be exactly +-0 and +-1 rather than nearly; and a scan for oddness to the bit, which is what
// makes it safe to divide by.
//
// AND THE RADIAN SINE AND COSINE AND THE LOGARITHM (Cephes' polynomials, as `main`'s `Precise` arm). The
// sine and the cosine are claimed to 2 ulp on |x| <= 8192, where their five-part reduction is exact to the
// last subtraction; past it the reduction runs out of bits, and what is held there is only that the answer
// is a number in [-1, 1] (the worst error per binade is printed). The sine is odd and the cosine even, to
// the bit. The logarithm is claimed on every positive float, subnormals included.
//
// AND THE LANE FORM AGAINST THE PLAIN ONE, bit for bit. One writing serves both, so a difference
// between them is a difference in what `Lane` does with it — the rounding, the exponent insertion —
// and that is a fault of its own kind rather than an accuracy question.

#include "heinzel/runtime/lane_value.hpp"
#include "heinzel/runtime/math_cores.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

using namespace heinzel;

namespace {

int g_failures = 0;

// How many representable floats apart two answers are. The IEEE-754 bit pattern of a non-negative
// float increases with the value, so the distance is a subtraction once both are mapped onto that
// order — which is what the flip below does for the negative half.
std::int64_t ordered(float x) {
    const std::int64_t bits = std::bit_cast<std::int32_t>(x);
    return bits >= 0 ? bits : -0x80000000LL - bits;
}

std::int64_t ulps_apart(float got, float want) {
    if (got != got || want != want) {
        return (got != got) == (want != want) ? 0 : (1LL << 40); // a NaN for a NaN is the same answer
    }
    // An infinity where a number is owed (or the reverse) is one step in bit order and no small error;
    // nor is the wrong zero, which `==` cannot see. A review found `tanh(-0.0) == +0.0` behind both.
    const bool got_inf = got - got != 0.0f;
    const bool want_inf = want - want != 0.0f;
    if (got_inf != want_inf || (got == 0.0f && want == 0.0f && std::bit_cast<std::uint32_t>(got) != std::bit_cast<std::uint32_t>(want))) {
        return 1LL << 40;
    }
    if (got == want) {
        return 0;
    }
    const std::int64_t d = ordered(got) - ordered(want);
    return d < 0 ? -d : d;
}

// The worst disagreement seen for one core, and where.
struct Worst {
    const char* what;
    std::int64_t ulps{0};
    float at{0.0f};
    float got{0.0f};
    float want{0.0f};
};

Worst g_exp{"exp"};
Worst g_tanh{"tanh"};
Worst g_sinpi{"sinpi"};
Worst g_sin{"sin"};
Worst g_cos{"cos"};
Worst g_log{"log"};
Worst g_far{"sin and cos past 8192"}; // held only to [-1, 1]

void record(Worst& worst, float x, float got, float want) {
    const std::int64_t d = ulps_apart(got, want);
    if (d > worst.ulps) {
        worst = Worst{worst.what, d, x, got, want};
    }
}

// One value in a lane of eight against the plain float it was made from, bit for bit.
bool lane_agrees(const char* what, float x, float plain, auto core) {
    const Lane<float, 8> wide = core(Lane<float, 8>::splat(x));
    for (std::size_t k = 0; k < 8; ++k) {
        if (std::bit_cast<std::uint32_t>(wide[k]) == std::bit_cast<std::uint32_t>(plain) ||
            (wide[k] != wide[k] && plain != plain)) {
            continue;
        }
        if (++g_failures <= 10) {
            std::printf("  a lane differs from the plain float at %a: %s %a vs %a\n", static_cast<double>(x), what,
                        static_cast<double>(wide[k]), static_cast<double>(plain));
        }
        return false;
    }
    return true;
}

// One argument through all six cores, at a plain float and in a lane of eight, against the double
// core rounded once. The lane is held to the plain float exactly: same writing, same answer.
void check(float x) {
    const double wide = static_cast<double>(x);
    const float exp_got = exponential_in_float(x);
    const float tanh_got = hyperbolic_tangent_in_float(x);
    const float sinpi_got = sine_of_half_turns_in_float(x);
    record(g_exp, x, exp_got, static_cast<float>(exponential(wide, FnAccuracy::Exact)));
    record(g_tanh, x, tanh_got, static_cast<float>(hyperbolic_tangent(wide, FnAccuracy::Exact)));
    record(g_sinpi, x, sinpi_got, static_cast<float>(sine_of_half_turns(wide, FnAccuracy::Exact)));
    lane_agrees("exp", x, exp_got, [](auto v) { return exponential_in_float(v); });
    lane_agrees("tanh", x, tanh_got, [](auto v) { return hyperbolic_tangent_in_float(v); });
    lane_agrees("sinpi", x, sinpi_got, [](auto v) { return sine_of_half_turns_in_float(v); });
    const float sin_got = sine_in_float(x);
    const float cos_got = cosine_in_float(x);
    const float log_got = logarithm_in_float(x);
    const float magnitude = x < 0.0f ? -x : x;
    record(magnitude <= 8192.0f ? g_sin : g_far, x, sin_got, static_cast<float>(sine(wide, FnAccuracy::Exact)));
    record(magnitude <= 8192.0f ? g_cos : g_far, x, cos_got, static_cast<float>(cosine(wide, FnAccuracy::Exact)));
    record(g_log, x, log_got, static_cast<float>(natural_logarithm(wide, FnAccuracy::Exact)));
    lane_agrees("sin", x, sin_got, [](auto v) { return sine_in_float(v); });
    lane_agrees("cos", x, cos_got, [](auto v) { return cosine_in_float(v); });
    lane_agrees("log", x, log_got, [](auto v) { return logarithm_in_float(v); });
    // Past the claimed domain, only this: a number in [-1, 1] for every finite argument.
    const bool finite = x - x == 0.0f;
    if (finite && !(sin_got >= -1.0f && sin_got <= 1.0f && cos_got >= -1.0f && cos_got <= 1.0f) && ++g_failures <= 10) {
        std::printf("  sin(%a) is %a and cos %a: outside [-1, 1]\n", static_cast<double>(x), static_cast<double>(sin_got),
                    static_cast<double>(cos_got));
    }
}

// Every argument in one binade, both signs: the magnitudes are spread over [2^e, 2^(e+1)) so that a
// relative bound is tested where it is hardest rather than only near one.
//
// Along the way: neither function ever steps DOWN as its argument goes up (across the polynomial's
// switch point and the hold at ten too), a tangent stays within one, and it is odd to the bit.
void sweep_binade(int e, int points) {
    const float low = std::bit_cast<float>(static_cast<std::uint32_t>(e + 127) << 23);
    static float tanh_before = 0.0f;
    static float exp_before = 1.0f; // the binades are swept upwards
    for (int i = 0; i < points; ++i) {
        const float x = low * (1.0f + static_cast<float>(i) / static_cast<float>(points));
        check(x);
        check(-x);
        const float t = hyperbolic_tangent_in_float(x);
        const float e_x = exponential_in_float(x);
        const bool odd = std::bit_cast<std::uint32_t>(hyperbolic_tangent_in_float(-x)) == (std::bit_cast<std::uint32_t>(t) ^ 0x80000000u);
        if ((t < tanh_before || t > 1.0f || e_x < exp_before || !odd) && ++g_failures <= 10) {
            std::printf("  at %a: tanh %a after %a (odd: %d), exp %a after %a\n", static_cast<double>(x), static_cast<double>(t),
                        static_cast<double>(tanh_before), odd ? 1 : 0, static_cast<double>(e_x), static_cast<double>(exp_before));
        }
        tanh_before = t;
        exp_before = e_x;
    }
}

// The half-turn sine over one binade, both signs, against the double core rounded to float — and the
// two claims a bound in ulps cannot make. ODDNESS to the bit, `sinpi(-x)` being the sign bit of
// `sinpi(x)` flipped, which is what an oscillator divided by its own output needs. And EXACTNESS at
// the arguments where the answer is not an approximation at all: an integer is a zero carrying the
// argument's sign, a half-integer is exactly +-1.
void sweep_half_turns(int e, int points) {
    const float low = std::bit_cast<float>(static_cast<std::uint32_t>(e + 127) << 23);
    for (int i = 0; i < points; ++i) {
        const float x = low * (1.0f + static_cast<float>(i) / static_cast<float>(points));
        check(x);
        check(-x);
        const float got = sine_of_half_turns_in_float(x);
        const std::uint32_t mirrored = std::bit_cast<std::uint32_t>(sine_of_half_turns_in_float(-x));
        if (mirrored != (std::bit_cast<std::uint32_t>(got) ^ 0x80000000u) && ++g_failures <= 10) {
            std::printf("  sinpi is not odd at %a: %a against %a\n", static_cast<double>(x),
                        static_cast<double>(got), static_cast<double>(sine_of_half_turns_in_float(-x)));
        }
        // The nearest whole and half turn to this argument, where the answer is not an approximation
        // and the bound in ulps would not notice: the double core answers a signed zero and an exact
        // +-1 there, so the two are compared BIT FOR BIT rather than within anything. Past 2^23 a
        // half turn rounds back to a whole one, which is the case those binades are swept for.
        const float whole = round_to_nearest_even(x);
        for (const float exact : {whole, -whole, whole + 0.5f, -whole - 0.5f}) {
            const float answer = sine_of_half_turns_in_float(exact);
            const float owed = static_cast<float>(sine_of_half_turns(static_cast<double>(exact), FnAccuracy::Exact));
            if (std::bit_cast<std::uint32_t>(answer) != std::bit_cast<std::uint32_t>(owed) && ++g_failures <= 10) {
                std::printf("  sinpi(%a) is %a where the double core is exactly %a\n", static_cast<double>(exact),
                            static_cast<double>(answer), static_cast<double>(owed));
            }
        }
    }
}

// The radian sine and cosine over one binade, both signs: the sine odd and the cosine even, to the bit, and —
// past the claimed domain — the worst ABSOLUTE error of the binade printed, which is the only measure left
// once the reduction has no bits.
void sweep_sine(int e, int points) {
    const float low = std::ldexp(1.0f, e);
    double worst_sin = 0.0;
    double worst_cos = 0.0;
    for (int i = 0; i < points; ++i) {
        const float x = low * (1.0f + static_cast<float>(i) / static_cast<float>(points));
        check(x);
        check(-x);
        const float got = sine_in_float(x);
        const float cos_got = cosine_in_float(x);
        if (std::bit_cast<std::uint32_t>(sine_in_float(-x)) != (std::bit_cast<std::uint32_t>(got) ^ 0x80000000u) && ++g_failures <= 10) {
            std::printf("  sin is not odd at %a\n", static_cast<double>(x));
        }
        if (std::bit_cast<std::uint32_t>(cosine_in_float(-x)) != std::bit_cast<std::uint32_t>(cos_got) && ++g_failures <= 10) {
            std::printf("  cos is not even at %a\n", static_cast<double>(x));
        }
        const double sin_error = std::fabs(static_cast<double>(got) - sine(static_cast<double>(x), FnAccuracy::Exact));
        const double cos_error = std::fabs(static_cast<double>(cos_got) - cosine(static_cast<double>(x), FnAccuracy::Exact));
        worst_sin = sin_error > worst_sin ? sin_error : worst_sin;
        worst_cos = cos_error > worst_cos ? cos_error : worst_cos;
    }
    if (e >= 13 && e <= 30) {
        std::printf("  sin and cos, binade 2^%d: worst absolute error %.3g and %.3g\n", e, worst_sin, worst_cos);
    }
}

// The logarithm over one positive binade, subnormal ones included (`ldexp` reaches below 2^-126).
void sweep_logarithm(int e, int points) {
    const float low = std::ldexp(1.0f, e);
    for (int i = 0; i < points; ++i) {
        check(low * (1.0f + static_cast<float>(i) / static_cast<float>(points)));
    }
}

// A core evaluated while the program is compiled gives the bits it gives when the program runs — in the
// build with the fused multiply-add too, where `multiply_add_as_the_machine_rounds` is then the correctly
// rounded core (a review found 1-ulp differences in sin and exp when it was not).
// (Points where the fused and the unfused multiply-add give different bits, for every core: 0x1.095cacp+7
// for the sine, -3.7 for sinpi, then one each for the exponential, the hyperbolic tangent and the
// logarithm, and 0x1.fa4df0p-3 for the cosine; 0x1.925fdp+7 is the sine's worst argument.)
constexpr std::array<float, 10> kAgreeAt{0x1.095cacp+7f, 0x1.925fdp+7f, 0x1.f6d9eep-4f, 0x1.8dd65cp-7f, 0x1.1bb4cep-116f, -3.7f, 0.3f, 11.5f, 1e-3f,
                                         0x1.fa4df0p-3f};
constexpr std::size_t kCores = 6;
constexpr std::array<float, kCores * kAgreeAt.size()> kWhileCompiling = [] {
    std::array<float, kCores * kAgreeAt.size()> out{};
    for (std::size_t i = 0; i < kAgreeAt.size(); ++i) {
        const float x = kAgreeAt[i];
        out[kCores * i] = exponential_in_float(x);
        out[kCores * i + 1] = hyperbolic_tangent_in_float(x);
        out[kCores * i + 2] = sine_of_half_turns_in_float(x);
        out[kCores * i + 3] = sine_in_float(x);
        out[kCores * i + 4] = logarithm_in_float(x < 0.0f ? -x : x);
        out[kCores * i + 5] = cosine_in_float(x);
    }
    return out;
}();

void check_compile_time_agrees() {
    for (std::size_t i = 0; i < kAgreeAt.size(); ++i) {
        volatile float held = kAgreeAt[i];
        const float x = held;
        const float ran[kCores] = {exponential_in_float(x), hyperbolic_tangent_in_float(x), sine_of_half_turns_in_float(x), sine_in_float(x),
                                   logarithm_in_float(x < 0.0f ? -x : x), cosine_in_float(x)};
        for (std::size_t k = 0; k < kCores; ++k) {
            if (std::bit_cast<std::uint32_t>(ran[k]) != std::bit_cast<std::uint32_t>(kWhileCompiling[kCores * i + k]) && ++g_failures <= 10) {
                std::printf("  core %zu at %a: %a at run time, %a while compiling\n", k, static_cast<double>(x), static_cast<double>(ran[k]),
                            static_cast<double>(kWhileCompiling[kCores * i + k]));
            }
        }
    }
}

// The radian sine and cosine and the logarithm: their binades, the quarter turns where the two change
// polynomial and sign (and their float neighbours), and the specials.
void sweep_sine_and_logarithm() {
    for (int e = -30; e <= 127; ++e) {
        sweep_sine(e, e <= 13 ? 4096 : 1024);
    }
    for (int e = -149; e <= 127; ++e) {
        sweep_logarithm(e, 4096);
    }
    for (int k = -10430; k <= 10430; ++k) { // every eighth of a turn (pi/4) up to 8192
        const float quarter = static_cast<float>(k * 0.78539816339744830962);
        for (int step = -2; step <= 2; ++step) {
            check(std::bit_cast<float>(std::bit_cast<std::int32_t>(quarter) + step));
        }
    }
    for (const float x : {0.0f, -0.0f, 1.0f, 2.0f, 0.5f, 0.70710677f, 0.70710683f, 1.41421354f, 3.40282347e38f, -3.40282347e38f,
                          0x1.925fdp+7f, 0x1.d30b72p+1f, // the worst two sines an exhaustive sweep found (2 ulp)
                          0x1.3e5a6p+4f, 0x1.c17f6ep+7f, // and cosines, unfused and fused (2 ulp)
                          std::bit_cast<float>(std::uint32_t{1}), std::bit_cast<float>(std::uint32_t{0x80000001}),
                          std::bit_cast<float>(std::uint32_t{0x7f800000}), std::bit_cast<float>(std::uint32_t{0xff800000}),
                          std::bit_cast<float>(std::uint32_t{0x7fc00000})}) {
        check(x);
    }
    for (int e = -149; e <= 127; ++e) { // every power of two: log is e ln 2, within the bound
        check(std::ldexp(1.0f, e));
    }
    if (std::bit_cast<std::uint32_t>(sine_in_float(-0.0f)) != 0x80000000u || std::bit_cast<std::uint32_t>(logarithm_in_float(1.0f)) != 0u ||
        logarithm_in_float(0.0f) != -std::numeric_limits<float>::infinity() || logarithm_in_float(-0.0f) != -std::numeric_limits<float>::infinity() ||
        cosine_in_float(0.0f) != 1.0f || cosine_in_float(-0.0f) != 1.0f) {
        ++g_failures;
        std::printf("  sin(-0) must be -0, log(1) +0, log(+-0) -inf, cos(+-0) 1\n");
    }
}

} // namespace

int main() {
    // 2^-30 is already below every float's tanh resolution and 2^7 = 128 is past where an exponential
    // has a finite answer at all, so the two together cover everything either core is asked for.
    for (int e = -30; e <= 7; ++e) {
        sweep_binade(e, 4096);
    }
    // The arguments no binade reaches. The two exponential thresholds are read off the double core
    // rather than written down: `main`'s were constants, and a constant is what goes stale.
    for (const float x : {0.0f, -0.0f, 0.625f, -0.625f, 1.0f, -1.0f, 88.7228393f, 88.722839f, 88.72284f, 89.0f,
                          -87.3365479f, -87.336548f, -103.972084f, -103.97208f, -104.0f, -150.0f, 1e-38f, -1e-38f,
                          std::bit_cast<float>(std::uint32_t{1}), std::bit_cast<float>(std::uint32_t{0x800001}),
                          std::bit_cast<float>(std::uint32_t{0x7f800000}), std::bit_cast<float>(std::uint32_t{0xff800000}),
                          std::bit_cast<float>(std::uint32_t{0x7fc00000})}) {
        check(x);
    }
    // And the last representable float either side of every threshold the cores change behaviour at,
    // which is where an off-by-one-ulp boundary would hide.
    for (const float edge : {88.7228393f, -87.3365479f, -103.972084f, 0.625f, -0.625f, 88.8f, -104.0f,
                             0.25f, -0.25f, 0.5f, -0.5f, 1.0f, -1.0f}) {
        for (int step = -4; step <= 4; ++step) {
            const std::int32_t bits = std::bit_cast<std::int32_t>(edge) + step;
            check(std::bit_cast<float>(bits));
        }
    }

    // The half-turn sine's own range, which is every binade a float has. It is swept to 2^26 because
    // the interesting part is PAST 2^23: there every float is an integer and the answer is a signed
    // zero, which is the reduction's claim rather than the polynomial's, and the parity taken in
    // floats is what has to survive an argument no integer here would hold.
    for (int e = -40; e <= 26; ++e) {
        sweep_half_turns(e, 512);
    }
    // And the last float either side of 2^22 and 2^23, where what an argument IS changes: above 2^22
    // a float is a multiple of a half turn, above 2^23 a whole one.
    for (const float edge : {4194304.0f, 8388608.0f, 16777216.0f}) {
        for (int step = -4; step <= 4; ++step) {
            check(std::bit_cast<float>(std::bit_cast<std::int32_t>(edge) + step));
            check(-std::bit_cast<float>(std::bit_cast<std::int32_t>(edge) + step));
        }
    }

    sweep_sine_and_logarithm();
    check_compile_time_agrees();

    // TWO ulp, and each core is expected to come in under it — the numbers are printed either way, so
    // a change that costs accuracy without breaking the bound is still visible in the log.
    for (const Worst& worst : {g_exp, g_tanh, g_sinpi, g_sin, g_cos, g_log}) {
        std::printf("%s in float: worst %lld ulp at %a (got %a, the double core rounds to %a)\n", worst.what,
                    static_cast<long long>(worst.ulps), static_cast<double>(worst.at), static_cast<double>(worst.got),
                    static_cast<double>(worst.want));
        if (worst.ulps > 2) {
            ++g_failures;
        }
    }
    if (g_failures != 0) {
        std::printf("%d float-core checks failed\n", g_failures);
        return 1;
    }
    return 0;
}
