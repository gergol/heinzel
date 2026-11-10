// Third-party comparison benchmark: Heinzel vs Cmajor (https://cmajor.dev), on two of Cmajor's own example patches,
// unchanged (benchmarks/cmajor/patches/): Freeverb (two Schroeder rooms, a wet/dry/width mixer, five smoothed knobs)
// and Filter (std::filters::tpt::svf, stereo). This file carries the Cmajor and hand arms; Heinzel's are
// bench_compare_cmajor_heinzel.cpp's, an object library of this target.
//
// Cmajor's arms are its C++ code generator's output (`cmaj generate --target=cpp`, committed), driven as a host drives
// the generated class (cmajor/arms.inc) and compiled with the compiler lines Cmajor's own cpp engine uses, nothing
// added (cmajor/README.md): `cmajor`, its default (`-O3`), and `cmajor-O4`, its `-O4` (`-O3 -ffast-math`). Neither has
// `-march=native`, which every other arm of the suite is built with: that is how Cmajor builds the code.
//
// Every arm is checked against the hand reference over a whole run before it is timed (SkipWithError on a mismatch),
// and reports `max_rel_err`: its worst error over that run against the same patch run in double, relative to the peak.
// Cmajor's Freeverb gets its knobs as events and runs 1024 frames of silence first, so its smoothers have settled and
// its rooms are still empty: from there it is the reference's fresh reverb at fixed knobs, its smoothers still run
// each frame.
//
// Built only when HEINZEL_BENCH_COMPARE_CMAJOR=ON.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>

#include "cmajor/cmajor_arms.hpp"
#include "cmajor_compare_common.hpp"

#include "page_vector.hpp"

using heinzel_bench::OutputVector;
using heinzel_bench::PageVector;

