// Applications (tier-2), the DSP rows `main`'s `apps_bench` had and this branch's port pruned (P6.2, BP.6;
// `Plan/archive/parallel/06` §5): each a nest arm over the same F32 data as `main`'s plain C++ arms, ported verbatim,
// every arm checked against a float64 reference before it is timed.
//
//   dsp/biquad     y = 0.3x + 0.4x[-1] + 0.2x[-2] + 0.5y[-1] - 0.2y[-2]     one recurrence, 1 M samples
//   dsp/cascade4   four such sections chained, only the last a result        four recurrences in one cycle
//   dsp/fir32      a 32-tap window over the samples                           a window fold
//   dsp/movavg64   the mean of a 64-tap window                                a window fold, 64 taps
//
// `dsp/biquad/<arm>/B<n>` and `dsp/cascade4/<arm>/B<n>` run the same 1M samples a block of n at a time, the state
// carried from block to block as a plugin's is, n known at run time on both sides: the Heinzel arm on
// `Executor<…, kMachine>` over a `Length`, prepared for n, the hand arm one call a block.
//
// The recurrences run over time (the stream axis); the two windows over an element axis of {N, 1}, as
// `bench_fusion`'s conv does — a window over the stream axis is a framed stream, which this tree refuses.
// `main` wrote movavg64 as the window mean too; its executor matched the running-sum hand arm, this tree folds
// the 64 taps per output (`hand-running` shows what a running sum is worth).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
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

constexpr std::int32_t kL = 1 << 20;
constexpr std::int32_t kFir = 32;
constexpr std::int32_t kWin = 64;

// `main`'s ramp, in float: a * ((i * 2654435761) % 1009) / 1009 + b.
PageVector<float> ramp(std::size_t n, float a, float b) {
    PageVector<float> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = a * static_cast<float>((i * 2654435761u) % 1009) / 1009.0f + b;
    }
    return v;
}

constexpr hz::Graph analysed(hz::Graph g) {
    if (!hz::infer(g).ok) {
        hz::fail("bench_apps_dsp: a graph did not infer");
    }
    return g;
}
constexpr double f(float v) { return static_cast<double>(v); } // a float constant, exactly

// The two recurrences over `length` samples (a number, or a `Length` given at run time), unanalysed: the 1M-sample
// rows analyse them once, the block rows hand them to `Executor`, which does.
constexpr hz::Graph biquad(auto length) {
    hz::Input x{"x", hz::Signal{length}};
    hz::Variable y;
    y[n] = f(0.3f) * x + f(0.4f) * x[n - 1] + f(0.2f) * x[n - 2] + f(0.5f) * y[n - 1] - f(0.2f) * y[n - 2];
    return hz::finish(hz::Output{"y"} = y);
}

constexpr hz::Graph cascade4(auto length) {
    hz::Input x{"x", hz::Signal{length}};
    hz::Variable y1;
    hz::Variable y2;
    hz::Variable y3;
    hz::Variable y4;
    y1[n] = f(0.3f) * x + f(0.2f) * x[n - 1] - f(0.4f) * y1[n - 1] + f(0.1f) * y1[n - 2];
    y2[n] = f(0.5f) * y1 + f(0.1f) * y1[n - 1] + f(0.3f) * y2[n - 1] - f(0.2f) * y2[n - 2];
    y3[n] = f(0.4f) * y2 + f(0.2f) * y2[n - 1] - f(0.1f) * y3[n - 1] + f(0.15f) * y3[n - 2];
    y4[n] = f(0.6f) * y3 + f(0.1f) * y3[n - 1] + f(0.2f) * y4[n - 1] - f(0.1f) * y4[n - 2];
    return hz::finish(hz::Output{"y"} = y4);
}

constexpr hz::Graph kBiquad = analysed(biquad(kL));
constexpr hz::Graph kCascade4 = analysed(cascade4(kL));

constexpr hz::Graph kFirGraph = [] {
    hz::Input x{"x", {kL}};
    hz::Input h{"h", {1, kFir}};
    return analysed(hz::finish(hz::Output{"y"} = (x.unfold(0, kFir) * h).sum(1)));
}();

