// Applications (tier-2) benchmark — the rewrite arm, re-expressed against the NEW API.
//
// Ported from ctg3 benchmarks/apps_bench.cpp — the PRUNED tier-2 subset (plan/01-classification.md):
// the kernels that add coverage BEYOND the eight primary groups, all pure notation with a matching
// rewrite parity/unit test. NOT ported (redundant with a primary bench): gemm≈contraction,
// softmax≈attention, box/sharpen≈fusion, biquad/cascade≈recurrence/channel_strip; logmel is DEFERRED
// (needs the absent `dsp::logmel`/`mel_filterbank`).
//
//   brightness  : clamp(1.2·x + 0.05, 0, 1)               image {512,512}      (min/max/clamp + affine)
//   rownorm     : x − rowmean(x)                          {1024,256}           (keepdims-mean broadcast subtract)
//   rmsnorm     : x / sqrt(mean_c(x²) + eps)              {1024,256}           (sqrt over a reduce)
//
// linear_relu and mlp2 (relu(x·W+b) and its 2-layer chain) live in bench_ml_layers.cpp — split out
// once both grew a nest arm, to keep this file and that one under the line cap.
//
// The mean-centre/rms broadcasts are the coverage the elementwise benches (core_bench) do not exercise.
// Spellings taken verbatim from tests/parity/rmsnorm_here.cpp (a keepdims mean over the columns, broadcast
// back across the row).
//
// `heinzel` = the nest path (F32, native). `naive`/`hand` are ported from ctg3 benchmarks/apps_bench.cpp,
// F32, over the same data as the `heinzel` (nest) arm.

#include <algorithm>
#include <cmath>
#include <utility>
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

double rel_err(double got, double ref) { return std::fabs(got - ref) / std::max(std::fabs(ref), 1e-30); }

// ---- brightness: clamp(1.2·x + 0.05, 0, 1) over a 512×512 image ----
constexpr int kH = 512;
constexpr int kW = 512;
// The nest arm: the same image op in F32 (as the old tree's arm is), planned for this machine.
constexpr hz::Graph bright_f32 = [] {
    hz::Input x{"x", {kH, kW}};
    hz::Graph g = hz::finish(hz::Output{"y"} = hz::clamp(hz::constant(1.2) * x + 0.05, 0.0, 1.0));
    if (!hz::infer(g).ok) { hz::fail("bench_apps: f32 brightness did not infer"); }
    return g;
}();
constexpr const hz::loops::Program& bright_nest = heinzel_bench::kProgramOf<bright_f32, heinzel_bench::kMachine>;

PageVector<float> bright_input() {
    PageVector<float> x(static_cast<std::size_t>(kH) * kW);
    for (std::size_t i = 0; i < x.size(); ++i) { x[i] = static_cast<float>(1.0 - 0.2 * static_cast<double>(i % 11)); }
    return x;
}

void bench_brightness_nest(benchmark::State& state) {
    const PageVector<float> x = bright_input();
    hz::loops::Workspace<bright_nest> work(hz::loops::Sizes{});
    const hz::loops::Feeds feeds{x.data()};
    heinzel_bench::run<bright_nest, false>(feeds, work);
    const float* y = work.data<float>(static_cast<std::size_t>(bright_nest.outputs[0]));
    for (const std::size_t i : {std::size_t{0}, x.size() / 2, x.size() - 1}) {
        const float v = 1.2f * x[i] + 0.05f;
        const float want = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        if (!(std::fabs(y[i] - want) <= 1e-6f)) { state.SkipWithError("bench_apps brightness: the nest output is wrong"); return; }
    }
    // max_rel_err over every pixel — O(1)-per-element reference, cheap at this size.
    double max_rel = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double v = 1.2 * x[i] + 0.05;
        const double want = v < 0 ? 0.0 : (v > 1 ? 1.0 : v);
        max_rel = std::max(max_rel, rel_err(y[i], want));
    }
    state.counters["max_rel_err"] = max_rel;
    for (auto _ : state) {
        heinzel_bench::run<bright_nest, false>(feeds, work);
        float first = y[0];
        benchmark::DoNotOptimize(first);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * kH * kW);
}

