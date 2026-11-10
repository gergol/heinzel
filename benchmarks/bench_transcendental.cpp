// The radian sine and cosine and the natural logarithm, one number per sample over a run-time length: what the
// float cores (`runtime/float_cores.hpp`) are worth on the nest path, against what computes the same elsewhere. The
// cosine's float core is the sine's a quarter turn along (Plan/28-dsl-proposals/05 D2); before it, the cosine ran its
// double core one number at a time.
//
//   heinzel      the nest path, F32: the float core in lanes of eight (Cephes' polynomials, a five-part
//                reduction for the sine and the cosine), <= 2 f32 ulp
//   naive        `std::sin` / `std::cos` / `std::log` over floats, one call per sample (libm; clang calls it per
//                element)
//   eigen        `Eigen::ArrayXf::sin()` / `.cos()` / `.log()`, Eigen's own vectorised float cores (opt-in,
//                HEINZEL_BENCH_COMPARE_EIGEN)
//
// The sine's and the cosine's inputs span [-8, 8], inside the sine's claimed domain; the logarithm's run from 2^-10
// to 2^10 log-uniformly. Every arm records `max_rel_err` against the float64 answer.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>

#ifdef HEINZEL_BENCH_COMPARE_EIGEN
#include <Eigen/Core>
#endif

#include "page_vector.hpp"
#include "parallel_arm.hpp"

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"

using heinzel_bench::PageVector;
using heinzel_bench::OutputVector;

namespace hz = heinzel;

namespace {

enum class Fn { Sin, Cos, Log };

PageVector<float> inputs(Fn fn, int n) {
    PageVector<float> v(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const double u = static_cast<double>(i % 4099) / 4099.0; // not a divisor of any size: every lane differs
        v[static_cast<std::size_t>(i)] = static_cast<float>(fn != Fn::Log ? -8.0 + 16.0 * u : std::exp2(-10.0 + 20.0 * u));
    }
    return v;
}

double reference(Fn fn, float x) {
    const auto v = static_cast<double>(x);
    return fn == Fn::Sin ? std::sin(v) : fn == Fn::Cos ? std::cos(v) : std::log(v);
}

// The error against the float64 answer, relative to the answer's magnitude — floored at 2^-10 for the
// sine and the cosine, whose zeros would otherwise report a number about the zero and not about the arithmetic.
double max_rel_err(Fn fn, const PageVector<float>& x, const float* y) {
    double worst = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double want = reference(fn, x[i]);
        worst = std::max(worst, std::fabs(static_cast<double>(y[i]) - want) / std::max(std::fabs(want), 0x1p-10));
    }
    return worst;
}

// The log's input is declared above 0, as `inputs` gives it (2^-10 to 2^10): the domain the proof of ranges asks of it.
template <Fn F>
constexpr hz::Graph graph() {
    const hz::Signal length{hz::Length{.multiple_of = 1, .min = 1, .expect = 262144}};
    const hz::Input x = F != Fn::Log ? hz::Input{"x", length} : hz::Input{"x", length, hz::gt(0)};
    hz::Graph g = hz::finish(hz::Output{"y"} = F == Fn::Sin ? hz::sin(x) : F == Fn::Cos ? hz::cos(x) : hz::log(x));
    if (!hz::infer(g).ok) {
        hz::fail("bench_transcendental: a graph did not infer");
    }
    return g;
}

template <Fn F>
constexpr hz::Graph kF32 = graph<F>();
template <Fn F>
constexpr const hz::loops::Program& kNest = heinzel_bench::kProgramOf<kF32<F>, heinzel_bench::kMachine>;

template <Fn F, int N>
void bench_nest(benchmark::State& state) {
    const PageVector<float> x = inputs(F, N);
    hz::loops::Workspace<kNest<F>> work(hz::loops::Sizes{N});
    const hz::loops::Feeds feeds{x.data()};
    hz::loops::run<kNest<F>>(feeds, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(kNest<F>.outputs[0]));
    state.counters["max_rel_err"] = max_rel_err(F, x, y);
    for (auto _ : state) {
        hz::loops::run<kNest<F>>(feeds, work);
        float first = y[0];
        benchmark::DoNotOptimize(first);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * N);
}

