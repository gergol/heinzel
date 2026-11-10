// The Heinzel arms of the Cmajor comparison (bench_compare_cmajor): Cmajor's Freeverb and Filter example patches, each
// a graph on `Executor<…, kMachine>` written op for op as the patch computes, its knobs the constants the patch's
// `init` values give (Cmajor's stay run-time events, smoothed per sample), checked against the hand reference before it
// is timed. Each at `Normal` (`heinzel`, the suite's level) and `Fast` (`heinzel-fast`); `-param`, the knobs as Params. An object library of the
// target: compiled with Heinzel's flags, apart from Cmajor's generated code.
//
// Layouts: the Filter's frames are interleaved as Cmajor's `float<2>` stream, a two-channel signal Heinzel lays out
// sample-major; the Freeverb's two channels are two outputs, one after the other, beside Cmajor's interleaved frames.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <benchmark/benchmark.h>

#include "cmajor_compare_common.hpp"
#include "heinzel/dsp/smoothed.hpp"
#include "heinzel_arms.hpp"

#include "page_vector.hpp"

using heinzel_bench::OutputVector;
using heinzel_bench::PageVector;

namespace {

using namespace cmajorcmp;
using namespace heinzel_arms;

constexpr auto kN = static_cast<std::int32_t>(N);

// One channel of MonoReverb, `spread` samples longer: eight combs on gain·x, summed, through four allpasses.
constexpr hz::Expr room(hz::Expr x, std::int32_t spread) {
    const hz::Expr in = x * f(kGain);
    hz::Expr acc = comb(in, kComb[0] + spread, kFeedback, kDamp);
    for (std::size_t k = 1; k < kComb.size(); ++k) {
        acc = acc + comb(in, kComb[k] + spread, kFeedback, kDamp);
    }
    for (const std::int32_t size : kAllpass) {
        acc = allpass(acc, size + spread, kAllpassFb);
    }
    return acc;
}

// Both rooms and WetDryMixer, finished at `Level`.
template <hz::Precision Level>
constexpr hz::Graph freeverb_graph() {
    hz::Input x{"x", hz::Signal{kN}};
    const hz::Expr left = room(x, 0);
    const hz::Expr right = room(x, kSpread);
    const hz::Expr dry = x * f(kDry);
    hz::Output l{"l"};
    hz::Output r{"r"};
    l = left * f(kWet1) + right * f(kWet2) + dry;
    r = right * f(kWet1) + left * f(kWet2) + dry;
    return hz::finish(Level, l, r);
}

// std::filters::tpt::svf, its low-pass out, on each channel of `in`.
constexpr hz::Expr svf(hz::Expr in) {
    hz::Variable z0;
    hz::Variable z1;
    const hz::Expr high = (in - f(kSvf.g1) * z0[n - 1] - z1[n - 1]) * f(kSvf.d);
    const hz::Expr band = f(kSvf.a) * high + z0[n - 1];
    const hz::Expr low = f(kSvf.a) * band + z1[n - 1];
    z0[n] = f(kSvf.a) * high + band;
    z1[n] = f(kSvf.a) * band + low;
    return low;
}
template <hz::Precision Level>
constexpr hz::Graph filter_graph() {
    hz::Input x{"x", hz::Signal{2, kN}};
    hz::Output y{"y"};
    y = svf(x);
    return hz::finish(Level, y);
}

// The knobs as Params, known only at run time, as Cmajor's are, each over the range the patch declares; the patch's
// knob math in the graph, which runs only in a run where a knob changed (docs/knob-math.md). `Moving`: a run is a host's
// block (a `Length`), and Freeverb's knobs are smoothed as Cmajor smooths them (`dsp::smoothed`); the Filter's coefficients
// change at a run's start, which is where Cmajor changes them when a block is a multiple of 32 frames.
template <bool Moving>
constexpr auto frames() {
    if constexpr (Moving) {
        return hz::Length{};
    } else {
        return kN;
    }
}

template <bool Moving>
constexpr hz::Expr smoothed_if(hz::Expr target) {
    if constexpr (Moving) {
        return hz::dsp::smoothed(target, 960); // std::smoothing::SmoothedValueStream: 0.02 s at 48 kHz
    } else {
        return target;
    }
}

constexpr hz::Expr comb_knob(hz::Expr in, std::int32_t size, hz::Expr feedback, hz::Expr damp) {
    hz::Variable v;
    hz::Variable lp;
    const hz::Expr o = v[n - size];
    lp[n] = o * (1.0 - damp) + lp[n - 1] * damp;
    v[n] = in + lp * feedback;
    return o;
}
constexpr hz::Expr room_knob(hz::Expr x, std::int32_t spread, hz::Expr feedback, hz::Expr damp) {
    const hz::Expr in = x * f(kGain);
    hz::Expr acc = comb_knob(in, kComb[0] + spread, feedback, damp);
    for (std::size_t k = 1; k < kComb.size(); ++k) {
        acc = acc + comb_knob(in, kComb[k] + spread, feedback, damp);
    }
    for (const std::int32_t size : kAllpass) {
        acc = allpass(acc, size + spread, kAllpassFb);
    }
    return acc;
}

// ParameterScaler, the smoothers if `Moving`, then both rooms and WetDryMixer, finished at `Level`.
template <hz::Precision Level, bool Moving = false>
constexpr hz::Graph freeverb_param_graph() {
    hz::Input x{"x", hz::Signal{frames<Moving>()}};
    hz::Param room_size{"roomSize", hz::ge(0.0), hz::le(100.0)};
    hz::Param damping{"damping", hz::ge(0.0), hz::le(100.0)};
    hz::Param width_knob{"width", hz::ge(0.0), hz::le(100.0)};
    hz::Param wet_level{"wetLevel", hz::ge(0.0), hz::le(100.0)};
    hz::Param dry_level{"dryLevel", hz::ge(0.0), hz::le(100.0)};
    const hz::Expr feedback = smoothed_if<Moving>(room_size * f(0.28f) / 100.0 + f(0.7f));
    const hz::Expr damp = smoothed_if<Moving>(damping * f(0.4f) / 100.0);
    const hz::Expr width = smoothed_if<Moving>(width_knob / 100.0);
    const hz::Expr wet = smoothed_if<Moving>(wet_level * 1.5 / 100.0);
    const hz::Expr dry_gain = smoothed_if<Moving>(dry_level * 2.0 / 100.0);
    const hz::Expr wet1 = wet * (1.0 + width);
    const hz::Expr wet2 = wet * (1.0 - width);
    const hz::Expr left = room_knob(x, 0, feedback, damp);
    const hz::Expr right = room_knob(x, kSpread, feedback, damp);
    const hz::Expr dry = x * dry_gain;
    hz::Output l{"l"};
    hz::Output r{"r"};
    l = left * wet1 + right * wet2 + dry;
    r = right * wet1 + left * wet2 + dry;
    return hz::finish(Level, l, r);
}

// std::filters::tpt::svf's setFrequency on the two knobs (in float, where Cmajor designs in double), then the filter.
template <bool Moving = false>
constexpr hz::Graph filter_param_graph() {
    hz::Input x{"x", hz::Signal{2, frames<Moving>()}};
    hz::Param frequency{"frequency", hz::ge(0.0), hz::le(20000.0)};
    hz::Param q{"q", hz::ge(0.01), hz::le(100.0)};
    const hz::Expr cutoff = hz::clamp(frequency, 0.0, kFs * 0.48);
    constexpr double period = 0.5 / kFs;
    // tan over [0, 0.48π], the cutoff's clamp: [0, 15.9], which the range proof does not derive.
    const hz::Expr tangent = hz::tan(6.283185307179586 * cutoff * period).promise(hz::ge(0.0), hz::le(16.0));
    const hz::Expr warped = 2.0 * kFs * tangent * period;
    const hz::Expr inv_q = 1.0 / q;
    const hz::Expr d = 1.0 / (1.0 + inv_q * warped + warped * warped);
    const hz::Expr g1 = warped + inv_q;
    hz::Variable z0;
    hz::Variable z1;
    const hz::Expr high = (x - g1 * z0[n - 1] - z1[n - 1]) * d;
    const hz::Expr band = warped * high + z0[n - 1];
    const hz::Expr low = warped * band + z1[n - 1];
    z0[n] = warped * high + band;
    z1[n] = warped * band + low;
    hz::Output y{"y"};
    y = low;
    return hz::finish(y);
}

// The patch's knobs at their `init` values, for the arms that take them as Params; none for those with constants.
constexpr auto kFreeverbKnobs = [](auto& run) {
    run.param("roomSize") = 80.0;
    run.param("damping") = 50.0;
    run.param("width") = 100.0;
    run.param("wetLevel") = 33.0;
    run.param("dryLevel") = 40.0;
};
constexpr auto kFilterKnobs = [](auto& run) {
    run.param("frequency") = 1000.0;
    run.param("q") = static_cast<double>(0.707107f);
};
constexpr auto kNoKnobs = [](auto&) {};

template <auto Build, auto Knobs>
void bench_heinzel_freeverb(benchmark::State& state) {
    if (!designed_ok()) {
        state.SkipWithError("freeverb/heinzel: a constant's copy differs from Cmajor's design");
        return;
    }
    const PageVector<float> x = make_mono(N);
    OutputVector<float> l(N);
    OutputVector<float, 1> r(N);
    std::vector<float> ref(2 * N);
    std::vector<float> both(2 * N);
    freeverb_cmajor_ref(x.data(), ref.data(), N);
    timed<Build>(
        state, kN,
        [&](auto& run) {
            run.input("x") = x.data();
            run.output("l") = l.data();
            run.output("r") = r.data();
            Knobs(run);
        },
        [&]() -> const char* {
            for (std::size_t i = 0; i < N; ++i) {
                if (std::fabs(l[i] - ref[2 * i]) > 1e-4f || std::fabs(r[i] - ref[2 * i + 1]) > 1e-4f) {
                    return "freeverb/heinzel: mismatch vs the hand Freeverb";
                }
                both[2 * i] = l[i];
                both[2 * i + 1] = r[i];
            }
            state.counters["max_rel_err"] = peak_rel_err(both.data(), freeverb_exact().data(), both.size());
            return nullptr;
        });
}

template <auto Build, auto Knobs>
void bench_heinzel_filter(benchmark::State& state) {
    if (!designed_ok()) {
        state.SkipWithError("filter/heinzel: a coefficient's copy differs from Cmajor's design");
        return;
    }
    const PageVector<float> x = make_stereo(N);
    OutputVector<float> y(2 * N);
    std::vector<float> ref(2 * N);
    svf_cmajor_ref(x.data(), ref.data(), N);
    timed<Build>(
        state, kN,
        [&](auto& run) {
            run.input("x") = x.data();
            run.output("y") = y.data();
            Knobs(run);
        },
        [&]() -> const char* {
            if (maxabs_diff(y.data(), ref.data(), y.size()) > 1e-4) {
                return "filter/heinzel: mismatch vs the hand svf";
            }
            state.counters["max_rel_err"] = peak_rel_err(y.data(), svf_exact().data(), y.size());
            return nullptr;
        });
}

// The knobs moving, block by block as cmajor_moving runs Cmajor's: one run of kBlock frames a block, its knobs set
// (`set`, from `sets`) and its buffers bound before it; the whole signal checked, then timed.
template <auto Build, class Bind, class Set, class Check>
void heinzel_moving(benchmark::State& state, Bind bind, Set set, Check check) {
    const auto run = std::make_unique<hz::Executor<Build, heinzel_bench::kMachine>>();
    run->prepare(static_cast<std::int64_t>(kBlock));
    const auto pass = [&] {
        for (std::size_t b = 0; b * kBlock < N; ++b) {
            bind(*run, b * kBlock);
            set(*run, b % 2);
            run->run(static_cast<std::int64_t>(kBlock));
        }
    };
    pass();
    if (const char* why = check()) {
        state.SkipWithError(why);
        return;
    }
    for (auto _ : state) {
        pass();
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(N));
}

template <hz::Precision Level>
void bench_heinzel_freeverb_moving(benchmark::State& state) {
    const PageVector<float> x = make_mono(N);
    OutputVector<float> l(N);
    OutputVector<float, 1> r(N);
    std::vector<float> ref(2 * N);
    std::vector<float> both(2 * N);
    freeverb_moving_ref(x.data(), ref.data(), N);
    heinzel_moving<freeverb_param_graph<Level, true>>(
        state,
        [&](auto& run, std::size_t at) {
            run.input("x") = x.data() + at;
            run.output("l") = l.data() + at;
            run.output("r") = r.data() + at;
        },
        [](auto& run, std::size_t k) {
            const auto& v = kFreeverbMoves[k];
            run.param("roomSize") = static_cast<double>(v[0]);
            run.param("damping") = static_cast<double>(v[1]);
            run.param("width") = static_cast<double>(v[2]);
            run.param("wetLevel") = static_cast<double>(v[3]);
            run.param("dryLevel") = static_cast<double>(v[4]);
        },
        [&]() -> const char* {
            for (std::size_t i = 0; i < N; ++i) {
                if (std::fabs(l[i] - ref[2 * i]) > 1e-4f || std::fabs(r[i] - ref[2 * i + 1]) > 1e-4f) {
                    return "freeverb-moving/heinzel: mismatch vs the hand Freeverb";
                }
                both[2 * i] = l[i];
                both[2 * i + 1] = r[i];
            }
            state.counters["max_rel_err"] = peak_rel_err(both.data(), freeverb_moving_exact().data(), both.size());
            return nullptr;
        });
}

void bench_heinzel_filter_moving(benchmark::State& state) {
    const PageVector<float> x = make_stereo(N);
    OutputVector<float> y(2 * N);
    std::vector<float> ref(2 * N);
    svf_moving_ref(x.data(), ref.data(), N);
    heinzel_moving<filter_param_graph<true>>(
        state,
        [&](auto& run, std::size_t at) {
            run.input("x") = x.data() + 2 * at;
            run.output("y") = y.data() + 2 * at;
        },
        [](auto& run, std::size_t k) {
            run.param("frequency") = static_cast<double>(kFilterMoves[k][0]);
            run.param("q") = static_cast<double>(kFilterMoves[k][1]);
        },
        [&]() -> const char* {
            if (maxabs_diff(y.data(), ref.data(), y.size()) > 1e-4) {
                return "filter-moving/heinzel: mismatch vs the hand svf";
            }
            state.counters["max_rel_err"] = peak_rel_err(y.data(), svf_moving_exact().data(), y.size());
            return nullptr;
        });
}

BENCHMARK(bench_heinzel_freeverb<freeverb_graph<hz::Normal>, kNoKnobs>)->Name("freeverb/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_freeverb<freeverb_graph<hz::Fast>, kNoKnobs>)->Name("freeverb/heinzel-fast")->UseRealTime();
BENCHMARK(bench_heinzel_freeverb<freeverb_param_graph<hz::Normal>, kFreeverbKnobs>)
    ->Name("freeverb/heinzel-param")
    ->UseRealTime();
BENCHMARK(bench_heinzel_freeverb<freeverb_param_graph<hz::Fast>, kFreeverbKnobs>)
    ->Name("freeverb/heinzel-param-fast")
    ->UseRealTime();
BENCHMARK(bench_heinzel_filter<filter_graph<hz::Normal>, kNoKnobs>)->Name("filter/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_filter<filter_graph<hz::Fast>, kNoKnobs>)->Name("filter/heinzel-fast")->UseRealTime();
BENCHMARK(bench_heinzel_filter<filter_param_graph<>, kFilterKnobs>)->Name("filter/heinzel-param")->UseRealTime();
BENCHMARK(bench_heinzel_freeverb_moving<hz::Normal>)->Name("freeverb-moving/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_freeverb_moving<hz::Fast>)->Name("freeverb-moving/heinzel-fast")->UseRealTime();
BENCHMARK(bench_heinzel_filter_moving)->Name("filter-moving/heinzel")->UseRealTime();

} // namespace
