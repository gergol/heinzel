// Third-party comparison benchmark: Heinzel vs KFR — FFT workloads. Part of the bench_compare_kfr
// target (see bench_compare_kfr.cpp for the file-split rationale and the dropped Heinzel arms — P7).
//
// main validated its FFT/rFFT arms against heinzel::dsp's own scalar radix-2 oracle
// (dsp::detail::fft_with_twiddles), which lives inside heinzel::dsp and isn't available on this
// branch. The library-independent reference here is instead a direct O(M^2) DFT over std::complex —
// slower, but algorithm-independent of BOTH Heinzel and KFR, so it is at least as honest a ground
// truth, just less of a "hand-optimized" one. It only runs at startup for the (small) correctness
// sizes, never in the timed loop.
//
// Arms:
//   fft/kfr, rfft/kfr       — KFR's compiled DFT (kfr_dft), forward, f32, N = 256..16384.
//   rfft_batched/kfr        — KFR has no batched real-FFT API, so the fair baseline is its per-frame
//                             loop (F=64 frames of NFft each), reported PER-FRAME so the numbers line
//                             up with the single rfft arm above — exactly the shape main used to show
//                             off Heinzel's batching (dropped here, see file header note).
//
// Built only when HEINZEL_BENCH_COMPARE_KFR=ON.

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>
#include <kfr/dft.hpp>

#include "kfr_compare_common.hpp"

#include "page_vector.hpp"

using heinzel_bench::PageVector;
using heinzel_bench::OutputVector;

