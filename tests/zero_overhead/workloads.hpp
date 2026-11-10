#pragma once

// The workloads' graphs in this tree's DSL, each in the namespace where `hand.hpp` gives its ports, its hand loop and
// its bound.

#include <array>
#include <cstddef>
#include <cstdint>

#include "heinzel/frontend/declarations.hpp"
#include "heinzel/frontend/graph_builder.hpp"

#include "corpus_graphs.hpp"
#include "hand.hpp"

namespace zo {

using namespace heinzel;

inline constexpr Index n{48'000};

} // namespace zo

namespace zo::w1 { // gain

constexpr Graph build() {
    Input x{"x", Signal{64}};
    Param g{"g"};
    Output y{"y"};
    y = x * g;
    return y;
}

} // namespace zo::w1

namespace zo::w2 { // elem, a Length

constexpr Graph build() { return corpus::elem(); }

} // namespace zo::w2

namespace zo::w3 { // biquad

constexpr Graph build() {
    Input x{"x", Signal{64}};
    Output y{"y"};
    y[n] = x[n] + kC1 * y[n - 1] + kC2 * y[n - 2];
    return y;
}

} // namespace zo::w3

namespace zo::w4 { // comb

constexpr Graph build() {
    Input x{"x", Signal{512}};
    Output y{"y"};
    y[n] = x[n] + 0.5 * y[n - 40];
    return y;
}

} // namespace zo::w4

namespace zo::w5 { // fir, 16 literal taps

constexpr Graph build() {
    Input x{"x", Signal{512}};
    Output y{"y"};
    Expr sum = kTaps[0] * x[n];
    for (std::int32_t k = 1; k < 16; ++k) {
        sum = sum + kTaps[static_cast<std::size_t>(k)] * x[n - k];
    }
    y = sum;
    return y;
}

} // namespace zo::w5

namespace zo::w6 { // rowsum

constexpr Graph build() {
    Input x{"x", {64, 64}};
    Output y{"y"};
    y = x.sum(1);
    return y;
}

} // namespace zo::w6

namespace zo::w7 { // matmul

constexpr Graph build() { return corpus::matmul<64, 64, 64>(); }

} // namespace zo::w7

namespace zo::w8 { // blur, banded

constexpr Graph build() { return corpus::blurxy<1024, 2048, 3>(); }

} // namespace zo::w8

namespace zo::w9 { // voice: `examples/synth_voice.cpp`'s graph, from `Plan/archive/13-loop-tree/measure/voice.hpp`

inline constexpr int kRate = 48'000;

constexpr Expr envelope(Expr gate, units::Quantity attack, units::Quantity release) {
    Variable level;
    Expr last = level[n - 1];
    level[n] = last + max(gate[n] - last, 0.0) / samples(n, attack) + min(gate[n] - last, 0.0) / samples(n, release);
    return level;
}

constexpr Expr saw(Expr hz) {
    Variable phase;
    phase[n] = frac(phase[n - 1] + hz[n] / kRate);
    return 2.0 * phase - 1.0;
}

constexpr Expr lowpass(Expr x, Expr cutoff, double damping) {
    Expr f = 2.0 * sinpi(cutoff / kRate);
    Variable low;
    Variable band;
    low[n] = low[n - 1] + f * band[n - 1];
    band[n] = band[n - 1] + f * (x[n] - low[n] - damping * band[n - 1]);
    return low;
}

constexpr Expr echo(Expr dry, units::Quantity time, double feedback) {
    Variable wet;
    wet[n] = dry[n] + feedback * wet[n - time];
    return wet;
}

constexpr Graph build() {
    Input pitch{"pitch", Signal{512}};
    Input gate{"gate", Signal{512}};
    Output out{"out"};
    Expr amp = envelope(gate, 5.0 * ms, 60.0 * ms);
    Expr tone = tanh(1.5 * lowpass(0.5 * (saw(pitch) + saw(pitch * 1.006)), 150.0 + 4000.0 * amp * amp, 0.3));
    out = 0.4 * echo(amp * tone, 375.0 * ms, 0.35);
    return out;
}

} // namespace zo::w9

namespace zo::w10 { // kernel, 3 constant taps

constexpr Graph build() {
    Input x{"x", {4096}};
    Output y{"y"};
    y = (x.unfold(0, 3) * constant(kSmooth)).sum(1);
    return y;
}

} // namespace zo::w10

// W1 with ranges (Plan/26-value-ranges/04): a ranged input, a ranged Param and a promise the proof does not prove. In
// release it is W1, byte for byte and instruction for instruction (`zero_overhead.cmake`'s SAME); with asserts on, the
// Executor checks all three. The harness's numbers keep them: x in [-1, 1), g 0.033.
namespace zo::w11 {

constexpr Graph build() {
    Input x{"x", Signal{64}, ge(-1), le(1)};
    Param g{"g", ge(0), le(1)};
    Output y{"y"};
    y = (x * g).promise(ge(-0.5), le(0.5));
    return y;
}

} // namespace zo::w11

// W12, sections in lanes (`docs/sections-in-lanes.md`): lr8 (an eighth-order Linkwitz-Riley low-pass at 2 kHz
// as four RBJ biquads, Q 0.541, 1.307, 0.541, 1.307), its coefficients constants, which the lanes rule takes at Normal:
// one skewed stage, the sections' constants one [4, 1] tensor. W3's sign on the poles' terms.
namespace zo::w12 {

inline constexpr std::array<std::array<float, 5>, 2> kSections{{
    {0x1.c28a2p-7f, 0x1.c28a2p-6f, 0x1.c28a2p-7f, 0x1.8f1e2ep+0f, -0x1.3a64fep-1f},
    {0x1.fbf5cep-7f, 0x1.fbf5cep-6f, 0x1.fbf5cep-7f, 0x1.c1fc24p+0f, -0x1.a3b7a4p-1f},
}};

constexpr Expr section(Expr x, const std::array<float, 5>& k) {
    const auto c = [&k](std::size_t i) { return static_cast<double>(k[i]); };
    Variable y;
    y[n] = c(0) * x + c(1) * x[n - 1] + c(2) * x[n - 2] + c(3) * y[n - 1] + c(4) * y[n - 2];
    return y;
}

constexpr Graph build() {
    Input x{"x", Signal{512}};
    Expr s = x;
    for (std::size_t j = 0; j < 4; ++j) {
        s = section(s, kSections[j % 2]);
    }
    Output y{"y"};
    y = s;
    return y;
}

} // namespace zo::w12
