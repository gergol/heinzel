// Third-party comparison benchmark: Heinzel vs KFR (kfrlib/kfr). This file carries KFR's and the hand arms of sine
// and the biquad cascades; bench_compare_kfr_delay_reverb.cpp, bench_compare_kfr_fft.cpp and bench_compare_kfr_fir.cpp
// carry the rest of main's workloads, bench_compare_kfr_eq8.cpp the eight-section EQ and the work around it (Plan/23
// 09 (a)), bench_compare_kfr_channels.cpp the three-section EQ on many channels (09 (b)), and
// bench_compare_kfr_heinzel*.cpp the Heinzel arms of all of them, in this ONE target (bench_compare_kfr).
// The Heinzel arms are an object library of their own: KFR and the hand loops are compiled without Heinzel's
// `-ffp-contract=off` (Plan/23 P7, `benchmarks/CMakeLists.txt`).
//
// main's optional PFFFT arms (fft/pffft, rfft/pffft, rfft_batched/pffft, HEINZEL_BENCH_COMPARE_PFFFT) are left out:
// PFFFT is not one of main's always-on dependencies (opt-in there too) and its source is not among the on-disk
// checkouts available to this port (build/bench-all/_deps has no pffft-src).
//
// Arms in THIS file:
//   sine     — KFR's sin expression vs a phase-accumulator + std::sin "idiomatic" hand loop (what an audio dev
//              writes first, no library).
//   biquad3  — 3-section cascade: KFR's sections-in-lanes IIR engine vs a textbook DF-I hand cascade, IDENTICAL
//              coefficients (both read kfrcmp::kEq), so any difference is rounding/topology, not the designer.
//   (biquad8, the same at eight sections, is bench_compare_kfr_eq8.cpp's, beside the rows that add work around it.)
//   biquad3dyn — the SAME 3-section cascade at a runtime-chosen block length, the state carried from block to block
//              (KFR's iir_state): the audio prepareToPlay-style sweep main ran.
//
// Built only when HEINZEL_BENCH_COMPARE_KFR=ON.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include <kfr/base.hpp>
#include <kfr/dsp.hpp>

#include "kfr_compare_common.hpp"
#include "kfr_compare_refs.hpp"

#include "page_vector.hpp"

using heinzel_bench::OutputVector;

