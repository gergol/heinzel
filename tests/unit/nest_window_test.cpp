// Sliding windows on the loop-nest path, against the loops written by hand. A window is no statement and
// has no storage: what reads it reads its source at `frame * hop + tap` (`loops/graph/views.hpp`). Where
// memory allows, the fold over the taps runs in lanes ACROSS the outputs, every output's sum in its one
// order from its first tap — so the comparison is exact, in float, with no multiply-add fused.
//
//   conv   p = x*x ; y[i]   = sum_k p[i + k] * h[k]        lanes across i; 37 - 5 + 1 = 33 outputs: one
//                                                          block of 4 x 8 and a tail of one
//   hop 2  y[i] = sum_k x[2i + k] * h[k]                   a frame is two elements on: no lane across i,
//                                                          and five taps fill none either — every loop
//                                                          serial, in the walk's order from the first tap
//   blur   p = x*x ; y[i,j] = sum_k p[i + k, j] * w[k]     lanes across j (70 = 2 x 32 + 6), taps a row apart
//   High   the conv, owed its strict order: a double accumulator, still in lanes across i
//   joined the fusion benchmark's stored schedules made one stage by hand, at its sizes: programs the runner accepts

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/graph/views.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/sema/analyze.hpp"

#include "canaried.hpp"
#include "planned.hpp"
#include "tree_facts.hpp"

#include <algorithm>
#include <cstdio>
#include <string_view>
#include <vector>

using namespace heinzel;