constexpr hz::Graph kMovavg = [] {
    hz::Input x{"x", {kL}};
    return analysed(hz::finish(hz::Output{"y"} = x.unfold(0, kWin).mean(1)));
}();

constexpr const hz::loops::Program& kBiquadNest = heinzel_bench::kProgramOf<kBiquad, heinzel_bench::kMachine>;
constexpr const hz::loops::Program& kCascadeNest = heinzel_bench::kProgramOf<kCascade4, heinzel_bench::kMachine>;
constexpr const hz::loops::Program& kFirNest = heinzel_bench::kProgramOf<kFirGraph, heinzel_bench::kMachine>;
constexpr const hz::loops::Program& kMovavgNest = heinzel_bench::kProgramOf<kMovavg, heinzel_bench::kMachine>;

// ---- the references, in double ----------------------------------------------------------------------------

std::vector<double> biquad_ref(const PageVector<float>& x) {
    std::vector<double> y(x.size());
    double x1 = 0;
    double x2 = 0;
    double y1 = 0;
    double y2 = 0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        y[i] = f(0.3f) * x[i] + f(0.4f) * x1 + f(0.2f) * x2 + f(0.5f) * y1 - f(0.2f) * y2;
        x2 = x1, x1 = x[i], y2 = y1, y1 = y[i];
    }
    return y;
}

std::vector<double> cascade_ref(const PageVector<float>& x) {
    std::vector<double> y(x.size());
    double xa1 = 0;
    double a1 = 0;
    double a2 = 0;
    double ya1 = 0;
    double b1 = 0;
    double b2 = 0;
    double yb1 = 0;
    double c1 = 0;
    double c2 = 0;
    double yc1 = 0;
    double d1 = 0;
    double d2 = 0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double s1 = f(0.3f) * x[i] + f(0.2f) * xa1 - f(0.4f) * a1 + f(0.1f) * a2;
        const double s2 = f(0.5f) * s1 + f(0.1f) * ya1 + f(0.3f) * b1 - f(0.2f) * b2;
        const double s3 = f(0.4f) * s2 + f(0.2f) * yb1 - f(0.1f) * c1 + f(0.15f) * c2;
        const double s4 = f(0.6f) * s3 + f(0.1f) * yc1 + f(0.2f) * d1 - f(0.1f) * d2;
        xa1 = x[i], a2 = a1, a1 = s1, ya1 = s1, b2 = b1, b1 = s2, yb1 = s2, c2 = c1, c1 = s3, yc1 = s3, d2 = d1, d1 = s4;
        y[i] = s4;
    }
    return y;
}

std::vector<double> window_ref(const PageVector<float>& x, const PageVector<float>* h, int taps) {
    std::vector<double> y(x.size() - static_cast<std::size_t>(taps) + 1);
    for (std::size_t i = 0; i < y.size(); ++i) {
        double a = 0;
        for (int k = 0; k < taps; ++k) {
            a += f(x[i + static_cast<std::size_t>(k)]) * (h ? f((*h)[static_cast<std::size_t>(k)]) : 1.0);
        }
        y[i] = h ? a : a / taps;
    }
    return y;
}

// Every output within `tol` of the reference, relative to the reference's largest magnitude (a recurrence's
// and a window's terms are of that size); the largest relative error seen, or -1 where one is out of bounds.
double checked(const float* y, const std::vector<double>& want, double tol) {
    double scale = 0.0;
    double worst = 0.0;
    for (const double w : want) {
        scale = std::max(scale, std::fabs(w));
    }
    for (std::size_t i = 0; i < want.size(); ++i) {
        const double off = std::fabs(static_cast<double>(y[i]) - want[i]) / std::max(scale, 1e-30);
        if (!(off <= tol)) {
            return -1.0;
        }
        worst = std::max(worst, off);
    }
    return worst;
}

