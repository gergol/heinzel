// Order-statistics (sliding median) benchmark — the rewrite arm, re-expressed against the NEW API.
//
// Ported from ctg3 benchmarks/order_stats_bench.cpp. Workload: a sliding median filter over an N=1<<20
// signal at window W ∈ {3,5,9}. `heinzel` is the nest path (F32, `.median(1)`). `hand` is ctg3's
// hard-coded branchless compare-exchange network (`hand_net3/5/9`); `std-nth_element` is ctg3's
// `hand_nth` (std::nth_element into a per-window scratch copy) — both ported verbatim, F32, over the
// same signal as the nest arm.
//
// MAPPING (graph verbatim from tests/parity/order_stats_here.cpp): the length-N signal is a tensor,
// x{N}; `x.unfold(axis=0, window=W, hop=1)` turns the data axis into (frame, window)
// → {N-W+1, W}, then an `op::Order` Quantile at q=0.5 over the window axis (axis 1) drops it to
// {N-W+1} — the exact middle element (W odd ⇒ integral quantile index ⇒ bit-exact selection, no
// arithmetic). Same selection as the old tree's `median_filter(x,W)`.

#include <algorithm>
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

constexpr int N = 1 << 20; // ~1M samples — out of L2, streaming regime (the old bench's N)

// The signal, shared by every arm.
PageVector<float> make_signal_f32() {
    PageVector<float> x(static_cast<std::size_t>(N));
    unsigned s = 0x9e3779b9u;
    for (auto& v : x) {
        s = s * 1664525u + 1013904223u;
        v = static_cast<float>(s >> 8) * (1.0f / 8388608.0f) - 1.0f; // spiky, so ordering matters
    }
    return x;
}

// The nest arm: the same filter in F32 (as the hand arms and the old tree's arm are), spelled with
// the `median` member, planned for this machine. The window is no buffer, the order statistic gathers its
// taps and selects the middle rank with the generated compare-exchange network, in lanes across outputs.
template <int W>
constexpr hz::Graph kMedF32 = [] {
    hz::Input x{"x", {N}};
    hz::Graph g = hz::finish(hz::Output{"y"} = x.unfold(0, W).median(1));
    if (!hz::infer(g).ok) { hz::fail("bench_order_stats: the f32 median graph did not infer"); }
    return g;
}();
template <int W>
constexpr const hz::loops::Program& kMedNest = heinzel_bench::kProgramOf<kMedF32<W>, heinzel_bench::kMachine>;

template <int W>
void bench_median_nest(benchmark::State& state) {
    const PageVector<float> x = make_signal_f32();
    const std::size_t n = static_cast<std::size_t>(N - W + 1);
    hz::loops::Workspace<kMedNest<W>> work(hz::loops::Sizes{});
    const hz::loops::Feeds feeds{x.data()};
    heinzel_bench::run<kMedNest<W>, false>(feeds, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(kMedNest<W>.outputs[0]));
    for (const std::size_t f : {std::size_t{0}, n / 2, n - 1}) {
        float buf[W];
        for (int k = 0; k < W; ++k) { buf[k] = x[f + static_cast<std::size_t>(k)]; }
        std::nth_element(buf, buf + W / 2, buf + W);
        if (!(y[f] == buf[W / 2])) {
            state.SkipWithError("bench_order_stats: the nest median disagrees with the reference");
            return;
        }
    }
    state.counters["max_rel_err"] = 0.0; // a selection: the sampled outputs above are the input's own values
    for (auto _ : state) {
        heinzel_bench::run<kMedNest<W>, false>(feeds, work);
        float first = y[0];
        benchmark::DoNotOptimize(first);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(n));
}

// hand: ctg3's hard-coded branchless compare-exchange networks, ported verbatim (float).
#define CE(a, b)                                                                                                           \
    do {                                                                                                                   \
        const float lo = a < b ? a : b, hi = a < b ? b : a;                                                                \
        a = lo;                                                                                                            \
        b = hi;                                                                                                            \
    } while (0)

