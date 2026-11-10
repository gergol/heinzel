// The proof obligation of `runtime/hardware_twins.hpp`: each twin answers, at run time, the bits its
// software core answers — over every binade boundary and its neighbours, every half-integer tie near
// zero and ties scattered up to 2^52, millions of random bit patterns (which reach every exponent, the
// subnormals, both infinities and NaNs of every kind), and for the fused multiply-add the inputs built
// to make an emulation fail: near-total cancellation, exact ties, products about to overflow or down
// among the subnormals. (The emulation this tree had before failed there: `docs/decisions.md`, 46.)
//
// Built twice more (tests/CMakeLists.txt): with the twins off, and with `-march=native`, because the
// fused multiply-add only has a twin where the target has the instruction. Its core is compared with
// the platform's `fma` in every build — the instruction, or libm's, which is correctly rounded too.

#include "heinzel/runtime/math_cores.hpp"

#include <bit>
#include <cstdint>
#include <cstdio>

using namespace heinzel;

namespace {

int g_failures = 0;

// The same bits, or both a NaN: which NaN is fixed by nothing (`runtime/hardware_twins.hpp`).
bool same_bits(double a, double b) {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b) || (a != a && b != b);
}

void report(const char* what, double a, double b, double c, double twin, double core) {
    if (++g_failures <= 20) {
        std::printf("  %s(%a, %a, %a): twin %a (%016llx), core %a (%016llx)\n", what, a, b, c, twin,
                    static_cast<unsigned long long>(std::bit_cast<std::uint64_t>(twin)), core,
                    static_cast<unsigned long long>(std::bit_cast<std::uint64_t>(core)));
    }
}

void check_unary(double x) {
    struct { const char* name; double (*twin)(double); double (*core)(double); } const ops[] = {
        {"square_root", square_root, square_root_core},
        {"truncate_toward_zero", truncate_toward_zero, truncate_toward_zero_core},
        {"round_toward_negative", round_toward_negative, round_toward_negative_core},
        {"round_toward_positive", round_toward_positive, round_toward_positive_core},
        {"round_to_nearest_even", round_to_nearest_even, round_to_nearest_even_core},
    };
    for (const auto& op : ops) {
        const double twin = op.twin(x);
        const double core = op.core(x);
        if (!same_bits(twin, core)) {
            report(op.name, x, 0.0, 0.0, twin, core);
        }
    }
}

void check_fma(double a, double b, double c) {
    const double core = multiply_add_rounding_once_core(a, b, c);
    if (!same_bits(multiply_add_rounding_once(a, b, c), core)) {
        report("multiply_add_rounding_once", a, b, c, multiply_add_rounding_once(a, b, c), core);
    }
    if (!same_bits(__builtin_fma(a, b, c), core)) {
        report("the platform's fma", a, b, c, __builtin_fma(a, b, c), core);
    }
}

