// The Heinzel arms of the FAUST comparison (bench_compare_faust, Plan/23 P1 and P7): sine, the strip, the biquad
// cascade, FAUST's linear Moog ladder (one voice and a bank of 16) and Freeverb, each a graph on `Executor<…,
// kMachine>`, checked against a hand reference before it is timed. An object library of the target: compiled with
// Heinzel's flags, apart from FAUST's generated files. The bank takes its 16 voices sample-major, as Heinzel lays a
// bank out: FAUST's per-channel input is interleaved once, before the timed runs.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <benchmark/benchmark.h>

#include "faust_compare_common.hpp"
#include "freeverb_ref.hpp"
#include "heinzel_arms.hpp"
#include "iir_arms.hpp"

#include "page_vector.hpp"

using heinzel_bench::PageVector;
using heinzel_bench::OutputVector;

namespace {

using namespace faustcmp;
using namespace heinzel_arms;

constexpr Strip kStrip{kGin, kD1, kSB1, kD2, kSB2, kD3, kSB3, kGout};

constexpr hz::Graph sine_graph() { return hz::finish(hz::Output{"y"} = sine(N, kAmp)); }
constexpr hz::Graph strip_graph() {
    hz::Input x{"x", hz::Signal{N}};
    return hz::finish(hz::Output{"y"} = strip(x, kStrip));
}
constexpr hz::Graph eq3() { return iir_arms::df1_cascade(iir_arms::kEq, N); }
constexpr hz::Graph eq3_dyn() { return iir_arms::df1_cascade(iir_arms::kEq, hz::Length{}); }

// One linear Moog voice per lane of `x`, FAUST's generated loop op for op (moog_ref).
constexpr hz::Expr moog(hz::Expr x) {
    hz::Variable r4;
    hz::Variable r3;
    hz::Variable r2;
    hz::Variable r1;
    hz::Variable r0;
    r4[n] = x + f(kMoogC1) * r4[n - 1] - f(kMoogRes) * r0[n - 1];
    r3[n] = r4 + f(kMoogC1) * r3[n - 1];
    r2[n] = r3 + f(kMoogC1) * r2[n - 1];
    r1[n] = r2 + f(kMoogC1) * r1[n - 1];
    r0[n] = f(kMoogC2) * r1;
    return r0;
}
constexpr hz::Graph ladder_graph() {
    hz::Input x{"x", hz::Signal{N}};
    return hz::finish(hz::Output{"y"} = moog(x));
}
constexpr hz::Graph ladder16_graph() {
    hz::Input x{"x", hz::Signal{kLadCh, kLadNc}};
    return hz::finish(hz::Output{"y"} = moog(x));
}
constexpr hz::Graph reverb_graph() {
    hz::Input x{"x", hz::Signal{N}};
    return hz::finish(hz::Output{"y"} = freeverb(x, freeverb_ref::kTuning, kRoomFb, kDamp, kWet, kDry));
}

const PageVector<float> g_x = make_sig(N);

// One input signal in, one out, against `reference` over all of it.
template <auto Build>
void bench_mono(benchmark::State& state, void (*reference)(const float*, float*, std::size_t), double tol, const char* what) {
    OutputVector<float> y(N);
    std::vector<float> ref(N);
    reference(g_x.data(), ref.data(), ref.size());
    timed<Build>(
        state, N,
        [&](auto& run) {
            run.input("x") = g_x.data();
            run.output("y") = y.data();
        },
        [&]() -> const char* { return maxabs_diff(y.data(), ref.data(), y.size()) <= tol ? nullptr : what; });
}

void bench_heinzel_sine(benchmark::State& state) {
    OutputVector<float> y(N);
    timed<sine_graph>(
        state, N,
        [&](auto& run) {
            run.param("inc") = static_cast<double>(kFreq / kFs);
            run.output("y") = y.data();
        },
        [&]() -> const char* {
            for (std::size_t i = 0; i < 512; ++i) {
                const float want = kAmp * std::sin(6.283185307179586f * kFreq * static_cast<float>(i) / kFs);
                if (std::fabs(y[i] - want) > 1e-4f) {
                    return "sine/heinzel: mismatch vs the analytic sine";
                }
            }
            return nullptr;
        });
}
void bench_heinzel_strip(benchmark::State& state) { bench_mono<strip_graph>(state, strip_ref, 1e-4, "strip/heinzel: mismatch vs the tanh reference"); }
void bench_heinzel_ladder(benchmark::State& state) { bench_mono<ladder_graph>(state, moog_ref, 1e-3, "ladder/heinzel: mismatch vs FAUST's loop"); }
void bench_heinzel_reverb(benchmark::State& state) {
    bench_mono<reverb_graph>(
        state, [](const float* x, float* y, std::size_t m) { freeverb_ref::run(x, y, m, kRoomFb, kDamp, kWet, kDry); }, 1e-4,
        "reverb/heinzel: mismatch vs the hand Freeverb");
}
void bench_heinzel_biquad3(benchmark::State& state) {
    if (!iir_arms::same_bits(iir_arms::kEq, kEq)) {
        state.SkipWithError("biquad3/heinzel: the coefficients' copy differs from kEq");
        return;
    }
    bench_mono<eq3>(state, biquad_ref, 1e-2, "biquad3/heinzel: mismatch vs DF-I reference");
}

// The bank: FAUST's 16 channels of kLadNc samples, voice after voice, as Heinzel's sample-major bank; each voice
// checked against the one-voice loop over its own channel.
void bench_heinzel_ladder16(benchmark::State& state) {
    PageVector<float> xi(N);
    OutputVector<float> y(N);
    std::vector<float> ref(N);
    for (std::size_t c = 0; c < kLadCh; ++c) {
        for (std::size_t i = 0; i < kLadNc; ++i) {
            xi[i * kLadCh + c] = g_x[c * kLadNc + i];
        }
        moog_ref(g_x.data() + c * kLadNc, ref.data() + c * kLadNc, kLadNc);
    }
    timed<ladder16_graph>(
        state, N,
        [&](auto& run) {
            run.input("x") = xi.data();
            run.output("y") = y.data();
        },
        [&]() -> const char* {
            for (std::size_t c = 0; c < kLadCh; ++c) {
                for (std::size_t i = 0; i < kLadNc; ++i) {
                    if (std::fabs(y[i * kLadCh + c] - ref[c * kLadNc + i]) > 1e-3f) {
                        return "ladder16/heinzel: a voice differs from FAUST's loop";
                    }
                }
            }
            return nullptr;
        });
}

// Dynamic block length: one run of M samples a time, the state carried from run to run, as FAUST's compute(count);
// the first run, from silence, checked against the DF-I reference over the same M samples.
void bench_heinzel_biquad_dyn(benchmark::State& state) {
    const int M = static_cast<int>(state.range(0));
    if (!iir_arms::same_bits(iir_arms::kEq, kEq)) {
        state.SkipWithError("biquad3dyn/heinzel: the coefficients' copy differs from kEq");
        return;
    }
    const std::size_t m = static_cast<std::size_t>(M);
    PageVector<float> x = make_sig(m);
    OutputVector<float> y(m, 0.0f);
    std::vector<float> ref(m);
    const auto run = std::make_unique<hz::Executor<eq3_dyn, heinzel_bench::kMachine>>();
    run->prepare(M);
    run->input("x") = x.data();
    run->output("y") = y.data();
    run->run(M);
    biquad_ref(x.data(), ref.data(), m);
    if (maxabs_diff(y.data(), ref.data(), m) > 1e-2) {
        state.SkipWithError("biquad3dyn/heinzel: mismatch vs DF-I reference");
        return;
    }
    for (auto _ : state) {
        run->run(M);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(M));
}

BENCHMARK(bench_heinzel_sine)->Name("sine/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_strip)->Name("strip/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_biquad3)->Name("biquad3/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_biquad_dyn)->Name("biquad3dyn/heinzel")->RangeMultiplier(4)->Range(256, 16384)->UseRealTime();
BENCHMARK(bench_heinzel_ladder)->Name("ladder/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_ladder16)->Name("ladder16/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_reverb)->Name("reverb/heinzel")->UseRealTime();

} // namespace
