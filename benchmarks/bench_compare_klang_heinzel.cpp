// The Heinzel arms of the klang comparison (bench_compare_klang, Plan/23 P1 and P7), apart from klang's file because
// klang.h's macros break Heinzel's headers, and an object library of the target so that klang's file is compiled
// without Heinzel's flags:
//   biquad3/heinzel    — the serial high-pass 80 Hz → low-pass 8 kHz → low-pass 3 kHz cascade, as the textbook DF-I;
//   eq/heinzel         — klang's EQ example: a low-pass at 200 Hz and a high-pass at 4 kHz in parallel, the bands
//                        mixed by their weights;
//   sine/heinzel-poly  — the sine at klang's accuracy (an odd degree-7 polynomial over the phase, main's arm), so the
//                        row compares code, not sin's precision;
//   sine/heinzel       — Heinzel's own sine (about 1e-7), what the precise sine costs beside it.
// Each checked against the hand reference (or the analytic sine) with the klang arm's tolerance before it is timed.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>

#include "heinzel_arms.hpp"
#include "iir_arms.hpp"
#include "klang_compare_common.hpp"

#include "page_vector.hpp"

using heinzel_bench::OutputVector;

namespace {

using namespace klangcmp;
using namespace heinzel_arms;

constexpr hz::Graph cascade() { return iir_arms::df1_cascade(iir_arms::kKlangBq3, N); }

constexpr hz::Graph eq() {
    hz::Input x{"x", hz::Signal{N}};
    const hz::Expr lows = iir_arms::df1(x, iir_arms::kKlangEq[0]);
    const hz::Expr highs = iir_arms::df1(x, iir_arms::kKlangEq[1]);
    const hz::Expr mids = x - (lows + highs);
    return hz::finish(hz::Output{"y"} = f(kWLow) * lows + f(kWMid) * mids + f(kWHigh) * highs);
}

// klang's accuracy class: x = (0.5 − p)·2π over the wrapped phase, sin(x) = sin(2π·p), a polynomial in x².
constexpr hz::Expr poly_sine(hz::Expr p) {
    const hz::Expr x = (0.5 - p) * f(kTwoPi);
    const hz::Expr x2 = x * x;
    return x * (f(0.99945009f) + x2 * (f(-0.165838337f) + x2 * (f(0.00799855086f) + x2 * f(-0.000147738668f))));
}
constexpr hz::Graph sine_poly() {
    hz::Param inc{"inc"};
    return hz::finish(hz::Output{"y"} = poly_sine(hz::frac(hz::iota(hz::Shape{N}) * inc)) * f(kAmp));
}
constexpr hz::Graph sine_exact() { return hz::finish(hz::Output{"y"} = sine(N, kAmp)); }

void bench_heinzel_biquad3(benchmark::State& state) {
    if (!iir_arms::same_bits(iir_arms::kKlangBq3, kBq3)) {
        state.SkipWithError("biquad3/heinzel: the coefficients' copy differs from kBq3");
        return;
    }
    OutputVector<float> y(N);
    std::vector<float> ref(N);
    hand_biquad3(g_in.data(), ref.data(), N);
    timed<cascade>(
        state, N,
        [&](auto& run) {
            run.input("x") = g_in.data();
            run.output("y") = y.data();
        },
        [&]() -> const char* { return max_abs_diff_ok(y.data(), ref.data(), N, 1e-3f) ? nullptr : "biquad3/heinzel: mismatch vs hand DF-II reference"; });
}

void bench_heinzel_eq(benchmark::State& state) {
    const std::array<BiquadCoeffs, 2> designed{kLpfEq, kHpfEq};
    if (!iir_arms::same_bits(iir_arms::kKlangEq, designed)) {
        state.SkipWithError("eq/heinzel: the coefficients' copy differs from kLpfEq, kHpfEq");
        return;
    }
    OutputVector<float> y(N);
    std::vector<float> ref(N);
    hand_eq(g_in.data(), ref.data(), N);
    timed<eq>(
        state, N,
        [&](auto& run) {
            run.input("x") = g_in.data();
            run.output("y") = y.data();
        },
        [&]() -> const char* { return max_abs_diff_ok(y.data(), ref.data(), N, 1e-3f) ? nullptr : "eq/heinzel: mismatch vs hand reference"; });
}

template <auto Build>
void bench_heinzel_sine(benchmark::State& state, float tol) {
    OutputVector<float> y(N);
    timed<Build>(
        state, N,
        [&](auto& run) {
            run.param("inc") = static_cast<double>(kFreq / kFs);
            run.output("y") = y.data();
        },
        [&]() -> const char* {
            for (std::size_t i = 0; i < 1024; ++i) {
                if (std::fabs(y[i] - kAmp * std::sin(kTwoPi * kFreq * static_cast<float>(i) / kFs)) > tol) {
                    return "sine/heinzel: mismatch vs the analytic sine";
                }
            }
            return nullptr;
        });
}
void bench_heinzel_sine_poly(benchmark::State& state) { bench_heinzel_sine<sine_poly>(state, 2e-3f); }
void bench_heinzel_sine_exact(benchmark::State& state) { bench_heinzel_sine<sine_exact>(state, 1e-4f); }

BENCHMARK(bench_heinzel_biquad3)->Name("biquad3/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_eq)->Name("eq/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_sine_poly)->Name("sine/heinzel-poly")->UseRealTime();
BENCHMARK(bench_heinzel_sine_exact)->Name("sine/heinzel")->UseRealTime();

} // namespace
