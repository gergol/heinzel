#pragma once

// What the Heinzel arms of the comparison benches share (Plan/23 P7): the way an arm is checked and timed, and the
// pieces of DSP their graphs are built from, each written as the hand reference it is checked against computes it.
// Only the arms' own translation units include this: the libraries' and the hand arms' are compiled without
// Heinzel's flags (`benchmarks/CMakeLists.txt`).

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include <benchmark/benchmark.h>

#include "heinzel/dsp/oscillator.hpp"
#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"

#include "bench_machine.hpp"

namespace heinzel_arms {

namespace hz = heinzel;

inline constexpr hz::Index n{};

// A float constant as the graph's constant, exactly.
constexpr double f(float v) { return static_cast<double>(v); }

// Whether `copy`, the `constexpr` copy of a set of floats designed at run time, holds exactly the designed bits: a copy
// that drifted from its source skips the arm instead of timing another filter.
template <class T>
bool same_bits(const T& copy, const T& designed) {
    return std::memcmp(&copy, &designed, sizeof(T)) == 0;
}

// One arm: `Build`'s Executor for the bench's machine (`heinzel_bench::kMachine`), made once, its ports bound (`bind`),
// one run checked (`check`, which names what differs, or returns nullptr), then timed run after run; `items` samples a
// run.
template <auto Build, class Bind, class Check>
void timed(benchmark::State& state, std::int64_t items, Bind bind, Check check) {
    const auto run = std::make_unique<hz::Executor<Build, heinzel_bench::kMachine>>();
    bind(*run);
    run->run();
    if (const char* why = check()) {
        state.SkipWithError(why);
        return;
    }
    for (auto _ : state) {
        run->run();
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * items);
}

// A sine over `samples` samples at a constant pitch: `amp · sin(2π · frac(i · inc))`, `inc` a Param.
constexpr hz::Expr sine(std::int32_t samples, float amp) {
    hz::Param inc{"inc"};
    return hz::dsp::affine_oscillator(hz::Shape{samples}, inc, hz::constant(f(amp)));
}

// A Schroeder allpass of `size` samples, the hand loop's buffer `buf[n] = in + fb·buf[n - size]`, out `buf[n - size] -
// in`.
constexpr hz::Expr allpass(hz::Expr in, std::int32_t size, float fb) {
    hz::Variable buf;
    const hz::Expr out = buf[n - size];
    buf[n] = in + out * f(fb);
    return -in + out;
}

// Freeverb's comb of `size` samples, its damping low-pass `lp` and its line `v`: `o = v[n - size]`, `lp = o·(1 −
// damp) + lp[n-1]·damp`, `v[n] = in + lp·feedback`; out `o`.
constexpr hz::Expr comb(hz::Expr in, std::int32_t size, float feedback, float damp) {
    hz::Variable v;
    hz::Variable lp;
    const hz::Expr o = v[n - size];
    lp[n] = o * f(1.0f - damp) + lp[n - 1] * f(damp);
    v[n] = in + lp * f(feedback);
    return o;
}

// Freeverb, mono: the input times `gain` into eight combs in parallel, summed, through four allpasses in series.
template <class Tuning>
constexpr hz::Expr freeverb(hz::Expr x, const Tuning& t, float feedback, float damp, float wet, float dry) {
    const hz::Expr in = x * f(t.gain);
    hz::Expr acc = comb(in, t.comb[0], feedback, damp);
    for (std::size_t k = 1; k < t.comb.size(); ++k) {
        acc = acc + comb(in, t.comb[k], feedback, damp);
    }
    for (const std::int32_t size : t.allpass) {
        acc = allpass(acc, size, t.allpass_feedback);
    }
    return acc * f(wet) + x * f(dry);
}

// The channel strip: a gain, three `tanh` saturators `tanh(d·s + b)`, a gain, clamped to ±1.
struct Strip {
    float gain_in;
    float d1;
    float b1;
    float d2;
    float b2;
    float d3;
    float b3;
    float gain_out;
};
constexpr hz::Expr strip(hz::Expr x, const Strip& c) {
    hz::Expr s = f(c.gain_in) * x;
    s = hz::tanh(f(c.d1) * s + f(c.b1));
    s = hz::tanh(f(c.d2) * s + f(c.b2));
    s = hz::tanh(f(c.d3) * s + f(c.b3));
    return hz::clamp(f(c.gain_out) * s, -1.0, 1.0);
}

} // namespace heinzel_arms
