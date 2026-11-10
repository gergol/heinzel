// Third-party comparison benchmark: Heinzel vs KFR — delay / reverb / allpass workloads, KFR's and the hand arms. Part
// of the bench_compare_kfr target (see bench_compare_kfr.cpp for the file split; Heinzel's arms are in
// bench_compare_kfr_heinzel.cpp). The hand references are kfr_compare_refs.hpp's.
//
// Arms:
//   delayD    — fixed integer delay sweep: KFR's SIMD delay<D> (caps D < 1024) vs a library-independent
//               shifted-copy reference (y[n] = x[n-D] for n>=D, else 0 — exact, not an approximation).
//   reverb    — Freeverb: KFR ships no reverb, so the only arm is the canonical hand Freeverb
//               (exactly what a KFR app would write itself), named `hand-freeverb`.
//   allpass / apchain — deep Schroeder allpass (D=521) and a 4-allpass diffuser chain: KFR has no
//               ready-made equivalent either, so these are `hand` arms against Heinzel's.
//
// Built only when HEINZEL_BENCH_COMPARE_KFR=ON.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>
#include <kfr/dsp.hpp>

#include "kfr_compare_common.hpp"
#include "kfr_compare_refs.hpp"

#include "page_vector.hpp"

using heinzel_bench::OutputVector;

namespace {
using namespace kfrcmp;

// ---- correctness checks ----------------------------------------------------------------------------
template <std::size_t D>
bool check_delay(std::string& err) {
    constexpr std::size_t Md = 2048;
    std::vector<float> ref(Md);
    hand_delay(g_sine_in.data(), ref.data(), Md, D);
    // Written as the arm writes: a check of another type can move the arm's row (bench_compare_kfr.cpp's checks).
    OutputVector<float> yk_mem(Md);
    auto yk = kfr::make_univector(yk_mem.data(), yk_mem.size());
    yk = kfr::delay<D>(kfr::make_univector(g_sine_in.data(), Md));
    if (!nearly_equal(yk.data(), ref.data(), Md, 1e-5f)) {
        err = "delay/kfr: mismatch vs shifted-copy reference";
        return false;
    }
    return true;
}

// ---- benches ------------------------------------------------------------------------------------
template <std::size_t D>
void bench_kfr_delay(benchmark::State& state) {
    std::string err;
    if (!check_delay<D>(err)) {
        state.SkipWithError(err.c_str());
        return;
    }
    // The output is ours, half a page into a page (Plan/30); KFR writes it through its view of a caller's memory.
    OutputVector<float> y_mem(static_cast<std::size_t>(N));
    auto y = kfr::make_univector(y_mem.data(), y_mem.size());
    for (auto _ : state) {
        y = kfr::delay<D>(kfr::make_univector(g_sine_in.data(), static_cast<std::size_t>(N)));
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * N);
}

void bench_hand_freeverb(benchmark::State& state) {
    OutputVector<float> y(static_cast<std::size_t>(N));
    for (auto _ : state) {
        hand_freeverb(g_sine_in.data(), y.data(), static_cast<std::size_t>(N));
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * N);
}

void bench_hand_allpass(benchmark::State& state) {
    OutputVector<float> y(static_cast<std::size_t>(N));
    for (auto _ : state) {
        hand_allpass(g_sine_in.data(), y.data(), static_cast<std::size_t>(N));
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * N);
}

void bench_hand_apchain(benchmark::State& state) {
    OutputVector<float> y(static_cast<std::size_t>(N));
    for (auto _ : state) {
        hand_allpass_chain(g_sine_in.data(), y.data(), static_cast<std::size_t>(N));
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * N);
}

BENCHMARK(bench_kfr_delay<16>)->Name("delay16/kfr")->UseRealTime();
BENCHMARK(bench_kfr_delay<256>)->Name("delay256/kfr")->UseRealTime();
BENCHMARK(bench_kfr_delay<1000>)->Name("delay1000/kfr")->UseRealTime();

BENCHMARK(bench_hand_freeverb)->Name("reverb/hand-freeverb")->UseRealTime();

BENCHMARK(bench_hand_allpass)->Name("allpass/hand")->UseRealTime();
BENCHMARK(bench_hand_apchain)->Name("apchain/hand")->UseRealTime();

} // namespace
