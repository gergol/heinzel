// The benchmarks' Param arms as `benchmarks/iir_arms.hpp`, `eq8_graph.hpp` and `bench_knobs.cpp` write them, for
// param_arms_test and param_moving_test (`docs/sections-in-lanes.md`): their coefficients, their Params' names
// and their graphs; and the noise both feed them, and how far a run lies from its reference.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

#include "heinzel/frontend/declarations.hpp"
#include "heinzel/frontend/graph_builder.hpp"

namespace param_arms {

using namespace heinzel;

inline constexpr Index n{};
inline constexpr double kRate = 48'000;

using Coefficients = std::array<float, 5>; // b0, b1, b2, a1, a2

// `iir_arms.hpp`'s copies: kEq (a high-pass at 80 Hz, a 1 kHz peak, a low-pass at 8 kHz), kEq8 (KFR's graphic EQ,
// 40 Hz to 12 kHz) and kLr8 (an eighth-order Linkwitz-Riley low-pass at 2 kHz), at 48 kHz.
inline constexpr std::array<Coefficients, 3> kEq{{
    {0x1.fc38e2p-1f, -0x1.fc38e2p+0f, 0x1.fc38e2p-1f, -0x1.fc355p+0f, 0x1.f878e4p-1f},
    {0x1.0b4082p+0f, -0x1.e533bcp+0f, 0x1.bc4618p-1f, -0x1.e533bcp+0f, 0x1.d2c71cp-1f},
    {0x1.3d86bap-3f, 0x1.3d86bap-2f, 0x1.3d86bap-3f, -0x1.3d86bap-1f, 0x1.ec35d2p-3f},
}};
inline constexpr std::array<Coefficients, 8> kEq8{{
    {0x1.fe1b8ap-1f, -0x1.fe1b8ap+0f, 0x1.fe1b8ap-1f, -0x1.fe1aa4p+0f, 0x1.fc38dep-1f},
    {0x1.00c51p+0f, -0x1.fc349ap+0f, 0x1.f6ff3p-1f, -0x1.fc349ap+0f, 0x1.f8894ep-1f},
    {0x1.fcd68p-1f, -0x1.f4e78cp+0f, 0x1.ed81fcp-1f, -0x1.f4e78cp+0f, 0x1.ea587ap-1f},
    {0x1.032206p+0f, -0x1.efbf68p+0f, 0x1.db5b6p-1f, -0x1.efbf68p+0f, 0x1.e19f7p-1f},
    {0x1.f422a6p-1f, -0x1.d357aep+0f, 0x1.ba9d6ep-1f, -0x1.d357aep+0f, 0x1.aec014p-1f},
    {0x1.0b65e2p+0f, -0x1.b92dcep+0f, 0x1.7ab028p-1f, -0x1.b92dcep+0f, 0x1.917beep-1f},
    {0x1.dae2e8p-1f, -0x1.4d5a2cp+0f, 0x1.26f52cp-1f, -0x1.4d5a2cp+0f, 0x1.01d814p-1f},
    {0x1.2be764p-2f, 0x1.2be764p-1f, 0x1.2be764p-2f, 0x1.b7df4cp-25f, 0x1.5f3b2cp-3f},
}};
inline constexpr std::array<Coefficients, 4> kLr8{{
    {0x1.c28a2p-7f, 0x1.c28a2p-6f, 0x1.c28a2p-7f, -0x1.8f1e2ep+0f, 0x1.3a64fep-1f},
    {0x1.fbf5cep-7f, 0x1.fbf5cep-6f, 0x1.fbf5cep-7f, -0x1.c1fc24p+0f, 0x1.a3b7a4p-1f},
    {0x1.c28a2p-7f, 0x1.c28a2p-6f, 0x1.c28a2p-7f, -0x1.8f1e2ep+0f, 0x1.3a64fep-1f},
    {0x1.fbf5cep-7f, 0x1.fbf5cep-6f, 0x1.fbf5cep-7f, -0x1.c1fc24p+0f, 0x1.a3b7a4p-1f},
}};

// Section j's five Params, and band j's three knobs.
inline constexpr std::array<std::array<const char*, 5>, 8> kCoefficient{{
    {"b0_0", "b1_0", "b2_0", "a1_0", "a2_0"},
    {"b0_1", "b1_1", "b2_1", "a1_1", "a2_1"},
    {"b0_2", "b1_2", "b2_2", "a1_2", "a2_2"},
    {"b0_3", "b1_3", "b2_3", "a1_3", "a2_3"},
    {"b0_4", "b1_4", "b2_4", "a1_4", "a2_4"},
    {"b0_5", "b1_5", "b2_5", "a1_5", "a2_5"},
    {"b0_6", "b1_6", "b2_6", "a1_6", "a2_6"},
    {"b0_7", "b1_7", "b2_7", "a1_7", "a2_7"},
}};
inline constexpr std::array<std::array<const char*, 3>, 8> kKnob{{
    {"freq0", "gain0", "q0"},
    {"freq1", "gain1", "q1"},
    {"freq2", "gain2", "q2"},
    {"freq3", "gain3", "q3"},
    {"freq4", "gain4", "q4"},
    {"freq5", "gain5", "q5"},
    {"freq6", "gain6", "q6"},
    {"freq7", "gain7", "q7"},
}};

// x over `Samples` samples, or over a Length given at run time where `Samples` is 0.
template <std::int32_t Samples>
constexpr Signal stream() {
    if constexpr (Samples == 0) {
        return Signal{Length{}};
    } else {
        return Signal{Samples};
    }
}

// `iir_arms::df1_param`: section j over x, its coefficients its five Params.
constexpr Expr df1_param(Expr x, std::size_t j) {
    const std::array<const char*, 5>& name = kCoefficient[j];
    Param b0{name[0]};
    Param b1{name[1]};
    Param b2{name[2]};
    Param a1{name[3]};
    Param a2{name[4]};
    Variable y;
    y[n] = b0 * x + b1 * x[n - 1] + b2 * x[n - 2] - a1 * y[n - 1] - a2 * y[n - 2];
    return y;
}

// `iir_arms::df1`: a section, its coefficients constants.
constexpr Expr df1(Expr x, const Coefficients& c) {
    const auto f = [](float v) { return static_cast<double>(v); };
    Variable y;
    y[n] = f(c[0]) * x + f(c[1]) * x[n - 1] + f(c[2]) * x[n - 2] - f(c[3]) * y[n - 1] - f(c[4]) * y[n - 2];
    return y;
}

// `bench_knobs`' peak: band j's RBJ peak, its coefficients worked out from its three knobs.
constexpr Expr peak(Expr x, std::size_t j) {
    const std::array<const char*, 3>& name = kKnob[j];
    Param freq{name[0], ge(20), le(20000)};
    Param gain{name[1], ge(-24), le(24)};
    Param q{name[2], ge(0.1), le(10)};
    const Expr turns = freq * (2.0 / kRate);
    const Expr c = cospi(turns);
    const Expr s = sinpi(turns);
    const Expr big = exp(gain * (std::numbers::ln10 / 40.0));
    const Expr alpha = s / (2.0 * q);
    const Expr a0 = (1.0 + alpha / big).promise(gt(0));
    const Expr b0 = (1.0 + alpha * big) / a0;
    const Expr b1 = -2.0 * c / a0;
    const Expr b2 = (1.0 - alpha * big) / a0;
    const Expr a1 = -2.0 * c / a0;
    const Expr a2 = (1.0 - alpha / big) / a0;
    Variable y;
    y[n] = b0 * x + b1 * x[n - 1] + b2 * x[n - 2] - a1 * y[n - 1] - a2 * y[n - 2];
    return y;
}

// `iir_arms::df1_cascade_param<K>`.
template <std::size_t K, std::int32_t Samples>
constexpr Graph param_cascade() {
    Input x{"x", stream<Samples>()};
    Expr s = x;
    for (std::size_t j = 0; j < K; ++j) {
        s = df1_param(s, j);
    }
    return finish(Output{"y"} = s);
}

// `eq8_graph::lr8`.
template <std::int32_t Samples>
constexpr Graph lr8() {
    Input x{"x", stream<Samples>()};
    Expr s = x;
    for (const Coefficients& c : kLr8) {
        s = df1(s, c);
    }
    return finish(Output{"y"} = s);
}

// `bench_knobs`' `knob_eq<First, Count>`: bands [First, First + Count) over a Length.
template <std::size_t First, std::size_t Count>
constexpr Graph knob_eq() {
    Input x{"x", Signal{Length{}}};
    Expr s = x;
    for (std::size_t j = First; j < First + Count; ++j) {
        s = peak(s, j);
    }
    return finish(Output{"y"} = s);
}

// `count` samples of noise in [-1, 1], each number a float.
inline std::vector<double> noise(std::size_t count) {
    std::vector<double> x(count);
    std::uint32_t state = 20261025u;
    for (double& v : x) {
        state = state * 1664525u + 1013904223u;
        v = static_cast<float>(static_cast<double>(static_cast<std::int32_t>(state >> 8) % 2001 - 1000) * 0.001);
    }
    return x;
}

// How far `got` lies from `want`, relative to `want`'s largest magnitude.
inline double off_by(const std::vector<double>& got, const std::vector<double>& want) {
    double scale = 0;
    double worst = 0;
    for (std::size_t i = 0; i < want.size(); ++i) {
        scale = std::max(scale, std::fabs(want[i]));
        worst = std::max(worst, std::fabs(got[i] - want[i]));
    }
    return worst / std::max(scale, 1e-30);
}

} // namespace param_arms
