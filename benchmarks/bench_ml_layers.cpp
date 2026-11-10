// ML layer benchmarks — the rewrite arm, re-expressed against the NEW API.
//
// Split out of bench_apps.cpp once both workloads grew a nest arm (that file was already at the
// 600-line cap). Ported from ctg3 benchmarks/apps_bench.cpp (plan/01-classification.md):
//
//   linear_relu : relu(x·W + b)                           {128,512}·{512,512}  (matmul + bias + relu epilogue)
//   mlp2        : relu(relu(x·W1+b1)·W2+b2)                2-layer MLP          (chained fused epilogues)
//
// The fused bias+relu epilogue is coverage the bare matmul (contraction_bench) does not exercise.
// Spellings taken verbatim from tests/parity/mlp_forward_here.cpp (einsum "ik,kj->ij" for a
// matmul of tensors; bias as {1,N} broadcast over the row axis; relu = max(_,0.0)).
//
// `heinzel` = the nest path (F32, native, no kernel). `naive`/`hand` are ported from ctg3
// benchmarks/apps_bench.cpp, F32, over the SAME data as the `heinzel` (nest) arm — one generator per
// workload, shared by all three F32 arms.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include "page_vector.hpp"
#include "parallel_arm.hpp"

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/notation/einsum.hpp"
#include "heinzel/sema/analyze.hpp"

using heinzel_bench::PageVector;
using heinzel_bench::OutputVector;

namespace hz = heinzel;

namespace {

// F32 generator, shared by every F32 arm of a workload (heinzel nest, naive, hand).
PageVector<float> rampf(std::size_t n, float a, float b) {
    PageVector<float> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = a * static_cast<float>((i * 2654435761u) % 1009) / 1009.0f + b;
    }
    return v;
}

double rel_err(double got, double ref) { return std::fabs(got - ref) / std::max(std::fabs(ref), 1e-30); }

// Fixed stratified sample of 5 (i,j) output points, shared by both workloads (same M,N) — every F32
// arm's correctness check / max_rel_err is taken over these.
constexpr int kM = 128;
constexpr int kK = 512;
constexpr int kN = 512;
constexpr std::array<std::pair<int, int>, 5> ml_samples() {
    return {std::pair{0, 0}, std::pair{kM / 4, kN / 3}, std::pair{kM / 2, kN / 2}, std::pair{3 * kM / 4, kN / 4},
            std::pair{kM - 1, kN - 1}};
}

// ---- linear_relu: relu(x·W + b), x{128,512} W{512,512} b{512} ----
// The one F32 generator for linear_relu, shared by the heinzel (nest), naive and hand arms.
struct LinReluInput {
    PageVector<float> x;
    PageVector<float> w;
    PageVector<float> b;
};
LinReluInput linrelu_input() {
    return {rampf(static_cast<std::size_t>(kM) * kK, 1.0f, -0.5f), rampf(static_cast<std::size_t>(kK) * kN, 1.0f, -0.5f),
            rampf(static_cast<std::size_t>(kN), 1.0f, -0.5f)};
}

// The nest arm: the same layer in F32 (as the old tree's arm is), planned for this machine. No
// specialized kernel — the scheduler chooses the loops for the einsum + bias-add + relu epilogue.
constexpr hz::Graph linrelu_f32 = [] {
    hz::Input x{"x", {kM, kK}};
    hz::Input w{"w", {kK, kN}};
    hz::Input b{"b", {1, kN}};
    hz::Graph g = hz::finish(hz::Output{"y"} = hz::max(hz::einsum("ik,kj->ij", x, w) + b, 0.0));
    if (!hz::infer(g).ok) { hz::fail("bench_ml_layers: f32 linear_relu did not infer"); }
    return g;
}();
constexpr const hz::loops::Program& linrelu_nest = heinzel_bench::kProgramOf<linrelu_f32, heinzel_bench::kMachine>;

