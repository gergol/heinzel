// The Heinzel arms of the JUCE comparison's coupled recurrences (bench_compare_juce, Plan/23 P7; the others are
// bench_compare_juce_heinzel.cpp's): the ladder (one voice and a bank of 16), the voice, the TPT state-variable filter
// (one voice and 16), the Linkwitz-Riley low-pass and MAI_verb's reverb, each a graph on `Executor<…, kMachine>`,
// checked against the hand reference before it is timed. The banks take their 16 voices sample-major, as Heinzel lays a
// bank out: the input, voice after voice for JUCE, is interleaved once, before the timed runs.
//
// The ladders run three saturators: `heinzel`, Heinzel's exact `tanh` (its float core, within 2 ulp), `heinzel-pade`,
// main's register-only Padé form (about 2e-2 off `tanh`), and `heinzel-table`, JUCE's own: `tanh` at 128 points from -5
// to 5, read by `lookup` (clamped, blended between two points; about 7e-4 off `tanh`), as JUCE's ladder reads it.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>

#include "heinzel/runtime/exponential_cores.hpp"

#include "heinzel_arms.hpp"
#include "juce_compare_common.hpp"
#include "juce_compare_refs.hpp"

#include "page_vector.hpp"

using heinzel_bench::PageVector;
using heinzel_bench::OutputVector;

