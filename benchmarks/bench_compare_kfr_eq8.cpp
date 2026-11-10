// The whole-picture rows of the KFR comparison (bench_compare_kfr, Plan/23 09 (a)): KFR's eight-section EQ, then one
// thing outside it a row; eq8_common.hpp has the rows, their references and their bounds. This file holds the `naive`
// arm of every row and KFR's arms, composed from its primitives as a KFR user writes them:
//   naive     — one loop a sample, each section in transposed direct form II as KFR's `biquad_process` computes it
//               (without its skew): the strongest fair hand loop, and what a KFR user writes where the engine cannot go.
//   biquad8   — `kfr`: the EQ in KFR's engine (`iir`, eight sections in eight lanes, skewed).
//   eq8mix    — `kfr`: one expression, `0.5·x + 0.5·clamp(iir(2·x), ±0.5)`. KFR's `iir` reads its gained input and the
//               clip and the mix run on its output, in its block loop, vectorised. The clip is `waveshaper_hardclip`'s
//               body, `clamp(input, -level, +level)`, with float bounds: that function takes its level as a double,
//               which would run the block's tail in double lanes.
//   lr8       — `naive`, and `kfr`: the eighth-order Linkwitz-Riley low-pass as four sections, in KFR's engine (four of
//               its eight lanes, the rest passing through).
//   eq8fb     — no `kfr` arm: a sample leaves the skewed engine seven steps after it entered, and the loop needs it at
//               the next sample.
//   eq8sweep  — `kfr`: three passes a block, the best split KFR allows: the fixed seven in its engine (eight lanes, the
//               last passing its input through, as KFR pads a cascade itself), the band's coefficients from its
//               vectorised `phasor`, `sin`, `exp2` and `cos` into buffers, then a hand loop for the band (the engine
//               takes no coefficient a sample).
// Each row runs the whole input and `/B256`: blocks of 256, the state carried, the same block fed again. The Heinzel
// arms are bench_compare_kfr_heinzel_eq8.cpp's, an object library of the target; this file is compiled as KFR is used,
// without Heinzel's `-ffp-contract=off`.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include <benchmark/benchmark.h>

#include <kfr/base.hpp>
#include <kfr/dsp.hpp>

#include "eq8_common.hpp"

#include "page_vector.hpp"

using heinzel_bench::OutputVector;

namespace {

using namespace kfrcmp;

constexpr auto kWhole = static_cast<std::size_t>(N);

// K sections in KFR's engine of eight lanes, as its biquad_section(a0, a1, a2, b0, b1, b2) (a1 and a2 the values the
// difference equation subtracts); a lane past K passes its input through.
template <std::size_t K>
kfr::iir_params<float, 8> params8(const std::array<BiquadCoeffs, K>& c) {
    std::array<kfr::biquad_section<float>, K> s{};
    for (std::size_t k = 0; k < K; ++k) {
        s[k] = kfr::biquad_section<float>(1.0f, c[k].a1, c[k].a2, c[k].b0, c[k].b1, c[k].b2);
    }
    return kfr::iir_params<float, 8>(s.data(), K);
}

// ---- naive: the row's loop in float, the state carried from run to run ---------------------------------------------
// A function of its own, as a user's process call is: inlined into the timing loop's closure, clang scheduled the same
// loop 40 % slower (15.7 ns a sample against 11.4, Plan/probes/audio-iir/feedback_bank.cpp).
template <Row R>
[[gnu::noinline]] void naive_block(RowState<float>& st, const float* x, float* y, std::size_t n) {
    run_row<R>(st, x, y, n);
}

template <Row R>
void bench_naive(benchmark::State& state, std::size_t m) {
    RowState<float> st{};
    time_arm<R>(state, m, "naive", [&](const float* x, float* y, std::size_t k) { naive_block<R>(st, x, y, k); });
}

// ---- naive-moving: biquad8 with one band swept, the moving band's coefficients set before every run -----------------
// The hand loop's form (transposed direct form II) over a bank it copies, so that no store to `y` can touch the
// coefficients; the caller's bank changes between runs.
[[gnu::noinline]] void naive_moving_block(RowState<float>& st, const std::array<BiquadCoeffs, 8>& bank, const float* x, float* y,
                                          std::size_t n) {
    const std::array<BiquadCoeffs, 8> c = bank;
    std::array<Tdf2<float>, 8> s = st.eq;
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = cascade(s, c, x[i]);
    }
    st.eq = s;
}

