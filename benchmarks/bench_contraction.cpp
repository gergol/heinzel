// Contraction (matmul) benchmark — the rewrite arm, re-expressed against the NEW API.
//
// Ported from ctg3 benchmarks/contraction_bench.cpp. The workload is a dense matmul
// C[M,N] = Σ_k A[M,K]·B[K,N], swept over the same square sizes {128,256,512,1024} and the same two
// rectangular shapes.
//
// MAPPING to the rewrite's streaming model (from tests/parity/einsum_contractions_here.cpp): the
// operands are tensors, a{M,K} and b{K,N}, y{M,N} spelled
// einsum("ik,kj->ij", a, b), and a tensor runs as one sample. Each benchmark iteration runs one sample = one
// full matmul, exactly as the old tree's `ex.run()` does. `heinzel` = the NEST path (F32, native, no
// kernel — the fair Heinzel-vs-competitor arm, with its GFLOP/s counter); the workspace is made outside
// the timed loop.
//
// `naive` (textbook i-j-k), `hand-ikj` (cache-friendly i-k-j, vectorizable SAXPY inner) and `eigen`
// (opt-in, HEINZEL_BENCH_COMPARE_EIGEN) are F32, over the SAME data as the `heinzel` arm, with the same
// GFLOP/s counter — ported from ctg3 benchmarks/contraction_bench.cpp.
//
// `rowdot` (at the end) is the other contraction, of aligned operands: each row of `a` against the same row of `b`.

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>

#include "page_vector.hpp"
#include "parallel_arm.hpp"

#include <cmath>
#include <utility>
#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/notation/einsum.hpp"
#include "heinzel/sema/analyze.hpp"

#ifdef HEINZEL_BENCH_COMPARE_EIGEN
#include <Eigen/Dense>
#endif

using heinzel_bench::PageVector;
using heinzel_bench::OutputVector;

namespace hz = heinzel;

namespace {

// F32 operands shared by every F32 arm (heinzel/naive/hand-ikj/eigen), matching the nest arm's data.
PageVector<float> make_a(std::size_t n) {
    PageVector<float> v(n);
    for (std::size_t i = 0; i < n; ++i) { v[i] = 0.001f * static_cast<float>(i % 1000) - 0.5f; }
    return v;
}
PageVector<float> make_b(std::size_t n) {
    PageVector<float> v(n);
    for (std::size_t i = 0; i < n; ++i) { v[i] = 0.002f * static_cast<float>(i % 499) - 0.4f; }
    return v;
}

} // namespace

// C[M,N] = A[M,K]·B[K,N], one sample (length-1 stream), one graph per (M,K,N). The nest arm: the
// contraction in F32 (as the old tree's arm is), planned for this machine. No kernel: a Reduce statement
// over (i, j, k), its loops chosen by the scheduler.
template <int M, int K, int N>
constexpr hz::Graph mm_graph_f32() {
    hz::Input a{"a", {M, K}};
    hz::Input b{"b", {K, N}};
    hz::Graph g = hz::finish(hz::Output{"y"} = hz::einsum("ik,kj->ij", a, b));
    if (!hz::infer(g).ok) {
        hz::fail("bench_contraction: the f32 matmul graph did not infer");
    }
    return g;
}
template <int M, int K, int N>
constexpr hz::Graph kMMF32 = mm_graph_f32<M, K, N>();
template <int M, int K, int N>
constexpr const hz::loops::Program& kMMNest = heinzel_bench::kProgramOf<kMMF32<M, K, N>, heinzel_bench::kMachine>;

// Fixed stratified sample of (i,j) points, shared by every F32 arm's correctness check / max_rel_err.
template <int M, int N>
constexpr std::array<std::pair<int, int>, 5> mm_samples() {
    return {std::pair{0, 0}, std::pair{M / 4, N / 3}, std::pair{M / 2, N / 2}, std::pair{3 * M / 4, N / 4},
            std::pair{M - 1, N - 1}};
}