namespace {

using namespace jucecmp;
using namespace heinzel_arms;

// main's Padé saturator: clamped to ±3, then z(27 + z²)/(27 + 9z²).
constexpr hz::Expr pade(hz::Expr z) {
    const hz::Expr c = hz::clamp(z, -3.0, 3.0);
    const hz::Expr c2 = c * c;
    return c * (27.0 + c2) / (27.0 + 9.0 * c2);
}
constexpr hz::Expr exact(hz::Expr z) { return hz::tanh(z); }

// JUCE's saturator (`LadderFilter`'s `saturationLUT`): tanh at 128 points spread from -5 to 5 as JUCE spreads them, in
// float, each point's tanh rounded to float (Heinzel's double core: `std::tanh` is no constant expression).
constexpr std::array<float, 128> kTanhPoints = [] {
    std::array<float, 128> t{};
    for (std::size_t i = 0; i < t.size(); ++i) {
        const float x = -5.0f + (10.0f * static_cast<float>(i)) / 127.0f;
        t[i] = static_cast<float>(hz::hyperbolic_tangent(static_cast<double>(x), hz::FnAccuracy::Exact));
    }
    return t;
}();

// JUCE's ladder (LPF12) over `x`, the drive and the resonance feedback through `sat`: the 2nd stage times `out`.
template <class Sat>
constexpr hz::Expr ladder(hz::Expr x, Sat sat, float out) {
    hz::Variable s0;
    hz::Variable s1;
    hz::Variable s2;
    hz::Variable s3;
    hz::Variable s4;
    const hz::Expr dx = f(kLadGain) * sat(f(kLadDrive) * x);
    s0[n] = dx + f(kLadK) * (f(kLadGain2) * sat(f(kLadDrive2) * s4[n - 1]) - dx * f(kLadComp));
    s1[n] = f(kLadB1) * s0[n - 1] + f(kLadA1) * s1[n - 1] + f(kLadB0) * s0;
    s2[n] = f(kLadB1) * s1[n - 1] + f(kLadA1) * s2[n - 1] + f(kLadB0) * s1;
    s3[n] = f(kLadB1) * s2[n - 1] + f(kLadA1) * s3[n - 1] + f(kLadB0) * s2;
    s4[n] = f(kLadB1) * s3[n - 1] + f(kLadA1) * s4[n - 1] + f(kLadB0) * s3;
    return f(out) * s2;
}

// One TPT state-variable low-pass over `in`, as JUCE's `processSample` writes it; its low-pass out.
constexpr hz::Expr tpt(hz::Expr in, const Tpt& c) {
    hz::Variable s1;
    hz::Variable s2;
    const hz::Expr hp = (in - f(c.r2 + c.g) * s1[n - 1] - s2[n - 1]) * f(c.h);
    const hz::Expr bp = f(c.g) * hp + s1[n - 1];
    s1[n] = f(c.g) * hp + bp;
    const hz::Expr lp = f(c.g) * bp + s2[n - 1];
    s2[n] = f(c.g) * bp + lp;
    return lp;
}

// `kSvf` and `kLr`: JUCE's TPT coefficients at 1 kHz (Q 0.707) and at 2 kHz (R2 = √2).
constexpr Tpt kSvfCopy{0x1.0c774ep-4f, 0x1.6a09e6p+0f, 0x1.d2bb7cp-1f};
constexpr Tpt kLrCopy{0x1.0d9fd4p-3f, 0x1.6a09e6p+0f, 0x1.a96b78p-1f};

template <auto Sat>
constexpr hz::Graph ladder_graph() {
    hz::Input x{"x", hz::Signal{N}};
    return hz::finish(hz::Output{"y"} = ladder(x, Sat, kLadOut));
}
// The ladder on JUCE's table, one table both saturators read.
constexpr hz::Graph ladder_table_graph() {
    hz::Input x{"x", hz::Signal{N}};
    const hz::Expr points = hz::constant(kTanhPoints);
    const auto table_tanh = [points](hz::Expr z) { return hz::lookup(points, z, -5.0, 5.0); };
    return hz::finish(hz::Output{"y"} = ladder(x, table_tanh, kLadOut));
}
constexpr hz::Graph ladder16_graph() {
    hz::Input x{"x", hz::Signal{kCh, kNc}};
    return hz::finish(hz::Output{"y"} = ladder(x, exact, kLadOut));
}
constexpr hz::Graph svf_graph() {
    hz::Input x{"x", hz::Signal{N}};
    return hz::finish(hz::Output{"y"} = tpt(x, kSvfCopy));
}
constexpr hz::Graph svf16_graph() {
    hz::Input x{"x", hz::Signal{kCh, kNc}};
    return hz::finish(hz::Output{"y"} = tpt(x, kSvfCopy));
}
constexpr hz::Graph linkwitz_graph() {
    hz::Input x{"x", hz::Signal{N}};
    return hz::finish(hz::Output{"y"} = tpt(tpt(x, kLrCopy), kLrCopy));
}

// The voice: the oscillator a phase accumulator in cycles (a signal, so the ladder can run over it), the sample read
// before the step, as JUCE's Oscillator starts at phase 0; then the ladder, its gain and the voice's folded.
constexpr hz::Graph voice_graph() {
    hz::Param inc{"inc"};
    hz::Variable phase{hz::Signal{N}};
    phase[n] = hz::frac(phase[n - 1] + inc);
    const hz::Expr osc = hz::dsp::wave::sine(phase[n - 1]) * f(kVoiceAmp);
    return hz::finish(hz::Output{"y"} = ladder(osc, exact, kLadOut * kVoiceGain));
}

// MAI_verb: four combs whose damping low-pass is one state, shared in tuning order (g_k = o_k·(1 − d) + g_{k−1}·d, from
// the last sample's g_4), their sum through four allpasses, mixed with the input, `tanh`.
constexpr hz::Graph maiverb_graph() {
    hz::Input x{"x", hz::Signal{N}};
    std::array<hz::Variable, 4> line{};
    hz::Variable damped;
    std::array<hz::Expr, 4> out{};
    std::array<hz::Expr, 4> g{};
    hz::Expr d = damped[n - 1];
    for (std::size_t k = 0; k < 4; ++k) {
        out[k] = line[k][n - kVerbComb[k]];
        d = out[k] * f(1.0f - kVerbDamp) + d * f(kVerbDamp);
        g[k] = d;
    }
    damped[n] = d;
    for (std::size_t k = 0; k < 4; ++k) {
        line[k][n] = x + g[k] * f(kVerbFeedback);
    }
    hz::Expr a = (out[0] + out[1] + out[2] + out[3]) * 0.25;
    for (const int size : kVerbAllpass) {
        a = allpass(a, size, kVerbAllpassFb);
    }
    return hz::finish(hz::Output{"y"} = hz::tanh(a * f(kVerbWet) + x * f(1.0f - kVerbWet)));
}

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

// The bank: JUCE's 16 voices of kNc samples, voice after voice, as Heinzel's sample-major bank; each voice checked
// against `reference` over its own slice.
template <auto Build>
void bench_bank(benchmark::State& state, void (*reference)(const float*, float*, std::size_t), double tol, const char* what) {
    PageVector<float> xi(static_cast<std::size_t>(N));
    OutputVector<float> y(xi.size());
    std::vector<float> ref(xi.size());
    for (std::size_t c = 0; c < kCh; ++c) {
        for (std::size_t i = 0; i < kNc; ++i) {
            xi[i * kCh + c] = g_in[c * kNc + i];
        }
        reference(g_in.data() + c * kNc, ref.data() + c * kNc, kNc);
    }
    timed<Build>(
        state, N,
        [&](auto& run) {
            run.input("x") = xi.data();
            run.output("y") = y.data();
        },
        [&]() -> const char* {
            for (std::size_t c = 0; c < kCh; ++c) {
                for (std::size_t i = 0; i < kNc; ++i) {
                    if (std::fabs(static_cast<double>(y[i * kCh + c]) - ref[c * kNc + i]) > tol) {
                        return what;
                    }
                }
            }
            return nullptr;
        });
}

void exact_ladder(const float* x, float* y, std::size_t m) { ladder_ref(tanh_exact, x, y, m); }
void pade_ladder(const float* x, float* y, std::size_t m) { ladder_ref(tanh_pade, x, y, m); }

void bench_heinzel_ladder(benchmark::State& state) {
    bench_mono<ladder_graph<exact>>(state, exact_ladder, 1e-3, "ladder/heinzel: mismatch vs the exact-tanh ladder");
}
void bench_heinzel_ladder_pade(benchmark::State& state) {
    bench_mono<ladder_graph<pade>>(state, pade_ladder, 1e-3, "ladder/heinzel-pade: mismatch vs the Pade ladder");
}
void bench_heinzel_ladder_table(benchmark::State& state) {
    bench_mono<ladder_table_graph>(state, exact_ladder, 2e-2, "ladder/heinzel-table: mismatch vs the exact-tanh ladder");
}
void bench_heinzel_ladder16(benchmark::State& state) {
    bench_bank<ladder16_graph>(state, exact_ladder, 1e-4, "ladder16/heinzel: a voice differs from the exact-tanh ladder");
}

void bench_heinzel_voice(benchmark::State& state) {
    OutputVector<float> y(static_cast<std::size_t>(N));
    std::vector<float> osc(y.size());
    std::vector<float> ref(y.size());
    accumulated_sine<double>(osc.data(), osc.size());
    voice_ref(osc.data(), ref.data(), ref.size());
    timed<voice_graph>(
        state, N,
        [&](auto& run) {
            run.param("inc") = static_cast<double>(kVoiceFreq / kFs);
            run.output("y") = y.data();
        },
        [&]() -> const char* { return maxabs_diff(y.data(), ref.data(), y.size()) <= 2e-3 ? nullptr : "voice/heinzel: mismatch vs the voice loop"; });
}

// The TPT filters run the copies; the copies hold the designs' bits.
const char* tpt_copies() {
    return same_bits(kSvfCopy, kSvf) && same_bits(kLrCopy, kLr) ? nullptr : "heinzel: a TPT coefficients' copy differs from the design";
}
void bench_heinzel_svf(benchmark::State& state) {
    if (const char* why = tpt_copies()) {
        state.SkipWithError(why);
        return;
    }
    bench_mono<svf_graph>(state, svf_ref, 1e-3, "svf/heinzel: mismatch vs the TPT loop");
}
void bench_heinzel_svf16(benchmark::State& state) {
    if (const char* why = tpt_copies()) {
        state.SkipWithError(why);
        return;
    }
    bench_bank<svf16_graph>(state, svf_ref, 1e-3, "svf16/heinzel: a voice differs from the TPT loop");
}
void bench_heinzel_linkwitz(benchmark::State& state) {
    if (const char* why = tpt_copies()) {
        state.SkipWithError(why);
        return;
    }
    bench_mono<linkwitz_graph>(state, linkwitz_ref, 1e-3, "linkwitz/heinzel: mismatch vs the two TPT stages");
}
void bench_heinzel_maiverb(benchmark::State& state) {
    bench_mono<maiverb_graph>(state, maiverb_ref, 2e-3, "maiverb/heinzel: mismatch vs the reverb loop");
}

BENCHMARK(bench_heinzel_ladder)->Name("ladder/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_ladder_pade)->Name("ladder/heinzel-pade")->UseRealTime();
BENCHMARK(bench_heinzel_ladder_table)->Name("ladder/heinzel-table")->UseRealTime();
BENCHMARK(bench_heinzel_ladder16)->Name("ladder16/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_voice)->Name("voice/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_svf)->Name("svf/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_svf16)->Name("svf16/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_linkwitz)->Name("linkwitz/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_maiverb)->Name("maiverb/heinzel")->UseRealTime();

} // namespace