namespace {

namespace loops = heinzel::loops;

constexpr std::int32_t kN = 37;
constexpr std::int32_t kTaps = 5;
constexpr std::int32_t kH = 9;
constexpr std::int32_t kW = 70;
constexpr std::int32_t kRows = 3;

constexpr Graph analysed(Graph g) {
    if (!infer(g).ok) {
        fail("nest_window_test: a graph did not resolve");
    }
    return g;
}

constexpr Graph conv(std::int32_t hop, bool squared, numerics::Precision level) {
    Input x{"x", {kN}};
    Input h{"h", {1, kTaps}};
    Expr windowed = (squared ? x * x : x).unfold(0, kTaps, hop);
    Graph g = finish(Output{"y"} = (windowed * h).sum(1));
    set_default_precision(g, level);
    return analysed(g);
}

constexpr Graph blur() {
    Input x{"x", {kH, kW}};
    Input w{"w", {1, kRows, 1}};
    Expr windowed = (x * x).unfold(0, kRows);
    return analysed(finish(Output{"y"} = (windowed * w).sum(1)));
}

// First with the square STORED, as a machine plans that never finds recomputing worth it; the schedule zen2
// itself gets — the square inside the window's reader — follows below.
constexpr Machine kStoring = [] {
    Machine m = zen2;
    m.recompute_always = m.recompute_if_uncached = -1;
    m.band_bytes = 0;
    return m;
}();
constexpr planned::Held kConvHeld{planned::program_of(conv(1, true, numerics::Precision::Normal), kStoring)};
constexpr const loops::Program& kConv = kConvHeld.program;
constexpr planned::Held kConvHighHeld{planned::program_of(conv(1, true, numerics::Precision::High), kStoring)};
constexpr const loops::Program& kConvHigh = kConvHighHeld.program;
constexpr planned::Held kHoppedHeld{planned::program_of(conv(2, false, numerics::Precision::Normal), zen2)};
constexpr const loops::Program& kHopped = kHoppedHeld.program;
constexpr planned::Held kBlurHeld{planned::program_of(blur(), kStoring)};
constexpr const loops::Program& kBlur = kBlurHeld.program;

// What was decided, read off the trees: lanes are a strip over the axis with a Vector (under an Unroll per body, where
// there are several bodies); a fold's own loops stand under its Registers node.

// For each walking node of stage `s`, in `tree_facts::walks` order: does it stand under a Registers node — one of a
// fold's own loops, or of the taps an order statistic gathers?
constexpr heinzel::fixed_vector<bool, 64> under_registers(const loops::Program& p, std::size_t s) {
    heinzel::fixed_vector<bool, 64> out{};
    for (std::int32_t n = p.stages[s].root + 1; n < tree_facts::subtree_end(p, s); ++n) {
        const loops::NodeKind k = loops::node_at(p, n).kind;
        if (k == loops::NodeKind::Loop || k == loops::NodeKind::Unroll || k == loops::NodeKind::Vector || k == loops::NodeKind::Turns) {
            out.push_back(loops::ancestor(p, n, [](const loops::Node& a) { return a.kind == loops::NodeKind::Registers; }) >= 0);
        }
    }
    return out;
}

// Stage `s` walks `axis` in lanes of `lanes`, `bodies` blocks side by side: a strip of `lanes * bodies` outermost over
// it, every other walk over it a Vector of `lanes` — each under an Unroll of `lanes` where there are several bodies.
constexpr bool in_lanes(const loops::Program& p, std::size_t s, std::int32_t axis, std::int32_t lanes, std::int32_t bodies) {
    const auto w = tree_facts::walks_over(p, s, axis);
    std::int32_t vectors = 0;
    std::int32_t unrolls = 0;
    for (std::size_t k = 1; k < w.size(); ++k) {
        vectors += w[k].kind == loops::NodeKind::Vector && w[k].step == lanes ? 1 : 0;
        unrolls += w[k].kind == loops::NodeKind::Unroll && w[k].step == lanes ? 1 : 0;
    }
    return w.size() >= 2 && loops::is_strip(w[0]) && w[0].step == lanes * bodies && vectors > 0 &&
           static_cast<std::size_t>(vectors + unrolls) == w.size() - 1 && unrolls == (bodies > 1 ? vectors : 0);
}

// The Registers node of statement `k` of stage `s` opens its fold at the first element, not the identity.
constexpr bool opens_first(const loops::Program& p, std::size_t s, std::int32_t k) {
    for (std::int32_t n = p.stages[s].root + 1; n < tree_facts::subtree_end(p, s); ++n) {
        const loops::Node& node = loops::node_at(p, n);
        if (node.kind == loops::NodeKind::Registers && node.stmt == k) {
            return node.opens_first;
        }
    }
    return false;
}

// The square is a stage of its own, stored; the window is nowhere; the fold's stage reads the square at `i + k` and
// runs in lanes of eight across `i`, four bodies side by side.
static_assert(kConv.stages.size() == 2 && kConv.buffers.size() == 4, "x, h, the square, the result: no buffer holds a window");
// (time, then the strip of lanes across the outputs, outside the fold; the taps its one loop of its own)
static_assert(tree_facts::walk(kConv, 1, 0).axis == 2 && tree_facts::serial(tree_facts::walk(kConv, 1, 0)) && !under_registers(kConv, 1)[0] &&
                  tree_facts::walk(kConv, 1, 1).axis == 0 && !under_registers(kConv, 1)[1] && in_lanes(kConv, 1, 0, 8, 4) &&
                  tree_facts::walks_over(kConv, 1, 1).size() == 1 && tree_facts::serial(tree_facts::walks_over(kConv, 1, 1)[0]) && under_registers(kConv, 1)[4] &&
                  tree_facts::walk(kConv, 1, 4).axis == 1 && tree_facts::walks_over(kConv, 1, 2).size() == 1,
              "lanes across the outputs, the taps one at a time inside them");
static_assert(kHopped.stages.size() == 1 && tree_facts::serial(tree_facts::walk(kHopped, 0, 1)) && tree_facts::serial(tree_facts::walk(kHopped, 0, 2)) &&
                  tree_facts::count(kHopped, 0, loops::NodeKind::Vector) == 0 && opens_first(kHopped, 0, 3),
              "a hop of two is no contiguous walk across outputs, and five taps fill no lane: every loop serial, in walk order");
// (time and rows, then the strip of lanes across the columns, outside the fold; the taps its one loop of its own)
static_assert(tree_facts::serial(tree_facts::walk(kBlur, 1, 0)) && tree_facts::serial(tree_facts::walk(kBlur, 1, 1)) && tree_facts::walk(kBlur, 1, 1).axis == 0 &&
                  !under_registers(kBlur, 1)[0] && !under_registers(kBlur, 1)[1] && !under_registers(kBlur, 1)[2] &&
                  loops::is_strip(tree_facts::walk(kBlur, 1, 2)) && tree_facts::walk(kBlur, 1, 2).axis == 2 &&
                  tree_facts::walk(kBlur, 1, 3).axis == 2 && tree_facts::walks_over(kBlur, 1, 1).size() == 1 && tree_facts::serial(tree_facts::walks_over(kBlur, 1, 1)[0]) &&
                  tree_facts::walk(kBlur, 1, 5).axis == 1 && under_registers(kBlur, 1)[5] && in_lanes(kBlur, 1, 2, 8, 4),
              "rows, then columns in lanes, then the taps");

// As zen2 plans them: four, or two, extra squares per output cost less than a second pass over a buffer,
// so the square is computed INSIDE its window's reader (`Schedule::Stage::through`), seen through the
// window — once per tap, never stored.
constexpr planned::Held kConvInlinedHeld{planned::program_of(conv(1, true, numerics::Precision::Normal), zen2)};
constexpr const loops::Program& kConvInlined = kConvInlinedHeld.program;
constexpr planned::Held kBlurInlinedHeld{planned::program_of(blur(), zen2)};
constexpr const loops::Program& kBlurInlined = kBlurInlinedHeld.program;
static_assert(kConvInlined.stages.size() == 1 && kConvInlined.buffers.size() == 3 && kBlurInlined.buffers.size() == 3,
              "one stage, and no buffer but the two inputs and the result");

// The fusion benchmark's `heinzel-inlined` arms (`benchmarks/bench_fusion.cpp`, `inlined`) join the stored schedule's two
// stages by hand — the square inlined into the window's reader, which keeps its loops: at every size they run, one
// stage the runner accepts.
constexpr loops::Schedule joined(const Graph& g) {
    const loops::Schedule stored = planned::schedule_of(g, kStoring);
    loops::Schedule::Stage reader = stored.stages[1];
    auto& members = reader.nodes; // (a schedule stage's nodes, not a graph's)
    members = stored.stages[0].nodes;
    for (const std::int32_t ni : stored.stages[1].nodes) {
        members.push_back(ni);
    }
    reader.inlined = static_cast<std::int32_t>(stored.stages[0].nodes.size());
    for (std::size_t ni = 0; ni < g.nodes.size(); ++ni) {
        reader.through = loops::graph::folds_into_its_readers(g.nodes[ni]) ? static_cast<std::int32_t>(ni) : reader.through;
    }
    loops::Schedule d{};
    d.stages.push_back(reader);
    return d;
}
constexpr bool joined_lowers(const Graph& g) { return planned::program_of(g, joined(g)).stages.size() == 1; }
constexpr Graph bench_conv(std::int32_t n, std::int32_t taps) {
    Input x{"x", {n}};
    Input h{"h", {1, taps}};
    return analysed(finish(Output{"y"} = ((x * x).unfold(0, taps) * h).sum(1)));
}
constexpr Graph bench_blur(std::int32_t rows, std::int32_t columns) {
    Input x{"x", {rows, columns}};
    Input w{"w", {1, 3, 1}};
    return analysed(finish(Output{"y"} = ((x * x).unfold(0, 3) * w).sum(1)));
}
static_assert(joined_lowers(bench_conv(65536, 16)) && joined_lowers(bench_conv(262144, 16)) && joined_lowers(bench_conv(1048576, 16)) &&
              joined_lowers(bench_conv(1048576, 32)) && joined_lowers(bench_conv(1048576, 4)) && joined_lowers(bench_conv(2097152, 16)) &&
              joined_lowers(bench_blur(16384, 256)) && joined_lowers(bench_blur(8192, 512)) && joined_lowers(bench_blur(4096, 1024)) &&
              joined_lowers(bench_blur(4096, 256)));

// ...but only a CHEAP producer, read by nothing else, and under few taps — or under some more, when
// its buffer would not fit the caches anyway.
constexpr Machine kNoRoom = [] {
    Machine m = zen2;
    m.l3_bytes = 64;
    return m;
}();
constexpr Graph windowed_sum(std::int32_t taps, bool rooted, bool read_elsewhere) {
    Input x{"x", {64}};
    Expr p = rooted ? sqrt(x * x) : x * x;
    Expr y = p.unfold(0, taps).sum(1);
    return analysed(read_elsewhere ? finish(Output{"y"} = y, Output{"p"} = p + 1.0) : finish(Output{"y"} = y));
}
static_assert(planned::schedule_of(windowed_sum(kTaps, true, false), zen2).stages.size() == 2, "a square root is no cheap operation to repeat per tap");
static_assert(planned::schedule_of(windowed_sum(kTaps, false, true), zen2).stages[0].through < 0, "what something else reads too has to be stored anyway");
static_assert(planned::schedule_of(windowed_sum(11, false, false), zen2).stages.size() == 2 && planned::schedule_of(windowed_sum(11, false, false), kNoRoom).stages.size() == 1,
              "ten extra squares per output: stored while the buffer fits the caches, inside once it would not");
static_assert(planned::schedule_of(windowed_sum(40, false, false), kNoRoom).stages.size() == 2, "thirty-nine extra squares per output: stored whatever the caches");

// A sliding median: the order statistic gathers its window — one turn of the inner loop per tap, written
// out — and a fixed network of compare-exchanges selects the middle rank, in lanes across the outputs.
// Pure selection: exact at any precision. A hop of two leaves no lane across outputs, so it runs one
// output at a time; and twice the median runs in the median's own stage, after its loop.
constexpr Graph sliding_median(std::int32_t taps, std::int32_t hop, bool doubled) {
    Input x{"x", {kN}};
    Expr m = x.unfold(0, taps, hop).median(1);
    return analysed(finish(Output{"y"} = doubled ? m * 2.0 : m));
}
template <std::int32_t Taps, std::int32_t Hop, bool Doubled>
constexpr planned::Held kMedianHeld{planned::program_of(sliding_median(Taps, Hop, Doubled), zen2)};
template <std::int32_t Taps, std::int32_t Hop, bool Doubled>
constexpr const loops::Program& kMedian = kMedianHeld<Taps, Hop, Doubled>.program;
// (the taps: one Turns node, a turn per tap, under the gathering Registers node)
static_assert(kMedian<9, 1, true>.stages.size() == 1 && kMedian<9, 1, true>.buffers.size() == 2 && tree_facts::walk(kMedian<9, 1, true>, 0, 1).axis == 0 &&
                  in_lanes(kMedian<9, 1, true>, 0, 0, 8, 1) && tree_facts::walks_over(kMedian<9, 1, true>, 0, 1).size() == 1 &&
                  tree_facts::walks_over(kMedian<9, 1, true>, 0, 1)[0].kind == loops::NodeKind::Turns &&
                  under_registers(kMedian<9, 1, true>, 0)[2] && tree_facts::walk(kMedian<9, 1, true>, 0, 2).axis == 1,
              "one stage, no window buffer, lanes across the outputs one body at a time, the taps gathered inside");
static_assert(tree_facts::serial(tree_facts::walk(kMedian<5, 2, false>, 0, 1)) && tree_facts::walk(kMedian<5, 2, false>, 0, 1).axis == 0, "a hop of two: no lanes");

int g_wrong = 0;

std::vector<float> noise(std::size_t n, std::uint32_t seed) {
    std::vector<float> v(n);
    std::uint32_t state = seed;
    for (float& x : v) {
        state = state * 1664525u + 1013904223u;
        x = static_cast<float>(static_cast<std::int32_t>(state >> 8) % 2001 - 1000) * 0.00173f;
    }
    return v;
}

void expect(const char* what, std::size_t at, float got, float want) {
    if (got != want && ++g_wrong <= 10) {
        std::printf("  %s [%zu]: got %a, want %a\n", what, at, static_cast<double>(got), static_cast<double>(want));
    }
}

template <const loops::Program& P, class Sum>
void check_conv(const char* what, std::size_t hop, bool squared, bool from_first) {
    const std::vector<float> x = noise(kN, 1);
    const std::vector<float> h = noise(kTaps, 2);
    canaried::Workspace<P> work(loops::Sizes{});
    canaried::run<P>(loops::Feeds{x.data(), h.data()}, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(P.outputs[0]));
    const std::size_t frames = (kN - kTaps) / hop + 1;
    for (std::size_t i = 0; i < frames; ++i) {
        Sum sum = 0;
        for (std::size_t k = 0; k < kTaps; ++k) {
            const float p = squared ? x[i * hop + k] * x[i * hop + k] : x[i * hop + k];
            const float term = p * h[k];
            sum = k == 0 && from_first ? static_cast<Sum>(term) : sum + static_cast<Sum>(term);
        }
        expect(what, i, y[i], static_cast<float>(sum));
    }
}

// Five taps fill no lane, so that fold stays in the walk's order and opens at its first tap: a sum of
// negative zeros is a negative zero (from the identity, +0.0, it would come out positive).
void check_a_sum_of_negative_zeros() {
    const std::vector<float> x(kN, -0.0f);
    const std::vector<float> h(kTaps, 1.0f);
    canaried::Workspace<kHopped> work(loops::Sizes{});
    canaried::run<kHopped>(loops::Feeds{x.data(), h.data()}, work);
    const float* y = work.data<float>(static_cast<std::size_t>(kHopped.outputs[0]));
    for (std::size_t i = 0; i * 2 + kTaps <= kN; ++i) {
        if (!__builtin_signbit(y[i]) && ++g_wrong <= 10) {
            std::printf("  a sum of negative zeros [%zu]: got %a, want -0x0p+0\n", i, static_cast<double>(y[i]));
        }
    }
}

template <const loops::Program& P>
void check_blur(const char* what) {
    const std::vector<float> x = noise(kH * kW, 3);
    const std::vector<float> w = noise(kRows, 4);
    canaried::Workspace<P> work(loops::Sizes{});
    canaried::run<P>(loops::Feeds{x.data(), w.data()}, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(P.outputs[0]));
    for (std::size_t i = 0; i + kRows <= kH; ++i) {
        for (std::size_t j = 0; j < kW; ++j) {
            float sum = 0.0f;
            for (std::size_t k = 0; k < kRows; ++k) {
                const float p = x[(i + k) * kW + j] * x[(i + k) * kW + j];
                sum = k == 0 ? p * w[k] : sum + p * w[k];
            }
            expect(what, i * kW + j, y[i * kW + j], sum);
        }
    }
}

template <std::int32_t Taps, std::int32_t Hop, bool Doubled>
void check_median(const char* what) {
    std::vector<float> x = noise(kN, 5);
    x[7] = x[8] = x[20]; // ties
    canaried::Workspace<kMedian<Taps, Hop, Doubled>> work(loops::Sizes{});
    canaried::run<kMedian<Taps, Hop, Doubled>>(loops::Feeds{x.data()}, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(kMedian<Taps, Hop, Doubled>.outputs[0]));
    for (std::size_t i = 0; i * Hop + Taps <= kN; ++i) {
        std::vector<float> window(x.begin() + static_cast<std::ptrdiff_t>(i * Hop), x.begin() + static_cast<std::ptrdiff_t>(i * Hop + Taps));
        std::sort(window.begin(), window.end());
        expect(what, i, y[i], Doubled ? window[Taps / 2] * 2.0f : window[Taps / 2]);
    }
}

bool refuses(const Graph& g) { return planned::refusal_of(g, zen2).view().starts_with("heinzel::compile: "); }

// A window that is itself a result would have to be stored; a view gets no storage here yet.
Graph window_as_a_result() {
    Input x{"x", {kN}};
    return analysed(finish(Output{"y"} = x.unfold(0, kTaps)));
}

Graph sorted_windows() {
    Input x{"x", {kN}};
    return analysed(finish(Output{"y"} = x.unfold(0, kTaps).sort(1)));
}

} // namespace

