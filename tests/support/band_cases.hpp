#pragma once

// The band cases (`nest_band_test`, `nest_band_workloads_test`): graphs planned by the schedules of a machine that
// bands nothing, a band added by hand over all their stages, and the run that holds a banded program to the unbanded
// one bit for bit, each run canaried (`canaried.hpp`: nothing written outside a buffer, the silence still zero). Two
// test files share it so neither is the suite's slowest translation unit alone (D-6, `Plan/archive/12` M4).

#include "heinzel/dsp/oscillator.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/notation/einsum.hpp"
#include "heinzel/sema/analyze.hpp"

#include "canaried.hpp"
#include "planned.hpp"

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string_view>
#include <vector>

namespace band_cases {

using namespace heinzel;
namespace loops = heinzel::loops;
using loops::Schedule;

// Bands are added by hand here (the planner's are tested in `nest_band_plan_test.cpp`): planned without.
constexpr Machine kMachine = [] {
    Machine m = zen2;
    m.band_bytes = 0;
    return m;
}();
// A machine that never recomputes a producer inside its window (`bench_fusion.cpp` `kAlwaysStores`).
constexpr Machine kStoring = [] {
    Machine m = zen2;
    m.recompute_always = m.recompute_if_uncached = -1;
    m.band_bytes = 0;
    return m;
}();
constexpr std::int32_t kH = 37;
constexpr std::int32_t kW = 70;

constexpr Graph analysed(Graph g) {
    if (!infer(g).ok) {
        fail("band_cases: a graph did not resolve");
    }
    return g;
}

constexpr Expr vblur(Expr v, Expr w) { return (v.unfold(0, 3) * w).sum(1); }

// `also`: 0 nothing more, 1 the horizontal blur is a second result, 2 the reader adds its first rows.
constexpr Graph blurxy(int also) {
    Input x{"x", {kH, kW}};
    Input wh{"wh", {1, 1, 3}};
    Input wv{"wv", {1, 3, 1}};
    Expr bx = (x.unfold(1, 3) * wh).sum(2);
    Expr y = vblur(bx, wv);
    if (also == 1) {
        return analysed(finish(Output{"y"} = y, Output{"bx"} = bx));
    }
    return analysed(finish(Output{"y"} = also == 2 ? y + bx.slice(0, 0, kH - 2) : y));
}

constexpr Graph harris() {
    Input x{"x", {12, kW - 33}};
    Input wx{"wx", {1, 3, 1, 3}};
    Input wy{"wy", {1, 3, 1, 3}};
    Expr taps = x.unfold2d(0, 3, 1, 3);
    Expr dx = (taps * wx).sum({1, 3});
    Expr dy = (taps * wy).sum({1, 3});
    const auto box = [](Expr v) { return v.unfold2d(0, 3, 1, 3).sum({1, 3}); };
    Expr sxx = box(dx * dx);
    Expr sxy = box(dx * dy);
    Expr syy = box(dy * dy);
    Expr tr = sxx + syy;
    return analysed(finish(Output{"y"} = sxx * syy - sxy * sxy - static_cast<double>(0.04f) * tr * tr));
}

constexpr Graph chain() {
    Input x{"x", {kH, kW}};
    Input w{"w", {1, 3, 1}};
    return analysed(finish(Output{"y"} = vblur(vblur(vblur(x, w), w), w)));
}

constexpr Graph conv() {
    Input x{"x", {1000}};
    Input h{"h", {1, 7}};
    return analysed(finish(Output{"y"} = ((x * x).unfold(0, 7) * h).sum(1)));
}

constexpr Graph oscillator() {
    Input inc{"inc", Signal{Length{.multiple_of = 1, .min = 1, .expect = 512}}};
    return analysed(finish(Output{"y"} = dsp::oscillator(inc, constant(0.5))));
}

constexpr Graph onepole() {
    constexpr Index n{};
    Input x{"x", Signal{3, Length{.multiple_of = 1, .min = 1, .expect = 64}}};
    Variable y;
    y[n] = x + 0.9375 * y[n - 1];
    return analysed(finish(Output{"y"} = y * 0.5));
}

// (A stage in a band is not split among workers — its band is — so the stages taken lose the split the planner
// gave them.)
constexpr Schedule with_band(Schedule d, Schedule::Band band) {
    d.bands.push_back(band);
    for (std::int32_t pos = 0; pos < band.count && static_cast<std::size_t>(band.first + pos) < d.stages.size(); ++pos) {
        d.stages[static_cast<std::size_t>(band.first + pos)].parallel = -1;
    }
    return d;
}

// The work after a cycle in a stage of its own, ahead of the stage after it or alone, as the planner laid these graphs
// out before it weighed that work (Plan/23 04 4.3a, `planner/trail.hpp`: a one-pole's or a phase's cheap follower now
// runs in the cycle's stage). The band cases want a feedback stage and a stage reading it, the band carrying the
// cycle's value between them. A stage of its own walks time, then the voices in the cycle stage's lanes.
constexpr Schedule trail_apart(const Graph& g, Schedule s) {
    const loops::graph::Facts facts = loops::graph::facts_of(g);
    const loops::graph::Feedback feedback = loops::graph::find_feedback(g, facts);
    for (std::size_t k = 0; k < s.stages.size(); ++k) {
        Schedule::Stage& stage = s.stages[k];
        std::size_t last = stage.nodes.size();
        for (std::size_t q = 0; q < stage.nodes.size(); ++q) {
            last = feedback.cycle[static_cast<std::size_t>(stage.nodes[q])] >= 0 ? q : last;
        }
        if (last + 1 >= stage.nodes.size()) {
            continue;
        }
        Schedule::Stage after = k + 1 < s.stages.size() ? s.stages[k + 1] : Schedule::Stage{};
        auto nodes = stage.nodes;
        auto kept = stage.nodes;
        nodes.clear(), kept.clear();
        for (std::size_t q = 0; q < stage.nodes.size(); ++q) {
            (q <= last ? kept : nodes).push_back(stage.nodes[q]);
        }
        for (const std::int32_t ni : after.nodes) {
            nodes.push_back(ni);
        }
        if (k + 1 == s.stages.size()) {
            const std::int32_t lanes = stage.loops[0].kind == loops::LoopKind::Vector ? stage.loops[0].step : 8;
            const bool voices = g.values[static_cast<std::size_t>(g.nodes[static_cast<std::size_t>(kept[0])].output)].shape.rank == 2;
            after.loops.push_back(voices ? Schedule::Loop{.axis = 1} : Schedule::Loop{.axis = 0, .step = lanes, .kind = loops::LoopKind::Vector});
            if (voices) {
                after.loops.push_back(Schedule::Loop{.axis = 0, .step = lanes, .kind = loops::LoopKind::Vector});
            }
            after.inner_from = static_cast<std::int32_t>(after.loops.size());
            s.stages.push_back(after);
        }
        stage.nodes = kept;
        s.stages[k + 1].nodes = nodes;
        return s;
    }
    return s;
}

// The graphs, what was planned for them (once per graph and machine; without bands, the work after a cycle apart), the
// program of that schedule, and the same with a band over all its stages.
constexpr Graph kBlurxy = blurxy(0);
constexpr Graph kBlurxyOut = blurxy(1);
constexpr Graph kBlurxyReaches = blurxy(2);
constexpr Graph kHarris = harris();
constexpr Graph kChain = chain();
constexpr Graph kConv = conv();
constexpr Graph kOsc = oscillator();
constexpr Graph kOnepole = onepole();
template <const Graph& G, const Machine& M = kMachine>
constexpr Schedule kSchedule = M.band_bytes == 0 ? trail_apart(G, planned::schedule_of(G, M)) : planned::schedule_of(G, M);
template <const Graph& G, const Machine& M = kMachine>
constexpr planned::Held kPlainHeld{planned::program_of(G, kSchedule<G, M>)};
template <const Graph& G, const Machine& M = kMachine>
constexpr const loops::Program& kPlain = kPlainHeld<G, M>.program;
template <const Graph& G, std::int32_t Axis, std::int32_t Step, const Machine& M = kMachine>
constexpr planned::Held kBandedHeld{planned::program_of(G, with_band(kSchedule<G, M>, Schedule::Band{0, static_cast<std::int32_t>(kSchedule<G, M>.stages.size()), Axis, Step}))};
template <const Graph& G, std::int32_t Axis, std::int32_t Step, const Machine& M = kMachine>
constexpr const loops::Program& kBanded = kBandedHeld<G, Axis, Step, M>.program;
// The schedule lowers (`planned::program_of` stops the build where the lowering refuses it).
template <const Graph& G, const Machine& M = kMachine>
constexpr bool kLowers = !kPlain<G, M>.stages.empty();

inline int g_wrong = 0;

inline std::vector<float> noise(std::size_t n, std::uint32_t seed) {
    std::vector<float> v(n);
    std::uint32_t state = seed;
    for (float& x : v) {
        state = state * 1664525u + 1013904223u;
        x = static_cast<float>(static_cast<std::int32_t>(state >> 8) % 2001 - 1000) * 0.00173f;
    }
    return v;
}

// Feeds go in the graph's input order, which is the builder's: bind them by name.
struct Feed {
    std::string_view name;
    const std::vector<float>* data;
};
inline loops::Feeds feeds_of(const Graph& g, std::initializer_list<Feed> by_name) {
    loops::Feeds feeds{};
    for (std::size_t i = 0; i < g.inputs.size(); ++i) {
        for (const Feed& f : by_name) {
            feeds[i] = g.inputs[i].name.view() == f.name ? f.data->data() : feeds[i];
        }
    }
    return feeds;
}

// Every output of `Banded` equals, bit for bit, the same output of `Plain` — each run twice on one workspace,
// in lockstep: the second continues the first (Plan/archive/14-dsl/05).
template <const loops::Program& Plain, const loops::Program& Banded>
void same(const char* what, const loops::Feeds& feeds, const loops::Sizes& sizes = loops::Sizes{}) {
    canaried::Workspace<Plain> plain(sizes);
    canaried::Workspace<Banded> banded(sizes);
    for (int run = 0; run < 2; ++run) {
        canaried::run<Plain>(feeds, plain);
        canaried::run<Banded>(feeds, banded);
        for (std::size_t o = 0; o < Plain.outputs.size(); ++o) {
            const std::size_t a = static_cast<std::size_t>(Plain.outputs[o]);
            const std::size_t b = static_cast<std::size_t>(Banded.outputs[o]);
            const std::int64_t n = loops::element_count(Plain.buffers[a], sizes);
            if (std::memcmp(plain.bytes_of(a), banded.bytes_of(b), static_cast<std::size_t>(n * 4)) != 0 && ++g_wrong <= 10) {
                std::printf("  %s: run %d, output %zu differs from the unbanded run\n", what, run, o);
            }
        }
    }
}

} // namespace band_cases
