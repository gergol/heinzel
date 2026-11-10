// `cos`, `tan`, `exp2` and `pow` (Plan/28-dsl-proposals/05 D2). Each spelling builds the graph of its operation, graph
// for graph. `tan`, `exp2` and `pow` have no float core yet, so the loop tree runs their double core one number at a
// time, which is what the walk computes: on `Executor` at High, Normal and Fast, over F32 and F64, on the default
// machine and on zen2, every number equals the walk's, bit for bit (both a NaN counts as equal). So does `cos` at High
// and over F64; an F32 `cos` at Normal and Fast runs its float core (step 2), as `sin` does, which the walk, computing
// in double, does not: there it is held to 2 ulp of the walk at Normal where the core claims it, |x| <= 8192, and past
// it to a number in [-1, 1] (`float_cores_test`); at Fast too, as the float core does not read the tier. And `sin`'s gradient, which emits `cos`, runs on `Executor`: at High as the
// walk runs it, against central differences and the closed form. A `pow` of a base the proof cannot keep at or above 0
// is the build_refuses_proof_a_pow_of_a_negative_base fixture.
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <type_traits>
#include <vector>

#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/gradients.hpp"
#include "heinzel/sema/analyze.hpp"

#include "executed.hpp"
#include "interpret.hpp"

using namespace heinzel;

