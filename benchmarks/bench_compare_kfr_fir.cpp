// Third-party comparison benchmark: Heinzel vs KFR — FIR (convolution) workloads, KFR's arms. Part of the
// bench_compare_kfr target (see bench_compare_kfr.cpp for the file split; Heinzel's arms are in
// bench_compare_kfr_heinzel_fir.cpp). The taps are kfr_compare_common.hpp's, the hand convolution
// kfr_compare_refs.hpp's.
//
// Same windowed-sinc low-pass task as main: a Hamming-windowed sinc low-pass, K taps, normalised to
// unity DC. Arms:
//   firK/kfr        — KFR's direct fir (K=65,79,257,1025,4097) or its dedicated compile-time-unrolled
//                     short_fir kernel (K=15, KFR's documented 2..32-tap fast path).
//   firK/kfr-fft    — KFR's FFT-based convolve_filter (K=257,1025,4097 — the long-FIR regime where
//                     FFT convolution takes over). Renamed from main's `firK/kfr(fft)` (no parens
//                     allowed in an arm name; `-fft` is the variant).
// Both validated against a plain hand convolution loop before timing.
//
// Built only when HEINZEL_BENCH_COMPARE_KFR=ON.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>
#include <kfr/dft.hpp> // kfr::convolve_filter
#include <kfr/dsp.hpp> // kfr::fir / short_fir

#include "kfr_compare_common.hpp"
#include "kfr_compare_refs.hpp"

#include "page_vector.hpp"

using heinzel_bench::OutputVector;

