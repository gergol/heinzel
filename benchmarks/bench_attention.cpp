// Attention benchmark — the rewrite arm, re-expressed against the NEW API.
//
// Ported from ctg3 benchmarks/attention_bench.cpp. The workload is single-head scaled dot-product
// self-attention  o = softmax_k( (Q·Kᵀ)·s )·V  with Q,K,V,o all [N,D], swept over the same sequence
// lengths N ∈ {32,64,128,256,512} at head dim D = 64. `heinzel` is the nest path (F32: two contractions,
// two folds and the maps between them, `exp` in float — no attention kernel). The old-tree large-N
// simd-only points (2048/4096) are deferred to "expand later".
//
// MAPPING to the rewrite's streaming model (taken VERBATIM from the proven graph in
// tests/unit/attention_test.cpp): each operand is a tensor {N,D}, run as one sample. The score contraction is
// spelled einsum("qd,kd->qk") — this contracts the feature axis d and yields Q·Kᵀ directly, so no
// explicit transpose op is needed (the old tree's `transpose(k, Perm{1,0})` is folded into the einsum
// index string). The stable softmax is the keepdims max/exp/sum pipeline over the key axis (Axes{1}),
// and the value contraction is einsum("qk,kd->qd"). The scale s = 1/sqrt(D) = 1/8 (D=64) is exactly
// representable and folded in as a constant, matching the attention_test spelling.
//
// `hand` is float, ported from ctg3's `attention_ref` — the textbook per-query two-pass loop a
// non-expert writes (dot-product scores, scalar exp, then a weighted sum over V), over the same sizes
// and generator as the nest arm.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

// The generator for every arm (ported from ctg3 attention_bench.cpp `make`).
PageVector<float> makef(std::size_t n, float seed) {
    PageVector<float> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = 0.001f * static_cast<float>(i % 1000) - seed;
    }
    return v;
}

// attention_ref, ported verbatim from ctg3 tests/test_graphs.hpp: the textbook per-query two-pass
// (dot-product scores, scalar exp, weighted sum over V), float throughout.
template <int N, int D>
void attention_ref(const float* q, const float* k, const float* v, float scale, float* o) {
    for (int qi = 0; qi < N; ++qi) {
        float score[N];
        float mx = -1e30f;
        for (int ki = 0; ki < N; ++ki) {
            float dot = 0.0f;
            for (int d = 0; d < D; ++d) { dot += q[qi * D + d] * k[ki * D + d]; }
            score[ki] = dot * scale;
            mx = score[ki] > mx ? score[ki] : mx;
        }
        float sum = 0.0f;
        for (int ki = 0; ki < N; ++ki) {
            score[ki] = std::exp(score[ki] - mx);
            sum += score[ki];
        }
        for (int d = 0; d < D; ++d) {
            float acc = 0.0f;
            for (int ki = 0; ki < N; ++ki) { acc += (score[ki] / sum) * v[ki * D + d]; }
            o[qi * D + d] = acc;
        }
    }
}

// Independent double oracle: the textbook per-query two-pass attention (as attention_test's reference).
template <int N, int D>
void reference(const std::vector<double>& q, const std::vector<double>& k, const std::vector<double>& v,
               std::vector<double>& out) {
    const double scale = 1.0 / 8.0;
    for (int qi = 0; qi < N; ++qi) {
        std::vector<double> score(static_cast<std::size_t>(N));
        double mx = -1e300;
        for (int ki = 0; ki < N; ++ki) {
            double dot = 0.0;
            for (int d = 0; d < D; ++d) {
                dot += q[static_cast<std::size_t>(qi) * D + d] * k[static_cast<std::size_t>(ki) * D + d];
            }
            score[static_cast<std::size_t>(ki)] = dot * scale;
            if (score[static_cast<std::size_t>(ki)] > mx) { mx = score[static_cast<std::size_t>(ki)]; }
        }
        double sum = 0.0;
        for (int ki = 0; ki < N; ++ki) {
            score[static_cast<std::size_t>(ki)] = std::exp(score[static_cast<std::size_t>(ki)] - mx);
            sum += score[static_cast<std::size_t>(ki)];
        }
        for (int d = 0; d < D; ++d) {
            double acc = 0.0;
            for (int ki = 0; ki < N; ++ki) {
                acc += (score[static_cast<std::size_t>(ki)] / sum) * v[static_cast<std::size_t>(ki) * D + d];
            }
            out[static_cast<std::size_t>(qi) * D + d] = acc;
        }
    }
}