void bench_linear_relu_nest(benchmark::State& state) {
    const LinReluInput in = linrelu_input();
    hz::loops::Workspace<linrelu_nest> work(hz::loops::Sizes{});
    const hz::loops::Feeds feeds{in.x.data(), in.w.data(), in.b.data()};
    heinzel_bench::run<linrelu_nest, false>(feeds, work);
    const float* y = work.data<float>(static_cast<std::size_t>(linrelu_nest.outputs[0]));
    double max_rel = 0.0;
    for (const auto [i, j] : ml_samples()) {
        double sum = 0.0;
        double magnitude = 0.0;
        for (int k = 0; k < kK; ++k) {
            const double term = static_cast<double>(in.x[static_cast<std::size_t>(i) * kK + static_cast<std::size_t>(k)]) *
                                 in.w[static_cast<std::size_t>(k) * kN + static_cast<std::size_t>(j)];
            sum += term;
            magnitude += std::fabs(term);
        }
        const double bj = in.b[static_cast<std::size_t>(j)];
        const double want = std::max(sum + bj, 0.0);
        magnitude += std::fabs(bj);
        const double got = y[static_cast<std::size_t>(i) * kN + static_cast<std::size_t>(j)];
        if (!(std::fabs(got - want) <= kK * 1.2e-7 * magnitude)) {
            state.SkipWithError("bench_ml_layers: linear_relu nest output is outside a float sum's rounding bound");
            return;
        }
        max_rel = std::max(max_rel, std::fabs(got - want) / std::max(magnitude, 1e-30));
    }
    state.counters["max_rel_err"] = max_rel;
    for (auto _ : state) {
        heinzel_bench::run<linrelu_nest, false>(feeds, work);
        float first = y[0];
        benchmark::DoNotOptimize(first);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * 2 * kM * kK * kN);
}

// naive/hand: ported from ctg3 ml_linear_naive / ml_linear_hand, over the same F32 data as the nest arm.
double linrelu_ref(const PageVector<float>& x, const PageVector<float>& w, const PageVector<float>& b, int i, int j) {
    double s = b[static_cast<std::size_t>(j)];
    for (int k = 0; k < kK; ++k) { s += static_cast<double>(x[static_cast<std::size_t>(i) * kK + static_cast<std::size_t>(k)]) * w[static_cast<std::size_t>(k) * kN + static_cast<std::size_t>(j)]; }
    return s < 0 ? 0.0 : s;
}
void bench_linear_relu_naive(benchmark::State& state) {
    const LinReluInput in = linrelu_input();
    OutputVector<float> y(static_cast<std::size_t>(kM) * kN);
    auto run = [&] {
        for (int i = 0; i < kM; ++i) {
            for (int j = 0; j < kN; ++j) {
                float a = in.b[static_cast<std::size_t>(j)];
                for (int k = 0; k < kK; ++k) { a += in.x[static_cast<std::size_t>(i) * kK + static_cast<std::size_t>(k)] * in.w[static_cast<std::size_t>(k) * kN + static_cast<std::size_t>(j)]; }
                y[static_cast<std::size_t>(i) * kN + static_cast<std::size_t>(j)] = a < 0 ? 0.0f : a;
            }
        }
    };
    run();
    for (const auto [i, j] : {std::pair{0, 0}, std::pair{kM / 2, kN / 2}, std::pair{kM - 1, kN - 1}}) {
        const double r = linrelu_ref(in.x, in.w, in.b, i, j);
        if (!(rel_err(y[static_cast<std::size_t>(i) * kN + static_cast<std::size_t>(j)], r) <= 1e-3)) {
            state.SkipWithError("bench_ml_layers: linear_relu naive disagrees with the reference"); return;
        }
    }
    for (auto _ : state) {
        benchmark::ClobberMemory();
        run();
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * 2 * kM * kK * kN);
}
void bench_linear_relu_hand(benchmark::State& state) { // cache-friendly i-k-j
    const LinReluInput in = linrelu_input();
    OutputVector<float> y(static_cast<std::size_t>(kM) * kN);
    auto run = [&] {
        for (int i = 0; i < kM; ++i) {
            for (int j = 0; j < kN; ++j) { y[static_cast<std::size_t>(i) * kN + static_cast<std::size_t>(j)] = in.b[static_cast<std::size_t>(j)]; }
            for (int k = 0; k < kK; ++k) {
                const float xv = in.x[static_cast<std::size_t>(i) * kK + static_cast<std::size_t>(k)];
                for (int j = 0; j < kN; ++j) { y[static_cast<std::size_t>(i) * kN + static_cast<std::size_t>(j)] += xv * in.w[static_cast<std::size_t>(k) * kN + static_cast<std::size_t>(j)]; }
            }
            for (int j = 0; j < kN; ++j) {
                const std::size_t idx = static_cast<std::size_t>(i) * kN + static_cast<std::size_t>(j);
                y[idx] = y[idx] < 0 ? 0.0f : y[idx];
            }
        }
    };
    run();
    for (const auto [i, j] : {std::pair{0, 0}, std::pair{kM / 2, kN / 2}, std::pair{kM - 1, kN - 1}}) {
        const double r = linrelu_ref(in.x, in.w, in.b, i, j);
        if (!(rel_err(y[static_cast<std::size_t>(i) * kN + static_cast<std::size_t>(j)], r) <= 1e-3)) {
            state.SkipWithError("bench_ml_layers: linear_relu hand disagrees with the reference"); return;
        }
    }
    for (auto _ : state) {
        benchmark::ClobberMemory();
        run();
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * 2 * kM * kK * kN);
}