namespace {

using namespace cmajorcmp;

using Reference = void (*)(const float*, float*, std::size_t);
using Exact = const std::vector<double>& (*)();

// `in_channels` interleaved channels in, two out, against `reference`; its error against `exact`, the double run.
void cmajor_arm(benchmark::State& state, const cmajor_arms::Patch& patch, std::size_t in_channels, Reference reference,
                Exact exact, const char* what) {
    if (!designed_ok()) {
        state.SkipWithError("cmajor: a constant's copy differs from Cmajor's design");
        return;
    }
    const PageVector<float> x = in_channels == 1 ? make_mono(N) : make_stereo(N);
    OutputVector<float> y(2 * N);
    std::vector<float> ref(2 * N);
    reference(x.data(), ref.data(), N);
    void* p = patch.make();
    patch.run(p, x.data(), y.data(), N);
    if (maxabs_diff(y.data(), ref.data(), y.size()) > 1e-4) {
        patch.destroy(p);
        state.SkipWithError(what);
        return;
    }
    state.counters["max_rel_err"] = peak_rel_err(y.data(), exact().data(), y.size());
    for (auto _ : state) {
        patch.run(p, x.data(), y.data(), N);
        benchmark::DoNotOptimize(y.data());
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(N));
    patch.destroy(p);
}

// The knobs moving (kBlock, cmajor_compare_common.hpp): from `start`, block by block, `sets` (two sets of `knobs`
// values) sent as events at each block's start, alternately; the whole signal checked, then timed.
void cmajor_moving(benchmark::State& state, const cmajor_arms::Patch& patch, std::size_t in_channels, const float* sets,
                   std::size_t knobs, Reference reference, Exact exact, const char* what) {
    const PageVector<float> x = in_channels == 1 ? make_mono(N) : make_stereo(N);
    OutputVector<float> y(2 * N);
    std::vector<float> ref(2 * N);
    reference(x.data(), ref.data(), N);
    void* p = patch.start();
    const auto pass = [&] {
        for (std::size_t b = 0; b * kBlock < N; ++b) {
            patch.knobs(p, sets + b % 2 * knobs);
            patch.run(p, x.data() + b * kBlock * in_channels, y.data() + b * kBlock * 2, kBlock);
        }
    };
    pass();
    if (maxabs_diff(y.data(), ref.data(), y.size()) > 1e-4) {
        patch.destroy(p);
        state.SkipWithError(what);
        return;
    }
    state.counters["max_rel_err"] = peak_rel_err(y.data(), exact().data(), y.size());
    for (auto _ : state) {
        pass();
        benchmark::DoNotOptimize(y.data());
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(N));
    patch.destroy(p);
}

// The reference itself, timed: the plain loop a programmer writes from the patch.
void hand_arm(benchmark::State& state, std::size_t in_channels, Reference reference, Exact exact) {
    const PageVector<float> x = in_channels == 1 ? make_mono(N) : make_stereo(N);
    OutputVector<float> y(2 * N);
    reference(x.data(), y.data(), N);
    state.counters["max_rel_err"] = peak_rel_err(y.data(), exact().data(), y.size());
    for (auto _ : state) {
        reference(x.data(), y.data(), N);
        benchmark::DoNotOptimize(y.data());
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(N));
}

void freeverb_cmajor(benchmark::State& s) {
    cmajor_arm(s, cmajor_arms::kO3.freeverb, 1, freeverb_cmajor_ref, freeverb_exact, "freeverb/cmajor: mismatch vs the hand Freeverb");
}
void freeverb_cmajor_o4(benchmark::State& s) {
    cmajor_arm(s, cmajor_arms::kO4.freeverb, 1, freeverb_cmajor_ref, freeverb_exact, "freeverb/cmajor-O4: mismatch vs the hand Freeverb");
}
void filter_cmajor(benchmark::State& s) {
    cmajor_arm(s, cmajor_arms::kO3.filter, 2, svf_cmajor_ref, svf_exact, "filter/cmajor: mismatch vs the hand svf");
}
void filter_cmajor_o4(benchmark::State& s) {
    cmajor_arm(s, cmajor_arms::kO4.filter, 2, svf_cmajor_ref, svf_exact, "filter/cmajor-O4: mismatch vs the hand svf");
}
void freeverb_hand(benchmark::State& s) { hand_arm(s, 1, freeverb_cmajor_ref, freeverb_exact); }
void filter_hand(benchmark::State& s) { hand_arm(s, 2, svf_cmajor_ref, svf_exact); }

void freeverb_moving_cmajor(benchmark::State& s) {
    cmajor_moving(s, cmajor_arms::kO3.freeverb, 1, kFreeverbMoves[0].data(), 5, freeverb_moving_ref, freeverb_moving_exact,
                  "freeverb-moving/cmajor: mismatch vs the hand Freeverb");
}
void freeverb_moving_cmajor_o4(benchmark::State& s) {
    cmajor_moving(s, cmajor_arms::kO4.freeverb, 1, kFreeverbMoves[0].data(), 5, freeverb_moving_ref, freeverb_moving_exact,
                  "freeverb-moving/cmajor-O4: mismatch vs the hand Freeverb");
}
void filter_moving_cmajor(benchmark::State& s) {
    cmajor_moving(s, cmajor_arms::kO3.filter, 2, kFilterMoves[0].data(), 2, svf_moving_ref, svf_moving_exact,
                  "filter-moving/cmajor: mismatch vs the hand svf");
}
void filter_moving_cmajor_o4(benchmark::State& s) {
    cmajor_moving(s, cmajor_arms::kO4.filter, 2, kFilterMoves[0].data(), 2, svf_moving_ref, svf_moving_exact,
                  "filter-moving/cmajor-O4: mismatch vs the hand svf");
}
void freeverb_moving_hand(benchmark::State& s) { hand_arm(s, 1, freeverb_moving_ref, freeverb_moving_exact); }
void filter_moving_hand(benchmark::State& s) { hand_arm(s, 2, svf_moving_ref, svf_moving_exact); }

BENCHMARK(freeverb_hand)->Name("freeverb/hand")->UseRealTime();
BENCHMARK(freeverb_cmajor)->Name("freeverb/cmajor")->UseRealTime();
BENCHMARK(freeverb_cmajor_o4)->Name("freeverb/cmajor-O4")->UseRealTime();
BENCHMARK(filter_hand)->Name("filter/hand")->UseRealTime();
BENCHMARK(filter_cmajor)->Name("filter/cmajor")->UseRealTime();
BENCHMARK(filter_cmajor_o4)->Name("filter/cmajor-O4")->UseRealTime();
BENCHMARK(freeverb_moving_hand)->Name("freeverb-moving/hand")->UseRealTime();
BENCHMARK(freeverb_moving_cmajor)->Name("freeverb-moving/cmajor")->UseRealTime();
BENCHMARK(freeverb_moving_cmajor_o4)->Name("freeverb-moving/cmajor-O4")->UseRealTime();
BENCHMARK(filter_moving_hand)->Name("filter-moving/hand")->UseRealTime();
BENCHMARK(filter_moving_cmajor)->Name("filter-moving/cmajor")->UseRealTime();
BENCHMARK(filter_moving_cmajor_o4)->Name("filter-moving/cmajor-O4")->UseRealTime();

} // namespace

BENCHMARK_MAIN();