namespace {

// ---- the spellings build their operations ---------------------------------------------------------------------------

template <bool Spelled>
constexpr Graph one_operand_built() {
    Input x{"x", Signal{8}, gt(-1.5), lt(1.5)};
    Output c{"c"};
    Output t{"t"};
    Output e{"e"};
    if constexpr (Spelled) {
        c = cos(x);
        t = tan(x);
        e = exp2(x);
    } else {
        c = apply(op::Cos, x);
        t = apply(op::Tan, x);
        e = apply(op::Exp2, x);
    }
    return finish(c, t, e);
}
static_assert(one_operand_built<true>() == one_operand_built<false>());

template <bool Spelled>
constexpr Graph powers_built() {
    Input x{"x", Signal{8}, gt(0)};
    Input y{"y", Signal{8}};
    Output both{"both"};
    Output squared{"squared"};
    Output tens{"tens"};
    if constexpr (Spelled) {
        both = pow(x, y);
        squared = pow(x, 2.0);
        tens = pow(10.0, y);
    } else {
        both = apply(op::Pow, x, y);
        Expr two = constant(2.0);
        squared = apply(op::Pow, x, two);
        Expr ten = constant(10.0);
        tens = apply(op::Pow, ten, y);
    }
    return finish(both, squared, tens);
}
static_assert(powers_built<true>() == powers_built<false>());

// ---- each on Executor, as the walk computes it ----------------------------------------------------------------------

constexpr std::size_t kPoints = 4096;

enum class Fn { Cos, Tan, Exp2, Pow };

// The function's graph at a level, its ports `x` (and `pow`'s exponent `e`) of type T: `tan`'s operand inside one
// branch, `pow`'s base above 0, as the proof of ranges asks.
template <Fn F, ElemType T, Precision Level>
constexpr Graph one_at() {
    if constexpr (F == Fn::Tan) {
        Input x{"x", Signal{kPoints}, T, gt(-1.57), lt(1.57)};
        Output y{"y"};
        y = tan(x);
        return finish(Level, y);
    } else if constexpr (F == Fn::Pow) {
        Input x{"x", Signal{kPoints}, T, gt(0)};
        Input e{"e", Signal{kPoints}, T};
        Output y{"y"};
        y = pow(x, e);
        return finish(Level, y);
    } else {
        Input x{"x", Signal{kPoints}, T};
        Output y{"y"};
        if constexpr (F == Fn::Cos) {
            y = cos(x);
        } else {
            y = exp2(x);
        }
        return finish(Level, y);
    }
}

// splitmix64: the same points on every machine.
std::uint64_t next(std::uint64_t& state) {
    state += 0x9e3779b97f4a7c15ULL;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

double uniform(std::uint64_t& state, double lo, double hi) {
    const double u = static_cast<double>(next(state) >> 11) * 0x1p-53;
    return lo + (hi - lo) * u;
}

// The operand's points: a dense sweep, then numbers of every size the function takes. `cos` also meets the
// zeros' signs, 2^41 radians (no answer past 2^40) and the infinities; `exp2` its integers, the subnormal results and
// the overflow of either type; `tan` the edges of its branch; `pow`'s bases are positive, 2^-40 to 2^40.
std::vector<double> operand_points(Fn f) {
    std::vector<double> x;
    std::uint64_t state = static_cast<std::uint64_t>(f) + 1;
    if (f == Fn::Cos) {
        x = {0.0, -0.0, 0x1p41, -0x1p41, INFINITY, -INFINITY, 1e9, -3e7};
        while (x.size() < kPoints / 2) {
            x.push_back(uniform(state, -10.0, 10.0));
        }
        while (x.size() < kPoints) {
            x.push_back(std::ldexp(uniform(state, -1.0, 1.0), static_cast<int>(next(state) % 61) - 30));
        }
    } else if (f == Fn::Exp2) {
        for (int k = -1100; k <= 1100; k += 50) {
            x.push_back(static_cast<double>(k));
        }
        while (x.size() < kPoints / 2) {
            x.push_back(uniform(state, -160.0, 140.0));
        }
        while (x.size() < kPoints) {
            x.push_back(uniform(state, -1100.0, 1100.0));
        }
    } else if (f == Fn::Tan) {
        x = {0.0, -0.0, 1.5699, -1.5699, 1.56, -1.56, 0x1p-30, -0x1p-30};
        while (x.size() < kPoints) {
            x.push_back(uniform(state, -1.5699, 1.5699));
        }
    } else {
        x = {1.0, 0x1p-40, 0x1p40, 2.0, 0.5, 10.0};
        while (x.size() < kPoints) {
            x.push_back(std::exp2(uniform(state, -40.0, 40.0)));
        }
    }
    return x;
}

// `pow`'s exponents: 0, the whole numbers, halves, then any in [-30, 30].
std::vector<double> exponent_points() {
    std::vector<double> e{0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 2.0, 3.0};
    std::uint64_t state = 9;
    while (e.size() < kPoints) {
        e.push_back(next(state) % 4 == 0 ? std::round(uniform(state, -30.0, 30.0)) : uniform(state, -30.0, 30.0));
    }
    return e;
}

// The walk of the graph `Build` makes, analysed at its own level, fed `x` (and `e`).
template <auto Build>
std::vector<double> walked(const std::vector<double>& x, const std::vector<double>& e) {
    Graph g = Build();
    if (!infer(g).ok) {
        fail("cos_tan_pow_exp2_test: a graph did not analyse");
    }
    Inputs feeds{};
    feeds.push_back(InputFeed{x.data(), 1});
    if (g.inputs.size() == 2) {
        feeds.push_back(InputFeed{e.data(), 1});
    }
    const auto signals = std::make_unique<Signals<kPoints, 8>>();
    run(g, feeds, *signals);
    std::vector<double> out(kPoints);
    for (std::size_t n = 0; n < kPoints; ++n) {
        out[n] = sample_at(*signals, g.outputs[0].value, 0, n);
    }
    return out;
}

template <class Number>
bool same(Number a, Number b) {
    using Bits = std::conditional_t<sizeof(Number) == 4, std::uint32_t, std::uint64_t>;
    return std::bit_cast<Bits>(a) == std::bit_cast<Bits>(b) || (a != a && b != b);
}

// An F32 `cos` at Normal and Fast runs the float core, as `sin` does; everything else here computes what the walk does.
template <Fn F, ElemType T, Precision Level>
constexpr bool runs_a_float_core = F == Fn::Cos && T == ElemType::F32 && Level != High;

// How many floats apart two answers are, a NaN for a NaN none (`float_cores_test` says why the negative half is flipped).
std::int64_t ulps_apart(float got, float want) {
    if (got != got || want != want) {
        return (got != got) == (want != want) ? 0 : (1LL << 40);
    }
    const auto ordered = [](float v) {
        const std::int64_t bits = std::bit_cast<std::int32_t>(v);
        return bits >= 0 ? bits : -0x80000000LL - bits;
    };
    const std::int64_t d = ordered(got) - ordered(want);
    return d < 0 ? -d : d;
}

// The float core's claim at one argument: 2 ulp of the double core rounded to float on |x| <= 8192, a number in
// [-1, 1] past it, a NaN at an infinity.
bool within_the_float_core(float x, float got, float want) {
    const float magnitude = std::fabs(x);
    if (magnitude <= 8192.0f) {
        return ulps_apart(got, want) <= 2;
    }
    if (magnitude - magnitude == 0.0f) {
        return got >= -1.0f && got <= 1.0f;
    }
    return got != got;
}

constexpr const char* level_name(Precision p) { return p == High ? "High" : p == Normal ? "Normal" : "Fast"; }

// One function at one level, in one type, on one machine: Executor's numbers against the walk's, every one.
template <Fn F, ElemType T, Precision Level, const Machine& M>
bool agrees(const char* name) {
    using Number = std::conditional_t<T == ElemType::F32, float, double>;
    const std::vector<double> wide_x = operand_points(F);
    const std::vector<double> wide_e = exponent_points();
    // The walk is fed what the ports hold: each number in the port's type.
    const std::vector<Number> x(wide_x.begin(), wide_x.end());
    const std::vector<Number> e(wide_e.begin(), wide_e.end());
    const std::vector<double> fed_x(x.begin(), x.end());
    const std::vector<double> fed_e(e.begin(), e.end());
    std::vector<Number> y(kPoints);
    Executor<one_at<F, T, Level>, M> ex;
    testing::held(ex);
    ex.input("x") = x.data();
    if constexpr (F == Fn::Pow) {
        ex.input("e") = e.data();
    }
    ex.output("y") = y.data();
    ex.run();
    // A float core does not read the tier (`float_cores.hpp`: the tier is a ceiling), so at Fast it is held to what it
    // approximates, the walk at Normal; the walk at Fast runs the coarse double core (`tier_bounds_test`), not 2 ulp.
    constexpr Precision kWalkedAt = runs_a_float_core<F, T, Level> ? Normal : Level;
    const std::vector<double> walk = walked<one_at<F, T, kWalkedAt>>(fed_x, fed_e);
    std::size_t differ = 0;
    std::size_t nans = 0;
    std::size_t beyond = 0;
    std::int64_t worst = 0;
    for (std::size_t i = 0; i < kPoints; ++i) {
        const Number want = static_cast<Number>(walk[i]);
        differ += same(y[i], want) ? 0 : 1;
        nans += y[i] != y[i] ? 1 : 0;
        if constexpr (runs_a_float_core<F, T, Level>) {
            beyond += within_the_float_core(x[i], y[i], want) ? 0 : 1;
            const std::int64_t d = std::fabs(x[i]) <= 8192.0f ? ulps_apart(y[i], want) : 0;
            worst = d > worst ? d : worst;
        }
    }
    std::printf("cos_tan_pow_exp2: %s, %s at %s on %s: %zu of %zu numbers differ from the walk (%zu NaN)", name,
                T == ElemType::F32 ? "f32" : "f64", level_name(Level), &M == &zen2 ? "zen2" : "the default machine", differ,
                kPoints, nans);
    if constexpr (runs_a_float_core<F, T, Level>) {
        std::printf(", the float core's: at most %lld ulp on |x| <= 8192, %zu beyond its claim", static_cast<long long>(worst),
                    beyond);
    }
    std::printf("\n");
    const bool agreed = runs_a_float_core<F, T, Level> ? beyond == 0 : differ == 0;
    return agreed && nans < kPoints / 64;
}

template <Fn F, ElemType T, Precision Level>
bool agrees_on_both(const char* name) {
    return agrees<F, T, Level, default_machine>(name) && agrees<F, T, Level, zen2>(name);
}

template <Fn F>
bool agrees_everywhere(const char* name) {
    bool ok = agrees_on_both<F, ElemType::F32, High>(name);
    ok = agrees_on_both<F, ElemType::F32, Normal>(name) && ok;
    ok = agrees_on_both<F, ElemType::F32, Fast>(name) && ok;
    ok = agrees_on_both<F, ElemType::F64, High>(name) && ok;
    ok = agrees_on_both<F, ElemType::F64, Normal>(name) && ok;
    return agrees_on_both<F, ElemType::F64, Fast>(name) && ok;
}

// ---- sin's gradient emits cos -----------------------------------------------------------------------------------

constexpr std::int32_t kTaps = 64;

// loss = mean_k sin(w x_k): dloss/dw = mean_k x_k cos(w x_k), which `sin`'s adjoint builds with a `cos`.
constexpr Graph sine_fit() {
    Input x{"x", {kTaps}, ElemType::F64};
    Input w{"w", {1}, ElemType::F64};
    Expr scaled = w * x;
    Expr wave = sin(scaled);
    Output loss{"loss"};
    loss = wave.mean(0);
    return gradients(loss, {Grad{"dw", w}}, High);
}

bool sine_gradient_runs() {
    std::array<double, kTaps> x{};
    for (std::size_t k = 0; k < x.size(); ++k) {
        x[k] = 0.31 * static_cast<double>((k * 5) % 23) - 2.0;
    }
    constexpr double kW = 0.7;
    constexpr double kStep = 1e-6;
    const auto at = [&](double w) {
        return testing::executed<sine_fit>({{"x", x.data(), kTaps}, {"w", &w, 1}}); // equal to the walk, bit for bit
    };
    const testing::Outputs here = at(kW);
    const double central = (at(kW + kStep)[0][0] - at(kW - kStep)[0][0]) / (2.0 * kStep);
    double closed = 0.0;
    for (const double v : x) {
        closed += v * std::cos(kW * v);
    }
    closed /= kTaps;
    const double dw = here[1][0];
    std::printf("cos_tan_pow_exp2: sin's gradient dw %.17g, central difference %.17g, closed form %.17g\n", dw, central, closed);
    return std::fabs(dw - central) < 1e-8 && std::fabs(dw - closed) < 1e-14;
}

} // namespace

int main() {
    bool ok = agrees_everywhere<Fn::Cos>("cos");
    ok = agrees_everywhere<Fn::Tan>("tan") && ok;
    ok = agrees_everywhere<Fn::Exp2>("exp2") && ok;
    ok = agrees_everywhere<Fn::Pow>("pow") && ok;
    ok = sine_gradient_runs() && ok;
    if (!ok) {
        return 1;
    }
    std::printf("cos_tan_pow_exp2: the four run on Executor as the walk computes them (an f32 cos at Normal and Fast "
                "within its float core's claim), and sin's gradient runs\n");
    return 0;
}
