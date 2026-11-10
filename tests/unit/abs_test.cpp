// `abs` as an operation (Plan/29 step 1): `abs(x)` is one `_abs` node, the sign bit cleared, where it was `max(x, -x)`
// (two nodes, -0 for +0, and on the M1 a compare and a blend where `fabs` is one instruction). Over ±0, ±denormals, the
// least normals, ±infinity, NaNs of either sign and finite numbers, in f32 and f64: on Executor at High bit for bit the
// reference walk's numbers (`executed`), and at Normal and on the walk bit for bit `std::fabs`, a NaN included (no
// arithmetic touches it, only its sign bit). abs(+0) and abs(-0) are +0.
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

constexpr std::size_t kPoints = 32;

template <class T>
constexpr ElemType kTypeOf = sizeof(T) == 4 ? ElemType::F32 : ElemType::F64;

template <class T, Precision Level>
constexpr Graph abs_at() {
    Input x{"x", Signal{kPoints}, kTypeOf<T>};
    Output y{"y"};
    y = abs(x);
    return finish(Level, y);
}

// One node, and none of the old composition's.
constexpr bool one_node() {
    const Graph g = abs_at<float, Normal>();
    int abs_nodes = 0;
    for (const Node& n : g.nodes) {
        abs_nodes += n.op == op::Abs ? 1 : 0;
        if (n.op == op::Max || n.op == op::Neg) {
            return false;
        }
    }
    return abs_nodes == 1 && g.nodes.size() == 1;
}
static_assert(one_node(), "abs(x) is one _abs node");

template <class T>
std::array<T, kPoints> points() {
    using L = std::numeric_limits<T>;
    const T largest_denormal = L::min() - L::denorm_min();
    const std::array<T, 22> special{T(0),          -T(0),          L::denorm_min(), -L::denorm_min(), largest_denormal,
                                    -largest_denormal, L::min(),    -L::min(),       T(1),             T(-1),
                                    T(1.5),        T(-2.75),       T(0.1),          T(-1e-30),        L::max(),
                                    -L::max(),     L::infinity(),  -L::infinity(),  L::quiet_NaN(),   -L::quiet_NaN(),
                                    T(3e5),        T(-7.25e-3)};
    std::array<T, kPoints> x{};
    for (std::size_t i = 0; i < kPoints; ++i) {
        x[i] = i < special.size() ? special[i] : T(-0.37) * static_cast<T>(i) + T(4);
    }
    return x;
}

template <class T>
bool same_bits(T a, T b) {
    return std::bit_cast<std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>(a) ==
           std::bit_cast<std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>(b);
}

// How many numbers `ran` gives other than `std::fabs` of the points, in any bit; each named.
template <class T, class Numbers>
int off_fabs(const char* where, const std::array<T, kPoints>& x, const Numbers& ran) {
    int wrong = 0;
    for (std::size_t i = 0; i < kPoints; ++i) {
        const T got = static_cast<T>(ran[i]);
        if (!same_bits(got, std::fabs(x[i]))) {
            std::printf("  abs %s at %g: %g, not %g\n", where, static_cast<double>(x[i]), static_cast<double>(got),
                        static_cast<double>(std::fabs(x[i])));
            ++wrong;
        }
    }
    return wrong;
}

template <class T>
int checked() {
    const std::array<T, kPoints> x = points<T>();
    const std::initializer_list<testing::NamedFeed> fed{{"x", x.data()}};
    // At High, Executor against the walk bit for bit (it exits naming the first number apart), and the walk's against
    // fabs: the walk is the oracle, and fabs the oracle's oracle.
    const testing::Outputs high = testing::executed<abs_at<T, High>>(fed);
    int wrong = off_fabs("at High", x, high[0]);
    std::array<T, kPoints> y{};
    Executor<abs_at<T, Normal>> ex;
    testing::held(ex);
    ex.input("x") = x.data();
    ex.output("y") = y.data();
    ex.run();
    wrong += off_fabs("at Normal", x, y);
    return wrong;
}

} // namespace

int main() {
    const int wrong = checked<float>() + checked<double>();
    if (wrong != 0) {
        std::printf("abs_test: %d numbers apart\n", wrong);
        return 1;
    }
    std::printf("abs_test: abs is one _abs node, fabs to the bit at High and Normal, in f32 and f64\n");
    return 0;
}
