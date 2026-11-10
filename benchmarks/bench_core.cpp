// Core-mechanism throughput benchmark — the rewrite arm, re-expressed against the NEW public API.
//
// Ported from ctg3 benchmarks/core_bench.cpp. The two core mechanisms are unchanged:
//   * the fused elementwise nest  y = a*b + b      (one multiply-add per element)
//   * the contiguous row-reduce   y[r] = Σ_c x[r,c]
// swept over the same sizes spanning the cache hierarchy, with the same benchmark names so a
// run-benchmarks-style grouping against the old tree lines up. `heinzel` is the NEST path (F32,
// native): the fair Heinzel-vs-competitor arm.
//
// `naive` is a plain F32 loop computing exactly what the `heinzel` (nest) graph computes, over the same
// data, so Heinzel's nest path can be read against an un-scheduled reference in addition to `main`.
//
// Correctness is the gate: each workload cross-checks its output against a hand double reference over
// sampled positions (or the whole buffer, where cheap) BEFORE any number is timed.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>

#include "page_vector.hpp"
#include "parallel_arm.hpp"

#include "heinzel/executor.hpp"               // Executor
#include "heinzel/frontend/graph_builder.hpp" // input, finish, operators
#include "heinzel/sema/analyze.hpp"           // infer

using heinzel_bench::PageVector;
using heinzel_bench::OutputVector;

namespace hz = heinzel;

namespace {

// F32 elementwise inputs, shared by the `heinzel` (nest) and `naive` arms so they compute over
// identical data.
PageVector<float> make_f32_elem(int n, float bias) {
    PageVector<float> v(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        v[static_cast<std::size_t>(i)] = 0.001f * static_cast<float>(i % 1000) - bias;
    }
    return v;
}

double rel_err(double got, double ref) { return std::fabs(got - ref) / std::max(std::fabs(ref), 1e-30); }

// ---- elementwise: y = a*b + b -------------------------------------------------------------------
// The nest arm: the same a*b + b, as the old tree's arm has it — F32, one number per sample, the
// length bound at run time — planned for this machine, lowered and run by `loops::run`.
constexpr hz::Graph elem_graph_f32() {
    hz::Input a{"a", hz::Signal{hz::Length{.multiple_of = 1, .min = 1, .expect = 262144}}};
    hz::Input b{"b", hz::Signal{hz::Length{.multiple_of = 1, .min = 1, .expect = 262144}}};
    hz::Graph g = hz::finish(hz::Output{"y"} = a * b + b);
    if (!hz::infer(g).ok) {
        hz::fail("bench_core: the f32 elementwise graph did not infer");
    }
    return g;
}
constexpr hz::Graph kElemF32 = elem_graph_f32();
constexpr const hz::loops::Program& kElemNest = heinzel_bench::kProgramOf<kElemF32, heinzel_bench::kMachine>;

template <int N, bool Par = false>
void bench_elem_nest(benchmark::State& state) {
    const PageVector<float> a = make_f32_elem(N, 1.0f);
    const PageVector<float> b = make_f32_elem(N, 0.7f);
    hz::loops::Workspace<kElemNest, heinzel_bench::copies<Par>()> work(hz::loops::Sizes{N});
    const hz::loops::Feeds feeds{a.data(), b.data()};
    heinzel_bench::run<kElemNest, Par>(feeds, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(kElemNest.outputs[0]));
    // Relative to the terms' magnitudes, not to the result: where a*b and b cancel, an error of half an
    // ULP of a term is thousands of ULPs of the sum, and says nothing about the arithmetic. (Not bit for bit: where the
    // machine has the multiply-add, `Normal` rounds a*b + b once.)
    double max_rel = 0.0;
    for (std::size_t i = 0; i < static_cast<std::size_t>(N); ++i) {
        const double product = static_cast<double>(a[i]) * b[i];
        max_rel = std::max(max_rel, std::fabs(y[i] - (product + b[i])) / std::max(std::fabs(product) + std::fabs(b[i]), 1e-30));
    }
    if (!(max_rel <= 1e-6)) {
        state.SkipWithError("bench_core elem: the nest output disagrees with the reference");
        return;
    }
    state.counters["max_rel_err"] = max_rel;
    if (Par && !heinzel_bench::same_as_serial<kElemNest>(feeds, work, hz::loops::Sizes{N})) {
        state.SkipWithError("bench_core: the split run differs from the serial run");
        return;
    }
    for (auto _ : state) {
        heinzel_bench::run<kElemNest, Par>(feeds, work);
        float first = y[0];
        benchmark::DoNotOptimize(first);
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) * N * static_cast<std::int64_t>(sizeof(float)));
}

