// Third-party comparison benchmark: Heinzel vs FAUST (https://faust.grame.fr), the dedicated external
// DSP compiler. This file carries sine, strip, and the biquad3 (+ dynamic block length) arms;
// bench_compare_faust_ladder.cpp carries ladder / ladder16 / reverb, both in this ONE target
// (bench_compare_faust) — split by workload family for the 600-line/60-line-function limits.
//
// Heinzel's arms for every row are bench_compare_faust_heinzel.cpp's (Plan/23 P1 and P7), an object library of the
// target: FAUST's generated code and this file are compiled without Heinzel's `-ffp-contract=off`.
//
// The FAUST C++ is GENERATED OFFLINE and COMMITTED (benchmarks/faust/generated/, copied verbatim
// from main via tools/gen-faust.sh there) — host-independent codegen, arch-specialized by
// -march=native at COMPILE time here — so no `faust` binary is needed to build this bench. Two FAUST
// arms per workload: `-scal` (FAUST's default, idiomatic) and `-vec` (its autovectorizer, `-lv 1`).
//
// Fairness (mirrors main): correctness is checked PER ARM against a library-independent reference,
// via SkipWithError (not abort) on mismatch, before that arm's timing loop runs. Topology-MATCHED
// arms (biquad3 — same RBJ coefficients baked into biquad3.dsp, same transfer function) are
// tolerance-checked against the scalar biquad_ref/strip_ref. sine is checked against its analytic
// RMS (FAUST's os.osc is a table oscillator — method-independent of any reference implementation, so
// RMS is the only meaningful cross-check, exactly as main did for its own sine/hz arm too).
//
// Built only when HEINZEL_BENCH_COMPARE_FAUST=ON.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include "faust/faust_arms.hpp" // committed FAUST arm factories
#include "faust_compare_common.hpp"

#include "page_vector.hpp"

using heinzel_bench::PageVector;
using heinzel_bench::OutputVector;

