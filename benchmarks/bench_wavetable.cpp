// The wavetable oscillator (Plan/28-dsl-proposals/05 D3): one cycle of a sine in 2,048 cells, read at a phase that
// steps by a pitch. `heinzel` is `wavetable(constant(kCycle), phase)` on `Executor<…, kMachine>`, its phase a
// recurrence (`frac(phase + inc)`, accumulated in double at `Normal`, a one-voice cycle, Decision 66, and read as a
// float), the read in the recurrence's loop, a sample a step; `naive` the loop as one writes it in C++, its phase too
// in double (a float phase stepped a million times drifts by a hundredth of a turn): wrapped, read as a float, its
// place in the table, the two cells it falls between blended in float, the second index wrapped to the first. Each is
// checked against the cycle's sine before it is timed.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>

#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/runtime/periodic_cores.hpp"

#include "bench_machine.hpp"

#include "page_vector.hpp"

using heinzel_bench::OutputVector;

namespace hz = heinzel;

namespace {

constexpr std::int32_t kCells = 2048;
constexpr std::int32_t N = 1 << 20;
constexpr float kInc = 440.0f / 48000.0f;
constexpr hz::Index n{};

// sin(2 pi i / N) for each cell, by Heinzel's double sine of half turns (std::sin is no constant expression).
constexpr std::array<float, kCells> kCycle = [] {
    std::array<float, kCells> c{};
    for (std::int32_t i = 0; i < kCells; ++i) {
        const double turns = static_cast<double>(i) / kCells;
        c[static_cast<std::size_t>(i)] = static_cast<float>(hz::sine_of_half_turns(2.0 * turns, hz::FnAccuracy::Exact));
    }
    return c;
}();

constexpr hz::Graph wavetable_osc() {
    hz::Param inc{"inc"};
    hz::Variable phase{hz::Signal{N}};
    phase[n] = hz::frac(phase[n - 1] + inc);
    hz::Output y{"y"};
    y = hz::wavetable(hz::constant(kCycle), phase);
    return y;
}

// The plain loop: the phase stepped and wrapped as `frac` does, read between two cells.
void naive_wavetable(float* y, std::size_t count, float inc) {
    double phase = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        phase += static_cast<double>(inc);
        phase -= std::floor(phase);
        const float place = static_cast<float>(phase) * static_cast<float>(kCells);
        const auto near = static_cast<std::int32_t>(place);
        const float f = place - static_cast<float>(near);
        const std::int32_t at = near < kCells ? near : 0;
        const std::int32_t next = at + 1 < kCells ? at + 1 : 0;
        y[i] = kCycle[static_cast<std::size_t>(at)] + f * (kCycle[static_cast<std::size_t>(next)] - kCycle[static_cast<std::size_t>(at)]);
    }
}

// Both read one sine at 440 Hz: each sample within 1e-3 of it (the table's interpolation is 1.2e-6 off, a float
// phase's place 1.2e-4 of a cell).
bool a_sine(const OutputVector<float>& y) {
    double phase = 0.0;
    for (const float v : y) {
        phase += static_cast<double>(kInc);
        phase -= std::floor(phase);
        if (!(std::fabs(static_cast<double>(v) - std::sin(6.283185307179586 * phase)) <= 1e-3)) {
            return false;
        }
    }
    return true;
}

void bench_heinzel(benchmark::State& state) {
    OutputVector<float> y(N);
    hz::Executor<wavetable_osc, heinzel_bench::kMachine> run;
    run.param("inc") = static_cast<double>(kInc);
    run.output("y") = y.data();
    run.run();
    if (!a_sine(y)) {
        state.SkipWithError("wavetable/heinzel: not the sine its table holds");
        return;
    }
    for (auto _ : state) {
        run.run();
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * N);
}

void bench_naive(benchmark::State& state) {
    OutputVector<float> y(N);
    naive_wavetable(y.data(), y.size(), kInc);
    if (!a_sine(y)) {
        state.SkipWithError("wavetable/naive: not the sine its table holds");
        return;
    }
    for (auto _ : state) {
        naive_wavetable(y.data(), y.size(), kInc);
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * N);
}

} // namespace

BENCHMARK(bench_heinzel)->Name("wavetable/heinzel")->UseRealTime();
BENCHMARK(bench_naive)->Name("wavetable/naive")->UseRealTime();

BENCHMARK_MAIN();