// ---- `Executor`'s own cost (Plan/archive/14-dsl/08 §3), not gated -------------------------------------------------
// `heinzel-executor` runs the graph through `Executor`: ports bound by name, the output copied out of its
// workspace after each run. Its control, which ran the same program by hand, and the numbers are in `Plan/archive/12`
// (L2): the difference is the copy, which L12 removes (DS-21).

template <int N>
void bench_elem_executor(benchmark::State& state) {
    const PageVector<float> a = make_f32_elem(N, 1.0f);
    const PageVector<float> b = make_f32_elem(N, 0.7f);
    OutputVector<float> y(N);
    hz::Executor<elem_graph_f32, heinzel_bench::kMachine> ex;
    ex.input("a") = a.data();
    ex.input("b") = b.data();
    ex.output("y") = y.data();
    ex.prepare(N);
    ex.run(N);
    for (std::size_t i = 0; i < static_cast<std::size_t>(N); ++i) { // the whole output, as the nest arm reads it
        const double product = static_cast<double>(a[i]) * b[i];
        if (!(std::fabs(y[i] - (product + b[i])) <= 1e-6 * std::max(std::fabs(product) + std::fabs(b[i]), 1e-30))) {
            state.SkipWithError("bench_core elem: the Executor's output disagrees with the reference");
            return;
        }
    }
    for (auto _ : state) {
        ex.run(N);
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) * N * static_cast<std::int64_t>(sizeof(float)));
}

// The naive arm: exactly what the nest graph computes, a plain F32 loop over the same inputs.
template <int N>
void bench_elem_naive(benchmark::State& state) {
    const PageVector<float> a = make_f32_elem(N, 1.0f);
    const PageVector<float> b = make_f32_elem(N, 0.7f);
    OutputVector<float> y(static_cast<std::size_t>(N));
    for (int i = 0; i < N; ++i) { // one-time correctness pass
        const std::size_t idx = static_cast<std::size_t>(i);
        y[idx] = a[idx] * b[idx] + b[idx];
    }
    for (const std::size_t n : {std::size_t{0}, std::size_t{N / 2}, std::size_t{N - 1}}) {
        const double ref = static_cast<double>(a[n]) * b[n] + b[n];
        if (!(rel_err(y[n], ref) <= 1e-6)) {
            state.SkipWithError("bench_core elem: naive output disagrees with the reference");
            return;
        }
    }
    for (auto _ : state) {
        benchmark::ClobberMemory();
        for (int i = 0; i < N; ++i) {
            const std::size_t idx = static_cast<std::size_t>(i);
            y[idx] = a[idx] * b[idx] + b[idx];
        }
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) * N * static_cast<std::int64_t>(sizeof(float)));
}

// F32 rowsum inputs, shared by the `heinzel` (nest) and `naive` arms.
PageVector<float> make_f32_rowsum(std::size_t n) {
    PageVector<float> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = 0.001f * static_cast<float>(i % 1000) - 0.5f;
    }
    return v;
}

} // namespace

// ---- reduce: y[r] = Σ_c x[r,c] (contiguous inner reduction) -------------------------------------
// The row index R is the streamed (innermost) axis and C the leading width axis, so the feed is
// sample-major with width C — feed[r*C + c] = x[r][c], the same row-major buffer the old tree fills.
// The nest arm of the row sum: F32, C columns folded per row, the row count bound at run time.
template <int C>
constexpr hz::Graph rowsum_graph_f32() {
    hz::Input x{"x", hz::Signal{C, hz::Length{.multiple_of = 1, .min = 1, .expect = 16384}}};
    hz::Graph g = hz::finish(hz::Output{"y"} = x.sum(0));
    if (!hz::infer(g).ok) {
        hz::fail("bench_core: the f32 rowsum graph did not infer");
    }
    return g;
}
template <int C>
constexpr hz::Graph kRowF32 = rowsum_graph_f32<C>();
template <int C>
constexpr const hz::loops::Program& kRowNest = heinzel_bench::kProgramOf<kRowF32<C>, heinzel_bench::kMachine>;

