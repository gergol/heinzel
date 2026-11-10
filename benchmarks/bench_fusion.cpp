// Fusion (producer-into-stencil) benchmark — the rewrite arm, re-expressed against the NEW API.
//
// Ported from ctg3 benchmarks/fusion_bench.cpp. Two workloads:
//   conv : p = x*x ; y[i] = Σ_k p[i+k]·h[k]              (1-D, N ∈ {65536,262144,1048576}, K ∈ {16,32})
//   blur : p = x*x ; y[i,j] = Σ_k p[i+k,j]·w[k]          (vertical, (H,W,K) three sizes at 16 MB p)
//
// **The fuse-vs-materialize distinction is DEFERRED** (a finding): the old bench's headline is a
// SCHEDULING choice — Heinzel `compute_at`-fuses the square producer into the stencil vs a materialized
// two-pass round-trip — decided by a roofline cost model (`TargetInfo.compute_rate/mem_bandwidth/
// l3_bandwidth`). The nest arms (`heinzel`, F32) run what the planner decides: the window folded into
// its reader's access, lanes across the outputs.
//
// MAPPING (graphs verbatim from tests/parity/fusion_here.cpp and tests/parity/vblur_here.cpp): the
// signal and the image are tensors. conv: x{N}, h{1,K}; unfold the data axis → {N-K+1,K}; reduce Sum over
// the window axis (1). blur: x{H,W}, w{1,K,1}; unfold the row axis → {H-K+1,K,W}; reduce Sum over axis 1
// → {H-K+1,W}.
//
// `hand-fused` / `hand-mat` are hand C++ loops standing in for ctg3's Heinzel-executor `fused` /
// `hz_mat` arms: since the rewrite has no fuse↔materialize schedule decision to pin (above), a
// hand-written loop is the closest like-for-like substitute — hand-fused recomputes p on the fly
// (no p buffer, exactly ctg3's `fused` semantics: p[i+k] recomputed once per consuming output),
// hand-mat writes p to a buffer once then reads it back (ctg3's `bench_mat`/two-pass baseline).
// Ported F32, same data/shapes as the nest arms.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include "page_vector.hpp"
#include "parallel_arm.hpp"

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/graph/views.hpp"
#include "heinzel/loops/lower.hpp"
#include "heinzel/loops/schedule.hpp"
#include "heinzel/sema/analyze.hpp"

using heinzel_bench::PageVector;
using heinzel_bench::OutputVector;

namespace hz = heinzel;

namespace {

// The data, shared by the nest and the hand arms.
PageVector<float> makef(std::size_t n, float bias) {
    PageVector<float> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = 0.001f * static_cast<float>(i % 1000) - bias;
    }
    return v;
}

double rel_err(double got, double ref) { return std::fabs(got - ref) / std::max(std::fabs(ref), 1e-30); }

// ---- the nest arms: 1-D conv  p = x*x ; y[i] = Σ_k p[i+k]·h[k],  and vertical blur  p = x*x ;
// y[i,j] = Σ_k p[i+k,j]·w[k],  in F32 (as the hand arms and the old tree's arms are), planned for this
// machine. The window is no statement and has no buffer (`loops/graph/views.hpp`); the fold over
// the taps runs in lanes across the outputs. No kernel: the same planner that lays out a matmul.
template <int N, int K>
constexpr hz::Graph kConvF32 = [] {
    hz::Input x{"x", {N}};
    hz::Input h{"h", {1, K}};
    hz::Expr y = ((x * x).unfold(0, K) * h).sum(1);
    hz::Graph g = hz::finish(hz::Output{"y"} = y);
    if (!hz::infer(g).ok) { hz::fail("bench_fusion: f32 conv graph did not infer"); }
    return g;
}();
template <int N, int K>
constexpr const hz::loops::Program& kConvNest = heinzel_bench::kProgramOf<kConvF32<N, K>, heinzel_bench::kMachine>;