namespace {
using namespace faustcmp;

void faust_run(::dsp* d, const float* const* in, float* const* out, int count) {
    d->compute(count, const_cast<FAUSTFLOAT**>(in), const_cast<FAUSTFLOAT**>(out));
}

// ---- correctness checks (run once per bench before timing; SkipWithError on mismatch) --------------
bool check_sine(::dsp* (*make)(), std::string& err) {
    ::dsp* d = make();
    d->init(static_cast<int>(kFs));
    OutputVector<float> y(N, 0.0f);
    float* out[1] = {y.data()};
    faust_run(d, nullptr, out, N);
    delete d;
    const double want = kAmp / std::sqrt(2.0);
    if (!all_finite(y.data(), N) || std::fabs(rms(y.data(), N) - want) > 0.05) {
        err = "sine/faust: rms off analytic reference";
        return false;
    }
    return true;
}

bool check_biquad3(::dsp* (*make)(), const float* x, std::string& err) {
    std::vector<float> ref(N);
    OutputVector<float> y(N, 0.0f);
    biquad_ref(x, ref.data(), N);
    ::dsp* d = make();
    d->init(static_cast<int>(kFs));
    const float* in[1] = {x};
    float* out[1] = {y.data()};
    faust_run(d, in, out, N);
    delete d;
    if (maxabs_diff(y.data(), ref.data(), N) > 1e-2) {
        err = "biquad3/faust: mismatch vs DF-I reference";
        return false;
    }
    return true;
}

bool check_strip(::dsp* (*make)(), const float* x, std::string& err) {
    std::vector<float> ref(N);
    OutputVector<float> y(N, 0.0f);
    strip_ref(x, ref.data(), N);
    ::dsp* d = make();
    d->init(static_cast<int>(kFs));
    const float* in[1] = {x};
    float* out[1] = {y.data()};
    faust_run(d, in, out, N);
    delete d;
    if (maxabs_diff(y.data(), ref.data(), N) > 2e-2) {
        err = "strip/faust: mismatch vs tanh reference";
        return false;
    }
    return true;
}

// ---- benches ------------------------------------------------------------------------------------
void faust_sine(benchmark::State& st, ::dsp* (*make)()) {
    std::string err;
    if (!check_sine(make, err)) {
        st.SkipWithError(err.c_str());
        return;
    }
    ::dsp* d = make();
    d->init(static_cast<int>(kFs));
    OutputVector<float> y(N, 0.0f);
    float* out[1] = {y.data()};
    for (auto _ : st) {
        faust_run(d, nullptr, out, N);
        benchmark::DoNotOptimize(y.data());
    }
    st.SetItemsProcessed(st.iterations() * static_cast<std::int64_t>(N));
    delete d;
}

void faust_mono(benchmark::State& st, ::dsp* (*make)(), bool (*check)(::dsp* (*)(), const float*, std::string&)) {
    PageVector<float> x = make_sig(N);
    std::string err;
    if (!check(make, x.data(), err)) {
        st.SkipWithError(err.c_str());
        return;
    }
    ::dsp* d = make();
    d->init(static_cast<int>(kFs));
    OutputVector<float> y(N, 0.0f);
    const float* in[1] = {x.data()};
    float* out[1] = {y.data()};
    for (auto _ : st) {
        faust_run(d, in, out, N);
        benchmark::DoNotOptimize(y.data());
    }
    st.SetItemsProcessed(st.iterations() * static_cast<std::int64_t>(N));
    delete d;
}

// Dynamic block length: FAUST's native runtime count (M = state.range, opaque). check_biquad3 above
// already validates this exact compute() call at the full N; no separate per-M check needed.
void faust_biquad_dyn(benchmark::State& st, ::dsp* (*make)()) {
    const int M = static_cast<int>(st.range(0));
    ::dsp* d = make();
    d->init(static_cast<int>(kFs));
    PageVector<float> x = make_sig(static_cast<std::size_t>(M));
    OutputVector<float> y(static_cast<std::size_t>(M), 0.0f);
    const float* in[1] = {x.data()};
    float* out[1] = {y.data()};
    for (auto _ : st) {
        faust_run(d, in, out, M);
        benchmark::DoNotOptimize(y.data());
    }
    st.SetItemsProcessed(st.iterations() * static_cast<std::int64_t>(M));
    delete d;
}

BENCHMARK_CAPTURE(faust_sine, scal, heinzel_make_sine_scal)->Name("sine/faust-scal")->UseRealTime();
BENCHMARK_CAPTURE(faust_sine, vec, heinzel_make_sine_vec)->Name("sine/faust-vec")->UseRealTime();

BENCHMARK_CAPTURE(faust_mono, scal, heinzel_make_strip_scal, check_strip)->Name("strip/faust-scal")->UseRealTime();
BENCHMARK_CAPTURE(faust_mono, vec, heinzel_make_strip_vec, check_strip)->Name("strip/faust-vec")->UseRealTime();

BENCHMARK_CAPTURE(faust_mono, scal, heinzel_make_biquad3_scal, check_biquad3)->Name("biquad3/faust-scal")->UseRealTime();
BENCHMARK_CAPTURE(faust_mono, vec, heinzel_make_biquad3_vec, check_biquad3)->Name("biquad3/faust-vec")->UseRealTime();

BENCHMARK_CAPTURE(faust_biquad_dyn, scal, heinzel_make_biquad3_scal)
    ->Name("biquad3dyn/faust-scal")
    ->RangeMultiplier(4)
    ->Range(256, 16384)
    ->UseRealTime();
BENCHMARK_CAPTURE(faust_biquad_dyn, vec, heinzel_make_biquad3_vec)
    ->Name("biquad3dyn/faust-vec")
    ->RangeMultiplier(4)
    ->Range(256, 16384)
    ->UseRealTime();

} // namespace

BENCHMARK_MAIN();