// naive: port of ctg3 img_brightness_naive, over the same F32 data as the nest arm.
void bench_brightness_naive(benchmark::State& state) {
    const PageVector<float> x = bright_input();
    OutputVector<float> y(x.size());
    auto run = [&] {
        for (std::size_t i = 0; i < x.size(); ++i) {
            float v = 1.2f * x[i] + 0.05f;
            y[i] = v < 0 ? 0.0f : (v > 1 ? 1.0f : v);
        }
    };
    run();
    for (const std::size_t i : {std::size_t{0}, x.size() / 2, x.size() - 1}) {
        const double v = 1.2 * x[i] + 0.05;
        const double want = v < 0 ? 0.0 : (v > 1 ? 1.0 : v);
        if (!(rel_err(y[i], want) <= 1e-6)) { state.SkipWithError("bench_apps brightness: naive disagrees with the reference"); return; }
    }
    for (auto _ : state) {
        benchmark::ClobberMemory();
        run();
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * kH * kW);
}

// ---- rownorm: x − rowmean(x), and rmsnorm: x / sqrt(mean_c(x²) + eps), over {1024,256} ----
constexpr int kRM = 1024;
constexpr int kRK = 256;
// The nest arms of the two row norms: F32, as the old tree's arms are.
constexpr hz::Graph rownorm_f32 = [] {
    hz::Input x{"x", {kRM, kRK}};
    hz::Graph g = hz::finish(hz::Output{"y"} = x - x.mean(1, hz::keepdims));
    if (!hz::infer(g).ok) { hz::fail("bench_apps: f32 rownorm did not infer"); }
    return g;
}();
constexpr hz::Graph rmsnorm_f32 = [] {
    hz::Input x{"x", {kRM, kRK}};
    hz::Expr ms = (x * x).mean(1, hz::keepdims);
    hz::Graph g = hz::finish(hz::Output{"y"} = x / hz::sqrt(ms + 1e-6f));
    if (!hz::infer(g).ok) { hz::fail("bench_apps: f32 rmsnorm did not infer"); }
    return g;
}();
constexpr const hz::loops::Program& rownorm_nest = heinzel_bench::kProgramOf<rownorm_f32, heinzel_bench::kMachine>;
constexpr const hz::loops::Program& rmsnorm_nest = heinzel_bench::kProgramOf<rmsnorm_f32, heinzel_bench::kMachine>;

PageVector<float> rownorm_input() {
    PageVector<float> x(static_cast<std::size_t>(kRM) * kRK);
    for (std::size_t i = 0; i < x.size(); ++i) { x[i] = static_cast<float>(1.0 - 0.5 * static_cast<double>(i % 13)); }
    return x;
}

// `want(i, j)` in double; the nest arm is held to a few float rounding steps of it. max_rel_err is
// taken over a fixed stratified sample of 16 rows (every column of each — the per-element reference
// is O(kRK), so a full sweep is not "cheap" at 1024 rows, a fixed sample is).
template <const hz::loops::Program& P, class Want>
void bench_norm_nest(benchmark::State& state, Want want) {
    const PageVector<float> x = rownorm_input();
    hz::loops::Workspace<P> work(hz::loops::Sizes{});
    const hz::loops::Feeds feeds{x.data()};
    heinzel_bench::run<P, false>(feeds, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(P.outputs[0]));
    for (const auto [i, j] : {std::pair{0, 0}, std::pair{kRM / 2, kRK / 2}, std::pair{kRM - 1, kRK - 1}}) {
        const double expect = want(x, i, j);
        if (!(std::fabs(y[static_cast<std::size_t>(i) * kRK + static_cast<std::size_t>(j)] - expect) <= 1e-5 * (1.0 + std::fabs(expect)))) {
            state.SkipWithError("bench_apps: a row-norm nest output is wrong"); return;
        }
    }
    double max_rel = 0.0;
    for (int s = 0; s < 16; ++s) {
        const int i = s * kRM / 16;
        for (int j = 0; j < kRK; ++j) {
            const double expect = want(x, i, j);
            max_rel = std::max(max_rel, rel_err(y[static_cast<std::size_t>(i) * kRK + static_cast<std::size_t>(j)], expect));
        }
    }
    state.counters["max_rel_err"] = max_rel;
    for (auto _ : state) {
        heinzel_bench::run<P, false>(feeds, work);
        float first = y[0];
        benchmark::DoNotOptimize(first);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * kRM * kRK);
}
double rownorm_want(const PageVector<float>& x, int i, int j) {
    double s = 0.0; for (int c = 0; c < kRK; ++c) { s += x[static_cast<std::size_t>(i) * kRK + static_cast<std::size_t>(c)]; }
    return x[static_cast<std::size_t>(i) * kRK + static_cast<std::size_t>(j)] - s / kRK;
}
double rmsnorm_want(const PageVector<float>& x, int i, int j) {
    double s = 0.0; for (int c = 0; c < kRK; ++c) { const double v = x[static_cast<std::size_t>(i) * kRK + static_cast<std::size_t>(c)]; s += v * v; }
    return x[static_cast<std::size_t>(i) * kRK + static_cast<std::size_t>(j)] / std::sqrt(s / kRK + 1e-6);
}
void bench_rownorm_nest(benchmark::State& state) { bench_norm_nest<rownorm_nest>(state, rownorm_want); }
void bench_rmsnorm_nest(benchmark::State& state) { bench_norm_nest<rmsnorm_nest>(state, rmsnorm_want); }

