// Third-party comparison benchmark: Heinzel vs FAUST — the Moog ladder VCF and Freeverb workloads.
// Part of the bench_compare_faust target (see bench_compare_faust.cpp for the file-split rationale
// and the dropped Heinzel arms — P7).
//
// These are DIVERGENT-METHOD arms: FAUST's own Moog ladder model (ve.moog_vcf) and Freeverb tuning
// (re.mono_freeverb) cannot be checked sample-for-sample against a from-scratch reference here (main
// checked ladder against Heinzel's op-for-op port of ve.moog_vcf instead, which this branch cannot
// carry — see the header note in bench_compare_faust.cpp). Per main's own fairness rule for
// divergent-method arms, the correctness check is a sanity band: finite output, RMS in a plausible
// range — a same-task throughput comparison, not a claim of matching output.
//
// The headline: ladder16, the 16-voice bank. FAUST's -vec vectorizes the TIME axis of 16 serial
// ladders (heinzel's across-voice SIMD lane packing is the P7 arm that would contrast with it).
//
// Built only when HEINZEL_BENCH_COMPARE_FAUST=ON.

#include <cstdint>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include "faust/faust_arms.hpp"
#include "faust_compare_common.hpp"

#include "page_vector.hpp"

using heinzel_bench::PageVector;
using heinzel_bench::OutputVector;

namespace {
using namespace faustcmp;

void faust_run(::dsp* d, const float* const* in, float* const* out, int count) {
    d->compute(count, const_cast<FAUSTFLOAT**>(in), const_cast<FAUSTFLOAT**>(out));
}

bool check_mono_sanity(::dsp* (*make)(), const float* x, const char* arm, std::string& err) {
    ::dsp* d = make();
    d->init(static_cast<int>(kFs));
    OutputVector<float> y(N, 0.0f);
    const float* in[1] = {x};
    float* out[1] = {y.data()};
    faust_run(d, in, out, N);
    delete d;
    if (!sanity_ok(y.data(), N)) {
        err = std::string(arm) + ": output outside the finite/RMS sanity band";
        return false;
    }
    return true;
}

bool check_ladder16_sanity(::dsp* (*make)(), const float* x, std::string& err) {
    ::dsp* d = make();
    d->init(static_cast<int>(kFs));
    OutputVector<float> y(N, 0.0f);
    const float* in[kLadCh];
    float* out[kLadCh];
    for (int c = 0; c < kLadCh; ++c) {
        in[c] = x + static_cast<std::size_t>(c) * kLadNc;
        out[c] = y.data() + static_cast<std::size_t>(c) * kLadNc;
    }
    faust_run(d, in, out, kLadNc);
    delete d;
    if (!sanity_ok(y.data(), N)) {
        err = "ladder16/faust: output outside the finite/RMS sanity band";
        return false;
    }
    return true;
}

// ---- benches ------------------------------------------------------------------------------------
void faust_ladder(benchmark::State& st, ::dsp* (*make)()) {
    PageVector<float> x = make_sig(N);
    std::string err;
    if (!check_mono_sanity(make, x.data(), "ladder/faust", err)) {
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

void faust_reverb(benchmark::State& st, ::dsp* (*make)()) {
    PageVector<float> x = make_sig(N);
    std::string err;
    if (!check_mono_sanity(make, x.data(), "reverb/faust", err)) {
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

void faust_ladder16(benchmark::State& st, ::dsp* (*make)()) {
    PageVector<float> x = make_sig(N);
    std::string err;
    if (!check_ladder16_sanity(make, x.data(), err)) {
        st.SkipWithError(err.c_str());
        return;
    }
    ::dsp* d = make();
    d->init(static_cast<int>(kFs));
    OutputVector<float> y(N, 0.0f);
    const float* in[kLadCh];
    float* out[kLadCh];
    for (int c = 0; c < kLadCh; ++c) {
        in[c] = x.data() + static_cast<std::size_t>(c) * kLadNc;
        out[c] = y.data() + static_cast<std::size_t>(c) * kLadNc;
    }
    for (auto _ : st) {
        faust_run(d, in, out, kLadNc);
        benchmark::DoNotOptimize(y.data());
    }
    st.SetItemsProcessed(st.iterations() * static_cast<std::int64_t>(N));
    delete d;
}

BENCHMARK_CAPTURE(faust_ladder, scal, heinzel_make_ladder_scal)->Name("ladder/faust-scal")->UseRealTime();
BENCHMARK_CAPTURE(faust_ladder, vec, heinzel_make_ladder_vec)->Name("ladder/faust-vec")->UseRealTime();

BENCHMARK_CAPTURE(faust_ladder16, scal, heinzel_make_ladder16_scal)->Name("ladder16/faust-scal")->UseRealTime();
BENCHMARK_CAPTURE(faust_ladder16, vec, heinzel_make_ladder16_vec)->Name("ladder16/faust-vec")->UseRealTime();

BENCHMARK_CAPTURE(faust_reverb, scal, heinzel_make_reverb_scal)->Name("reverb/faust-scal")->UseRealTime();
BENCHMARK_CAPTURE(faust_reverb, vec, heinzel_make_reverb_vec)->Name("reverb/faust-vec")->UseRealTime();

} // namespace