// The two ends of the fuse-vs-materialize choice, authored, beside the planner's own (`heinzel`):
// `heinzel-stored` computes the square once into a buffer (planned for a machine on which recomputing
// never pays), `heinzel-inlined` computes it inside the window's reader, once per tap, storing nothing
// (`Schedule::Stage::through`). The planner's arm has to be level with the better of the two.
constexpr hz::Machine kAlwaysStores = [] {
    hz::Machine m = heinzel_bench::kMachine;
    m.recompute_always = m.recompute_if_uncached = -1;
    m.band_bytes = 0; // stored whole: no band either
    return m;
}();
// The stored arm's schedule with its two stages made one — the square's nodes inlined into the window's reader,
// which keeps its loops — and lowered. Held as the one member of a struct, so the runner's template
// argument names a subobject (`CLAUDE.md`: a whole `constexpr` program costs clang's code generation dearly).
struct Inlined {
    hz::loops::Program program;
};
constexpr Inlined inlined(const hz::Graph& g) {
    const auto stored = hz::loops::planner::schedule_of(g, kAlwaysStores);
    if (!stored) {
        hz::fail(stored.error().view());
    }
    hz::loops::Schedule::Stage reader = stored->stages[1];
    auto& members = reader.nodes;
    members = stored->stages[0].nodes;
    for (const std::int32_t ni : stored->stages[1].nodes) {
        members.push_back(ni);
    }
    reader.inlined = static_cast<std::int32_t>(stored->stages[0].nodes.size());
    for (std::size_t ni = 0; ni < g.nodes.size(); ++ni) {
        reader.through = hz::loops::graph::folds_into_its_readers(g.nodes[ni]) ? static_cast<std::int32_t>(ni) : reader.through;
    }
    hz::loops::Schedule d{};
    d.stages.push_back(reader);
    return Inlined{hz::loops::lower(g, d)};
}
template <int N, int K>
constexpr const hz::loops::Program& kConvStored = heinzel_bench::kProgramOf<kConvF32<N, K>, kAlwaysStores>;
template <int N, int K>
constexpr Inlined kConvInlinedHeld = inlined(kConvF32<N, K>);
template <int N, int K>
constexpr const hz::loops::Program& kConvInlined = kConvInlinedHeld<N, K>.program;

template <int H, int Wd, int K>
constexpr hz::Graph kBlurF32 = [] {
    hz::Input x{"x", {H, Wd}};
    hz::Input w{"w", {1, K, 1}};
    hz::Expr y = ((x * x).unfold(0, K) * w).sum(1);
    hz::Graph g = hz::finish(hz::Output{"y"} = y);
    if (!hz::infer(g).ok) { hz::fail("bench_fusion: f32 blur graph did not infer"); }
    return g;
}();
template <int H, int Wd, int K>
constexpr const hz::loops::Program& kBlurNest = heinzel_bench::kProgramOf<kBlurF32<H, Wd, K>, heinzel_bench::kMachine>;
template <int H, int Wd, int K>
constexpr const hz::loops::Program& kBlurStored = heinzel_bench::kProgramOf<kBlurF32<H, Wd, K>, kAlwaysStores>;
template <int H, int Wd, int K>
constexpr Inlined kBlurInlinedHeld = inlined(kBlurF32<H, Wd, K>);
template <int H, int Wd, int K>
constexpr const hz::loops::Program& kBlurInlined = kBlurInlinedHeld<H, Wd, K>.program;

// Run once, check samples against float64 relative to the terms' magnitudes, then time. `ref(at)`
// returns {sum, sum of |terms|} of output element `at`.
template <const hz::loops::Program& P, bool Par>
void bench_window_nest(benchmark::State& state, const hz::loops::Feeds& feeds, std::int64_t outputs, int taps, auto ref) {
    hz::loops::Workspace<P, heinzel_bench::copies<Par>()> work(hz::loops::Sizes{});
    heinzel_bench::run<P, Par>(feeds, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(P.outputs[0]));
    double max_rel = 0.0;
    for (const std::int64_t at : {std::int64_t{0}, outputs / 2, outputs - 1}) {
        const auto [want, magnitude] = ref(at);
        const double off = std::fabs(static_cast<double>(y[at]) - want);
        if (!(off <= taps * 2.4e-7 * magnitude)) {
            state.SkipWithError("bench_fusion: the nest output is outside a float sum's rounding bound");
            return;
        }
        max_rel = std::max(max_rel, off / std::max(magnitude, 1e-30));
    }
    state.counters["max_rel_err"] = max_rel;
    if (Par && !heinzel_bench::same_as_serial<P>(feeds, work)) {
        state.SkipWithError("bench_fusion: the split run differs from the serial run");
        return;
    }
    for (auto _ : state) {
        heinzel_bench::run<P, Par>(feeds, work);
        float first = y[0];
        benchmark::DoNotOptimize(first);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * outputs);
}