// naive: ports of ctg3 ml_rownorm_naive / ml_rmsnorm_naive, over the same F32 data as the nest arm.
void bench_rownorm_naive(benchmark::State& state) {
    const PageVector<float> x = rownorm_input();
    OutputVector<float> y(x.size());
    auto run = [&] {
        for (int i = 0; i < kRM; ++i) {
            float m = 0;
            for (int j = 0; j < kRK; ++j) { m += x[static_cast<std::size_t>(i) * kRK + static_cast<std::size_t>(j)]; }
            m *= (1.0f / kRK);
            for (int j = 0; j < kRK; ++j) {
                const std::size_t idx = static_cast<std::size_t>(i) * kRK + static_cast<std::size_t>(j);
                y[idx] = x[idx] - m;
            }
        }
    };
    run();
    for (const auto [i, j] : {std::pair{0, 0}, std::pair{kRM / 2, kRK / 2}, std::pair{kRM - 1, kRK - 1}}) {
        const double expect = rownorm_want(x, i, j);
        if (!(std::fabs(y[static_cast<std::size_t>(i) * kRK + static_cast<std::size_t>(j)] - expect) <= 1e-5 * (1.0 + std::fabs(expect)))) {
            state.SkipWithError("bench_apps: rownorm naive disagrees with the reference"); return;
        }
    }
    for (auto _ : state) {
        benchmark::ClobberMemory();
        run();
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * kRM * kRK);
}
void bench_rmsnorm_naive(benchmark::State& state) {
    const PageVector<float> x = rownorm_input();
    OutputVector<float> y(x.size());
    auto run = [&] {
        for (int i = 0; i < kRM; ++i) {
            float ss = 0;
            for (int j = 0; j < kRK; ++j) {
                const float v = x[static_cast<std::size_t>(i) * kRK + static_cast<std::size_t>(j)];
                ss += v * v;
            }
            const float inv = 1.0f / std::sqrt(ss * (1.0f / kRK) + 1e-6f);
            for (int j = 0; j < kRK; ++j) {
                const std::size_t idx = static_cast<std::size_t>(i) * kRK + static_cast<std::size_t>(j);
                y[idx] = x[idx] * inv;
            }
        }
    };
    run();
    for (const auto [i, j] : {std::pair{0, 0}, std::pair{kRM / 2, kRK / 2}, std::pair{kRM - 1, kRK - 1}}) {
        const double expect = rmsnorm_want(x, i, j);
        if (!(std::fabs(y[static_cast<std::size_t>(i) * kRK + static_cast<std::size_t>(j)] - expect) <= 1e-5 * (1.0 + std::fabs(expect)))) {
            state.SkipWithError("bench_apps: rmsnorm naive disagrees with the reference"); return;
        }
    }
    for (auto _ : state) {
        benchmark::ClobberMemory();
        run();
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * kRM * kRK);
}

} // namespace

BENCHMARK(bench_brightness_nest)->Name("img/brightness/heinzel")->UseRealTime();
BENCHMARK(bench_brightness_naive)->Name("img/brightness/naive")->UseRealTime();
BENCHMARK(bench_rownorm_nest)->Name("ml/rownorm/heinzel")->UseRealTime();
BENCHMARK(bench_rownorm_naive)->Name("ml/rownorm/naive")->UseRealTime();
BENCHMARK(bench_rmsnorm_nest)->Name("ml/rmsnorm/heinzel")->UseRealTime();
BENCHMARK(bench_rmsnorm_naive)->Name("ml/rmsnorm/naive")->UseRealTime();

BENCHMARK_MAIN();
