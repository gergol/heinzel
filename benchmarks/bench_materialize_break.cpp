// Materialize-break crossover benchmark — the target curve for the upcoming fusion cost model.
//
// Ported from ctg3 benchmarks/materialize_break_bench.cpp verbatim (hand arms only, by design — see
// that file's header): a sliding window of width W recomputes a prologue f(x[i]) up to W times unless
// it is materialized once. The old tree had no Heinzel arm (no overlapping-window view there); here the
// window is `x.unfold(0, W)`, folded into its reader's access, and `heinzel` is what the planner
// decides between the two ends the hand arms are (`compute_cheap_producers_inside_their_windows`).
//
//   hand-recompute   — out[i] = Σ_w f(x[i+w]), f evaluated inline (f computed ~W× per element).
//   hand-materialize — tmp[j]=f(x[j]) once (a flat, vectorizable loop), then a cheap windowed sum
//                      out[i]=Σ_w tmp[i+w] (+ the tmp buffer traffic).
//
// Two prologue costs bracket the decision: cheap f(p)=p·p (buffer traffic dominates, recompute should
// win at small W) and heavy (an iterated cubic clip; f dominates, materialize should win even at small
// W). kIterClipIters=10, the same knob main's test_graphs.hpp uses.

#include <cstddef>
#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>

#include "page_vector.hpp"
#include "parallel_arm.hpp"

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"

using heinzel_bench::PageVector;
using heinzel_bench::OutputVector;

namespace hz = heinzel;

namespace {
constexpr std::size_t L = 1u << 22; // 4M-element stream
constexpr int kIterClipIters = 10;

inline float f_cheap(float p) { return p * p; }

inline float f_heavy(float p) {
    for (int k = 0; k < kIterClipIters; ++k) {
        p = 1.5f * p - 0.5f * (p * p * p);
    }
    return p;
}

PageVector<float> make_x() {
    PageVector<float> x(L);
    for (std::size_t i = 0; i < L; ++i) {
        x[i] = -1.0f + 2.0f * static_cast<float>(i % 4096) / 4096.0f;
    }
    return x;
}

const PageVector<float> g_x = make_x();

// W is a template NTTP, not a benchmark::Arg — Arg() would append its own "/<value>" suffix after
// the custom ->Name(), breaking the uniform <workload>/<arm>/<size> naming.
template <std::size_t W, bool Heavy>
void bench_recompute(benchmark::State& state) {
    constexpr std::size_t n = L - W + 1;
    OutputVector<float> out(n);
    for (auto _ : state) {
        for (std::size_t i = 0; i < n; ++i) {
            float acc = 0.0f;
            for (std::size_t w = 0; w < W; ++w) {
                acc += Heavy ? f_heavy(g_x[i + w]) : f_cheap(g_x[i + w]);
            }
            out[i] = acc;
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * n * W);
}

template <std::size_t W, bool Heavy>
void bench_materialize(benchmark::State& state) {
    constexpr std::size_t n = L - W + 1;
    OutputVector<float> out(n);
    OutputVector<float, 1> tmp(L);
    for (auto _ : state) {
        for (std::size_t j = 0; j < L; ++j) {
            tmp[j] = Heavy ? f_heavy(g_x[j]) : f_cheap(g_x[j]);
        }
        for (std::size_t i = 0; i < n; ++i) {
            float acc = 0.0f;
            for (std::size_t w = 0; w < W; ++w) {
                acc += tmp[i + w];
            }
            out[i] = acc;
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * n * W);
}

// The nest arm: the same sum over a window of f(x), as a graph, run as the planner decides — f inside
// the window's reader (recomputed per tap) or stored once. The two hand arms are the two ends of that
// decision, so this arm has to be level with the better of them at every window.
template <int W, bool Heavy>
constexpr hz::Graph kWindowed = [] {
    hz::Expr p = hz::Input{"x", {static_cast<std::int32_t>(L)}};
    for (int k = 0; k < (Heavy ? kIterClipIters : 0); ++k) {
        p = 1.5 * p - 0.5 * (p * p * p);
    }
    p = Heavy ? p : p * p;
    hz::Graph g = hz::finish(hz::Output{"y"} = p.unfold(0, W).sum(1));
    if (!hz::infer(g).ok) { hz::fail("bench_materialize_break: the graph did not infer"); }
    return g;
}();
template <int W, bool Heavy>
constexpr const hz::loops::Program& kWindowedNest = heinzel_bench::kProgramOf<kWindowed<W, Heavy>, heinzel_bench::kMachine>;

template <int W, bool Heavy>
void bench_nest(benchmark::State& state) {
    constexpr std::size_t n = L - W + 1;
    hz::loops::Workspace<kWindowedNest<W, Heavy>> work(hz::loops::Sizes{});
    const hz::loops::Feeds feeds{g_x.data()};
    hz::loops::run<kWindowedNest<W, Heavy>>(feeds, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(kWindowedNest<W, Heavy>.outputs[0]));
    double max_rel = 0.0;
    for (const std::size_t i : {std::size_t{0}, n / 2, n - 1}) {
        double want = 0.0;
        double magnitude = 0.0;
        for (std::size_t w = 0; w < static_cast<std::size_t>(W); ++w) {
            const double f = Heavy ? f_heavy(g_x[i + w]) : f_cheap(g_x[i + w]);
            want += f;
            magnitude += f < 0 ? -f : f;
        }
        const double off = want - y[i] < 0 ? y[i] - want : want - y[i];
        if (!(off <= (W + 40) * 2.4e-7 * magnitude)) {
            state.SkipWithError("bench_materialize_break: the nest output is outside a float sum's rounding bound");
            return;
        }
        max_rel = off / (magnitude > 1e-30 ? magnitude : 1e-30) > max_rel ? off / (magnitude > 1e-30 ? magnitude : 1e-30) : max_rel;
    }
    state.counters["max_rel_err"] = max_rel;
    for (auto _ : state) {
        hz::loops::run<kWindowedNest<W, Heavy>>(feeds, work);
        float first = y[0];
        benchmark::DoNotOptimize(first);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * n * W);
}
} // namespace

#define HEINZEL_WIN(W)                                                                                                     \
    BENCHMARK(bench_nest<W, false>)->Name("cheap/heinzel/win" #W)->UseRealTime();                                          \
    BENCHMARK(bench_nest<W, true>)->Name("heavy/heinzel/win" #W)->UseRealTime();                                           \
    BENCHMARK(bench_recompute<W, false>)->Name("cheap/hand-recompute/win" #W)->UseRealTime();                              \
    BENCHMARK(bench_materialize<W, false>)->Name("cheap/hand-materialize/win" #W)->UseRealTime();                          \
    BENCHMARK(bench_recompute<W, true>)->Name("heavy/hand-recompute/win" #W)->UseRealTime();                               \
    BENCHMARK(bench_materialize<W, true>)->Name("heavy/hand-materialize/win" #W)->UseRealTime();
HEINZEL_WIN(1)
HEINZEL_WIN(4)
HEINZEL_WIN(16)
HEINZEL_WIN(64)

BENCHMARK_MAIN();