void bench_naive_moving(benchmark::State& state) {
    if (!moving_every_block()) {
        state.SkipWithError("biquad8/naive-moving: the moving band repeats the block's before");
        return;
    }
    const std::vector<BiquadCoeffs>& table = moving_table();
    std::array<BiquadCoeffs, 8> bank = kEq8;
    RowState<float> st{};
    OutputVector<float> out(2 * kBlock);
    float* y = out.data();
    for (std::size_t at = 0, j = 0; at < out.size(); at += kBlock, ++j) {
        bank[kMovingBand] = table[j];
        naive_moving_block(st, bank, g_sine_in.data() + at, y + at, kBlock);
    }
    const std::vector<double> want = moving_reference(false, out.size());
    double err = 0.0;
    for (std::size_t i = 0; i < out.size(); ++i) {
        err = std::max(err, std::fabs(static_cast<double>(y[i]) - want[i]));
    }
    if (!(err <= spec(Row::biquad8).bound)) {
        state.SkipWithError("biquad8/naive-moving: mismatch vs the double reference");
        return;
    }
    state.counters["max_err"] = err;
    std::size_t j = 0;
    for (auto _ : state) {
        bank[kMovingBand] = table[j];
        j = j + 1 == table.size() ? 0 : j + 1;
        naive_moving_block(st, bank, g_sine_in.data(), y, kBlock);
        benchmark::DoNotOptimize(y);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(kBlock));
    state.counters["max_err"] = err;
}

// ---- KFR: the EQ in its engine, the work around it in the same expression ------------------------------------------
void bench_kfr_eq8(benchmark::State& state, std::size_t m) {
    kfr::iir_state<float, 8> st{params8(kEq8)};
    time_arm<Row::biquad8>(state, m, "kfr", [&](const float* x, float* y, std::size_t k) {
        kfr::make_univector(y, k) = kfr::iir(kfr::make_univector(x, k), std::ref(st));
    });
}

void bench_kfr_lr8(benchmark::State& state, std::size_t m) {
    kfr::iir_state<float, 8> st{params8(kLr8)};
    time_arm<Row::lr8>(state, m, "kfr", [&](const float* x, float* y, std::size_t k) {
        kfr::make_univector(y, k) = kfr::iir(kfr::make_univector(x, k), std::ref(st));
    });
}

void bench_kfr_mix(benchmark::State& state, std::size_t m) {
    kfr::iir_state<float, 8> st{params8(kEq8)};
    time_arm<Row::mix>(state, m, "kfr", [&](const float* x, float* y, std::size_t k) {
        const auto in = kfr::make_univector(x, k);
        const auto wet = kfr::clamp(kfr::iir(in * kMixGain, std::ref(st)), -kMixClip, kMixClip);
        kfr::make_univector(y, k) = kMixDry * in + kMixWet * wet;
    });
}

// ---- KFR: eq8sweep in three passes a block ------------------------------------------------------------------------
// The buffers are ours, half a page into a page (Plan/30: the arm writes them), and the passes run on KFR's views of
// them (`make_univector` over a caller's memory); time_arm's output is slot 0.
struct Sweep {
    kfr::iir_state<float, 8> fixed{params8(kEq7)};
    Tdf2<float> band{};
    std::size_t done = 0; // samples run so far: where the LFO is
    OutputVector<float, 1> u_mem;
    OutputVector<float, 2> w_mem;
    OutputVector<float, 3> cos_w_mem;
    OutputVector<float, 4> alpha_mem;
    OutputVector<float, 5> r_mem;
    OutputVector<float, 6> b0_mem;
    OutputVector<float, 7> k_mem;
    OutputVector<float, 8> b2_mem;
    OutputVector<float, 9> a2_mem;
    kfr::univector_ref<float> u = kfr::make_univector(u_mem.data(), u_mem.size());
    kfr::univector_ref<float> w = kfr::make_univector(w_mem.data(), w_mem.size());
    kfr::univector_ref<float> cos_w = kfr::make_univector(cos_w_mem.data(), cos_w_mem.size());
    kfr::univector_ref<float> alpha = kfr::make_univector(alpha_mem.data(), alpha_mem.size());
    kfr::univector_ref<float> r = kfr::make_univector(r_mem.data(), r_mem.size());
    kfr::univector_ref<float> b0 = kfr::make_univector(b0_mem.data(), b0_mem.size());
    kfr::univector_ref<float> k = kfr::make_univector(k_mem.data(), k_mem.size());
    kfr::univector_ref<float> b2 = kfr::make_univector(b2_mem.data(), b2_mem.size());
    kfr::univector_ref<float> a2 = kfr::make_univector(a2_mem.data(), a2_mem.size());

    explicit Sweep(std::size_t m)
        : u_mem(m), w_mem(m), cos_w_mem(m), alpha_mem(m), r_mem(m), b0_mem(m), k_mem(m), b2_mem(m), a2_mem(m) {}
    // A copy's views would point into the source's vectors; with these deleted, there is no move either.
    Sweep(const Sweep&) = delete;
    Sweep& operator=(const Sweep&) = delete;