template <int M, int K, int N, bool Par = false>
void bench_matmul_nest(benchmark::State& state) {
    const PageVector<float> a = make_a(static_cast<std::size_t>(M) * K);
    const PageVector<float> b = make_b(static_cast<std::size_t>(K) * N);
    hz::loops::Workspace<kMMNest<M, K, N>, heinzel_bench::copies<Par>()> work(hz::loops::Sizes{});
    const hz::loops::Feeds feeds{a.data(), b.data()};
    heinzel_bench::run<kMMNest<M, K, N>, Par>(feeds, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(kMMNest<M, K, N>.outputs[0]));
    double max_rel = 0.0;
    for (const auto [i, j] : mm_samples<M, N>()) {
        double want = 0.0;
        double magnitude = 0.0;
        for (int k = 0; k < K; ++k) {
            const double term = static_cast<double>(a[static_cast<std::size_t>(i) * K + k]) * b[static_cast<std::size_t>(k) * N + j];
            want += term;
            magnitude += std::fabs(term);
        }
        const double got = y[static_cast<std::size_t>(i) * N + j];
        if (!(std::fabs(got - want) <= K * 1.2e-7 * magnitude)) {
            state.SkipWithError("bench_contraction: the nest output is outside a float sum's rounding bound");
            return;
        }
        max_rel = std::max(max_rel, std::fabs(got - want) / std::max(magnitude, 1e-30));
    }
    state.counters["max_rel_err"] = max_rel;
    if (Par && !heinzel_bench::same_as_serial<kMMNest<M, K, N>>(feeds, work)) {
        state.SkipWithError("bench_contraction: the split run differs from the serial run");
        return;
    }
    for (auto _ : state) {
        heinzel_bench::run<kMMNest<M, K, N>, Par>(feeds, work);
        float first = y[0];
        benchmark::DoNotOptimize(first);
        benchmark::ClobberMemory();
    }
    state.counters["GFLOP/s"] = benchmark::Counter(2.0 * M * K * N * 1e-9, benchmark::Counter::kIsIterationInvariantRate);
}

// ---- `Executor`'s own cost (Plan/archive/14-dsl/08 §3), not gated -------------------------------------------------
// `heinzel-executor` runs the graph through `Executor`: ports bound by name, the output copied out of its
// workspace after each run. Its control, which ran the same program by hand, and the numbers are in `Plan/archive/12`
// (L2): the difference is the copy, which L12 removes (DS-21).

// Every point of an arm's result as the double sum of products, to a float sum's bound.
template <int M, int K, int N>
bool every_point_holds(const PageVector<float>& a, const PageVector<float>& b, const OutputVector<float>& y) {
    for (int i = 0; i < M; ++i) {
        for (int j = 0; j < N; ++j) {
            double want = 0.0;
            double magnitude = 0.0;
            for (int k = 0; k < K; ++k) {
                const double term = static_cast<double>(a[static_cast<std::size_t>(i) * K + k]) * b[static_cast<std::size_t>(k) * N + j];
                want += term;
                magnitude += std::fabs(term);
            }
            if (!(std::fabs(y[static_cast<std::size_t>(i) * N + j] - want) <= K * 1.2e-7 * magnitude)) {
                return false;
            }
        }
    }
    return true;
}

