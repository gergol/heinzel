// Lane-recurrence benchmark — the rewrite arm, re-expressed against the NEW API.
//
// Ported from ctg3 benchmarks/lane_recurrence_bench.cpp. Workload: a bank of C=16 independent one-pole
// voices, each a serial recurrence along time, y[c][n] = x[c][n] + a·y[c][n-1], N=1<<16 samples/voice,
// a=0.95; and the heavy 5-state ladder bank (below). The old bench's laned-ILP / relayout / calibrated
// ARMS are scheduling variants driven by a roofline cost model (`TargetInfo` lane-vs-serial gate,
// `disable_relayout`), DEFERRED here; we run one plain graph and compare it against the old tree's
// `hz_calibrated` arm (its realistic deployment path) with the pinned `relayout` arm shown for context.
//
// MAPPING to the rewrite: the C voices are the WIDTH axis and the recurrence runs along the streamed
// (sample) axis, runtime-resolved so N=1<<16 need not be a compile-time extent — the recurrence pattern
// of tests/unit/runtime_recurrence_test.cpp, widened from width-1 (mono) to width-C. `y[n - 1]`
// taps the prior SAMPLE per width-lane independently, which is exactly a bank of independent voices.
// (The per-voice correctness gate below refuses to time an unverified bank.) x is fed sample-major:
// sample i is the C voices' i-th samples, contiguous (width C). `hand` is ctg3's `scalar_onepole`
// reference, ported verbatim (float, ctg3's own voice-major layout/generator).
//
// `heinzel` is the nest path: the same bank in F32 (what `main`'s arms and `hand` compute in), planned
// for this machine — feedback makes the stage walk time a sample at a time; the sixteen voices are two
// lanes of eight, which is one block of bodies, so time runs innermost and the state is handed on in
// registers (read once per run from the sample of silence kept before the block).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>

#include "page_vector.hpp"
#include "parallel_arm.hpp"

#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"

using heinzel_bench::PageVector;
using heinzel_bench::OutputVector;

namespace hz = heinzel;
constexpr hz::Index n{};