template <const hz::loops::Program& P>
void time_nest(benchmark::State& state, const hz::loops::Feeds& feeds, const std::vector<double>& want, double tol) {
    hz::loops::Workspace<P> work(hz::loops::Sizes{});
    hz::loops::run<P>(feeds, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(P.outputs[0]));
    const double worst = checked(y, want, tol);
    if (worst < 0) {
        state.SkipWithError("bench_apps_dsp: the nest arm is outside its bound");
        return;
    }
    state.counters["max_rel_err"] = worst;
    for (auto _ : state) {
        hz::loops::run<P>(feeds, work);
        benchmark::DoNotOptimize(y);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(want.size()));
}

// A plain C++ arm: run once and check, then time (inputs reloaded each iteration: ClobberMemory before).
template <class Run>
void time_ref(benchmark::State& state, OutputVector<float>& y, const std::vector<double>& want, double tol, const Run& run) {
    run();
    if (checked(y.data(), want, tol) < 0) {
        state.SkipWithError("bench_apps_dsp: a reference arm disagrees with the float64 reference");
        return;
    }
    for (auto _ : state) {
        benchmark::ClobberMemory();
        run();
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(want.size()));
}

// ---- the arms -----------------------------------------------------------------------------------------------

void biquad_nest(benchmark::State& s) {
    const PageVector<float> x = ramp(kL, 1, -0.5f);
    time_nest<kBiquadNest>(s, hz::loops::Feeds{x.data()}, biquad_ref(x), 1e-5);
}
void biquad_hand(benchmark::State& s) {
    const PageVector<float> x = ramp(kL, 1, -0.5f);
    OutputVector<float> y(kL);
    time_ref(s, y, biquad_ref(x), 1e-5, [&] {
        float x1 = 0;
        float x2 = 0;
        float y1 = 0;
        float y2 = 0;
        for (std::int32_t i = 0; i < kL; ++i) {
            float o = 0.3f * x[i] + 0.4f * x1 + 0.2f * x2 + 0.5f * y1 - 0.2f * y2;
            x2 = x1, x1 = x[i], y2 = y1, y1 = o;
            y[i] = o;
        }
    });
}

void cascade_nest(benchmark::State& s) {
    const PageVector<float> x = ramp(kL, 1, -0.5f);
    time_nest<kCascadeNest>(s, hz::loops::Feeds{x.data()}, cascade_ref(x), 1e-5);
}
void cascade_hand(benchmark::State& s) {
    const PageVector<float> x = ramp(kL, 1, -0.5f);
    OutputVector<float> y(kL);
    time_ref(s, y, cascade_ref(x), 1e-5, [&] {
        float a1 = 0;
        float a2 = 0;
        float b1 = 0;
        float b2 = 0;
        float c1 = 0;
        float c2 = 0;
        float d1 = 0;
        float d2 = 0;
        float xa1 = 0;
        float ya1 = 0;
        float yb1 = 0;
        float yc1 = 0;
        for (std::int32_t i = 0; i < kL; ++i) {
            float s1 = 0.3f * x[i] + 0.2f * xa1 - 0.4f * a1 + 0.1f * a2;
            xa1 = x[i], a2 = a1, a1 = s1;
            float s2 = 0.5f * s1 + 0.1f * ya1 + 0.3f * b1 - 0.2f * b2;
            ya1 = s1, b2 = b1, b1 = s2;
            float s3 = 0.4f * s2 + 0.2f * yb1 - 0.1f * c1 + 0.15f * c2;
            yb1 = s2, c2 = c1, c1 = s3;
            float s4 = 0.6f * s3 + 0.1f * yc1 + 0.2f * d1 - 0.1f * d2;
            yc1 = s3, d2 = d1, d1 = s4;
            y[i] = s4;
        }
    });
}

// ---- the recurrences at audio block sizes ------------------------------------------------------------------

// The block length is a `Length`: given at run time, as a host gives a plugin its block, and as the hand arm's call
// is given it.
constexpr hz::Graph biquad_stream() { return biquad(hz::Length{}); }
constexpr hz::Graph cascade4_stream() { return cascade4(hz::Length{}); }