template <const hz::loops::Program& P, int N, int K, bool Par = false>
void bench_conv_nest(benchmark::State& state) {
    const PageVector<float> x = makef(N, 0.6f);
    const PageVector<float> h = makef(K, 0.3f);
    bench_window_nest<P, Par>(state, hz::loops::Feeds{x.data(), h.data()}, N - K + 1, K, [&](std::int64_t i) {
        double sum = 0.0;
        double magnitude = 0.0;
        for (int k = 0; k < K; ++k) {
            const double p = static_cast<double>(x[static_cast<std::size_t>(i + k)]) * x[static_cast<std::size_t>(i + k)];
            sum += p * h[static_cast<std::size_t>(k)];
            magnitude += std::fabs(p * h[static_cast<std::size_t>(k)]);
        }
        return std::pair{sum, magnitude};
    });
}

template <const hz::loops::Program& P, int H, int Wd, int K>
void bench_blur_nest(benchmark::State& state) {
    const PageVector<float> x = makef(static_cast<std::size_t>(H) * Wd, 0.5f);
    const PageVector<float> w = makef(K, 0.25f);
    bench_window_nest<P, false>(state, hz::loops::Feeds{x.data(), w.data()}, static_cast<std::int64_t>(H - K + 1) * Wd, K, [&](std::int64_t at) {
        double sum = 0.0;
        double magnitude = 0.0;
        for (int k = 0; k < K; ++k) {
            const double p = static_cast<double>(x[static_cast<std::size_t>(at + k * Wd)]) * x[static_cast<std::size_t>(at + k * Wd)];
            sum += p * w[static_cast<std::size_t>(k)];
            magnitude += std::fabs(p * w[static_cast<std::size_t>(k)]);
        }
        return std::pair{sum, magnitude};
    });
}

// hand-fused: p recomputed on the fly, no buffer — ctg3's `fused` semantics by hand.
template <int N, int K>
void bench_conv_hand_fused(benchmark::State& state) {
    const PageVector<float> x = makef(N, 0.6f);
    const PageVector<float> h = makef(K, 0.3f);
    const int frames = N - K + 1;
    OutputVector<float> y(static_cast<std::size_t>(frames));
    auto run = [&] {
        for (int i = 0; i < frames; ++i) {
            float acc = 0.0f;
            for (int k = 0; k < K; ++k) {
                const float pv = x[static_cast<std::size_t>(i + k)];
                acc += pv * pv * h[static_cast<std::size_t>(k)];
            }
            y[static_cast<std::size_t>(i)] = acc;
        }
    };
    run();
    auto ref = [&](int i) { double s = 0.0; for (int k = 0; k < K; ++k) { const double pv = x[static_cast<std::size_t>(i + k)]; s += pv * pv * h[static_cast<std::size_t>(k)]; } return s; };
    for (const int f : {0, frames / 2, frames - 1}) {
        if (!(rel_err(y[static_cast<std::size_t>(f)], ref(f)) <= 1e-4)) { state.SkipWithError("bench_fusion: conv hand-fused disagrees with the reference"); return; }
    }
    for (auto _ : state) {
        benchmark::ClobberMemory();
        run();
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * frames);
}

