// The Heinzel arms of the JUCE comparison (bench_compare_juce, Plan/23 P7), the feed-forward rows and the simple
// recurrences: sine, the EQ (whole and in blocks), the strip, the delay effect, the FIR, the peak follower, the bank
// mixed to mono, and the chorus and the long delay (a delay that changes, Plan/28-dsl-proposals/05 D4), each a graph on
// `Executor<…, kMachine>`, checked against the hand reference before it is timed. The rest are
// bench_compare_juce_heinzel_filters.cpp's. An object library of the target: compiled with Heinzel's flags,
// apart from JUCE's files. Where JUCE designs the coefficients, the graph holds a `constexpr` copy of JUCE's bits,
// checked against the library's own before anything is timed. The bank takes its 16 voices sample-major, as Heinzel
// lays a bank out: the input, voice after voice for JUCE, is interleaved once, before the timed runs.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <benchmark/benchmark.h>

#include "heinzel_arms.hpp"
#include "iir_arms.hpp"
#include "juce_compare_common.hpp"
#include "juce_compare_refs.hpp"

#include "page_vector.hpp"

using heinzel_bench::PageVector;
using heinzel_bench::OutputVector;

namespace {

using namespace jucecmp;
using namespace heinzel_arms;

// One input signal in, one out, against `reference` over all of it.
template <auto Build>
void bench_mono(benchmark::State& state, void (*reference)(const float*, float*, std::size_t), double tol, const char* what) {
    OutputVector<float> y(static_cast<std::size_t>(N));
    std::vector<float> ref(y.size());
    reference(g_in.data(), ref.data(), ref.size());
    timed<Build>(
        state, N,
        [&](auto& run) {
            run.input("x") = g_in.data();
            run.output("y") = y.data();
        },
        [&]() -> const char* { return maxabs_diff(y.data(), ref.data(), y.size()) <= tol ? nullptr : what; });
}

// ---- sine: the affine oscillator over the whole block; a kAmp sine by its RMS, and sample for sample (main's) --------
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
            const double want = kAmp / std::sqrt(2.0);
            double diff = 0.0;
            for (std::size_t i = 0; i < 4096; ++i) {
                diff = std::max(diff, std::fabs(y[i] - kAmp * std::sin(2.0 * kPi * kFreq * static_cast<double>(i) / kFs)));
            }
            return std::fabs(rms(y.data(), y.size()) - want) <= 0.02 * want && diff <= 1e-3 ? nullptr : "sine/heinzel: mismatch vs the analytic sine";
        });
}

// ---- the EQ: JUCE's coefficients, as the textbook direct form I (iir_arms.hpp) -----------------------------------
constexpr hz::Graph eq3() { return iir_arms::df1_cascade(iir_arms::kJuceEq, N); }
constexpr hz::Graph eq3_dyn() { return iir_arms::df1_cascade(iir_arms::kJuceEq, hz::Length{}); }

void bench_heinzel_eq3(benchmark::State& state) {
    if (!iir_arms::near_bits(iir_arms::kJuceEq, juce_eq(), 4)) {
        state.SkipWithError("biquad3/heinzel: the coefficients' copy differs from JUCE's design");
        return;
    }
    bench_mono<eq3>(
        state, [](const float* x, float* y, std::size_t m) { eq_ref(juce_eq(), x, y, m); }, 1e-3, "biquad3/heinzel: mismatch vs the direct-form-I cascade");
}

// The same EQ with its coefficients known only at run time, set from JUCE's design before the first run.
constexpr hz::Graph eq3_param() { return iir_arms::df1_cascade_param<3>(N); }
constexpr hz::Graph eq3_param_dyn() { return iir_arms::df1_cascade_param<3>(hz::Length{}); }

void bench_heinzel_eq3_param(benchmark::State& state) {
    OutputVector<float> y(static_cast<std::size_t>(N));
    std::vector<float> ref(y.size());
    eq_ref(juce_eq(), g_in.data(), ref.data(), ref.size());
    timed<eq3_param>(
        state, N,
        [&](auto& run) {
            iir_arms::bind_coefficients(run, juce_eq());
            run.input("x") = g_in.data();
            run.output("y") = y.data();
        },
        [&]() -> const char* {
            return maxabs_diff(y.data(), ref.data(), y.size()) <= 1e-3 ? nullptr : "biquad3/heinzel-param: mismatch vs the direct-form-I cascade";
        });
}

// One run of M samples a time, the state carried from run to run (a plugin's block); the first run, from silence,
// checked against the hand cascade over the same M samples. `bind` sets anything else the graph reads.
template <auto Build, class Bind>
void eq_dyn(benchmark::State& state, Bind bind, const char* mismatch) {
    const auto m = static_cast<std::size_t>(state.range(0));
    const auto run = std::make_unique<hz::Executor<Build, heinzel_bench::kMachine>>();
    run->prepare(static_cast<std::int64_t>(m));
    OutputVector<float> y(m);
    std::vector<float> ref(m);
    bind(*run);
    run->input("x") = g_in.data();
    run->output("y") = y.data();
    run->run(static_cast<std::int64_t>(m));
    eq_ref(juce_eq(), g_in.data(), ref.data(), m);
    if (maxabs_diff(y.data(), ref.data(), m) > 1e-3) {
        state.SkipWithError(mismatch);
        return;
    }
    for (auto _ : state) {
        run->run(static_cast<std::int64_t>(m));
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(m));
}

