// `log10` and `cospi`, composed (Plan/28-dsl-proposals/02 A6). Each builds the graph of the operations it names, graph
// for graph: `log10(x)` is `log(x) * log10e`, `cospi(x)` is `sinpi(1/2 - r)` with `r = a - 2 floor(a/2)`, `a = |x|`.
// Then each runs on `Executor` at Normal and High over a few thousand floats, against long double, held to its bound.
//
// The bounds, from sweeps over every float (F32 ports, the default machine, 2026-10-16), with a little headroom:
//   log10, every positive float: Normal 2.03 ulp at 0.350355953 (40 floats over 2), High 1.37 ulp at 6.04565479e-29.
//   cospi, every float: Normal 1.70 ulp at 0.249258772, High 1.06 ulp at 0.249794945.
// High cannot reach 1 ulp for a composed log10: the log rounded to f32 first is up to 0.87 ulp of the result (where
// log10 has the finer exponent), the f32 constant 0.34, the product 0.5. cospi's worst lies just below a quarter turn,
// where 1/2 - r rounds; reducing x rather than |x| answered -0 at -0.49999997 (1.3e7 ulp: x + 2k rounds below 0).
// The points here hold each worst above, so a run finds the sweeps' figures. cospi is also exact at the integers and
// the halves. An operand of log10 the proof cannot keep above 0 is refused as log's is: the
// build_refuses_proof_a_log10_of_zero fixture.
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>

#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

template <bool Spelled>
constexpr Graph log10_built() {
    Input x{"x", Signal{8}, gt(0)};
    Output y{"y"};
    if constexpr (Spelled) {
        y = log10(x);
    } else {
        Expr ln = apply(op::Log, x);
        y = apply(op::Mul, ln, constant(std::numbers::log10e));
    }
    return y;
}
static_assert(log10_built<true>() == log10_built<false>());

template <bool Spelled>
constexpr Graph cospi_built() {
    Input x{"x", Signal{8}};
    Output y{"y"};
    if constexpr (Spelled) {
        y = cospi(x);
    } else {
        Expr a = abs(x);
        Expr half = apply(op::Mul, a, constant(0.5));
        Expr whole = apply(op::Floor, half);
        Expr twice = apply(op::Mul, constant(2.0), whole);
        Expr r = apply(op::Sub, a, twice);
        Expr turned = apply(op::Sub, constant(0.5), r);
        y = apply(op::SinPi, turned);
    }
    return y;
}
static_assert(cospi_built<true>() == cospi_built<false>());

constexpr std::size_t kPoints = 4096;
using Points = std::array<float, kPoints>;

template <Precision Level>
constexpr Graph log10_at() {
    Input x{"x", Signal{kPoints}, gt(0)};
    Output y{"y"};
    y = log10(x);
    return finish(Level, y);
}

template <Precision Level>
constexpr Graph cospi_at() {
    Input x{"x", Signal{kPoints}};
    Output y{"y"};
    y = cospi(x);
    return finish(Level, y);
}

long double log10_wanted(float x) { return std::log10(static_cast<long double>(x)); }

// cos(pi x) by an exact reduction, 0 exactly at the halves: t = 1/2 - (|x| mod 2) is exact in long double.
long double cospi_wanted(float x) {
    const long double r = std::fmod(std::fabs(static_cast<long double>(x)), 2.0L);
    const long double t = 0.5L - r;
    const long double pi = std::numbers::pi_v<long double>;
    return t < -0.5L ? -std::sin(pi * (t + 1.0L)) : std::sin(pi * t);
}

// A float's ulp at the wanted value's magnitude, the subnormals' below 2^-126. The exponent is the long double's: one
// rounded to float first would measure a value just below a power of two in the next binade's ulp, half size.
double ulp_at(long double wanted) {
    if (wanted == 0.0L) {
        return std::numeric_limits<float>::denorm_min();
    }
    int exponent = 0;
    std::frexp(std::fabs(wanted), &exponent);
    return std::ldexp(1.0, std::max(exponent - 24, -149));
}