namespace {
using namespace kfrcmp;

// KFR's causal output at index K-1+i equals the hand loop's valid-region output at i.
template <int K>
bool check_fir(std::string& err) {
    constexpr int kCheck = 4096;
    const int nout = N - K + 1;
    const int m = std::min(kCheck, nout);
    std::vector<float> ref(static_cast<std::size_t>(m));
    hand_fir(g_sine_in.data(), kFirTaps<K>.data(), K, m, ref.data());
    const float tol = 2e-4f * static_cast<float>(K) / 65.0f;

    // Each arm's call, read and written as that arm makes it, but short_fir's: a check of other types can move the
    // arms' rows (bench_compare_kfr.cpp's checks), and sharing short_fir's moves it the other way.
    const auto kin = kfr::make_univector(g_sine_in.data(), static_cast<std::size_t>(N));
    OutputVector<float> kf_mem(static_cast<std::size_t>(N));
    auto kf = kfr::make_univector(kf_mem.data(), kf_mem.size());
    kfr::fir_state<float> state(kfr::fir_params<float>(kFirTaps<K>.data(), static_cast<std::size_t>(K)));
    kf = kfr::fir(kin, std::ref(state));
    if (!nearly_equal(kf.data() + (K - 1), ref.data(), static_cast<std::size_t>(m), tol)) {
        err = "fir/kfr: mismatch vs hand-convolution reference at K=" + std::to_string(K);
        return false;
    }
    if constexpr (K <= 32) {
        kfr::univector<float, K> taps;
        std::copy(kFirTaps<K>.begin(), kFirTaps<K>.end(), taps.begin());
        // Into KFR's own vector, not the arm's view: sharing the arm's instantiation took short_fir's process out of
        // line in the arm (fir15/kfr 569 to 616 us), where the unchecked arm had it inlined.
        const kfr::univector<float> ks = kfr::short_fir(kin, taps);
        if (!nearly_equal(ks.data() + (K - 1), ref.data(), static_cast<std::size_t>(m), tol)) {
            err = "fir/kfr: short_fir mismatch vs hand-convolution reference at K=" + std::to_string(K);
            return false;
        }
    }
    kfr::convolve_filter<float> cf(kfr::make_univector(kFirTaps<K>.data(), static_cast<std::size_t>(K)));
    OutputVector<float> cc(static_cast<std::size_t>(N));
    cf.apply(cc.data(), g_sine_in.data(), static_cast<std::size_t>(N));
    if (!nearly_equal(cc.data() + (K - 1), ref.data(), static_cast<std::size_t>(m), tol)) {
        err = "fir/kfr-fft: mismatch vs hand-convolution reference at K=" + std::to_string(K);
        return false;
    }
    return true;
}

template <int K>
void bench_kfr_fir(benchmark::State& state) {
    std::string err;
    if (!check_fir<K>(err)) {
        state.SkipWithError(err.c_str());
        return;
    }
    // The input and the output are ours (Plan/30: the input at a page, the output half a page past one): KFR reads and
    // writes them through its views of a caller's memory.
    const auto kin = kfr::make_univector(g_sine_in.data(), static_cast<std::size_t>(N));
    OutputVector<float> out_mem(static_cast<std::size_t>(N));
    auto out = kfr::make_univector(out_mem.data(), out_mem.size());
    kfr::fir_state<float> st(kfr::fir_params<float>(kFirTaps<K>.data(), static_cast<std::size_t>(K)));
    for (auto _ : state) {
        out = kfr::fir(kin, std::ref(st));
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * (N - K + 1));
}

// KFR's dedicated compile-time-unrolled small-FIR kernel (documented range: 2..32 taps).
template <int K>
void bench_kfr_short_fir(benchmark::State& state) {
    static_assert(K >= 2 && K <= 32, "short_fir is KFR's 2..32-tap kernel");
    std::string err;
    if (!check_fir<K>(err)) {
        state.SkipWithError(err.c_str());
        return;
    }
    const auto kin = kfr::make_univector(g_sine_in.data(), static_cast<std::size_t>(N));
    OutputVector<float> out_mem(static_cast<std::size_t>(N));
    auto out = kfr::make_univector(out_mem.data(), out_mem.size());
    kfr::univector<float, K> taps;
    std::copy(kFirTaps<K>.begin(), kFirTaps<K>.end(), taps.begin());
    for (auto _ : state) {
        out = kfr::short_fir(kin, taps);
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * (N - K + 1));
}

template <int K>
void bench_kfr_convolve(benchmark::State& state) {
    std::string err;
    if (!check_fir<K>(err)) {
        state.SkipWithError(err.c_str());
        return;
    }
    // The input and the output are ours (Plan/30: the input at a page, the output half a page past one), given to
    // `apply` as pointers: the process_buffer call its univector form made (a view of the const input would take its
    // expression path instead).
    OutputVector<float> out(static_cast<std::size_t>(N));
    kfr::convolve_filter<float> cf(kfr::make_univector(kFirTaps<K>.data(), static_cast<std::size_t>(K)));
    for (auto _ : state) {
        cf.apply(out.data(), g_sine_in.data(), static_cast<std::size_t>(N));
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * (N - K + 1));
}

BENCHMARK(bench_kfr_short_fir<15>)->Name("fir15/kfr")->UseRealTime();
BENCHMARK(bench_kfr_fir<65>)->Name("fir65/kfr")->UseRealTime();
BENCHMARK(bench_kfr_fir<79>)->Name("fir79/kfr")->UseRealTime();
BENCHMARK(bench_kfr_fir<257>)->Name("fir257/kfr")->UseRealTime();
BENCHMARK(bench_kfr_fir<1025>)->Name("fir1025/kfr")->UseRealTime();
BENCHMARK(bench_kfr_fir<4097>)->Name("fir4097/kfr")->UseRealTime();
BENCHMARK(bench_kfr_convolve<257>)->Name("fir257/kfr-fft")->UseRealTime();
BENCHMARK(bench_kfr_convolve<1025>)->Name("fir1025/kfr-fft")->UseRealTime();
BENCHMARK(bench_kfr_convolve<4097>)->Name("fir4097/kfr-fft")->UseRealTime();

} // namespace