void hand_net3(const float* __restrict x, float* __restrict y, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        float a = x[i];
        float b = x[i + 1];
        float c = x[i + 2];
        CE(a, b); CE(b, c); CE(a, b);
        y[i] = b;
    }
}
void hand_net5(const float* __restrict x, float* __restrict y, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        float a = x[i];
        float b = x[i + 1];
        float c = x[i + 2];
        float d = x[i + 3];
        float e = x[i + 4];
        CE(a, b); CE(c, d); CE(a, c); CE(b, d); CE(c, e); CE(b, c); CE(c, e);
        y[i] = c;
    }
}
void hand_net9(const float* __restrict x, float* __restrict y, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        float v[9];
        for (int k = 0; k < 9; ++k) { v[static_cast<std::size_t>(k)] = x[i + static_cast<std::size_t>(k)]; }
        // Paeth's 19-comparator median-of-9.
        CE(v[1], v[2]); CE(v[4], v[5]); CE(v[7], v[8]);
        CE(v[0], v[1]); CE(v[3], v[4]); CE(v[6], v[7]);
        CE(v[1], v[2]); CE(v[4], v[5]); CE(v[7], v[8]);
        CE(v[0], v[3]); CE(v[5], v[8]); CE(v[4], v[7]);
        CE(v[3], v[6]); CE(v[1], v[4]); CE(v[2], v[5]);
        CE(v[4], v[7]); CE(v[4], v[2]); CE(v[6], v[4]);
        CE(v[4], v[2]);
        y[i] = v[4];
    }
}
#undef CE

// std-nth_element: ctg3's `hand_nth` — std::nth_element into a per-window scratch copy.
template <int W>
void hand_nth(const float* __restrict x, float* __restrict y, std::size_t n) {
    float buf[W];
    for (std::size_t i = 0; i < n; ++i) {
        for (int k = 0; k < W; ++k) { buf[k] = x[i + static_cast<std::size_t>(k)]; }
        std::nth_element(buf, buf + W / 2, buf + W);
        y[i] = buf[W / 2];
    }
}

template <int W, void Fn(const float*, float*, std::size_t)>
void bench_hand(benchmark::State& state) {
    const PageVector<float> x = make_signal_f32();
    const std::size_t n = static_cast<std::size_t>(N - W + 1);
    OutputVector<float> y(n);
    Fn(x.data(), y.data(), n);
    for (const std::size_t f : {std::size_t{0}, n / 2, n - 1}) {
        float buf[W];
        for (int k = 0; k < W; ++k) { buf[k] = x[f + static_cast<std::size_t>(k)]; }
        std::nth_element(buf, buf + W / 2, buf + W);
        if (!(y[f] == buf[W / 2])) {
            state.SkipWithError("bench_order_stats: hand arm disagrees with the reference");
            return;
        }
    }
    for (auto _ : state) {
        Fn(x.data(), y.data(), n);
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(n));
}

} // namespace

#define HEINZEL_MEDIAN(W)                                                                                                  \
    BENCHMARK(bench_median_nest<W>)->Name("median_filter/heinzel/W" #W)->UseRealTime();
HEINZEL_MEDIAN(3)
HEINZEL_MEDIAN(5)
HEINZEL_MEDIAN(9)

BENCHMARK(bench_hand<3, hand_net3>)->Name("median_filter/hand/W3")->UseRealTime();
BENCHMARK(bench_hand<3, hand_nth<3>>)->Name("median_filter/std-nth_element/W3")->UseRealTime();
BENCHMARK(bench_hand<5, hand_net5>)->Name("median_filter/hand/W5")->UseRealTime();
BENCHMARK(bench_hand<5, hand_nth<5>>)->Name("median_filter/std-nth_element/W5")->UseRealTime();
BENCHMARK(bench_hand<9, hand_net9>)->Name("median_filter/hand/W9")->UseRealTime();
BENCHMARK(bench_hand<9, hand_nth<9>>)->Name("median_filter/std-nth_element/W9")->UseRealTime();

BENCHMARK_MAIN();