namespace {
using namespace kfrcmp;

PageVector<std::complex<float>> make_csignal(std::size_t M) {
    PageVector<std::complex<float>> x(M);
    for (std::size_t n = 0; n < M; ++n) {
        x[n] = {0.7f * std::sin(0.3f * static_cast<float>(n)) + 0.3f * std::cos(0.013f * static_cast<float>(n)),
                0.2f * std::sin(0.07f * static_cast<float>(n))};
    }
    return x;
}

PageVector<float> make_rsignal(std::size_t M) {
    PageVector<float> x(M);
    for (std::size_t n = 0; n < M; ++n) {
        x[n] = 0.7f * std::sin(0.3f * static_cast<float>(n)) + 0.3f * std::cos(0.013f * static_cast<float>(n));
    }
    return x;
}

// Direct O(M^2) DFT — the library-independent oracle (see file header for why not a radix-2 FFT).
std::vector<std::complex<float>> direct_dft(std::span<const std::complex<float>> x) {
    const std::size_t M = x.size();
    std::vector<std::complex<float>> out(M);
    const double two_pi = 6.283185307179586;
    for (std::size_t k = 0; k < M; ++k) {
        std::complex<double> acc{0.0, 0.0};
        for (std::size_t n = 0; n < M; ++n) {
            const double ang = -two_pi * static_cast<double>(k) * static_cast<double>(n) / static_cast<double>(M);
            acc += std::complex<double>(x[n].real(), x[n].imag()) * std::complex<double>(std::cos(ang), std::sin(ang));
        }
        out[k] = {static_cast<float>(acc.real()), static_cast<float>(acc.imag())};
    }
    return out;
}

bool close_complex(const std::complex<float>& a, const std::complex<float>& b, float tol) {
    return std::fabs(a.real() - b.real()) <= tol && std::fabs(a.imag() - b.imag()) <= tol;
}

bool check_fft(std::size_t M, std::string& err) {
    const float tol = 2e-2f * static_cast<float>(M) / 1024.0f;
    const auto x0 = make_csignal(M);
    const auto oracle = direct_dft(x0);

    kfr::dft_plan<float> plan(M);
    kfr::univector<kfr::u8> temp(plan.temp_size);
    kfr::univector<kfr::complex<float>> kin(M);
    kfr::univector<kfr::complex<float>> kout(M);
    for (std::size_t n = 0; n < M; ++n) {
        kin[n] = kfr::complex<float>(x0[n].real(), x0[n].imag());
    }
    plan.execute(kout.data(), kin.data(), temp.data());
    for (std::size_t k = 0; k < M; ++k) {
        if (!close_complex({kout[k].real(), kout[k].imag()}, oracle[k], tol)) {
            err = "fft/kfr: mismatch vs direct-DFT oracle at N=" + std::to_string(M);
            return false;
        }
    }
    return true;
}

bool check_rfft(std::size_t M, std::string& err) {
    const float tol = 2e-2f * static_cast<float>(M) / 1024.0f;
    const auto r0 = make_rsignal(M);
    std::vector<std::complex<float>> rc(M);
    for (std::size_t n = 0; n < M; ++n) {
        rc[n] = {r0[n], 0.0f};
    }
    const auto oracle = direct_dft(rc);
    const std::size_t bins = M / 2 + 1;

    kfr::dft_plan_real<float> plan(M, kfr::dft_pack_format::CCs);
    kfr::univector<kfr::u8> temp(plan.temp_size);
    kfr::univector<float> in(M);
    kfr::univector<kfr::complex<float>> out(bins);
    for (std::size_t n = 0; n < M; ++n) {
        in[n] = r0[n];
    }
    plan.execute(out.data(), in.data(), temp.data());
    for (std::size_t k = 0; k < bins; ++k) {
        if (!close_complex({out[k].real(), out[k].imag()}, oracle[k], tol)) {
            err = "rfft/kfr: mismatch vs direct-DFT oracle at N=" + std::to_string(M);
            return false;
        }
    }
    return true;
}

void bench_kfr_fft(benchmark::State& state) {
    const std::size_t M = static_cast<std::size_t>(state.range(0));
    std::string err;
    // Correctness only feasible (and needed) up to a few thousand points (O(M^2) oracle); larger
    // sizes reuse the same kfr::dft_plan code path already checked at those sizes.
    if (M <= 4096 && !check_fft(M, err)) {
        state.SkipWithError(err.c_str());
        return;
    }
    const auto x0 = make_csignal(M);
    kfr::dft_plan<float> plan(M);
    // The plan's scratch and the arm's input and output are ours (Plan/30: the input at a page, what `execute` writes
    // half a page past one, the output in slot 0 and the scratch after it): `execute` takes pointers.
    OutputVector<kfr::u8, 1> temp(plan.temp_size);
    PageVector<kfr::complex<float>> in(M);
    OutputVector<kfr::complex<float>> out(M);
    for (std::size_t n = 0; n < M; ++n) {
        in[n] = kfr::complex<float>(x0[n].real(), x0[n].imag());
    }
    for (auto _ : state) {
        plan.execute(out.data(), in.data(), temp.data());
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(M));
}

void bench_kfr_rfft(benchmark::State& state) {
    const std::size_t M = static_cast<std::size_t>(state.range(0));
    std::string err;
    if (M <= 4096 && !check_rfft(M, err)) {
        state.SkipWithError(err.c_str());
        return;
    }
    const auto r0 = make_rsignal(M);
    kfr::dft_plan_real<float> plan(M, kfr::dft_pack_format::CCs);
    OutputVector<kfr::u8, 1> temp(plan.temp_size);
    PageVector<float> in(M);
    OutputVector<kfr::complex<float>> out(M / 2 + 1);
    for (std::size_t n = 0; n < M; ++n) {
        in[n] = r0[n];
    }
    for (auto _ : state) {
        plan.execute(out.data(), in.data(), temp.data());
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(M));
}

// BATCHED real FFT (per-frame throughput): KFR has no batched API, so the baseline is its per-frame
// loop. F frames of NFft each; reported PER-FRAME so the numbers line up with rfft/kfr above.
constexpr std::size_t kBatchF = 64;

PageVector<float> make_rframes(std::size_t M, std::size_t F) {
    PageVector<float> v(M * F);
    for (std::size_t f = 0; f < F; ++f) {
        for (std::size_t n = 0; n < M; ++n) {
            v[f * M + n] = 0.7f * std::sin(0.3f * static_cast<float>(n) + 0.5f * static_cast<float>(f)) +
                           0.3f * std::cos(0.013f * static_cast<float>(n) - 0.2f * static_cast<float>(f));
        }
    }
    return v;
}

void bench_kfr_rfft_loop(benchmark::State& state) {
    const std::size_t M = static_cast<std::size_t>(state.range(0));
    const std::size_t F = kBatchF;
    const std::size_t B = M / 2 + 1;
    const auto frames = make_rframes(M, F);
    kfr::dft_plan_real<float> plan(M, kfr::dft_pack_format::CCs);
    OutputVector<kfr::u8, 2> temp(plan.temp_size);
    OutputVector<float, 1> in(M); // each frame copied in before the plan's scratch is written
    OutputVector<kfr::complex<float>> out(B);
    for (auto _ : state) {
        for (std::size_t f = 0; f < F; ++f) {
            for (std::size_t n = 0; n < M; ++n) {
                in[n] = frames[f * M + n];
            }
            plan.execute(out.data(), in.data(), temp.data());
            benchmark::DoNotOptimize(out.data());
        }
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(F * M));
}

BENCHMARK(bench_kfr_fft)->RangeMultiplier(2)->Range(256, 16384)->Name("fft/kfr")->UseRealTime();
BENCHMARK(bench_kfr_rfft)->RangeMultiplier(2)->Range(256, 16384)->Name("rfft/kfr")->UseRealTime();
BENCHMARK(bench_kfr_rfft_loop)->RangeMultiplier(2)->Range(256, 4096)->Name("rfft_batched/kfr")->UseRealTime();

} // namespace