// All kL samples, B a run, through one `Executor` prepared for B; checked against the double reference first.
template <auto Build, std::int32_t B>
void block_heinzel(benchmark::State& s, std::vector<double> (*want)(const PageVector<float>&)) {
    const PageVector<float> x = ramp(kL, 1, -0.5f);
    OutputVector<float> y(kL);
    const auto run = std::make_unique<hz::Executor<Build, heinzel_bench::kMachine>>();
    run->prepare(B);
    const auto all = [&] {
        for (std::int32_t b = 0; b < kL; b += B) {
            run->input("x") = x.data() + b;
            run->output("y") = y.data() + b;
            run->run(B);
        }
    };
    all();
    const double worst = checked(y.data(), want(x), 1e-5);
    if (worst < 0) {
        s.SkipWithError("bench_apps_dsp: a block arm is outside its bound");
        return;
    }
    s.counters["max_rel_err"] = worst;
    for (auto _ : s) {
        all();
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    s.SetItemsProcessed(static_cast<std::int64_t>(s.iterations()) * kL);
}

struct BiquadState {
    float x1 = 0;
    float x2 = 0;
    float y1 = 0;
    float y2 = 0;
};
[[gnu::noinline]] void biquad_hand_block(const float* x, float* y, std::int32_t len, BiquadState& st) {
    float x1 = st.x1;
    float x2 = st.x2;
    float y1 = st.y1;
    float y2 = st.y2;
    for (std::int32_t i = 0; i < len; ++i) {
        const float o = 0.3f * x[i] + 0.4f * x1 + 0.2f * x2 + 0.5f * y1 - 0.2f * y2;
        x2 = x1, x1 = x[i], y2 = y1, y1 = o, y[i] = o;
    }
    st = {x1, x2, y1, y2};
}

struct CascadeState {
    float a1 = 0;
    float a2 = 0;
    float b1 = 0;
    float b2 = 0;
    float c1 = 0;
    float c2 = 0;
    float d1 = 0;
    float d2 = 0;
    float xa1 = 0;
    float ya1 = 0;
    float yb1 = 0;
    float yc1 = 0;
};
[[gnu::noinline]] void cascade4_hand_block(const float* x, float* y, std::int32_t len, CascadeState& st) {
    auto [a1, a2, b1, b2, c1, c2, d1, d2, xa1, ya1, yb1, yc1] = st;
    for (std::int32_t i = 0; i < len; ++i) {
        const float s1 = 0.3f * x[i] + 0.2f * xa1 - 0.4f * a1 + 0.1f * a2;
        xa1 = x[i], a2 = a1, a1 = s1;
        const float s2 = 0.5f * s1 + 0.1f * ya1 + 0.3f * b1 - 0.2f * b2;
        ya1 = s1, b2 = b1, b1 = s2;
        const float s3 = 0.4f * s2 + 0.2f * yb1 - 0.1f * c1 + 0.15f * c2;
        yb1 = s2, c2 = c1, c1 = s3;
        const float s4 = 0.6f * s3 + 0.1f * yc1 + 0.2f * d1 - 0.1f * d2;
        yc1 = s3, d2 = d1, d1 = s4;
        y[i] = s4;
    }
    st = {a1, a2, b1, b2, c1, c2, d1, d2, xa1, ya1, yb1, yc1};
}

// All kL samples, B a call, the state carried in `State`.
template <class State, auto Block, std::int32_t B>
void block_hand(benchmark::State& s, std::vector<double> (*want)(const PageVector<float>&)) {
    const PageVector<float> x = ramp(kL, 1, -0.5f);
    OutputVector<float> y(kL);
    time_ref(s, y, want(x), 1e-5, [&] {
        State st{};
        for (std::int32_t b = 0; b < kL; b += B) {
            Block(x.data() + b, y.data() + b, B, st);
        }
    });
}

template <std::int32_t B>
void register_blocks() {
    const std::string size = "/B" + std::to_string(B);
    using benchmark::RegisterBenchmark;
    RegisterBenchmark("dsp/biquad/heinzel" + size, block_heinzel<biquad_stream, B>, biquad_ref)->UseRealTime();
    RegisterBenchmark("dsp/biquad/hand" + size, block_hand<BiquadState, biquad_hand_block, B>, biquad_ref)
        ->UseRealTime();
    RegisterBenchmark("dsp/cascade4/heinzel" + size, block_heinzel<cascade4_stream, B>, cascade_ref)->UseRealTime();
    RegisterBenchmark("dsp/cascade4/hand" + size, block_hand<CascadeState, cascade4_hand_block, B>, cascade_ref)
        ->UseRealTime();
}

const bool g_blocks = [] {
    register_blocks<32>();
    register_blocks<64>();
    register_blocks<256>();
    return true;
}();

void fir_nest(benchmark::State& s) {
    const PageVector<float> x = ramp(kL, 1, -0.5f);
    const PageVector<float> h = ramp(kFir, 0.1f, 0.0f);
    time_nest<kFirNest>(s, hz::loops::Feeds{x.data(), h.data()}, window_ref(x, &h, kFir), 1e-5);
}
void fir_naive(benchmark::State& s) {
    const PageVector<float> x = ramp(kL, 1, -0.5f);
    const PageVector<float> h = ramp(kFir, 0.1f, 0.0f);
    OutputVector<float> y(kL - kFir + 1);
    time_ref(s, y, window_ref(x, &h, kFir), 1e-5, [&] {
        for (std::int32_t i = 0; i < kL - kFir + 1; ++i) {
            float a = 0;
            for (std::int32_t k = 0; k < kFir; ++k) {
                a += x[i + k] * h[k];
            }
            y[i] = a;
        }
    });
}

void movavg_nest(benchmark::State& s) {
    const PageVector<float> x = ramp(kL, 1, -0.5f);
    time_nest<kMovavgNest>(s, hz::loops::Feeds{x.data()}, window_ref(x, nullptr, kWin), 1e-5);
}
void movavg_naive(benchmark::State& s) {
    const PageVector<float> x = ramp(kL, 1, -0.5f);
    OutputVector<float> y(kL - kWin + 1);
    time_ref(s, y, window_ref(x, nullptr, kWin), 1e-5, [&] {
        for (std::int32_t i = 0; i < kL - kWin + 1; ++i) {
            float a = 0;
            for (std::int32_t k = 0; k < kWin; ++k) {
                a += x[i + k];
            }
            y[i] = a * (1.0f / kWin);
        }
    });
}
void movavg_hand_running(benchmark::State& s) { // O(N): a running sum — which drifts, its error growing with the length
    const PageVector<float> x = ramp(kL, 1, -0.5f);
    OutputVector<float> y(kL - kWin + 1);
    time_ref(s, y, window_ref(x, nullptr, kWin), 1e-2, [&] { // (measured: 3e-3 of the largest output)
        float a = 0;
        for (std::int32_t k = 0; k < kWin; ++k) {
            a += x[k];
        }
        y[0] = a * (1.0f / kWin);
        for (std::int32_t i = 1; i < kL - kWin + 1; ++i) {
            a += x[i + kWin - 1] - x[i - 1];
            y[i] = a * (1.0f / kWin);
        }
    });
}

} // namespace

BENCHMARK(biquad_nest)->Name("dsp/biquad/heinzel")->UseRealTime();
BENCHMARK(biquad_hand)->Name("dsp/biquad/hand")->UseRealTime();
BENCHMARK(cascade_nest)->Name("dsp/cascade4/heinzel")->UseRealTime();
BENCHMARK(cascade_hand)->Name("dsp/cascade4/hand")->UseRealTime();
BENCHMARK(fir_nest)->Name("dsp/fir32/heinzel")->UseRealTime();
BENCHMARK(fir_naive)->Name("dsp/fir32/naive")->UseRealTime();
BENCHMARK(movavg_nest)->Name("dsp/movavg64/heinzel")->UseRealTime();
BENCHMARK(movavg_naive)->Name("dsp/movavg64/naive")->UseRealTime();
BENCHMARK(movavg_hand_running)->Name("dsp/movavg64/hand-running")->UseRealTime();

BENCHMARK_MAIN();