template <int R, int C, bool Par = false>
void bench_red_nest(benchmark::State& state) {
    const PageVector<float> x = make_f32_rowsum(static_cast<std::size_t>(R) * C);
    hz::loops::Sizes sizes{};
    sizes[0] = R;
    hz::loops::Workspace<kRowNest<C>, heinzel_bench::copies<Par>()> work(sizes);
    const hz::loops::Feeds feeds{x.data()};
    heinzel_bench::run<kRowNest<C>, Par>(feeds, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(kRowNest<C>.outputs[0]));
    // Two orders of one sum differ by at most (n-1) * eps * sum|x|; check every row against that bound
    // before timing (cheap: R*C is bounded), and report the largest relative error seen as a counter.
    double max_rel = 0.0;
    for (int r = 0; r < R; ++r) {
        double want = 0.0;
        double magnitude = 0.0;
        for (int c = 0; c < C; ++c) {
            want += x[static_cast<std::size_t>(r) * C + static_cast<std::size_t>(c)];
            magnitude += std::fabs(x[static_cast<std::size_t>(r) * C + static_cast<std::size_t>(c)]);
        }
        if (!(std::fabs(y[static_cast<std::size_t>(r)] - want) <= (C - 1) * 1.2e-7 * std::max(magnitude, 1e-30))) {
            state.SkipWithError("bench_core rowsum: the nest output is outside the reassociation bound");
            return;
        }
        max_rel = std::max(max_rel, std::fabs(y[static_cast<std::size_t>(r)] - want) / std::max(magnitude, 1e-30));
    }
    state.counters["max_rel_err"] = max_rel;
    if (Par && !heinzel_bench::same_as_serial<kRowNest<C>>(feeds, work, sizes)) {
        state.SkipWithError("bench_core: the split run differs from the serial run");
        return;
    }
    for (auto _ : state) {
        heinzel_bench::run<kRowNest<C>, Par>(feeds, work);
        float first = y[0];
        benchmark::DoNotOptimize(first);
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) * R * C * static_cast<std::int64_t>(sizeof(float)));
}

// The naive arm: exactly what the nest graph computes (Σ_c x[r,c]), a plain F32 loop, same data.
template <int R, int C>
void bench_red_naive(benchmark::State& state) {
    const PageVector<float> x = make_f32_rowsum(static_cast<std::size_t>(R) * C);
    OutputVector<float> y(static_cast<std::size_t>(R));
    for (int r = 0; r < R; ++r) {
        float acc = 0.0f;
        for (int c = 0; c < C; ++c) {
            acc += x[static_cast<std::size_t>(r) * C + static_cast<std::size_t>(c)];
        }
        y[static_cast<std::size_t>(r)] = acc;
    }
    for (const std::size_t r : {std::size_t{0}, std::size_t{R / 2}, std::size_t{R - 1}}) {
        double want = 0.0;
        double magnitude = 0.0;
        for (int c = 0; c < C; ++c) {
            want += x[r * C + static_cast<std::size_t>(c)];
            magnitude += std::fabs(x[r * C + static_cast<std::size_t>(c)]);
        }
        if (!(std::fabs(y[r] - want) <= (C - 1) * 1.2e-7 * std::max(magnitude, 1e-30))) {
            state.SkipWithError("bench_core rowsum: naive output is outside the reassociation bound");
            return;
        }
    }
    for (auto _ : state) {
        benchmark::ClobberMemory();
        for (int r = 0; r < R; ++r) {
            float acc = 0.0f;
            for (int c = 0; c < C; ++c) {
                acc += x[static_cast<std::size_t>(r) * C + static_cast<std::size_t>(c)];
            }
            y[static_cast<std::size_t>(r)] = acc;
        }
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) * R * C * static_cast<std::int64_t>(sizeof(float)));
}

#define HEINZEL_ELEM_NEST(N) BENCHMARK(bench_elem_nest<N>)->Name("elem/heinzel/N" #N)->UseRealTime();
#define HEINZEL_ELEM_NAIVE(N) BENCHMARK(bench_elem_naive<N>)->Name("elem/naive/N" #N)->UseRealTime();
HEINZEL_ELEM_NEST(4096)
HEINZEL_ELEM_NEST(262144)
HEINZEL_ELEM_NEST(1048576)
BENCHMARK((bench_elem_nest<1048576, true>))->Name("elem/heinzel-par/N1048576")->UseRealTime();
BENCHMARK(bench_elem_executor<4096>)->Name("elem/heinzel-executor/N4096")->UseRealTime();
BENCHMARK(bench_elem_executor<262144>)->Name("elem/heinzel-executor/N262144")->UseRealTime();
HEINZEL_ELEM_NAIVE(4096)
HEINZEL_ELEM_NAIVE(262144)
HEINZEL_ELEM_NAIVE(1048576)

#define HEINZEL_RED_NEST(R, C) BENCHMARK(bench_red_nest<R, C>)->Name("rowsum/heinzel/C" #C)->UseRealTime();
HEINZEL_RED_NEST(65536, 64)
HEINZEL_RED_NEST(16384, 256)
HEINZEL_RED_NEST(4096, 1024)
BENCHMARK((bench_red_nest<4096, 1024, true>))->Name("rowsum/heinzel-par/C1024")->UseRealTime();
#define HEINZEL_RED_NAIVE(R, C) BENCHMARK(bench_red_naive<R, C>)->Name("rowsum/naive/C" #C)->UseRealTime();
HEINZEL_RED_NAIVE(65536, 64)
HEINZEL_RED_NAIVE(16384, 256)
HEINZEL_RED_NAIVE(4096, 1024)

BENCHMARK_MAIN();
