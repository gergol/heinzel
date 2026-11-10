#pragma once

// Heinzel's graphs of the whole-picture rows (eq8_common.hpp has the rows), over the whole input or a `Length` (a block
// given at run time), written as a user writes them: KFR's EQ as eight direct-form-I sections (`iir_arms::df1` over
// the copy `iir_arms::kEq8`, checked bit for bit against `kEq8` before anything is timed), the work outside it in plain
// DSL, the moving band in transposed direct form II, as every arm runs it (once coefficients move, the form changes the
// numbers). Finding the fast form is the library's job. And the way an arm of them is checked and timed, biquad8's
// with its coefficients as Params among them, for both targets that run them (bench_compare_kfr_heinzel_eq8.cpp,
// bench_compare_juce_heinzel_eq8.cpp); only Heinzel's files include it.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <vector>

#include <benchmark/benchmark.h>

#include "eq8_common.hpp"
#include "heinzel_arms.hpp"
#include "iir_arms.hpp"

#include "page_vector.hpp"

namespace eq8_graph {

namespace hz = heinzel;
using heinzel_bench::OutputVector;
using iir_arms::f;
using kfrcmp::Row;
using kfrcmp::kFbClip, kfrcmp::kFbGain, kfrcmp::kFs, kfrcmp::kMixClip, kfrcmp::kMixDry, kfrcmp::kMixGain, kfrcmp::kMixWet;
using kfrcmp::kSweepA, kfrcmp::kSweepCentre, kfrcmp::kSweepInc, kfrcmp::kSweepInvA, kfrcmp::kSweepOctaves, kfrcmp::kSweepQ;

inline constexpr hz::Index n{};

// eq8sweep's fixed seven: the copy of kEq8 without its moving band.
inline constexpr std::array<iir_arms::Section, 7> kEq7 = kfrcmp::fixed_seven(iir_arms::kEq8);

// K direct-form-I sections in series over `s`.
template <std::size_t K>
constexpr hz::Expr cascade(hz::Expr s, const std::array<iir_arms::Section, K>& c) {
    for (const iir_arms::Section& section : c) {
        s = iir_arms::df1(s, section);
    }
    return s;
}

// One section in transposed direct form II over `in`, its coefficients signals of their own: out = b0·in + s1[n-1],
// s1 = s2[n-1] + b1·in − a1·out, s2 = b2·in − a2·out (KFR's `biquad_process`).
constexpr hz::Expr tdf2(hz::Expr in, hz::Expr b0, hz::Expr b1, hz::Expr b2, hz::Expr a1, hz::Expr a2) {
    hz::Variable s1;
    hz::Variable s2;
    const hz::Expr out = b0 * in + s1[n - 1];
    s1[n] = s2[n - 1] + b1 * in - a1 * out;
    s2[n] = b2 * in - a2 * out;
    return out;
}

// biquad8: the EQ alone.
constexpr hz::Graph biquad8(auto length) { return iir_arms::df1_cascade(iir_arms::kEq8, length); }

// lr8: the eighth-order Linkwitz-Riley low-pass, four direct-form-I sections.
constexpr hz::Graph lr8(auto length) { return iir_arms::df1_cascade(iir_arms::kLr8, length); }

// eq8mix: a gain before the EQ, a hard clip and a dry/wet mix after it.
constexpr hz::Graph mix(auto length) {
    hz::Input x{"x", hz::Signal{length}};
    const hz::Expr wet = hz::clamp(cascade(f(kMixGain) * x, iir_arms::kEq8), -f(kMixClip), f(kMixClip));
    return hz::finish(hz::Output{"y"} = f(kMixDry) * x + f(kMixWet) * wet);
}

// eq8fb: the output fed back through a clip into the EQ's input, the loop a variable of its own (y[n] = EQ8(u), u
// reading y[n-1]).
constexpr hz::Graph feedback(auto length) {
    hz::Input x{"x", hz::Signal{length}};
    hz::Variable y;
    y[n] = cascade(x - f(kFbGain) * hz::clamp(y[n - 1], -f(kFbClip), f(kFbClip)), iir_arms::kEq8);
    return hz::finish(hz::Output{"y"} = y);
}

// eq8sweep: the fixed seven, then the band whose centre the LFO moves every sample. RBJ's peak from sin w and cos w,
// as half-turns (w/π = 2·f0/fs: sinpi(w/π), sinpi(w/π + 1/2)); 2^v as e^(v·ln 2); one division by a0.
constexpr hz::Graph sweep(auto length) {
    hz::Input x{"x", hz::Signal{length}};
    hz::Variable phase{hz::Signal{length}};
    phase[n] = hz::frac(phase[n - 1] + f(kSweepInc));
    const hz::Expr centre = f(kSweepCentre) * hz::exp(f(kSweepOctaves) * std::numbers::ln2 * hz::sinpi(2.0 * phase));
    const hz::Expr half_turns = centre * (2.0 / f(kFs));
    const hz::Expr alpha = hz::sinpi(half_turns) * (0.5 / f(kSweepQ));
    const hz::Expr cos_w = hz::sinpi(half_turns + 0.5);
    const hz::Expr r = 1.0 / (1.0 + alpha * f(kSweepInvA));
    const hz::Expr k = -2.0 * cos_w * r; // b1, and a1 alike
    const hz::Expr b0 = (1.0 + alpha * f(kSweepA)) * r;
    const hz::Expr b2 = (1.0 - alpha * f(kSweepA)) * r;
    const hz::Expr a2 = (1.0 - alpha * f(kSweepInvA)) * r;
    return hz::finish(hz::Output{"y"} = tdf2(cascade(x, kEq7), b0, k, b2, k, a2));
}

namespace {

// The graphs' copy holds kEq8's bits (the fixed seven are cut from the same copy), or lr8's, which are the design's.
template <Row R>
const char* copies() {
    if constexpr (R == Row::lr8) {
        if (!kfrcmp::lr8_designed()) {
            return "heinzel: kLr8 differs from the RBJ design";
        }
        return iir_arms::same_bits(iir_arms::kLr8, kfrcmp::kLr8) ? nullptr : "heinzel: the coefficients' copy differs from kLr8";
    }
    return iir_arms::same_bits(iir_arms::kEq8, kfrcmp::kEq8) ? nullptr : "heinzel: the coefficients' copy differs from kEq8";
}

// Row R's arm over the whole input: `Build`'s Executor, one run checked against the row's reference, then timed.
template <Row R, auto Build>
void time_whole(benchmark::State& state) {
    if (const char* why = copies<R>()) {
        state.SkipWithError(why);
        return;
    }
    OutputVector<float> y(static_cast<std::size_t>(kfrcmp::N));
    double err = 0.0;
    heinzel_arms::timed<Build>(
        state, kfrcmp::N,
        [&](auto& run) {
            run.input("x") = kfrcmp::g_sine_in.data();
            run.output("y") = y.data();
        },
        [&]() -> const char* {
            err = kfrcmp::max_error<R>(y.data(), y.size());
            return err <= kfrcmp::spec(R).bound ? nullptr : "heinzel: mismatch vs the double reference";
        });
    state.counters["max_err"] = err;
}

// Row R's arm in blocks: `Build`'s Executor over a `Length`, prepared for kBlock; two blocks in a row checked against
// the row's reference, then the first fed again and again, the state carried.
template <Row R, auto Build>
void time_blocks(benchmark::State& state) {
    if (const char* why = copies<R>()) {
        state.SkipWithError(why);
        return;
    }
    constexpr auto m = static_cast<std::int64_t>(kfrcmp::kBlock);
    const auto run = std::make_unique<hz::Executor<Build, heinzel_bench::kMachine>>();
    run->prepare(m);
    OutputVector<float> y(2 * kfrcmp::kBlock);
    for (std::size_t at = 0; at < y.size(); at += kfrcmp::kBlock) {
        run->input("x") = kfrcmp::g_sine_in.data() + at;
        run->output("y") = y.data() + at;
        run->run(m);
    }
    const double err = kfrcmp::max_error<R>(y.data(), y.size());
    if (!(err <= kfrcmp::spec(R).bound)) {
        state.SkipWithError("heinzel: two blocks in a row mismatch vs the double reference");
        return;
    }
    state.counters["max_err"] = err;
    run->input("x") = kfrcmp::g_sine_in.data();
    run->output("y") = y.data();
    for (auto _ : state) {
        run->run(m);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * m);
}

// The moving band's five Params, as a host sets a knob's.
void set_moving_band(auto& run, const kfrcmp::BiquadCoeffs& c) {
    constexpr const char* const* name = iir_arms::kCoefficient[kfrcmp::kMovingBand];
    run.param(name[0]) = static_cast<double>(c.b0);
    run.param(name[1]) = static_cast<double>(c.b1);
    run.param(name[2]) = static_cast<double>(c.b2);
    run.param(name[3]) = static_cast<double>(c.a1);
    run.param(name[4]) = static_cast<double>(c.a2);
}

// biquad8 with its coefficients Params (`iir_arms::df1_cascade_param<8>`), `Build`'s Executor: kEq8's
// set once, then one run over the whole input, checked against the row's reference.
template <auto Build>
void param_whole(benchmark::State& state) {
    OutputVector<float> y(static_cast<std::size_t>(kfrcmp::N));
    double err = 0.0;
    heinzel_arms::timed<Build>(
        state, kfrcmp::N,
        [&](auto& run) {
            iir_arms::bind_coefficients(run, kfrcmp::kEq8);
            run.input("x") = kfrcmp::g_sine_in.data();
            run.output("y") = y.data();
        },
        [&]() -> const char* {
            err = kfrcmp::max_error<Row::biquad8>(y.data(), y.size());
            return err <= kfrcmp::spec(Row::biquad8).bound ? nullptr : "heinzel: mismatch vs the double reference";
        });
    state.counters["max_err"] = err;
}

// The same in blocks of kBlock, `Build`'s Executor over a `Length`, the state carried. At rest, kEq8's coefficients stay
// as set; moving, the band's are set from the table before every run, and two blocks in a row are checked against the
// sweep's first two. Then the first block is fed again and again.
template <auto Build, bool Moving>
void param_blocks(benchmark::State& state) {
    if (Moving && !kfrcmp::moving_every_block()) {
        state.SkipWithError("heinzel: the moving band repeats the block's before");
        return;
    }
    constexpr auto m = static_cast<std::int64_t>(kfrcmp::kBlock);
    const std::vector<kfrcmp::BiquadCoeffs>& table = kfrcmp::moving_table();
    const auto run = std::make_unique<hz::Executor<Build, heinzel_bench::kMachine>>();
    run->prepare(m);
    iir_arms::bind_coefficients(*run, kfrcmp::kEq8);
    OutputVector<float> y(2 * kfrcmp::kBlock);
    for (std::size_t at = 0, j = 0; at < y.size(); at += kfrcmp::kBlock, ++j) {
        if (Moving) {
            set_moving_band(*run, table[j]);
        }
        run->input("x") = kfrcmp::g_sine_in.data() + at;
        run->output("y") = y.data() + at;
        run->run(m);
    }
    double err = 0.0;
    if (Moving) {
        const std::vector<double> want = kfrcmp::moving_reference(true, y.size());
        for (std::size_t i = 0; i < y.size(); ++i) {
            err = std::max(err, std::fabs(static_cast<double>(y[i]) - want[i]));
        }
    } else {
        err = kfrcmp::max_error<Row::biquad8>(y.data(), y.size());
    }
    if (!(err <= kfrcmp::spec(Row::biquad8).bound)) {
        state.SkipWithError("heinzel: two blocks in a row mismatch vs the double reference");
        return;
    }
    state.counters["max_err"] = err;
    run->input("x") = kfrcmp::g_sine_in.data();
    run->output("y") = y.data();
    std::size_t j = 0;
    for (auto _ : state) {
        if (Moving) {
            set_moving_band(*run, table[j]);
            j = j + 1 == table.size() ? 0 : j + 1;
        }
        run->run(m);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * m);
}

} // namespace
} // namespace eq8_graph