void bench_heinzel_eq_dyn(benchmark::State& state) {
    if (!iir_arms::near_bits(iir_arms::kJuceEq, juce_eq(), 4)) {
        state.SkipWithError("biquad3dyn/heinzel: the coefficients' copy differs from JUCE's design");
        return;
    }
    eq_dyn<eq3_dyn>(state, [](auto&) {}, "biquad3dyn/heinzel: mismatch vs the direct-form-I cascade");
}

void bench_heinzel_eq_param_dyn(benchmark::State& state) {
    eq_dyn<eq3_param_dyn>(
        state, [](auto& run) { iir_arms::bind_coefficients(run, juce_eq()); }, "biquad3dyn/heinzel-param: mismatch vs the direct-form-I cascade");
}

// ---- the strip: one graph, the stages fused --------------------------------------------------------------------
constexpr Strip kStrip{kGin, kD1, kSB1, kD2, kSB2, kD3, kSB3, kGout};
constexpr hz::Graph strip_graph() {
    hz::Input x{"x", hz::Signal{N}};
    return hz::finish(hz::Output{"y"} = strip(x, kStrip));
}

// ---- the delay effect: the line, and the high-pass over it D ago that feeds it back ----------------------------------
// `juce_delay_hp()`: JUCE's `makeFirstOrderHighPass` at 1 kHz.
constexpr FirstOrder kDelayHp{0x1.e0818ap-1f, -0x1.e0818ap-1f, -0x1.c10312p-1f};

constexpr hz::Graph delay_graph() {
    hz::Input x{"x", hz::Signal{N}};
    hz::Variable hp;
    hz::Variable line;
    line[n] = x + f(kDelFb) * hp;
    hp[n] = f(kDelayHp.b0) * line[n - kDelD] + f(kDelayHp.b1) * line[n - (kDelD + 1)] - f(kDelayHp.a1) * hp[n - 1];
    return hz::finish(hz::Output{"y"} = x + f(kDelWet) * hp);
}

void bench_heinzel_delay(benchmark::State& state) {
    if (!same_bits(kDelayHp, juce_delay_hp())) {
        state.SkipWithError("delay/heinzel: the high-pass's copy differs from JUCE's design");
        return;
    }
    bench_mono<delay_graph>(
        state, [](const float* x, float* y, std::size_t m) { delay_ref(juce_delay_hp(), x, y, m); }, 1e-3, "delay/heinzel: mismatch vs the delay loop");
}

// ---- the FIR: a window over the input times the reversed taps, summed; the taps an input, bound at run time --------
constexpr hz::Graph fir_graph() {
    hz::Input x{"x", {N}};
    hz::Input h{"h", {1, kFirK}};
    return hz::finish(hz::Output{"y"} = (x.unfold(0, kFirK) * h).sum(1));
}

// main's checks: the design is a low-pass (its analytic response), and the graph computes it (the valid region against
// the convolution in double).
void bench_heinzel_fir(benchmark::State& state) {
    const PageVector<float> reversed(kFirTaps.rbegin(), kFirTaps.rend());
    OutputVector<float> y(static_cast<std::size_t>(kFirNout));
    timed<fir_graph>(
        state, N,
        [&](auto& run) {
            run.input("x") = g_in.data();
            run.input("h") = reversed.data();
            run.output("y") = y.data();
        },
        [&]() -> const char* {
            return fir_gain(1000.0) > 0.9 && fir_gain(12000.0) < 0.1 && fir_maxdiff(y.data(), g_in.data()) <= 1e-4
                       ? nullptr
                       : "fir/heinzel: mismatch vs the convolution";
        });
}

// ---- the peak follower: the coefficient picked by `where` inside the recurrence --------------------------------------
// `kBal`: JUCE's exp(−2π·1000/(fs·ms)) at 1 ms and 100 ms.
constexpr Ballistics kBalCopy{0x1.c12e38p-1f, 0x1.ff548ap-1f};

// The two numbers take the type of the multiply that reads them.
constexpr hz::Graph envfollow_graph() {
    hz::Input x{"x", hz::Signal{N}};
    hz::Variable env;
    const hz::Expr xa = hz::abs(x);
    env[n] = xa + hz::where(xa > env[n - 1], f(kBalCopy.at), f(kBalCopy.rl)) * (env[n - 1] - xa);
    return hz::finish(hz::Output{"y"} = env);
}

void bench_heinzel_envfollow(benchmark::State& state) {
    if (!same_bits(kBalCopy, kBal)) {
        state.SkipWithError("envfollow/heinzel: the coefficients' copy differs from the design");
        return;
    }
    bench_mono<envfollow_graph>(state, envfollow_ref, 2e-3, "envfollow/heinzel: mismatch vs the peak follower");
}