// splitmix64: the same points on every machine.
std::uint64_t next(std::uint64_t& state) {
    state += 0x9e3779b97f4a7c15ULL;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

// Powers of ten, the floats either side of 1, the sweeps' worst, a band where log10 has the finer exponent
// (log x in [64, 73.7]), then positive floats of every exponent, subnormals included.
Points log10_points() {
    Points x{};
    std::size_t i = 0;
    for (int k = -45; k <= 38; ++k) {
        x[i++] = static_cast<float>(std::pow(10.0L, k));
    }
    float up = 1.0f;
    float down = 1.0f;
    x[i++] = 1.0f;
    for (int k = 0; k < 64; ++k) {
        up = std::nextafter(up, 2.0f);
        down = std::nextafter(down, 0.0f);
        x[i++] = up;
        x[i++] = down;
    }
    x[i++] = 0x1.66c3b6p-2f;  // Normal's worst
    x[i++] = 0x1.328d16p-94f; // High's worst
    x[i++] = std::numeric_limits<float>::denorm_min();
    x[i++] = std::numeric_limits<float>::max();
    std::uint64_t state = 1;
    for (int k = 0; k < 1000; ++k) {
        const double u = static_cast<double>(next(state) >> 11) * 0x1p-53;
        x[i++] = static_cast<float>(std::exp(64.0 + 9.7 * u));
    }
    while (i < kPoints) {
        x[i++] = std::bit_cast<float>(static_cast<std::uint32_t>(next(state) % 0x7f7fffffU) + 1U);
    }
    return x;
}

// The integers and halves to 16, a sweep of [-4, 4] in steps no power of two divides, the floats beside each half
// from -4.5 to 4.5, the sweeps' worst and their negatives, big floats (every one an integer, past 2^24 an even one),
// then floats of magnitudes 2^-25 to 2^25, either sign.
Points cospi_points() {
    Points x{};
    std::size_t i = 0;
    for (int k = -32; k <= 32; ++k) {
        x[i++] = static_cast<float>(k) * 0.5f;
    }
    for (int k = 0; k <= 2000; ++k) {
        x[i++] = -4.0f + 8.0f * static_cast<float>(k) / 2000.0f;
    }
    for (int k = -5; k <= 4; ++k) {
        float below = static_cast<float>(k) + 0.5f;
        float above = below;
        for (int m = 0; m < 8; ++m) {
            below = std::nextafter(below, -8.0f);
            above = std::nextafter(above, 8.0f);
            x[i++] = below;
            x[i++] = above;
        }
    }
    constexpr std::array<float, 2> kWorst{0x1.fe7b62p-3f, 0x1.ff947ep-3f}; // Normal's, High's
    for (float worst : kWorst) {
        x[i++] = worst;
        x[i++] = -worst;
    }
    constexpr std::array<float, 6> kBig{0x1p23f, 0x1p24f, 0x1p30f, 1e9f, 3e38f, 0x1p22f + 0.5f};
    for (float big : kBig) {
        for (int k = -4; k <= 4; ++k) {
            x[i++] = big + static_cast<float>(k);
            x[i++] = -(big + static_cast<float>(k));
        }
    }
    std::uint64_t state = 2;
    while (i < kPoints) {
        const int exponent = static_cast<int>(next(state) % 51) - 25;
        const double fraction = static_cast<double>(next(state) >> 11) * 0x1p-53;
        const double sign = (next(state) & 1U) != 0 ? -1.0 : 1.0;
        x[i++] = static_cast<float>(sign * std::ldexp(1.0 + fraction, exponent));
    }
    return x;
}

bool whole_or_half(float x) { return std::floor(2.0f * x) == 2.0f * x; }

// The worst error in ulp over the points, and how many integers and halves were not answered exactly.
struct Worst {
    double ulp{0.0};
    float at{0.0f};
    int inexact{0};
};

template <auto Build>
Worst measured(const Points& x, long double (*wanted)(float), bool exact_at_halves) {
    Points y{};
    Executor<Build> ex;
    testing::held(ex);
    ex.input("x") = x.data();
    ex.output("y") = y.data();
    ex.run();
    Worst worst;
    for (std::size_t i = 0; i < kPoints; ++i) {
        const long double want = wanted(x[i]);
        const double off = static_cast<double>(std::fabs(static_cast<long double>(y[i]) - want)) / ulp_at(want);
        const double ulp = off == off ? off : std::numeric_limits<double>::infinity();
        if (ulp > worst.ulp) {
            worst.ulp = ulp;
            worst.at = x[i];
        }
        if (exact_at_halves && whole_or_half(x[i]) && static_cast<long double>(y[i]) != want) {
            ++worst.inexact;
        }
    }
    return worst;
}

// One function at one level within its bound, every integer and half exact where asked.
template <auto Build>
bool holds(const char* name, const Points& x, long double (*wanted)(float), double bound, bool exact_at_halves) {
    const Worst worst = measured<Build>(x, wanted, exact_at_halves);
    std::printf("log10_cospi: %s at most %.3f ulp (at %.9g), bound %.2f; %d integers or halves not exact\n", name,
                worst.ulp, static_cast<double>(worst.at), bound, worst.inexact);
    return worst.ulp <= bound && worst.inexact == 0;
}

} // namespace

int main() {
    const Points logs = log10_points();
    const Points turns = cospi_points();
    bool ok = holds<log10_at<Normal>>("log10 at Normal", logs, log10_wanted, 2.25, false);
    ok = holds<log10_at<High>>("log10 at High", logs, log10_wanted, 1.5, false) && ok;
    ok = holds<cospi_at<Normal>>("cospi at Normal", turns, cospi_wanted, 2.0, true) && ok;
    ok = holds<cospi_at<High>>("cospi at High", turns, cospi_wanted, 1.25, true) && ok;
    if (!ok) {
        return 1;
    }
    std::printf("log10_cospi: both spell the graphs they compose and hold their bounds at Normal and High\n");
    return 0;
}