// o = softmax_k( (Q·Kᵀ)·s )·V, one sample (length-1 stream), graph structure from
// tests/unit/attention_test.cpp. The nest arm: F32 (as the hand arm and the old tree's arm are), planned
// for this machine. No attention kernel: two contractions, two folds and the maps between them, `exp` in
// float.
template <int N, int D>
constexpr hz::Graph kAttnF32 = [] {
    hz::Input Q{"Q", {N, D}};
    hz::Input K{"K", {N, D}};
    hz::Input V{"V", {N, D}};
    hz::Expr scores = hz::einsum("qd,kd->qk", Q, K) * (1.0 / 8.0);
    hz::Expr e = hz::exp(scores - scores.max(1, hz::keepdims));
    hz::Expr attn = e / e.sum(1, hz::keepdims);
    hz::Graph g = hz::finish(hz::Output{"out"} = hz::einsum("qk,kd->qd", attn, V));
    if (!hz::infer(g).ok) { hz::fail("bench_attention: the f32 attention graph did not infer"); }
    return g;
}();
template <int N, int D>
constexpr const hz::loops::Program& kAttnNest = heinzel_bench::kProgramOf<kAttnF32<N, D>, heinzel_bench::kMachine>;

template <int N, int D, bool Par = false>
void bench_attention_nest(benchmark::State& state) {
    const PageVector<float> Q = makef(static_cast<std::size_t>(N) * D, 0.5f);
    const PageVector<float> K = makef(static_cast<std::size_t>(N) * D, 0.3f);
    const PageVector<float> V = makef(static_cast<std::size_t>(N) * D, 0.1f);
    hz::loops::Workspace<kAttnNest<N, D>, heinzel_bench::copies<Par>()> work(hz::loops::Sizes{});
    const hz::loops::Feeds feeds{Q.data(), K.data(), V.data()};
    heinzel_bench::run<kAttnNest<N, D>, Par>(feeds, work);
    const float* o = work.template data<float>(static_cast<std::size_t>(kAttnNest<N, D>.outputs[0]));
    std::vector<double> ref(static_cast<std::size_t>(N) * D);
    reference<N, D>(std::vector<double>(Q.begin(), Q.end()), std::vector<double>(K.begin(), K.end()),
                    std::vector<double>(V.begin(), V.end()), ref);
    double max_rel = 0.0;
    for (const int idx : {0, (N * D) / 2, N * D - 1}) {
        const double r = ref[static_cast<std::size_t>(idx)];
        const double off = std::fabs(o[idx] - r);
        if (!(off <= 1e-4 * (1.0 + std::fabs(r)))) {
            state.SkipWithError("bench_attention: the nest attention disagrees with the reference");
            return;
        }
        max_rel = std::max(max_rel, off / (1.0 + std::fabs(r)));
    }
    state.counters["max_rel_err"] = max_rel;
    if (Par && !heinzel_bench::same_as_serial<kAttnNest<N, D>>(feeds, work)) {
        state.SkipWithError("bench_attention: the split run differs from the serial run");
        return;
    }
    for (auto _ : state) {
        heinzel_bench::run<kAttnNest<N, D>, Par>(feeds, work);
        float first = o[0];
        benchmark::DoNotOptimize(first);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * 4 * N * N * D);
}

// hand: attention_ref, float, ported from ctg3 attention_bench.cpp's `bench_hand`.
template <int N, int D>
void bench_attention_hand(benchmark::State& state) {
    const PageVector<float> Q = makef(static_cast<std::size_t>(N) * D, 0.5f);
    const PageVector<float> K = makef(static_cast<std::size_t>(N) * D, 0.3f);
    const PageVector<float> V = makef(static_cast<std::size_t>(N) * D, 0.1f);
    OutputVector<float> o(static_cast<std::size_t>(N) * D);
    const float inv = 1.0f / std::sqrt(static_cast<float>(D));

    // Sanity check against the double reference before timing.
    attention_ref<N, D>(Q.data(), K.data(), V.data(), inv, o.data());
    std::vector<double> Qd(Q.begin(), Q.end());
    std::vector<double> Kd(K.begin(), K.end());
    std::vector<double> Vd(V.begin(), V.end());
    std::vector<double> ref(static_cast<std::size_t>(N) * D);
    reference<N, D>(Qd, Kd, Vd, ref);
    for (const int idx : {0, (N * D) / 2, N * D - 1}) {
        const double r = ref[static_cast<std::size_t>(idx)];
        if (!(std::fabs(o[static_cast<std::size_t>(idx)] - r) <= 1e-3 * (1.0 + std::fabs(r)))) {
            state.SkipWithError("bench_attention: hand disagrees with the reference");
            return;
        }
    }
    for (auto _ : state) {
        attention_ref<N, D>(Q.data(), K.data(), V.data(), inv, o.data());
        benchmark::DoNotOptimize(o.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * 4 * N * N * D);
}

} // namespace

#define HEINZEL_ATTN(N)                                                                                                    \
    BENCHMARK(bench_attention_nest<N, 64>)->Name("attn/heinzel/" #N "x64")->UseRealTime();                                 \
    BENCHMARK((bench_attention_nest<N, 64, true>))->Name("attn/heinzel-par/" #N "x64")->UseRealTime();                     \
    BENCHMARK(bench_attention_hand<N, 64>)->Name("attn/hand/" #N "x64")->UseRealTime();
HEINZEL_ATTN(32)
HEINZEL_ATTN(64)
HEINZEL_ATTN(128)
HEINZEL_ATTN(256)
HEINZEL_ATTN(512)

BENCHMARK_MAIN();