// ---- the bank mixed to mono: the 16 one-poles in lanes, summed over the voices ------------------------------------
constexpr hz::Graph bankmix_graph() {
    hz::Input x{"x", hz::Signal{kCh, kNc}};
    hz::Variable y;
    y[n] = f(kBankB0) * x + f(kBankFb) * y[n - 1];
    return hz::finish(hz::Output{"y"} = y.sum(0));
}

void bench_heinzel_bankmix(benchmark::State& state) {
    PageVector<float> xi(static_cast<std::size_t>(N));
    OutputVector<float> y(static_cast<std::size_t>(kNc));
    std::vector<float> ref(y.size());
    for (std::size_t c = 0; c < kCh; ++c) {
        for (std::size_t i = 0; i < kNc; ++i) {
            xi[i * kCh + c] = g_in[c * kNc + i];
        }
    }
    bank_mix_ref(g_in.data(), ref.data());
    timed<bankmix_graph>(
        state, N,
        [&](auto& run) {
            run.input("x") = xi.data();
            run.output("y") = y.data();
        },
        [&]() -> const char* { return maxabs_diff(y.data(), ref.data(), y.size()) <= 1e-3 ? nullptr : "bankmix16/heinzel: mismatch vs the bank loop"; });
}

void bench_heinzel_strip(benchmark::State& state) { bench_mono<strip_graph>(state, strip_ref, 2e-5, "strip/heinzel: mismatch vs the tanh reference"); }

// ---- the chorus and the long delay: x and x as it was d ago, d swept by the triangle ------------------------------
// Over N samples, or a `Length` run a block at a time.
template <const Sweep& S, class Samples>
constexpr hz::Graph swept(Samples samples) {
    constexpr hz::Index at{kFs};
    hz::Input x{"x", hz::Signal{samples}};
    hz::Variable phase;
    phase[at] = hz::frac(phase[at - 1] + f(kSweepInc));
    hz::Expr tri = 4.0 * hz::abs(phase - 0.5) - 1.0;
    hz::Expr d = f(S.centre) + f(S.depth) * tri;
    hz::Expr wet = x[at - d.at_most(f(S.centre + S.depth)) * hz::ms];
    hz::Output y{"y"};
    y = f(kSweepMix) * x + f(kSweepMix) * wet;
    return y;
}
constexpr hz::Graph chorus_whole() { return swept<kChorus>(N); }
constexpr hz::Graph chorus_blocks() { return swept<kChorus>(hz::Length{}); }
constexpr hz::Graph longdelay_blocks() { return swept<kLongDelay>(hz::Length{}); }

void bench_heinzel_chorus(benchmark::State& state) {
    bench_mono<chorus_whole>(
        state, [](const float* x, float* y, std::size_t m) { chorus_ref(kChorus, x, y, m); }, 1e-5, "chorus/heinzel: mismatch vs the reference");
}

// One block of `M` samples a run, fed again and again, its state carried; two blocks in a row checked first.
template <auto Build, const Sweep& S, std::int64_t M>
void bench_heinzel_swept_blocks(benchmark::State& state) {
    const auto run = std::make_unique<hz::Executor<Build, heinzel_bench::kMachine>>();
    run->prepare(M);
    OutputVector<float> y(2 * M);
    std::vector<float> ref(y.size());
    for (std::size_t at = 0; at < y.size(); at += M) {
        run->input("x") = g_in.data() + at;
        run->output("y") = y.data() + at;
        run->run(M);
    }
    chorus_ref(S, g_in.data(), ref.data(), ref.size());
    if (maxabs_diff(y.data(), ref.data(), y.size()) > 1e-5) {
        state.SkipWithError("heinzel: two blocks in a row mismatch vs the reference");
        return;
    }
    run->input("x") = g_in.data();
    run->output("y") = y.data();
    for (auto _ : state) {
        run->run(M);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * M);
}

BENCHMARK(bench_heinzel_sine)->Name("sine/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_eq3)->Name("biquad3/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_eq_dyn)->Name("biquad3dyn/heinzel")->RangeMultiplier(4)->Range(256, 16384)->UseRealTime();
BENCHMARK(bench_heinzel_eq3_param)->Name("biquad3/heinzel-param")->UseRealTime();
BENCHMARK(bench_heinzel_eq_param_dyn)->Name("biquad3dyn/heinzel-param")->RangeMultiplier(4)->Range(256, 16384)->UseRealTime();
BENCHMARK(bench_heinzel_strip)->Name("strip/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_delay)->Name("delay/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_fir)->Name("fir/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_envfollow)->Name("envfollow/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_bankmix)->Name("bankmix16/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_chorus)->Name("chorus/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_swept_blocks<chorus_blocks, kChorus, kChorusBlock>)->Name("chorus/heinzel/B64")->UseRealTime();
BENCHMARK(bench_heinzel_swept_blocks<longdelay_blocks, kLongDelay, kChorusBlock>)->Name("longdelay/heinzel/B64")->UseRealTime();
BENCHMARK(bench_heinzel_swept_blocks<longdelay_blocks, kLongDelay, kLongBlock>)->Name("longdelay/heinzel/B4096")->UseRealTime();

} // namespace