// ---- mlp2: relu(relu(x·W1+b1)·W2+b2), 2-layer MLP ----
constexpr int kHid = 256;
// The one F32 generator for mlp2, shared by the heinzel (nest) and hand arms (mlp2 has no naive arm in
// main either — only hand).
struct Mlp2Input {
    PageVector<float> x;
    PageVector<float> w1;
    PageVector<float> b1;
    PageVector<float> w2;
    PageVector<float> b2;
};
Mlp2Input mlp2_input() {
    return {rampf(static_cast<std::size_t>(kM) * kK, 1.0f, -0.5f), rampf(static_cast<std::size_t>(kK) * kHid, 1.0f, -0.5f),
            rampf(static_cast<std::size_t>(kHid), 1.0f, -0.5f), rampf(static_cast<std::size_t>(kHid) * kN, 1.0f, -0.5f),
            rampf(static_cast<std::size_t>(kN), 1.0f, -0.5f)};
}

// The nest arm: the same 2-layer MLP in F32, planned for this machine. No specialized kernel.
constexpr hz::Graph mlp2_f32 = [] {
    hz::Input x{"x", {kM, kK}};
    hz::Input w1{"w1", {kK, kHid}};
    hz::Input b1{"b1", {1, kHid}};
    hz::Input w2{"w2", {kHid, kN}};
    hz::Input b2{"b2", {1, kN}};
    hz::Expr h = hz::max(hz::einsum("ik,kj->ij", x, w1) + b1, 0.0);
    hz::Expr y = hz::max(hz::einsum("ik,kj->ij", h, w2) + b2, 0.0);
    hz::Graph g = hz::finish(hz::Output{"y"} = y);
    if (!hz::infer(g).ok) { hz::fail("bench_ml_layers: f32 mlp2 did not infer"); }
    return g;
}();
constexpr const hz::loops::Program& mlp2_nest = heinzel_bench::kProgramOf<mlp2_f32, heinzel_bench::kMachine>;

void bench_mlp2_nest(benchmark::State& state) {
    const Mlp2Input in = mlp2_input();
    hz::loops::Workspace<mlp2_nest> work(hz::loops::Sizes{});
    const hz::loops::Feeds feeds{in.x.data(), in.w1.data(), in.b1.data(), in.w2.data(), in.b2.data()};
    heinzel_bench::run<mlp2_nest, false>(feeds, work);
    const float* y = work.data<float>(static_cast<std::size_t>(mlp2_nest.outputs[0]));

    // The float64 hidden layer, computed fully once (128x256 — cheap) so the layer-2 reference below
    // is exact double math, not a second rounding-prone reduction.
    std::vector<double> hidden(static_cast<std::size_t>(kM) * kHid);
    for (int i = 0; i < kM; ++i) {
        for (int hh = 0; hh < kHid; ++hh) {
            double s = in.b1[static_cast<std::size_t>(hh)];
            for (int k = 0; k < kK; ++k) {
                s += static_cast<double>(in.x[static_cast<std::size_t>(i) * kK + static_cast<std::size_t>(k)]) *
                     in.w1[static_cast<std::size_t>(k) * kHid + static_cast<std::size_t>(hh)];
            }
            hidden[static_cast<std::size_t>(i) * kHid + static_cast<std::size_t>(hh)] = s < 0 ? 0.0 : s;
        }
    }
    double max_rel = 0.0;
    for (const auto [i, j] : ml_samples()) {
        double sum = 0.0;
        double magnitude = 0.0;
        for (int hh = 0; hh < kHid; ++hh) {
            const double term = hidden[static_cast<std::size_t>(i) * kHid + static_cast<std::size_t>(hh)] *
                                 in.w2[static_cast<std::size_t>(hh) * kN + static_cast<std::size_t>(j)];
            sum += term;
            magnitude += std::fabs(term);
        }
        const double bj = in.b2[static_cast<std::size_t>(j)];
        const double want = std::max(sum + bj, 0.0);
        magnitude += std::fabs(bj);
        const double got = y[static_cast<std::size_t>(i) * kN + static_cast<std::size_t>(j)];
        // Generous: on top of the layer-2 reduction's own rounding, the layer-1 F32 hidden value
        // (recomputed here in exact double) carries its own rounding, which propagates through W2.
        const double tol = (kK + kHid) * 2.4e-7 * magnitude;
        if (!(std::fabs(got - want) <= tol)) {
            state.SkipWithError("bench_ml_layers: mlp2 nest output is outside the two-layer rounding bound");
            return;
        }
        max_rel = std::max(max_rel, std::fabs(got - want) / std::max(magnitude, 1e-30));
    }
    state.counters["max_rel_err"] = max_rel;
    for (auto _ : state) {
        heinzel_bench::run<mlp2_nest, false>(feeds, work);
        float first = y[0];
        benchmark::DoNotOptimize(first);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * (2 * kM * kK * kHid + 2 * kM * kHid * kN));
}