// splitmix64: every bit pattern is reachable and the sequence is the same on every host.
std::uint64_t next(std::uint64_t& state) {
    std::uint64_t z = (state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

double from_bits(std::uint64_t bits) { return std::bit_cast<double>(bits); }

// A double of moderate exponent, so a product of two neither overflows nor underflows.
double moderate(std::uint64_t& state) {
    const std::uint64_t bits = next(state);
    const std::uint64_t exponent = 1023 - 40 + (bits >> 52) % 80;
    return from_bits((bits & 0x800fffffffffffffull) | (exponent << 52));
}

void sweep_unary() {
    // Every binade boundary, both signs, with the pattern on either side of it — which takes in the
    // zeros, the subnormal/normal seam, the largest finite value and the infinities.
    for (std::uint64_t exponent = 0; exponent <= 0x7ff; ++exponent) {
        for (std::uint64_t sign = 0; sign <= 1; ++sign) {
            const std::uint64_t boundary = (sign << 63) | (exponent << 52);
            for (std::int64_t step = -2; step <= 2; ++step) {
                check_unary(from_bits(boundary + static_cast<std::uint64_t>(step)));
            }
        }
    }
    // The rounding operations live between -2^53 and 2^53: every half-integer tie and its neighbours
    // near zero, and ties scattered up to where doubles stop having a fraction.
    for (int k = -4096; k <= 4096; ++k) {
        const double tie = 0.5 * k;
        for (std::int64_t step = -1; step <= 1; ++step) {
            check_unary(from_bits(std::bit_cast<std::uint64_t>(tie) + static_cast<std::uint64_t>(step)));
        }
    }
    std::uint64_t state = 1;
    for (int i = 0; i < (1 << 22); ++i) {
        check_unary(from_bits(next(state)));
    }
    for (int i = 0; i < (1 << 20); ++i) {
        const double whole = static_cast<double>(static_cast<std::int64_t>(next(state) >> 12)); // below 2^52
        check_unary(whole + 0.5);
        check_unary(-whole - 0.5);
    }
}

// The float multiply-add (what a contracted float product-sum computes): core against the platform's.
void sweep_fma_float() {
    std::uint64_t state = 3;
    const auto some_float = [&] { return std::bit_cast<float>(static_cast<std::uint32_t>(next(state))); };
    for (int i = 0; i < (1 << 22); ++i) {
        float a = some_float();
        float b = some_float();
        float c = some_float();
        if (i % 3 == 0) { // near-total cancellation, and ties a few float ulps around it
            c = std::bit_cast<float>(std::bit_cast<std::uint32_t>(-(a * b)) + static_cast<std::uint32_t>(next(state) % 9) - 4u);
        } else if (i % 3 == 1) {
            c = (a * b) * 0x1p-24f;
        }
        const float core = multiply_add_rounding_once_core(a, b, c);
        const float platform = __builtin_fmaf(a, b, c);
        const bool same = std::bit_cast<std::uint32_t>(core) == std::bit_cast<std::uint32_t>(platform) || (core != core && platform != platform);
        if (!same || !same_bits(multiply_add_rounding_once(a, b, c), core)) {
            report("float multiply_add_rounding_once", a, b, c, platform, core);
        }
    }
}

void sweep_fma_edges(std::uint64_t& state) {
    // Products about to overflow (one of them past the largest double, with a `c` that brings the sum
    // back), and down among the subnormals, where a rounding position moves.
    for (int i = 0; i < (1 << 19); ++i) {
        const double a = moderate(state);
        const double b = moderate(state);
        check_fma(a * 0x1p500, b * 0x1p500, moderate(state) * 0x1p1000);
        check_fma(a * 0x1p500, b * 0x1p523, -0x1.fffffffffffffp1023);
        check_fma(a * 0x1p-500, b * 0x1p-500, moderate(state) * 0x1p-1000);
        check_fma(a * 0x1p-520, b * 0x1p-520, moderate(state) * 0x1p-1060);
        check_fma(a * 0x1p-500, b * 0x1p-570, from_bits(next(state) % 4096));
    }
}

void sweep_fma() {
    const double specials[] = {0.0, -0.0, 1.0, -1.0, 0x1p-1074, -0x1p-1074, 0x1p-1022, 0x1.fffffffffffffp1023,
                               -0x1.fffffffffffffp1023, kPositiveInfinity, -kPositiveInfinity, kQuietNaN,
                               0x1p600, 0x1p-600, 3.0, 0x1.0000000000001p0};
    for (double a : specials) {
        for (double b : specials) {
            for (double c : specials) {
                check_fma(a, b, c);
            }
        }
    }
    std::uint64_t state = 2;
    for (int i = 0; i < (1 << 21); ++i) {
        check_fma(from_bits(next(state)), from_bits(next(state)), from_bits(next(state)));
    }
    for (int i = 0; i < (1 << 21); ++i) {
        const double a = moderate(state);
        const double b = moderate(state);
        check_fma(a, b, moderate(state));
        // Near-total cancellation: c within a few ulps of -(a*b), where an emulation that rounded the
        // product early is visibly wrong.
        check_fma(a, b, from_bits(std::bit_cast<std::uint64_t>(-(a * b)) + (next(state) % 9) - 4));
        // Ties: c far below the product, landing on or beside the rounding boundary.
        check_fma(a, b, (a * b) * 0x1p-53);
        check_fma(a, b, -(a * b) * 0x1p-54);
    }
    sweep_fma_edges(state);
    sweep_fma_float();
}

} // namespace

// Under constant evaluation a twin IS its core — shown, so the branch cannot silently invert.
static_assert(square_root(2.0) == square_root_core(2.0) && round_to_nearest_even(2.5) == 2.0,
              "constant evaluation runs the cores");
// The two inputs the emulation rounded twice, and the two it answered NaN for.
static_assert(multiply_add_rounding_once(0x1.fffffffffffffp1023, -2.0, kPositiveInfinity) == kPositiveInfinity &&
                  multiply_add_rounding_once(0x1.c1581fc011949p+511, 0x1.78b7936ec695dp+511, 0x1.6ac4589729371p+1022) ==
                      0x1.fffffffffffffp+1023 &&
                  multiply_add_rounding_once(0x1.0000000000001p0, 0x1.0000000000001p0, 3.0) == 0x1.0000000000001p2 &&
                  multiply_add_rounding_once(3.0, 0x1.0000000000001p0, -0x1p-1074) == 0x1.8000000000001p1 &&
                  multiply_add_rounding_once(0.0, 0.0, kPositiveInfinity) == kPositiveInfinity &&
                  multiply_add_rounding_once(1.0, 0x1.fffffffffffffp1023, 0x1.fffffffffffffp1023) == kPositiveInfinity,
              "the fused multiply-add is correctly rounded");

int main() {
    sweep_unary();
    sweep_fma();
#if HEINZEL_HARDWARE_TWINS
    std::printf("hardware twins: sqrt%s%s\n", HEINZEL_ROUNDING_TWIN ? ", trunc, floor, ceil, roundeven" : "",
                HEINZEL_FMA_TWIN ? ", fma" : "");
#else
    std::printf("hardware twins: off\n");
#endif
    if (g_failures != 0) {
        std::printf("%d disagreements between a twin and its core\n", g_failures);
        return 1;
    }
    return 0;
}
