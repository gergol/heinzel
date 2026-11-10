// `Lane<T, W>` moves no number: lane k of every operation is, bit for bit (or NaN for NaN), the scalar
// operation on lane k of the operands — for float and double, at widths 2, 4 and 8, over random bit patterns (which
// reach NaNs of every kind, both infinities, the subnormals) and a table of the values where a lane
// form is most likely to part from the scalar one: the zeros' signs, NaN in either operand of min and
// max, ties for the roundings, negatives for the root.

#include "heinzel/runtime/lane_value.hpp"
#include "heinzel/runtime/math_cores.hpp" // the scalar operations a lane is held to

#include <bit>
#include <cstdint>
#include <cstdio>

using namespace heinzel;

namespace {

int g_failures = 0;

// The same bits, or both a NaN: which NaN an operation answers is fixed by nothing — not by IEEE 754,
// and not between two compilations of one scalar expression (`runtime/hardware_twins.hpp`).
template <class T>
bool same(T a, T b) {
    using Bits = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
    return std::bit_cast<Bits>(a) == std::bit_cast<Bits>(b) || (a != a && b != b);
}

std::uint64_t next(std::uint64_t& state) { // splitmix64
    std::uint64_t z = (state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

template <class T>
T pattern(std::uint64_t& state, int round) {
    constexpr T table[] = {T(0.0),  T(-0.0), T(1.0),  T(-1.0),  T(0.5),  T(-0.5), T(1.5),    T(2.5),
                           T(-2.5), T(4.0),  T(-4.0), T(1e-40), T(3e38), T(7.25), T(-7.75),  T(9.0)};
    if (round % 4 == 0) {
        return table[next(state) % 16];
    }
    using Bits = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
    return std::bit_cast<T>(static_cast<Bits>(next(state)));
}

// The scalar definitions, spelled as `runtime/compute.hpp` spells them.
template <class T> T scalar_min(T a, T b) { return a < b ? a : b; }
template <class T> T scalar_max(T a, T b) { return a > b ? a : b; }

template <class T, std::size_t W>
void expect(const char* what, Lane<T, W> got, const T* want) {
    for (std::size_t k = 0; k < W; ++k) {
        if (!same(got[k], want[k]) && ++g_failures <= 20) {
            std::printf("  %s<%zu-byte, %zu> lane %zu: got %a, the scalar answers %a\n", what, sizeof(T), W, k,
                        static_cast<double>(got[k]), static_cast<double>(want[k]));
        }
    }
}

template <class T, std::size_t W>
void sweep(std::uint64_t seed) {
    using L = Lane<T, W>;
    std::uint64_t state = seed;
    for (int round = 0; round < 200000; ++round) {
        T a[W];
        T b[W];
        T c[W];
        T want[W];
        for (std::size_t k = 0; k < W; ++k) {
            a[k] = pattern<T>(state, round);
            b[k] = pattern<T>(state, round + 1);
            c[k] = pattern<T>(state, round + 2);
        }
        const L la = L::load(a);
        const L lb = L::load(b);
        const L lc = L::load(c);
        const auto each_lane = [&](auto scalar) {
            for (std::size_t k = 0; k < W; ++k) {
                want[k] = static_cast<T>(scalar(k));
            }
            return want;
        };
        expect("add", la + lb, each_lane([&](std::size_t k) { return a[k] + b[k]; }));
        expect("subtract", la - lb, each_lane([&](std::size_t k) { return a[k] - b[k]; }));
        expect("multiply", la * lb, each_lane([&](std::size_t k) { return a[k] * b[k]; }));
        expect("divide", la / lb, each_lane([&](std::size_t k) { return a[k] / b[k]; }));
        expect("negate", -la, each_lane([&](std::size_t k) { return -a[k]; }));
        expect("min", min(la, lb), each_lane([&](std::size_t k) { return scalar_min(a[k], b[k]); }));
        expect("max", max(la, lb), each_lane([&](std::size_t k) { return scalar_max(a[k], b[k]); }));
        expect("select <", select(la < lb, la, lc), each_lane([&](std::size_t k) { return a[k] < b[k] ? a[k] : c[k]; }));
        expect("select <=", select(la <= lb, la, lc), each_lane([&](std::size_t k) { return a[k] <= b[k] ? a[k] : c[k]; }));
        expect("select ==", select(la == lb, la, lc), each_lane([&](std::size_t k) { return a[k] == b[k] ? a[k] : c[k]; }));
        expect("select !=", select(la != la, lb, lc), each_lane([&](std::size_t k) { return a[k] != a[k] ? b[k] : c[k]; }));
        expect("select & !", select((la > lb) & !(lb >= lc), la, lc),
               each_lane([&](std::size_t k) { return (a[k] > b[k]) && !(b[k] >= c[k]) ? a[k] : c[k]; }));
        expect("splat", L::splat(a[0]), each_lane([&](std::size_t) { return a[0]; }));
        expect("square_root", square_root(la), each_lane([&](std::size_t k) { return square_root(static_cast<double>(a[k])); }));
        expect("truncate", truncate_toward_zero(la), each_lane([&](std::size_t k) { return truncate_toward_zero(static_cast<double>(a[k])); }));
        expect("floor", round_toward_negative(la), each_lane([&](std::size_t k) { return round_toward_negative(static_cast<double>(a[k])); }));
        expect("ceil", round_toward_positive(la), each_lane([&](std::size_t k) { return round_toward_positive(static_cast<double>(a[k])); }));
        expect("round", round_to_nearest_even(la), each_lane([&](std::size_t k) { return round_to_nearest_even(static_cast<double>(a[k])); }));
    }
}

// The one writing of each operation (`compute_as`), at a lane against at a plain number of its type.
template <class T, std::size_t W>
void sweep_compute_as(std::uint64_t seed) {
    using L = Lane<T, W>;
    constexpr OpCode ops[] = {op::Add,  op::Sub,   op::Mul,   op::Div,   op::Neg, op::Min, op::Max, op::Sign, op::Sqrt,
                              op::Rsqrt, op::Floor, op::Ceil, op::Trunc, op::Round, op::Lt, op::Le, op::Eq, op::Select, op::Abs};
    std::uint64_t state = seed;
    for (int round = 0; round < 50000; ++round) {
        T a[W];
        T b[W];
        T c[W];
        T want[W];
        for (std::size_t k = 0; k < W; ++k) {
            a[k] = pattern<T>(state, round);
            b[k] = pattern<T>(state, round + 1);
            c[k] = pattern<T>(state, round + 2);
        }
        for (const OpCode op : ops) {
            for (std::size_t k = 0; k < W; ++k) {
                want[k] = compute_as<T>(op, FnAccuracy::Exact, a[k], b[k], c[k]);
            }
            expect("compute_as", compute_as<L>(op, FnAccuracy::Exact, L::load(a), L::load(b), L::load(c)), want);
        }
    }
}

// Float lanes only: writing an exponent into the bits, which is how a reduced exponential puts its
// scaling back (`runtime/float_cores.hpp`). Held to the plain float, over every whole exponent the
// float cores can reach — the two ends included, where the halving is what keeps the answer right.
template <std::size_t W>
void sweep_exponent_insertion(std::uint64_t seed) {
    using L = Lane<float, W>;
    std::uint64_t state = seed;
    for (int round = 0; round < 20000; ++round) {
        float x[W];
        float whole[W];
        float want[W];
        for (std::size_t k = 0; k < W; ++k) {
            x[k] = 0.5f + static_cast<float>(next(state) % 2000) / 1000.0f; // a significand's worth
            whole[k] = static_cast<float>(static_cast<int>(next(state) % 279) - 150);
            want[k] = scaled_by_two_to_the(x[k], whole[k]);
        }
        expect("scaled_by_two_to_the", scaled_by_two_to_the(L::load(x), L::load(whole)), want);
    }
}

// Double lanes only: that is the `_fma` operation. A float lane's is a different number by design.
template <std::size_t W>
void sweep_multiply_add(std::uint64_t seed) {
    using L = Lane<double, W>;
    std::uint64_t state = seed;
    for (int round = 0; round < 200000; ++round) {
        double a[W];
        double b[W];
        double c[W];
        double want[W];
        for (std::size_t k = 0; k < W; ++k) {
            a[k] = pattern<double>(state, round);
            b[k] = pattern<double>(state, round + 1);
            c[k] = round % 3 == 0 ? -(a[k] * b[k]) : pattern<double>(state, round + 2);
            want[k] = multiply_add_rounding_once(a[k], b[k], c[k]);
        }
        expect("multiply_add", multiply_add_rounding_once(L::load(a), L::load(b), L::load(c)), want);
    }
}

} // namespace

int main() {
    sweep<float, 2>(1), sweep<float, 4>(2), sweep<float, 8>(3);
    sweep<double, 2>(4), sweep<double, 4>(5), sweep<double, 8>(6);
    sweep_multiply_add<2>(7), sweep_multiply_add<4>(8);
    sweep_exponent_insertion<4>(12), sweep_exponent_insertion<8>(13);
    sweep_compute_as<float, 8>(9), sweep_compute_as<double, 4>(10), sweep_compute_as<float, 4>(11);

    // A store and a load one element off a vector's alignment, which is what a tile edge is (an element
    // is always aligned to its own type: a pointer to a double that is not is undefined behaviour before
    // anything is stored through it, which the sanitizer build reports).
    alignas(64) double storage[8]{};
    Lane<double, 4>::splat(1.5).store(storage + 1);
    const Lane<double, 4> back = Lane<double, 4>::load(storage + 1);
    if (storage[0] != 0.0 || storage[5] != 0.0 || back[0] != 1.5 || back[3] != 1.5) {
        ++g_failures;
        std::printf("  a store and load off a vector's alignment\n");
    }
    if (g_failures != 0) {
        std::printf("%d lanes disagree with the scalar operation\n", g_failures);
        return 1;
    }
    return 0;
}