namespace {
using namespace kfrcmp;

// The EQ sets as KFR's biquad_section(a0, a1, a2, b0, b1, b2): normalized (a0 == 1), a1 and a2 the values KFR's (and
// main's) difference equation SUBTRACTS — the same convention as the hand DF-I reference.
template <std::size_t K>
std::array<kfr::biquad_section<float>, K> kfr_sections(const std::array<BiquadCoeffs, K>& c) {
    std::array<kfr::biquad_section<float>, K> s{};
    for (std::size_t k = 0; k < K; ++k) {
        s[k] = kfr::biquad_section<float>(1.0f, c[k].a1, c[k].a2, c[k].b0, c[k].b1, c[k].b2);
    }
    return s;
}
const std::array<kfr::biquad_section<float>, 3> kKfrEq = kfr_sections(kEq);

// ---- correctness checks (SkipWithError on mismatch; run once per bench before timing) --------------
// Each writes as its arm does, through a view of an OutputVector (Plan/30): check and arm share one instantiation of
// KFR's `process`. With a univector of KFR's here, biquad3's arm had one of its own, inlined, and ran 11 % slower.
bool check_sine(std::string& err) {
    constexpr std::int32_t M = 512;
    std::vector<float> ref(static_cast<std::size_t>(M));
    for (std::int32_t i = 0; i < M; ++i) {
        ref[static_cast<std::size_t>(i)] = kAmp * std::sin(kTwoPi * kFreq * static_cast<float>(i) / kFs);
    }
    OutputVector<float> yk_mem(static_cast<std::size_t>(M));
    auto yk = kfr::make_univector(yk_mem.data(), yk_mem.size());
    yk = kfr::sin(kfr::counter(0.0f, kTwoPi * kFreq / kFs)) * kAmp;
    if (!nearly_equal(yk.data(), ref.data(), static_cast<std::size_t>(M), 1e-3f)) {
        err = "sine/kfr: mismatch vs analytic reference";
        return false;
    }
    return true;
}

bool check_biquad3(std::string& err) {
    constexpr std::int32_t M = 512;
    std::vector<float> ref(static_cast<std::size_t>(M));
    df1_cascade<3>(kEq, g_sine_in.data(), ref.data(), M);
    OutputVector<float> yk_mem(static_cast<std::size_t>(M));
    auto yk = kfr::make_univector(yk_mem.data(), yk_mem.size());
    yk = kfr::iir(kfr::make_univector(g_sine_in.data(), static_cast<std::size_t>(M)),
                  kfr::iir_params<float, 3>(kKfrEq.data(), kKfrEq.size()));
    if (!nearly_equal(yk.data(), ref.data(), static_cast<std::size_t>(M), 1e-3f)) {
        err = "biquad3/kfr: mismatch vs hand DF-I reference";
        return false;
    }
    return true;
}

// ---- benches ------------------------------------------------------------------------------------
void bench_kfr_sine(benchmark::State& state) {
    std::string err;
    if (!check_sine(err)) {
        state.SkipWithError(err.c_str());
        return;
    }
    // The output is ours, half a page into a page (Plan/30); KFR writes it through its view of a caller's memory.
    OutputVector<float> y_mem(static_cast<std::size_t>(N));
    auto y = kfr::make_univector(y_mem.data(), y_mem.size());
    for (auto _ : state) {
        y = kfr::sin(kfr::counter(0.0f, kTwoPi * kFreq / kFs)) * kAmp;
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * N);
}

void bench_idiomatic_sine(benchmark::State& state) {
    OutputVector<float> y(static_cast<std::size_t>(N));
    for (auto _ : state) {
        idiomatic_sine(y.data(), N);
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * N);
}

void bench_kfr_eq(benchmark::State& state) {
    std::string err;
    if (!check_biquad3(err)) {
        state.SkipWithError(err.c_str());
        return;
    }
    OutputVector<float> y_mem(static_cast<std::size_t>(N));
    auto y = kfr::make_univector(y_mem.data(), y_mem.size());
    for (auto _ : state) {
        y = kfr::iir(kfr::make_univector(g_sine_in.data(), static_cast<std::size_t>(N)),
                     kfr::iir_params<float, 3>(kKfrEq.data(), kKfrEq.size()));
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * N);
}

void bench_idiomatic_eq(benchmark::State& state) {
    OutputVector<float> y(static_cast<std::size_t>(N));
    for (auto _ : state) {
        df1_cascade<3>(kEq, g_sine_in.data(), y.data(), N);
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * N);
}

// Dynamic block length: M = state.range(0), a runtime value, swept over typical audio block sizes. The filter's
// state is carried from block to block, as a plugin's is (KFR's own way: `iir` over a `std::ref` to an `iir_state`);
// two blocks in a row are checked against the hand cascade over both first.
bool check_biquad3_dyn(std::size_t M, std::string& err) {
    kfr::iir_state<float, 3> st{kfr::iir_params<float, 3>(kKfrEq.data(), kKfrEq.size())};
    // Written as the arm writes: a check of another type can move the arm's row (the note above check_sine).
    OutputVector<float> y_mem(2 * M);
    auto y = kfr::make_univector(y_mem.data(), y_mem.size());
    for (std::size_t b = 0; b < 2; ++b) {
        y.slice(b * M, M) = kfr::iir(kfr::make_univector(g_sine_in.data() + b * M, M), std::ref(st));
    }
    std::vector<float> ref(2 * M);
    df1_cascade<3>(kEq, g_sine_in.data(), ref.data(), static_cast<std::int32_t>(2 * M));
    if (!nearly_equal(y.data(), ref.data(), 2 * M, 1e-3f)) {
        err = "biquad3dyn/kfr: two blocks in a row mismatch vs hand DF-I reference";
        return false;
    }
    return true;
}

void bench_kfr_eq_dyn(benchmark::State& state) {
    const std::size_t M = static_cast<std::size_t>(state.range(0));
    std::string err;
    if (!check_biquad3_dyn(M, err)) {
        state.SkipWithError(err.c_str());
        return;
    }
    kfr::iir_state<float, 3> st{kfr::iir_params<float, 3>(kKfrEq.data(), kKfrEq.size())};
    OutputVector<float> y_mem(M);
    auto y = kfr::make_univector(y_mem.data(), y_mem.size());
    for (auto _ : state) {
        y = kfr::iir(kfr::make_univector(g_sine_in.data(), M), std::ref(st));
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(M));
}

BENCHMARK(bench_kfr_sine)->Name("sine/kfr")->UseRealTime();
BENCHMARK(bench_idiomatic_sine)->Name("sine/hand-idiomatic")->UseRealTime();

BENCHMARK(bench_kfr_eq)->Name("biquad3/kfr")->UseRealTime();
BENCHMARK(bench_idiomatic_eq)->Name("biquad3/hand-idiomatic")->UseRealTime();

BENCHMARK(bench_kfr_eq_dyn)->Name("biquad3dyn/kfr")->RangeMultiplier(4)->Range(256, 16384)->UseRealTime();

} // namespace

BENCHMARK_MAIN();
