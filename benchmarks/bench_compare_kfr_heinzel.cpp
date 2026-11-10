// The Heinzel arms of the KFR comparison (bench_compare_kfr, Plan/23 P1 and P7): sine, the three-section cascade, the
// delays, the allpasses and Freeverb, each a graph on `Executor<…, kMachine>`, checked against the hand
// reference KFR's arm is checked against before it is timed. An object library of the target: compiled with Heinzel's
// flags, apart from KFR's and the hand arms' files. The FIR rows' arms are bench_compare_kfr_heinzel_fir.cpp's, the
// eight-section EQ's and the rows around it bench_compare_kfr_heinzel_eq8.cpp's, the many channels'
// bench_compare_kfr_heinzel_channels.cpp's.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>

#include "heinzel_arms.hpp"
#include "iir_arms.hpp"
#include "kfr_compare_common.hpp"
#include "kfr_compare_refs.hpp"

#include "page_vector.hpp"

using heinzel_bench::OutputVector;

namespace {

using namespace kfrcmp;
using namespace heinzel_arms;

// ---- sine: the affine oscillator over the whole block --------------------------------------------------------
constexpr hz::Graph sine_graph() { return hz::finish(hz::Output{"y"} = sine(N, kAmp)); }

void bench_heinzel_sine(benchmark::State& state) {
    OutputVector<float> y(static_cast<std::size_t>(N));
    timed<sine_graph>(
        state, N,
        [&](auto& run) {
            run.param("inc") = static_cast<double>(kFreq / kFs);
            run.output("y") = y.data();
        },
        [&]() -> const char* {
            for (std::int32_t i = 0; i < 512; ++i) {
                const float want = kAmp * std::sin(kTwoPi * kFreq * static_cast<float>(i) / kFs);
                if (std::fabs(y[static_cast<std::size_t>(i)] - want) > 1e-4f) {
                    return "sine/heinzel: mismatch vs the analytic sine";
                }
            }
            return nullptr;
        });
}

// ---- the biquad cascades: the textbook DF-I (iir_arms.hpp), as the hand reference -------------------------------
constexpr hz::Graph eq3() { return iir_arms::df1_cascade(iir_arms::kEq, N); }
constexpr hz::Graph eq3_dyn() { return iir_arms::df1_cascade(iir_arms::kEq, hz::Length{}); }

template <auto Build, std::size_t K>
void bench_heinzel_eq(benchmark::State& state, const std::array<BiquadCoeffs, K>& set, const std::array<iir_arms::Section, K>& copy,
                      float tol) {
    if (!iir_arms::same_bits(copy, set)) {
        state.SkipWithError("heinzel: the coefficients' copy differs from the designed set");
        return;
    }
    OutputVector<float> y(static_cast<std::size_t>(N));
    std::vector<float> ref(static_cast<std::size_t>(N));
    df1_cascade<K>(set, g_sine_in.data(), ref.data(), N);
    timed<Build>(
        state, N,
        [&](auto& run) {
            run.input("x") = g_sine_in.data();
            run.output("y") = y.data();
        },
        [&]() -> const char* {
            return nearly_equal(y.data(), ref.data(), y.size(), tol) ? nullptr : "heinzel: mismatch vs hand DF-I reference";
        });
}
void bench_heinzel_eq3(benchmark::State& state) { bench_heinzel_eq<eq3, 3>(state, kEq, iir_arms::kEq, 1e-3f); }

// One run of M samples a time, the state carried from run to run (a plugin's block); the first run, from silence,
// checked against the hand cascade over the same M samples.
void bench_heinzel_eq_dyn(benchmark::State& state) {
    const std::int32_t M = static_cast<std::int32_t>(state.range(0));
    if (!iir_arms::same_bits(iir_arms::kEq, kEq)) {
        state.SkipWithError("biquad3dyn/heinzel: the coefficients' copy differs from kEq");
        return;
    }
    const auto run = std::make_unique<hz::Executor<eq3_dyn, heinzel_bench::kMachine>>();
    run->prepare(M);
    OutputVector<float> y(static_cast<std::size_t>(M));
    std::vector<float> ref(static_cast<std::size_t>(M));
    run->input("x") = g_sine_in.data();
    run->output("y") = y.data();
    run->run(M);
    df1_cascade<3>(kEq, g_sine_in.data(), ref.data(), M);
    if (!nearly_equal(y.data(), ref.data(), static_cast<std::size_t>(M), 1e-3f)) {
        state.SkipWithError("biquad3dyn/heinzel: mismatch vs hand DF-I reference");
        return;
    }
    for (auto _ : state) {
        run->run(M);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * M);
}

// The same cascade with its coefficients known only at run time (`heinzel-param`), set from kEq before the first run.
constexpr hz::Graph eq3_param() { return iir_arms::df1_cascade_param<3>(N); }
constexpr hz::Graph eq3_param_dyn() { return iir_arms::df1_cascade_param<3>(hz::Length{}); }

void bench_heinzel_eq3_param(benchmark::State& state) {
    OutputVector<float> y(static_cast<std::size_t>(N));
    std::vector<float> ref(static_cast<std::size_t>(N));
    df1_cascade<3>(kEq, g_sine_in.data(), ref.data(), N);
    timed<eq3_param>(
        state, N,
        [&](auto& run) {
            iir_arms::bind_coefficients(run, kEq);
            run.input("x") = g_sine_in.data();
            run.output("y") = y.data();
        },
        [&]() -> const char* {
            return nearly_equal(y.data(), ref.data(), y.size(), 1e-3f) ? nullptr : "biquad3/heinzel-param: mismatch vs hand DF-I reference";
        });
}

void bench_heinzel_eq_param_dyn(benchmark::State& state) {
    const std::int32_t M = static_cast<std::int32_t>(state.range(0));
    const auto run = std::make_unique<hz::Executor<eq3_param_dyn, heinzel_bench::kMachine>>();
    run->prepare(M);
    iir_arms::bind_coefficients(*run, kEq);
    OutputVector<float> y(static_cast<std::size_t>(M));
    std::vector<float> ref(static_cast<std::size_t>(M));
    run->input("x") = g_sine_in.data();
    run->output("y") = y.data();
    run->run(M);
    df1_cascade<3>(kEq, g_sine_in.data(), ref.data(), M);
    if (!nearly_equal(y.data(), ref.data(), static_cast<std::size_t>(M), 1e-3f)) {
        state.SkipWithError("biquad3dyn/heinzel-param: mismatch vs hand DF-I reference");
        return;
    }
    for (auto _ : state) {
        run->run(M);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * M);
}

// ---- the delays, the allpasses and Freeverb: one input signal in, one out, checked over all of it --------------
template <auto Build>
void bench_heinzel_mono(benchmark::State& state, void (*reference)(const float*, float*, std::size_t), float tol, const char* what) {
    OutputVector<float> y(static_cast<std::size_t>(N));
    std::vector<float> ref(static_cast<std::size_t>(N));
    reference(g_sine_in.data(), ref.data(), ref.size());
    timed<Build>(
        state, N,
        [&](auto& run) {
            run.input("x") = g_sine_in.data();
            run.output("y") = y.data();
        },
        [&]() -> const char* { return nearly_equal(y.data(), ref.data(), y.size(), tol) ? nullptr : what; });
}

template <int D>
constexpr hz::Graph delay_graph() {
    hz::Input x{"x", hz::Signal{N}};
    return hz::finish(hz::Output{"y"} = x[n - D]);
}
template <int D>
void bench_heinzel_delay(benchmark::State& state) {
    bench_heinzel_mono<delay_graph<D>>(
        state, [](const float* x, float* y, std::size_t m) { hand_delay(x, y, m, D); }, 0.0f, "delay/heinzel: mismatch vs the shifted copy");
}

constexpr hz::Graph allpass_graph() {
    hz::Input x{"x", hz::Signal{N}};
    return hz::finish(hz::Output{"y"} = allpass(x, kApD, 0.5f));
}
constexpr hz::Graph apchain_graph() {
    hz::Input x{"x", hz::Signal{N}};
    hz::Expr s = x;
    for (const int size : kChain) {
        s = allpass(s, size, 0.5f);
    }
    return hz::finish(hz::Output{"y"} = s);
}
constexpr hz::Graph reverb_graph() {
    hz::Input x{"x", hz::Signal{N}};
    return hz::finish(hz::Output{"y"} = heinzel_arms::freeverb(x, freeverb_ref::kTuning, kRoomFb, kDamp, kWet, kDry));
}

void bench_heinzel_allpass(benchmark::State& state) {
    bench_heinzel_mono<allpass_graph>(state, hand_allpass, 1e-5f, "allpass/heinzel: mismatch vs the hand allpass");
}
void bench_heinzel_apchain(benchmark::State& state) {
    bench_heinzel_mono<apchain_graph>(state, hand_allpass_chain, 1e-5f, "apchain/heinzel: mismatch vs the hand chain");
}
void bench_heinzel_reverb(benchmark::State& state) {
    bench_heinzel_mono<reverb_graph>(state, hand_freeverb, 1e-4f, "reverb/heinzel: mismatch vs the hand Freeverb");
}

BENCHMARK(bench_heinzel_sine)->Name("sine/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_eq3)->Name("biquad3/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_eq_dyn)->Name("biquad3dyn/heinzel")->RangeMultiplier(4)->Range(256, 16384)->UseRealTime();
BENCHMARK(bench_heinzel_eq3_param)->Name("biquad3/heinzel-param")->UseRealTime();
BENCHMARK(bench_heinzel_eq_param_dyn)->Name("biquad3dyn/heinzel-param")->RangeMultiplier(4)->Range(256, 16384)->UseRealTime();
BENCHMARK(bench_heinzel_delay<16>)->Name("delay16/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_delay<256>)->Name("delay256/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_delay<1000>)->Name("delay1000/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_allpass)->Name("allpass/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_apchain)->Name("apchain/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_reverb)->Name("reverb/heinzel")->UseRealTime();

} // namespace