namespace {

constexpr int C = 16;                // voices — the width (lane) axis
constexpr int N = 1 << 16;           // samples per voice
constexpr double kA = static_cast<double>(0.95f); // one-pole coefficient (float, widened)
constexpr float kAf = 0.95f;
constexpr std::int32_t kLengthId = hz::kStreamLength; // the symbol a Length declares

// hand: ctg3's scalar_onepole reference, ported verbatim — float, voice-major layout (own generator,
// independent of the width-C bank's sample-major x above).
void onepole_ref(const float* x, float* y) {
    for (int c = 0; c < C; ++c) {
        float p = 0;
        for (int i = 0; i < N; ++i) {
            const float v = x[static_cast<std::size_t>(c) * N + static_cast<std::size_t>(i)] + kAf * p;
            y[static_cast<std::size_t>(c) * N + static_cast<std::size_t>(i)] = v;
            p = v;
        }
    }
}

PageVector<float> onepole_input() {
    PageVector<float> x(static_cast<std::size_t>(C) * N);
    for (std::size_t i = 0; i < x.size(); ++i) { x[i] = 0.4f * std::sin(0.001f * static_cast<float>(i)); }
    return x;
}

void bench_hand_onepole(benchmark::State& state) {
    const PageVector<float> x = onepole_input();
    OutputVector<float> y(x.size(), 0.0f);
    onepole_ref(x.data(), y.data());
    // Sanity: re-derive a few (voice, sample) points independently in double.
    for (const int c : {0, C / 2, C - 1}) {
        double p = 0.0;
        for (int i = 0; i <= N / 2; ++i) {
            p = static_cast<double>(x[static_cast<std::size_t>(c) * N + static_cast<std::size_t>(i)]) + kA * p;
        }
        const double got = y[static_cast<std::size_t>(c) * N + N / 2];
        if (!(std::fabs(got - p) <= 1e-3 * (1.0 + std::fabs(p)))) {
            state.SkipWithError("bench_lane_recurrence: hand disagrees with the reference");
            return;
        }
    }
    for (auto _ : state) {
        onepole_ref(x.data(), y.data());
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * C * N);
}
constexpr hz::Length kFrames{.multiple_of = 1, .min = 1, .expect = 512};

// ---- the nest arm: the bank in F32, planned for this machine ------------------------------------------
// A bank of C one-pole voices over a runtime-resolved streamed axis. Width C, recurrence along the
// stream: each lane's y[n - 1] is that lane's prior sample. Sample-major: x[i * C + c] is voice c's
// i-th sample.
PageVector<float> sample_major_input() {
    PageVector<float> x(static_cast<std::size_t>(C) * N);
    for (std::size_t k = 0; k < x.size(); ++k) {
        x[k] = 0.4f * std::sin(0.001f * static_cast<float>(k));
    }
    return x;
}

constexpr hz::Graph build_bank_f32() {
    hz::Input x{"x", hz::Signal{C, kFrames}};
    hz::Variable y;
    y[n] = x + kA * y[n - 1];
    hz::Graph g = hz::finish(hz::Output{"y"} = y);
    if (!hz::infer(g).ok) {
        hz::fail("bench_lane_recurrence: the F32 one-pole bank did not infer");
    }
    return g;
}
constexpr hz::Graph kBankF32 = build_bank_f32();
constexpr const hz::loops::Program& kBankNest = heinzel_bench::kProgramOf<kBankF32, heinzel_bench::kMachine>;

// (No `-par` arm: its voices are one split unit, so the run never splits them.)
template <bool Par = false>
void bench_onepole_bank_nest(benchmark::State& state) {
    const PageVector<float> x = sample_major_input();
    hz::loops::Sizes sizes{};
    sizes[kLengthId] = N;
    hz::loops::Workspace<kBankNest, heinzel_bench::copies<Par>()> work(sizes);
    const hz::loops::Feeds feeds{x.data()};
    heinzel_bench::run<kBankNest, Par>(feeds, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(kBankNest.outputs[0]));
    // Every voice against the recurrence in double. A one-pole's rounding errors decay as its state
    // does, so float stays within a few ulp of 1 / (1 - a) = 20 units of the input's 0.4.
    double max_rel = 0.0;
    for (int c = 0; c < C; ++c) {
        double p = 0.0;
        for (int i = 0; i < N; ++i) {
            p = static_cast<double>(x[static_cast<std::size_t>(i) * C + c]) + kA * p;
            const double err = std::fabs(static_cast<double>(y[static_cast<std::size_t>(i) * C + c]) - p) / 8.0;
            max_rel = err > max_rel ? err : max_rel;
        }
    }
    if (!(max_rel <= 1e-5)) {
        state.SkipWithError("bench_lane_recurrence: the nest bank disagrees with the per-voice reference");
        return;
    }
    state.counters["max_rel_err"] = max_rel;
    if (Par && !heinzel_bench::same_as_serial<kBankNest>(feeds, work, sizes)) {
        state.SkipWithError("bench_lane_recurrence: the split run differs from the serial run");
        return;
    }
    for (auto _ : state) {
        heinzel_bench::run<kBankNest, Par>(feeds, work);
        benchmark::DoNotOptimize(y);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * C * N);
}

// ---- `Executor`'s own cost (Plan/archive/14-dsl/08 §3), not gated -------------------------------------------------
// `heinzel-executor` runs the graph through `Executor`: ports bound by name, the output copied out of its
// workspace after each run. Its control, which ran the same program by hand, and the numbers are in `Plan/archive/12`
// (L2): the difference is the copy, which L12 removes (DS-21).

void bench_onepole_bank_executor(benchmark::State& state) {
    const PageVector<float> x = sample_major_input();
    OutputVector<float> y(x.size());
    hz::Executor<build_bank_f32, heinzel_bench::kMachine> ex;
    ex.input("x") = x.data();
    ex.output("y") = y.data();
    ex.prepare(N);
    ex.run(N);
    for (int c = 0; c < C; ++c) { // every voice against the recurrence in double, as the nest arm checks it
        double p = 0.0;
        for (int i = 0; i < N; ++i) {
            p = static_cast<double>(x[static_cast<std::size_t>(i) * C + c]) + kA * p;
            if (!(std::fabs(static_cast<double>(y[static_cast<std::size_t>(i) * C + c]) - p) / 8.0 <= 1e-5)) {
                state.SkipWithError("bench_lane_recurrence: the Executor's bank disagrees with the per-voice reference");
                return;
            }
        }
    }
    for (auto _ : state) {
        ex.run(N);
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * C * N);
}

// ---- the carry (Plan/archive/14-dsl/08 §3), not gated ----------------------------------------------------------------
// A 375 ms echo at 48 kHz — 18 000 samples of past — run in 64-sample blocks on one workspace, each run continuing
// the last: `B64` is what one block costs, carry included, against its real-time budget of 64 samples (1.33 ms);
// `N48000` runs one second whole, for the cost of the arithmetic alone. The carry moves the whole past every
// block, which outweighs a block's arithmetic by far (the sliding origin that would not is backlog B-17).
constexpr hz::Index kSampleClock{48'000};
constexpr hz::Length kEchoLength{};
constexpr hz::Graph echo_graph() {
    hz::Input x{"x", hz::Signal{kEchoLength}};
    hz::Output y{"y"};
    y[kSampleClock] = x[kSampleClock] + 0.35 * y[kSampleClock - 375.0 * hz::units::ms];
    return y;
}
constexpr hz::Compiled kEcho = hz::compile(echo_graph());

template <std::int64_t Block>
void bench_echo(benchmark::State& state) {
    PageVector<float> x(static_cast<std::size_t>(Block));
    for (std::size_t k = 0; k < x.size(); ++k) {
        x[k] = 0.4f * std::sin(0.01f * static_cast<float>(k));
    }
    hz::loops::Sizes sizes{};
    sizes[0] = Block;
    hz::loops::Workspace<kEcho.program> work(sizes);
    const hz::loops::Feeds feeds{x.data()};
    hz::loops::run<kEcho.program>(feeds, work); // the first run, from silence, against the loop
    const float* y = work.data<float>(static_cast<std::size_t>(kEcho.program.outputs[0]));
    std::vector<float> want(x.size());
    for (std::size_t k = 0; k < x.size(); ++k) {
        want[k] = x[k] + (k >= 18'000 ? 0.35f * want[k - 18'000] : 0.0f);
        if (!(std::fabs(y[k] - want[k]) <= 1e-6f)) {
            state.SkipWithError("bench_lane_recurrence: the echo disagrees with the loop");
            return;
        }
    }
    for (std::int64_t k = 0; k < 18'000 / Block + 1; ++k) { // past the echo's depth: the state is no longer silence
        hz::loops::run<kEcho.program>(feeds, work);
    }
    for (auto _ : state) {
        hz::loops::run<kEcho.program>(feeds, work);
        benchmark::DoNotOptimize(y);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * Block);
}

// ---- the ladder bank: five states per voice, a saturation inside the loop ----------------------------
//
// JUCE's dsp::LadderFilter (LPF12 @ 1 kHz, resonance 0.7), as ctg3's bench has it: the heavy recurrence.
// `hand` is ctg3's `ladder_ref`, verbatim but for its layout, which is this file's sample-major one so
// that both arms read the same data; the nest arm is the same filter as a graph of five variables.
constexpr float kA1 = 0.87730577f;
constexpr float kG = 1.0f - kA1;
constexpr float kB0 = kG * 0.76923076923f;
constexpr float kB1 = kG * 0.23076923076f;
constexpr float kDrive = 1.2f;
constexpr float kGain = 0.76730449f;
constexpr float kDrive2 = 1.008f;
constexpr float kGain2 = 0.98788632f;
constexpr float kComp = 0.5f;
constexpr float kK = -2.92f;
constexpr float kOut = 1.2f;

void ladder_ref(const float* x, float* y) {
    for (int c = 0; c < C; ++c) {
        float s0 = 0;
        float s1 = 0;
        float s2 = 0;
        float s3 = 0;
        float s4 = 0;
        for (int i = 0; i < N; ++i) {
            const std::size_t at = static_cast<std::size_t>(i) * C + static_cast<std::size_t>(c);
            const float dx = kGain * std::tanh(kDrive * x[at]);
            const float a0 = dx + kK * (kGain2 * std::tanh(kDrive2 * s4) - dx * kComp);
            const float a1 = kB1 * s0 + kA1 * s1 + kB0 * a0;
            const float a2 = kB1 * s1 + kA1 * s2 + kB0 * a1;
            const float a3 = kB1 * s2 + kA1 * s3 + kB0 * a2;
            const float a4 = kB1 * s3 + kA1 * s4 + kB0 * a3;
            y[at] = kOut * a2;
            s0 = a0, s1 = a1, s2 = a2, s3 = a3, s4 = a4;
        }
    }
}

void bench_hand_ladder(benchmark::State& state) {
    const PageVector<float> x = sample_major_input();
    OutputVector<float> y(x.size(), 0.0f);
    for (auto _ : state) {
        ladder_ref(x.data(), y.data());
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * C * N);
}

constexpr hz::Graph build_ladder() {
    hz::Input x{"x", hz::Signal{C, kFrames}};
    hz::Variable s0;
    hz::Variable s1;
    hz::Variable s2;
    hz::Variable s3;
    hz::Variable s4;
    const auto f = [](float v) { return static_cast<double>(v); };
    hz::Expr dx = f(kGain) * hz::tanh(f(kDrive) * x);
    s0[n] = dx + f(kK) * (f(kGain2) * hz::tanh(f(kDrive2) * s4[n - 1]) - dx * f(kComp));
    s1[n] = f(kB1) * s0[n - 1] + f(kA1) * s1[n - 1] + f(kB0) * s0;
    s2[n] = f(kB1) * s1[n - 1] + f(kA1) * s2[n - 1] + f(kB0) * s1;
    s3[n] = f(kB1) * s2[n - 1] + f(kA1) * s3[n - 1] + f(kB0) * s2;
    s4[n] = f(kB1) * s3[n - 1] + f(kA1) * s4[n - 1] + f(kB0) * s3;
    hz::Graph g = hz::finish(hz::Output{"y"} = f(kOut) * s2);
    if (!hz::infer(g).ok) {
        hz::fail("bench_lane_recurrence: the ladder bank did not infer");
    }
    return g;
}
constexpr hz::Graph kLadder = build_ladder();
constexpr const hz::loops::Program& kLadderNest = heinzel_bench::kProgramOf<kLadder, heinzel_bench::kMachine>;

// (No `-par` arm: the bank runs as a stream band, which carries its state from chunk to chunk.)
template <bool Par = false>
void bench_ladder_bank_nest(benchmark::State& state) {
    const PageVector<float> x = sample_major_input();
    std::vector<float> want(x.size(), 0.0f);
    ladder_ref(x.data(), want.data());
    hz::loops::Sizes sizes{};
    sizes[kLengthId] = N;
    hz::loops::Workspace<kLadderNest, heinzel_bench::copies<Par>()> work(sizes);
    const hz::loops::Feeds feeds{x.data()};
    heinzel_bench::run<kLadderNest, Par>(feeds, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(kLadderNest.outputs[0]));
    // Against the hand loop on the platform's tanh. The float core is within an ulp or two of it and the
    // filter is stable, so the two stay a few ulp apart, relative to the output's unit scale.
    double max_rel = 0.0;
    for (std::size_t k = 0; k < x.size(); ++k) {
        const double err = std::fabs(static_cast<double>(y[k]) - static_cast<double>(want[k]));
        max_rel = err > max_rel ? err : max_rel;
    }
    if (!(max_rel <= 1e-5)) {
        state.SkipWithError("bench_lane_recurrence: the nest ladder disagrees with the hand loop");
        return;
    }
    state.counters["max_rel_err"] = max_rel;
    if (Par && !heinzel_bench::same_as_serial<kLadderNest>(feeds, work, sizes)) {
        state.SkipWithError("bench_lane_recurrence: the split run differs from the serial run");
        return;
    }
    for (auto _ : state) {
        heinzel_bench::run<kLadderNest, Par>(feeds, work);
        benchmark::DoNotOptimize(y);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * C * N);
}

} // namespace

BENCHMARK(bench_ladder_bank_nest)->Name("ladder_bank/heinzel")->UseRealTime();
BENCHMARK(bench_hand_ladder)->Name("ladder_bank/hand")->UseRealTime();
BENCHMARK(bench_onepole_bank_nest)->Name("onepole_bank/heinzel")->UseRealTime();
BENCHMARK(bench_hand_onepole)->Name("onepole_bank/hand")->UseRealTime();
BENCHMARK(bench_onepole_bank_executor)->Name("onepole_bank/heinzel-executor")->UseRealTime();
BENCHMARK(bench_echo<64>)->Name("echo375/heinzel/B64")->UseRealTime();
BENCHMARK(bench_echo<48000>)->Name("echo375/heinzel/N48000")->UseRealTime();

BENCHMARK_MAIN();