// hand-mat: p materialized to a buffer once, then read back — ctg3's two-pass `bench_mat` by hand.
template <int N, int K>
void bench_conv_hand_mat(benchmark::State& state) {
    const PageVector<float> x = makef(N, 0.6f);
    const PageVector<float> h = makef(K, 0.3f);
    const int frames = N - K + 1;
    OutputVector<float, 1> p(N);
    OutputVector<float> y(static_cast<std::size_t>(frames));
    auto run = [&] {
        for (int j = 0; j < N; ++j) { p[static_cast<std::size_t>(j)] = x[static_cast<std::size_t>(j)] * x[static_cast<std::size_t>(j)]; }
        for (int i = 0; i < frames; ++i) {
            float acc = 0.0f;
            for (int k = 0; k < K; ++k) { acc += p[static_cast<std::size_t>(i + k)] * h[static_cast<std::size_t>(k)]; }
            y[static_cast<std::size_t>(i)] = acc;
        }
    };
    run();
    auto ref = [&](int i) { double s = 0.0; for (int k = 0; k < K; ++k) { const double pv = x[static_cast<std::size_t>(i + k)]; s += pv * pv * h[static_cast<std::size_t>(k)]; } return s; };
    for (const int f : {0, frames / 2, frames - 1}) {
        if (!(rel_err(y[static_cast<std::size_t>(f)], ref(f)) <= 1e-4)) { state.SkipWithError("bench_fusion: conv hand-mat disagrees with the reference"); return; }
    }
    for (auto _ : state) {
        benchmark::ClobberMemory();
        run();
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * frames);
}

// blur hand-fused / hand-mat: same idea, over rows.
template <int H, int Wd, int K>
void bench_blur_hand_fused(benchmark::State& state) {
    const PageVector<float> x = makef(static_cast<std::size_t>(H) * Wd, 0.5f);
    const PageVector<float> w = makef(K, 0.25f);
    const int oh = H - K + 1;
    OutputVector<float> y(static_cast<std::size_t>(oh) * Wd);
    auto run = [&] {
        for (int i = 0; i < oh; ++i) {
            for (int j = 0; j < Wd; ++j) {
                float acc = 0.0f;
                for (int k = 0; k < K; ++k) {
                    const float pv = x[static_cast<std::size_t>(i + k) * Wd + static_cast<std::size_t>(j)];
                    acc += pv * pv * w[static_cast<std::size_t>(k)];
                }
                y[static_cast<std::size_t>(i) * Wd + static_cast<std::size_t>(j)] = acc;
            }
        }
    };
    run();
    auto ref = [&](int i, int j) { double s = 0.0; for (int k = 0; k < K; ++k) { const double pv = x[static_cast<std::size_t>(i + k) * Wd + static_cast<std::size_t>(j)]; s += pv * pv * w[static_cast<std::size_t>(k)]; } return s; };
    for (const auto ij : {std::pair<int,int>{0,0}, std::pair<int,int>{oh/2, Wd/2}, std::pair<int,int>{oh-1, Wd-1}}) {
        if (!(rel_err(y[static_cast<std::size_t>(ij.first) * Wd + static_cast<std::size_t>(ij.second)], ref(ij.first, ij.second)) <= 1e-4)) {
            state.SkipWithError("bench_fusion: blur hand-fused disagrees with the reference"); return;
        }
    }
    for (auto _ : state) {
        benchmark::ClobberMemory();
        run();
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(oh) * Wd);
}
template <int H, int Wd, int K>
void bench_blur_hand_mat(benchmark::State& state) {
    const PageVector<float> x = makef(static_cast<std::size_t>(H) * Wd, 0.5f);
    const PageVector<float> w = makef(K, 0.25f);
    const int oh = H - K + 1;
    OutputVector<float, 1> p(static_cast<std::size_t>(H) * Wd);
    OutputVector<float> y(static_cast<std::size_t>(oh) * Wd);
    auto run = [&] {
        for (std::size_t j = 0; j < p.size(); ++j) { p[j] = x[j] * x[j]; }
        for (int i = 0; i < oh; ++i) {
            for (int j = 0; j < Wd; ++j) {
                float acc = 0.0f;
                for (int k = 0; k < K; ++k) { acc += p[static_cast<std::size_t>(i + k) * Wd + static_cast<std::size_t>(j)] * w[static_cast<std::size_t>(k)]; }
                y[static_cast<std::size_t>(i) * Wd + static_cast<std::size_t>(j)] = acc;
            }
        }
    };
    run();
    auto ref = [&](int i, int j) { double s = 0.0; for (int k = 0; k < K; ++k) { const double pv = x[static_cast<std::size_t>(i + k) * Wd + static_cast<std::size_t>(j)]; s += pv * pv * w[static_cast<std::size_t>(k)]; } return s; };
    for (const auto ij : {std::pair<int,int>{0,0}, std::pair<int,int>{oh/2, Wd/2}, std::pair<int,int>{oh-1, Wd-1}}) {
        if (!(rel_err(y[static_cast<std::size_t>(ij.first) * Wd + static_cast<std::size_t>(ij.second)], ref(ij.first, ij.second)) <= 1e-4)) {
            state.SkipWithError("bench_fusion: blur hand-mat disagrees with the reference"); return;
        }
    }
    for (auto _ : state) {
        benchmark::ClobberMemory();
        run();
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(oh) * Wd);
}

} // namespace