template <int M, int K, int N>
void bench_matmul_executor(benchmark::State& state) {
    const PageVector<float> a = make_a(static_cast<std::size_t>(M) * K);
    const PageVector<float> b = make_b(static_cast<std::size_t>(K) * N);
    OutputVector<float> y(static_cast<std::size_t>(M) * N);
    hz::Executor<mm_graph_f32<M, K, N>, heinzel_bench::kMachine> ex;
    ex.input("a") = a.data();
    ex.input("b") = b.data();
    ex.output("y") = y.data();
    ex.run();
    if (!every_point_holds<M, K, N>(a, b, y)) {
        state.SkipWithError("bench_contraction: the Executor's output is outside a float sum's rounding bound");
        return;
    }
    for (auto _ : state) {
        ex.run();
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.counters["GFLOP/s"] = benchmark::Counter(2.0 * M * K * N * 1e-9, benchmark::Counter::kIsIterationInvariantRate);
}

// A cheap sanity check for a reference arm's result against the double sum-of-products, over the same
// fixed sample points the nest arm uses. SkipWithError on gross mismatch — a fast wrong number is never
// reported.
template <int M, int K, int N>
bool matmul_sane(benchmark::State& state, const PageVector<float>& a, const PageVector<float>& b, const float* c) {
    for (const auto [i, j] : mm_samples<M, N>()) {
        double want = 0.0;
        double magnitude = 0.0;
        for (int k = 0; k < K; ++k) {
            const double term = static_cast<double>(a[static_cast<std::size_t>(i) * K + k]) * b[static_cast<std::size_t>(k) * N + j];
            want += term;
            magnitude += std::fabs(term);
        }
        const double got = c[static_cast<std::size_t>(i) * N + j];
        if (!(std::fabs(got - want) <= K * 1.2e-7 * magnitude)) {
            state.SkipWithError("bench_contraction: reference arm disagrees with the double reference");
            return false;
        }
    }
    return true;
}

// naive: the textbook i-j-k triple loop, each C[i,j] a dot product, B accessed column-wise (strided).
template <int M, int K, int N>
void bench_matmul_naive(benchmark::State& state) {
    const PageVector<float> a = make_a(static_cast<std::size_t>(M) * K);
    const PageVector<float> b = make_b(static_cast<std::size_t>(K) * N);
    OutputVector<float> c(static_cast<std::size_t>(M) * N);
    auto run = [&] {
        for (int i = 0; i < M; ++i) {
            for (int j = 0; j < N; ++j) {
                float acc = 0.0f;
                for (int k = 0; k < K; ++k) {
                    acc += a[static_cast<std::size_t>(i) * K + static_cast<std::size_t>(k)] *
                           b[static_cast<std::size_t>(k) * N + static_cast<std::size_t>(j)];
                }
                c[static_cast<std::size_t>(i) * N + static_cast<std::size_t>(j)] = acc;
            }
        }
    };
    run();
    if (!matmul_sane<M, K, N>(state, a, b, c.data())) { return; }
    for (auto _ : state) {
        benchmark::ClobberMemory();
        run();
        benchmark::DoNotOptimize(c.data());
        benchmark::ClobberMemory();
    }
    state.counters["GFLOP/s"] = benchmark::Counter(2.0 * M * K * N * 1e-9, benchmark::Counter::kIsIterationInvariantRate);
}

// hand-ikj: the cache-friendly i-k-j order — the inner j-loop is a SAXPY the compiler auto-vectorizes.
template <int M, int K, int N>
void bench_matmul_hand_ikj(benchmark::State& state) {
    const PageVector<float> a = make_a(static_cast<std::size_t>(M) * K);
    const PageVector<float> b = make_b(static_cast<std::size_t>(K) * N);
    OutputVector<float> c(static_cast<std::size_t>(M) * N);
    auto run = [&] {
        for (std::size_t i = 0; i < c.size(); ++i) { c[i] = 0.0f; }
        for (int i = 0; i < M; ++i) {
            for (int k = 0; k < K; ++k) {
                const float aik = a[static_cast<std::size_t>(i) * K + static_cast<std::size_t>(k)];
                for (int j = 0; j < N; ++j) {
                    c[static_cast<std::size_t>(i) * N + static_cast<std::size_t>(j)] +=
                        aik * b[static_cast<std::size_t>(k) * N + static_cast<std::size_t>(j)];
                }
            }
        }
    };
    run();
    if (!matmul_sane<M, K, N>(state, a, b, c.data())) { return; }
    for (auto _ : state) {
        benchmark::ClobberMemory();
        run();
        benchmark::DoNotOptimize(c.data());
        benchmark::ClobberMemory();
    }
    state.counters["GFLOP/s"] = benchmark::Counter(2.0 * M * K * N * 1e-9, benchmark::Counter::kIsIterationInvariantRate);
}

#ifdef HEINZEL_BENCH_COMPARE_EIGEN
// eigen: Eigen's row-major GEMM (industrial reference; -march=native, so full SIMD).
template <int M, int K, int N>
void bench_matmul_eigen(benchmark::State& state) {
    using RowMat = Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
    const PageVector<float> a = make_a(static_cast<std::size_t>(M) * K);
    const PageVector<float> b = make_b(static_cast<std::size_t>(K) * N);
    OutputVector<float> c(static_cast<std::size_t>(M) * N);
    Eigen::Map<const RowMat> A(a.data(), M, K);
    Eigen::Map<const RowMat> B(b.data(), K, N);
    Eigen::Map<RowMat> C(c.data(), M, N);
    C.noalias() = A * B;
    if (!matmul_sane<M, K, N>(state, a, b, c.data())) { return; }
    for (auto _ : state) {
        C.noalias() = A * B;
        benchmark::DoNotOptimize(c.data());
        benchmark::ClobberMemory();
    }
    state.counters["GFLOP/s"] = benchmark::Counter(2.0 * M * K * N * 1e-9, benchmark::Counter::kIsIterationInvariantRate);
}
#define HEINZEL_MM_EIGEN(TAG, M, K, N) BENCHMARK(bench_matmul_eigen<M, K, N>)->Name("matmul/eigen/" TAG)->UseRealTime();
#else
#define HEINZEL_MM_EIGEN(TAG, M, K, N)
#endif

#define HEINZEL_MM_NEST(TAG, M, K, N)                                                                   \
    BENCHMARK(bench_matmul_nest<M, K, N>)->Name("matmul/heinzel/" TAG)->UseRealTime();                  \
    BENCHMARK((bench_matmul_nest<M, K, N, true>))->Name("matmul/heinzel-par/" TAG)->UseRealTime();
HEINZEL_MM_NEST("128", 128, 128, 128)
BENCHMARK((bench_matmul_executor<128, 128, 128>))->Name("matmul/heinzel-executor/128")->UseRealTime();
HEINZEL_MM_NEST("256", 256, 256, 256)
HEINZEL_MM_NEST("512", 512, 512, 512)
HEINZEL_MM_NEST("1024", 1024, 1024, 1024)
HEINZEL_MM_NEST("1024x256x1024", 1024, 256, 1024)
HEINZEL_MM_NEST("2048x64x2048", 2048, 64, 2048)

#define HEINZEL_MM_COMPETITORS(TAG, M, K, N)                                                                               \
    BENCHMARK(bench_matmul_naive<M, K, N>)->Name("matmul/naive/" TAG)->UseRealTime();                                      \
    BENCHMARK(bench_matmul_hand_ikj<M, K, N>)->Name("matmul/hand-ikj/" TAG)->UseRealTime();                                \
    HEINZEL_MM_EIGEN(TAG, M, K, N)
HEINZEL_MM_COMPETITORS("128", 128, 128, 128)
HEINZEL_MM_COMPETITORS("256", 256, 256, 256)
HEINZEL_MM_COMPETITORS("512", 512, 512, 512)
HEINZEL_MM_COMPETITORS("1024", 1024, 1024, 1024)
HEINZEL_MM_COMPETITORS("1024x256x1024", 1024, 256, 1024)
HEINZEL_MM_COMPETITORS("2048x64x2048", 2048, 64, 2048)

// ---- rowdot: each row of `a` against the same row of `b`, y[r] = Σ_f a[r,f]·b[r,f] (Decision 78) ------------------
// `einsum("bf,bf->b", a, b)`, operands with the same labels in the same order: a product and a sum, which the loop
// tree runs since Plan 28's D1 (before it, the graph was refused). `heinzel` is the nest path at `Normal`, its
// workspace made outside the timed loop; `naive` the plain row loop, whose float sum the compiler may not
// reassociate; `eigen` (opt-in) Eigen's row-wise sum of the coefficient-wise product. Each reads two numbers for a
// multiply and an add, so the larger sizes are bound by the caches and memory, not the arithmetic.
template <int R, int F>
constexpr hz::Graph rowdot_graph() {
    hz::Input a{"a", {R, F}};
    hz::Input b{"b", {R, F}};
    hz::Output y{"y"};
    y = hz::einsum("bf,bf->b", a, b);
    hz::Graph g = hz::finish(y);
    if (!hz::infer(g).ok) {
        hz::fail("bench_contraction: the rowdot graph did not infer");
    }
    return g;
}
template <int R, int F>
constexpr hz::Graph kRowDot = rowdot_graph<R, F>();
template <int R, int F>
constexpr const hz::loops::Program& kRowDotNest = heinzel_bench::kProgramOf<kRowDot<R, F>, heinzel_bench::kMachine>;

// Every row of an arm's result as the double sum of products, to a float sum's bound.
template <int R, int F>
bool rows_hold(const PageVector<float>& a, const PageVector<float>& b, const float* y) {
    for (int r = 0; r < R; ++r) {
        double want = 0.0;
        double magnitude = 0.0;
        for (int f = 0; f < F; ++f) {
            const std::size_t at = static_cast<std::size_t>(r) * F + static_cast<std::size_t>(f);
            const double term = static_cast<double>(a[at]) * b[at];
            want += term;
            magnitude += std::fabs(term);
        }
        if (!(std::fabs(y[r] - want) <= F * 1.2e-7 * magnitude)) {
            return false;
        }
    }
    return true;
}

template <int R, int F>
void bench_rowdot_heinzel(benchmark::State& state) {
    const PageVector<float> a = make_a(static_cast<std::size_t>(R) * F);
    const PageVector<float> b = make_b(static_cast<std::size_t>(R) * F);
    hz::loops::Workspace<kRowDotNest<R, F>> work(hz::loops::Sizes{});
    const hz::loops::Feeds feeds{a.data(), b.data()};
    heinzel_bench::run<kRowDotNest<R, F>, false>(feeds, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(kRowDotNest<R, F>.outputs[0]));
    if (!rows_hold<R, F>(a, b, y)) {
        state.SkipWithError("bench_contraction: the rowdot nest is outside a float sum's rounding bound");
        return;
    }
    for (auto _ : state) {
        heinzel_bench::run<kRowDotNest<R, F>, false>(feeds, work);
        float first = y[0];
        benchmark::DoNotOptimize(first);
        benchmark::ClobberMemory();
    }
    state.counters["GFLOP/s"] = benchmark::Counter(2.0 * R * F * 1e-9, benchmark::Counter::kIsIterationInvariantRate);
}

template <int R, int F>
void bench_rowdot_naive(benchmark::State& state) {
    const PageVector<float> a = make_a(static_cast<std::size_t>(R) * F);
    const PageVector<float> b = make_b(static_cast<std::size_t>(R) * F);
    OutputVector<float> y(static_cast<std::size_t>(R));
    auto run = [&] {
        for (int r = 0; r < R; ++r) {
            float acc = 0.0f;
            for (int f = 0; f < F; ++f) {
                const std::size_t at = static_cast<std::size_t>(r) * F + static_cast<std::size_t>(f);
                acc += a[at] * b[at];
            }
            y[static_cast<std::size_t>(r)] = acc;
        }
    };
    run();
    if (!rows_hold<R, F>(a, b, y.data())) {
        state.SkipWithError("bench_contraction: the naive rowdot is outside a float sum's rounding bound");
        return;
    }
    for (auto _ : state) {
        benchmark::ClobberMemory();
        run();
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.counters["GFLOP/s"] = benchmark::Counter(2.0 * R * F * 1e-9, benchmark::Counter::kIsIterationInvariantRate);
}

#ifdef HEINZEL_BENCH_COMPARE_EIGEN
template <int R, int F>
void bench_rowdot_eigen(benchmark::State& state) {
    using RowMat = Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
    const PageVector<float> a = make_a(static_cast<std::size_t>(R) * F);
    const PageVector<float> b = make_b(static_cast<std::size_t>(R) * F);
    OutputVector<float> y(static_cast<std::size_t>(R));
    Eigen::Map<const RowMat> A(a.data(), R, F);
    Eigen::Map<const RowMat> B(b.data(), R, F);
    Eigen::Map<Eigen::VectorXf> Y(y.data(), R);
    Y = A.cwiseProduct(B).rowwise().sum();
    if (!rows_hold<R, F>(a, b, y.data())) {
        state.SkipWithError("bench_contraction: Eigen's rowdot is outside a float sum's rounding bound");
        return;
    }
    for (auto _ : state) {
        Y = A.cwiseProduct(B).rowwise().sum();
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.counters["GFLOP/s"] = benchmark::Counter(2.0 * R * F * 1e-9, benchmark::Counter::kIsIterationInvariantRate);
}
#define HEINZEL_ROWDOT_EIGEN(TAG, R, F) BENCHMARK(bench_rowdot_eigen<R, F>)->Name("rowdot/eigen/" TAG)->UseRealTime();
#else
#define HEINZEL_ROWDOT_EIGEN(TAG, R, F)
#endif

#define HEINZEL_ROWDOT(TAG, R, F)                                                              \
    BENCHMARK(bench_rowdot_heinzel<R, F>)->Name("rowdot/heinzel/" TAG)->UseRealTime();         \
    BENCHMARK(bench_rowdot_naive<R, F>)->Name("rowdot/naive/" TAG)->UseRealTime();             \
    HEINZEL_ROWDOT_EIGEN(TAG, R, F)
HEINZEL_ROWDOT("64x64", 64, 64)
HEINZEL_ROWDOT("256x256", 256, 256)
HEINZEL_ROWDOT("1024x1024", 1024, 1024)

BENCHMARK_MAIN();