    // m samples of x into y; the buffers hold m (whole runs: N). A function of its own, as the naive arm's.
    [[gnu::noinline]] void process(const float* x, float* y, std::size_t m) {
        u = kfr::iir(kfr::make_univector(x, m), std::ref(fixed));
        // phase[n] = frac((n + 1)·2^-16), exact in float: KFR's phasor from this block's first phase.
        const float first = static_cast<float>((done + 1) % 65536) * kSweepInc;
        const auto phase = kfr::phasor<float>(kSweepInc, 1.0f, first);
        w = kSweepCentre * kfr::exp2(kSweepOctaves * kfr::sin(kTwoPi * phase)) * (kTwoPi / kFs);
        cos_w = kfr::cos(w);
        alpha = kfr::sin(w) * (0.5f / kSweepQ);
        r = 1.0f / (1.0f + alpha * kSweepInvA);
        b0 = (1.0f + alpha * kSweepA) * r;
        k = -2.0f * cos_w * r; // b1, and a1 alike
        b2 = (1.0f - alpha * kSweepA) * r;
        a2 = (1.0f - alpha * kSweepInvA) * r;
        Tdf2<float> s = band; // local, so that no store to y can touch it: the loop keeps it in registers
        for (std::size_t i = 0; i < m; ++i) {
            y[i] = s.step(Band<float>{b0[i], k[i], b2[i], k[i], a2[i]}, u[i]);
        }
        band = s;
        done += m;
    }
};

void bench_kfr_sweep(benchmark::State& state, std::size_t m) {
    Sweep sweep(m);
    time_arm<Row::sweep>(state, m, "kfr", [&](const float* x, float* y, std::size_t n) { sweep.process(x, y, n); });
}

BENCHMARK_CAPTURE(bench_naive<Row::biquad8>, whole, kWhole)->Name("biquad8/naive")->UseRealTime();
BENCHMARK_CAPTURE(bench_naive<Row::biquad8>, block, kBlock)->Name("biquad8/naive/B256")->UseRealTime();
BENCHMARK_CAPTURE(bench_kfr_eq8, whole, kWhole)->Name("biquad8/kfr")->UseRealTime();
BENCHMARK_CAPTURE(bench_kfr_eq8, block, kBlock)->Name("biquad8/kfr/B256")->UseRealTime();

BENCHMARK(bench_naive_moving)->Name("biquad8/naive-moving/B256")->UseRealTime();

BENCHMARK_CAPTURE(bench_naive<Row::lr8>, whole, kWhole)->Name("lr8/naive")->UseRealTime();
BENCHMARK_CAPTURE(bench_naive<Row::lr8>, block, kBlock)->Name("lr8/naive/B256")->UseRealTime();
BENCHMARK_CAPTURE(bench_kfr_lr8, whole, kWhole)->Name("lr8/kfr")->UseRealTime();
BENCHMARK_CAPTURE(bench_kfr_lr8, block, kBlock)->Name("lr8/kfr/B256")->UseRealTime();

BENCHMARK_CAPTURE(bench_naive<Row::mix>, whole, kWhole)->Name("eq8mix/naive")->UseRealTime();
BENCHMARK_CAPTURE(bench_naive<Row::mix>, block, kBlock)->Name("eq8mix/naive/B256")->UseRealTime();
BENCHMARK_CAPTURE(bench_kfr_mix, whole, kWhole)->Name("eq8mix/kfr")->UseRealTime();
BENCHMARK_CAPTURE(bench_kfr_mix, block, kBlock)->Name("eq8mix/kfr/B256")->UseRealTime();

BENCHMARK_CAPTURE(bench_naive<Row::fb>, whole, kWhole)->Name("eq8fb/naive")->UseRealTime();
BENCHMARK_CAPTURE(bench_naive<Row::fb>, block, kBlock)->Name("eq8fb/naive/B256")->UseRealTime();

BENCHMARK_CAPTURE(bench_naive<Row::sweep>, whole, kWhole)->Name("eq8sweep/naive")->UseRealTime();
BENCHMARK_CAPTURE(bench_naive<Row::sweep>, block, kBlock)->Name("eq8sweep/naive/B256")->UseRealTime();
BENCHMARK_CAPTURE(bench_kfr_sweep, whole, kWhole)->Name("eq8sweep/kfr")->UseRealTime();
BENCHMARK_CAPTURE(bench_kfr_sweep, block, kBlock)->Name("eq8sweep/kfr/B256")->UseRealTime();

} // namespace