#define HEINZEL_CONV_NEST(N, K)                                                                                            \
    BENCHMARK(bench_conv_nest<kConvNest<N, K>, N, K>)->Name("conv/heinzel/N" #N "_K" #K)->UseRealTime();                   \
    BENCHMARK(bench_conv_nest<kConvStored<N, K>, N, K>)->Name("conv/heinzel-stored/N" #N "_K" #K)->UseRealTime();          \
    BENCHMARK(bench_conv_nest<kConvInlined<N, K>, N, K>)->Name("conv/heinzel-inlined/N" #N "_K" #K)->UseRealTime();
HEINZEL_CONV_NEST(65536, 16)
HEINZEL_CONV_NEST(262144, 16)
HEINZEL_CONV_NEST(1048576, 16)
HEINZEL_CONV_NEST(1048576, 32)
BENCHMARK((bench_conv_nest<kConvNest<1048576, 16>, 1048576, 16, true>))->Name("conv/heinzel-par/N1048576_K16")->UseRealTime();
BENCHMARK((bench_conv_nest<kConvNest<1048576, 32>, 1048576, 32, true>))->Name("conv/heinzel-par/N1048576_K32")->UseRealTime();
// (few taps, and a buffer at exactly half the last-level cache: where storing used to be chosen and lost)
HEINZEL_CONV_NEST(1048576, 4)
HEINZEL_CONV_NEST(2097152, 16)

#define HEINZEL_CONV_HAND(N, K)                                                                                           \
    BENCHMARK(bench_conv_hand_fused<N, K>)->Name("conv/hand-fused/N" #N "_K" #K)->UseRealTime();                           \
    BENCHMARK(bench_conv_hand_mat<N, K>)->Name("conv/hand-mat/N" #N "_K" #K)->UseRealTime();
HEINZEL_CONV_HAND(65536, 16)
HEINZEL_CONV_HAND(262144, 16)
HEINZEL_CONV_HAND(1048576, 16)
HEINZEL_CONV_HAND(1048576, 32)

#define HEINZEL_BLUR_NEST(H, Wd, K)                                                                                        \
    BENCHMARK(bench_blur_nest<kBlurNest<H, Wd, K>, H, Wd, K>)->Name("blur/heinzel/H" #H "_W" #Wd)->UseRealTime();           \
    BENCHMARK(bench_blur_nest<kBlurStored<H, Wd, K>, H, Wd, K>)->Name("blur/heinzel-stored/H" #H "_W" #Wd)->UseRealTime();  \
    BENCHMARK(bench_blur_nest<kBlurInlined<H, Wd, K>, H, Wd, K>)->Name("blur/heinzel-inlined/H" #H "_W" #Wd)->UseRealTime();
HEINZEL_BLUR_NEST(16384, 256, 3)
HEINZEL_BLUR_NEST(8192, 512, 3)
HEINZEL_BLUR_NEST(4096, 1024, 3)
HEINZEL_BLUR_NEST(4096, 256, 3)

#define HEINZEL_BLUR_HAND(H, Wd, K)                                                                                       \
    BENCHMARK(bench_blur_hand_fused<H, Wd, K>)->Name("blur/hand-fused/H" #H "_W" #Wd)->UseRealTime();                      \
    BENCHMARK(bench_blur_hand_mat<H, Wd, K>)->Name("blur/hand-mat/H" #H "_W" #Wd)->UseRealTime();
HEINZEL_BLUR_HAND(16384, 256, 3)
HEINZEL_BLUR_HAND(8192, 512, 3)
HEINZEL_BLUR_HAND(4096, 1024, 3)

BENCHMARK_MAIN();