int main() {
    check_conv<kConv, float>("conv", 1, true, true);
    check_conv<kConvHigh, double>("conv, High", 1, true, true);
    check_conv<kHopped, float>("conv, hop 2", 2, false, true);
    check_a_sum_of_negative_zeros();
    check_blur<kBlur>("blur");
    check_conv<kConvInlined, float>("conv, the square inside its reader", 1, true, true);
    check_blur<kBlurInlined>("blur, the square inside its reader");
    check_median<3, 1, false>("median of 3");
    check_median<5, 1, false>("median of 5");
    check_median<9, 1, true>("twice the median of 9");
    check_median<5, 2, false>("median of 5, hop 2");
    const Graph result = window_as_a_result();
    const Graph sorted = sorted_windows();
    if (!refuses(result) || loops::lowers(result, Machine{})) {
        ++g_wrong;
        std::printf("  a window that is a graph result must be refused by name\n");
    }
    if (!refuses(sorted) || loops::lowers(sorted, Machine{})) {
        ++g_wrong;
        std::printf("  a sort — more than one rank kept — is not lowered yet and must be refused by name\n");
    }
    if (g_wrong != 0) {
        std::printf("%d results are wrong\n", g_wrong);
        return 1;
    }
    std::printf("nest_window: ok\n");
    return 0;
}