// hand: ported from ctg3 ml_mlp2_hand, over the same F32 data as the nest arm.
void bench_mlp2_hand(benchmark::State& state) {
    const Mlp2Input in = mlp2_input();
    OutputVector<float, 1> h(static_cast<std::size_t>(kM) * kHid);
    OutputVector<float> y(static_cast<std::size_t>(kM) * kN);
    auto hidden_ref = [&](int i, int hh) {
        double s = in.b1[static_cast<std::size_t>(hh)];
        for (int k = 0; k < kK; ++k) { s += static_cast<double>(in.x[static_cast<std::size_t>(i) * kK + static_cast<std::size_t>(k)]) * in.w1[static_cast<std::size_t>(k) * kHid + static_cast<std::size_t>(hh)]; }
        return s < 0 ? 0.0 : s;
    };
    auto ref = [&](int i, int j) {
        double s = in.b2[static_cast<std::size_t>(j)];
        for (int hh = 0; hh < kHid; ++hh) { s += hidden_ref(i, hh) * in.w2[static_cast<std::size_t>(hh) * kN + static_cast<std::size_t>(j)]; }
        return s < 0 ? 0.0 : s;
    };
    auto run = [&] {
        for (int i = 0; i < kM; ++i) {
            for (int j = 0; j < kHid; ++j) { h[static_cast<std::size_t>(i) * kHid + static_cast<std::size_t>(j)] = in.b1[static_cast<std::size_t>(j)]; }
            for (int k = 0; k < kK; ++k) {
                const float xv = in.x[static_cast<std::size_t>(i) * kK + static_cast<std::size_t>(k)];
                for (int j = 0; j < kHid; ++j) { h[static_cast<std::size_t>(i) * kHid + static_cast<std::size_t>(j)] += xv * in.w1[static_cast<std::size_t>(k) * kHid + static_cast<std::size_t>(j)]; }
            }
            for (int j = 0; j < kHid; ++j) {
                const std::size_t idx = static_cast<std::size_t>(i) * kHid + static_cast<std::size_t>(j);
                h[idx] = h[idx] < 0 ? 0.0f : h[idx];
            }
        }
        for (int i = 0; i < kM; ++i) {
            for (int j = 0; j < kN; ++j) { y[static_cast<std::size_t>(i) * kN + static_cast<std::size_t>(j)] = in.b2[static_cast<std::size_t>(j)]; }
            for (int k = 0; k < kHid; ++k) {
                const float hv = h[static_cast<std::size_t>(i) * kHid + static_cast<std::size_t>(k)];
                for (int j = 0; j < kN; ++j) { y[static_cast<std::size_t>(i) * kN + static_cast<std::size_t>(j)] += hv * in.w2[static_cast<std::size_t>(k) * kN + static_cast<std::size_t>(j)]; }
            }
            for (int j = 0; j < kN; ++j) {
                const std::size_t idx = static_cast<std::size_t>(i) * kN + static_cast<std::size_t>(j);
                y[idx] = y[idx] < 0 ? 0.0f : y[idx];
            }
        }
    };
    run();
    for (const auto [i, j] : {std::pair{0, 0}, std::pair{kM / 2, kN / 2}, std::pair{kM - 1, kN - 1}}) {
        if (!(rel_err(y[static_cast<std::size_t>(i) * kN + static_cast<std::size_t>(j)], ref(i, j)) <= 1e-2)) {
            state.SkipWithError("bench_ml_layers: mlp2 hand disagrees with the reference"); return;
        }
    }
    for (auto _ : state) {
        benchmark::ClobberMemory();
        run();
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * 2 * kM * kK * kHid + static_cast<std::int64_t>(state.iterations()) * 2 * kM * kHid * kN);
}

} // namespace

BENCHMARK(bench_linear_relu_nest)->Name("ml/linear_relu/heinzel")->UseRealTime();
BENCHMARK(bench_linear_relu_naive)->Name("ml/linear_relu/naive")->UseRealTime();
BENCHMARK(bench_linear_relu_hand)->Name("ml/linear_relu/hand")->UseRealTime();
BENCHMARK(bench_mlp2_nest)->Name("ml/mlp2/heinzel")->UseRealTime();
BENCHMARK(bench_mlp2_hand)->Name("ml/mlp2/hand")->UseRealTime();

BENCHMARK_MAIN();