template <Fn F, int N>
void bench_naive(benchmark::State& state) {
    const PageVector<float> x = inputs(F, N);
    OutputVector<float> y(static_cast<std::size_t>(N));
    const auto run = [&] {
        for (std::size_t i = 0; i < y.size(); ++i) {
            y[i] = F == Fn::Sin ? std::sin(x[i]) : F == Fn::Cos ? std::cos(x[i]) : std::log(x[i]);
        }
    };
    run();
    state.counters["max_rel_err"] = max_rel_err(F, x, y.data());
    for (auto _ : state) {
        benchmark::ClobberMemory();
        run();
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * N);
}

#ifdef HEINZEL_BENCH_COMPARE_EIGEN
template <Fn F, int N>
void bench_eigen(benchmark::State& state) {
    const PageVector<float> x = inputs(F, N);
    OutputVector<float> y(static_cast<std::size_t>(N));
    const Eigen::Map<const Eigen::ArrayXf> in(x.data(), N);
    Eigen::Map<Eigen::ArrayXf> out(y.data(), N);
    const auto run = [&] {
        if constexpr (F == Fn::Sin) {
            out = in.sin();
        } else if constexpr (F == Fn::Cos) {
            out = in.cos();
        } else {
            out = in.log();
        }
    };
    run();
    state.counters["max_rel_err"] = max_rel_err(F, x, y.data());
    for (auto _ : state) {
        run();
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * N);
}
#endif

} // namespace

#define HEINZEL_TRANSCENDENTAL(FN, NAME, N)                                                                            \
    BENCHMARK(bench_nest<Fn::FN, N>)->Name(NAME "/heinzel/N" #N)->UseRealTime();                                    \
    BENCHMARK(bench_naive<Fn::FN, N>)->Name(NAME "/naive/N" #N)->UseRealTime();
HEINZEL_TRANSCENDENTAL(Sin, "sin", 4096)
HEINZEL_TRANSCENDENTAL(Sin, "sin", 262144)
HEINZEL_TRANSCENDENTAL(Sin, "sin", 1048576)
HEINZEL_TRANSCENDENTAL(Cos, "cos", 4096)
HEINZEL_TRANSCENDENTAL(Cos, "cos", 262144)
HEINZEL_TRANSCENDENTAL(Cos, "cos", 1048576)
HEINZEL_TRANSCENDENTAL(Log, "log", 4096)
HEINZEL_TRANSCENDENTAL(Log, "log", 262144)
HEINZEL_TRANSCENDENTAL(Log, "log", 1048576)

#ifdef HEINZEL_BENCH_COMPARE_EIGEN
BENCHMARK(bench_eigen<Fn::Sin, 4096>)->Name("sin/eigen/N4096")->UseRealTime();
BENCHMARK(bench_eigen<Fn::Sin, 262144>)->Name("sin/eigen/N262144")->UseRealTime();
BENCHMARK(bench_eigen<Fn::Sin, 1048576>)->Name("sin/eigen/N1048576")->UseRealTime();
BENCHMARK(bench_eigen<Fn::Cos, 4096>)->Name("cos/eigen/N4096")->UseRealTime();
BENCHMARK(bench_eigen<Fn::Cos, 262144>)->Name("cos/eigen/N262144")->UseRealTime();
BENCHMARK(bench_eigen<Fn::Cos, 1048576>)->Name("cos/eigen/N1048576")->UseRealTime();
BENCHMARK(bench_eigen<Fn::Log, 4096>)->Name("log/eigen/N4096")->UseRealTime();
BENCHMARK(bench_eigen<Fn::Log, 262144>)->Name("log/eigen/N262144")->UseRealTime();
BENCHMARK(bench_eigen<Fn::Log, 1048576>)->Name("log/eigen/N1048576")->UseRealTime();
#endif

BENCHMARK_MAIN();
