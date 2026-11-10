// Which graph nodes share a stage (`loops/planner/stages.hpp`), against loops written by hand. The planner
// visits, among the nodes whose operands are all there, one RELATED to the open stage first; several folds
// of one domain over the same axes share a stage (siblings), and a map over a fold's domain that reads
// nothing after the fold runs inside its loops. Each case asserts the planner's stages and runs the program:
// exactly where no sum is reordered (lanes across outputs, or none), within the bound two orders of a sum
// can differ by where lanes run along a fold's own axis.
//
//   dx/dy      two 3x3 window folds of one image, two weight sets        one stage
//   rows       sum and max of rows, lanes along them or across them      one stage, under Normal and High
//   dxdy       the two siblings and a map of both after them             one stage
//   harris     dx, dy, three products, three box sums, the response      two stages
//   rownorm    x - mean(x): a map over the domain that reads the fold    two stages
//   ready      a = x*2 ; b = y*3 (another shape) ; c = a + 1             {a, c}, {b}
//   unrelated  maps not related to the open stage are not pulled in      as before: producers inlined
//   after      s = sum(x) ; t = y*2 (s's shape, an input) ; s + t        one stage
//   keepdims   a sum beside a mean that keeps its axes                   two stages
//   cycle      a map, a one-pole, a map                                  the map before joins the cycle's stage (the cost
//                                                                        model: it costs a one-pole's stage nothing),
//                                                                        the map after it apart
//   refusals   in `nest_grouping_refusals_test.cpp`
//   chain      150 maps in a row: what planning them costs (compile time, recorded not asserted)

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/graph/regions.hpp"
#include "heinzel/loops/graph/views.hpp"
#include "heinzel/loops/planner/stages.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/notation/einsum.hpp"
#include "heinzel/sema/analyze.hpp"

#include "canaried.hpp"
#include "planned.hpp"

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string_view>
#include <vector>

using namespace heinzel;

namespace {

namespace loops = heinzel::loops;

constexpr std::int32_t kH = 12;
constexpr std::int32_t kW = 37;
constexpr std::int32_t kR = 9;
constexpr std::int32_t kC = 70;
constexpr std::int32_t kN = 37;

constexpr Graph analysed(Graph g) {
    if (!infer(g).ok) {
        fail("nest_grouping_test: a graph did not resolve");
    }
    return g;
}

// ---- the graphs ----------------------------------------------------------------------------------------
// `harris` at 0 returns dx and dy, at 1 dx*dy, at 2 the whole corner response.
constexpr Graph harris(int upto) {
    Input x{"x", {kH, kW}};
    Input wx{"wx", {1, 3, 1, 3}};
    Input wy{"wy", {1, 3, 1, 3}};
    Expr taps = x.unfold2d(0, 3, 1, 3);
    Expr dx = (taps * wx).sum({1, 3});
    Expr dy = (taps * wy).sum({1, 3});
    if (upto == 0) {
        return analysed(finish(Output{"dx"} = dx, Output{"dy"} = dy));
    }
    if (upto == 1) {
        return analysed(finish(Output{"y"} = dx * dy));
    }
    const auto box = [](Expr v) { return v.unfold2d(0, 3, 1, 3).sum({1, 3}); };
    Expr sxx = box(dx * dx);
    Expr sxy = box(dx * dy);
    Expr syy = box(dy * dy);
    Expr tr = sxx + syy;
    return analysed(finish(Output{"y"} = sxx * syy - sxy * sxy - static_cast<double>(0.04f) * tr * tr));
}

constexpr Graph rows(std::int32_t axis, numerics::Precision level) {
    Input x{"x", {kR, kC}};
    Output sum{"sum"};
    Output largest{"max"};
    sum = x.sum(axis);
    largest = x.max(axis);
    Graph g = finish(sum, largest);
    set_default_precision(g, level);
    return analysed(g);
}

constexpr Graph rownorm() {
    Input x{"x", {kR, kC}};
    return analysed(finish(Output{"y"} = x - x.mean(1, keepdims)));
}

constexpr Graph ready() {
    Input x{"x", {kN}};
    Input y{"y", {5}};
    Expr a = x * 2.0;
    Expr b = y * 3.0;
    Expr c = a + 1.0;
    return analysed(finish(Output{"b"} = b, Output{"c"} = c));
}

// q = z*3 ; r (another shape) ; p = x*x ; a fold over a window of p. `r` stands between q and p, so p is
// its own maps-only stage and the inline pass puts it inside the fold's stage; p must not be pulled into
// q's stage, to which it is not related.
constexpr Graph unrelated_producer() {
    Input z{"z", {kN}};
    Input w{"w", {5}};
    Input x{"x", {kN}};
    Expr q = z * 3.0;
    Expr r = w * 2.0;
    Expr p = x * x;
    Expr y = p.unfold(0, 3).sum(1);
    return analysed(finish(Output{"q"} = q, Output{"r"} = r, Output{"y"} = y));
}

// A fold over D, then q = z*3 over D with z an input the fold stage does not read: q is no producer of it.
constexpr Graph unrelated_map() {
    Input x{"x", {kR, kC}};
    Input z{"z", {kR, kC}};
    Expr s = x.sum(0);
    Expr q = z * 3.0;
    return analysed(finish(Output{"s"} = s, Output{"q"} = q));
}

// Two box sums over two cheap producers, both boxes weighted by one `w` — a plain input, or a view of one
// that the stages only broadcast. The second box is ready while the first one's stage is open; they are
// not related, so each producer is still inlined into its own box.
constexpr Graph two_boxes(bool viewed_weight) {
    Input a{"a", {kN}};
    Input b{"b", {kN}};
    Input v{"v", {5}};
    Expr w0 = viewed_weight ? Input{"w", {}} : Input{"w", {1, 1}};
    Expr w = viewed_weight ? w0.reshape({1, 1}) : w0;
    Expr aa = a * a;
    Expr r = v * 2.0;
    Expr bb = b * b;
    Expr ya = (aa.unfold(0, 3) * w).sum(1);
    Expr yb = (bb.unfold(0, 3) * w).sum(1);
    return analysed(finish(Output{"r"} = r, Output{"ya"} = ya, Output{"yb"} = yb));
}

constexpr Graph after_by_shape() {
    Input x{"x", {kR, kC}};
    Input y{"y", {kR}};
    Expr s = x.sum(1);
    Expr t = y * 2.0;
    return analysed(finish(Output{"out"} = s + t));
}

constexpr Graph sum_beside_mean() {
    Input x{"x", {kR, kC}};
    Output sum{"sum"};
    Output mean{"mean"};
    sum = x.sum(1);
    mean = x.mean(1, keepdims);
    return analysed(finish(sum, mean));
}

constexpr float kPole = 0.75f;
constexpr Graph cycle() {
    constexpr Index n{};
    Input x{"x", Signal{kN}};
    Expr m = x * 2.0;
    Variable y;
    y[n] = m + static_cast<double>(kPole) * y[n - 1];
    return analysed(finish(Output{"out"} = y * 0.5));
}

constexpr Graph chain(int maps) {
    Expr x = Input{"x", {kN}};
    for (int k = 0; k < maps; ++k) {
        x = x * x; // (no constant: a graph holds 320 values)
    }
    return analysed(finish(Output{"y"} = x));
}

// ---- what was decided ------------------------------------------------------------------------------------
// The planner's schedule — its stages, their members and loops — and the program it lowers to, each graph planned once.
template <const Graph& G>
constexpr loops::Schedule kPlan = planned::schedule_of(G, zen2);
template <const Graph& G>
constexpr planned::Held kProgramHeld{planned::program_of(G, kPlan<G>)};
template <const Graph& G>
constexpr const loops::Program& kProgram = kProgramHeld<G>.program;

constexpr Graph kDxDy = harris(0);
constexpr Graph kDxTimesDy = harris(1);
constexpr Graph kHarris = harris(2);
constexpr Graph kRowsLanes = rows(1, numerics::Precision::Normal);
constexpr Graph kRowsHigh = rows(1, numerics::Precision::High);
constexpr Graph kRowsAcross = rows(0, numerics::Precision::Normal);
constexpr Graph kRownorm = rownorm();
constexpr Graph kReady = ready();
constexpr Graph kUnrelated = unrelated_producer();
constexpr Graph kUnrelatedMap = unrelated_map();
constexpr Graph kTwoBoxes = two_boxes(false);
constexpr Graph kTwoBoxesViewed = two_boxes(true);
constexpr Graph kAfter = after_by_shape();
constexpr Graph kKeepdims = sum_beside_mean();
constexpr Graph kCycle = cycle();
constexpr Graph kChain = chain(150);

static_assert(kPlan<kDxDy>.stages.size() == 1, "dx and dy fold one window of the image over the same axes: one stage");
static_assert(kPlan<kDxTimesDy>.stages.size() == 1, "two siblings and a map of both after them: one stage");
static_assert(kPlan<kHarris>.stages.size() == 2 && kPlan<kHarris>.stages[0].nodes.size() == 7 &&
                  kPlan<kHarris>.stages[1].nodes.size() == 10,
              "harris: {dx, dy, the three products} and {the three box sums, the response}");
static_assert(kPlan<kHarris>.stages[0].through < 0 && kPlan<kHarris>.stages[1].through < 0,
              "the products are after-maps of a fold stage, so no producer is computed inside a window");
static_assert(kPlan<kRowsLanes>.stages.size() == 1 && kPlan<kRowsLanes>.stages[0].loops[2].kind == loops::LoopKind::Vector &&
                  kPlan<kRowsLanes>.stages[0].loops[2].axis == 1,
              "a sum and a max of the same rows: one stage, lanes along the rows (both folds may reassociate)");
static_assert(kPlan<kRowsHigh>.stages.size() == 1 && kPlan<kRowsHigh>.stages[0].loops[2].kind == loops::LoopKind::Serial,
              "...under High neither may: one stage, the rows walked in order");
static_assert(kPlan<kRowsAcross>.stages.size() == 1 && kPlan<kRowsAcross>.stages[0].loops[1].kind == loops::LoopKind::Vector,
              "folded across the rows: one stage, lanes across the outputs");
static_assert(kPlan<kRownorm>.stages.size() == 2, "x - mean(x) reads the fold's result: no producer of it");
static_assert(kPlan<kReady>.stages.size() == 2 && kPlan<kReady>.stages[0].nodes.size() == 2 && kPlan<kReady>.stages[1].nodes.size() == 1,
              "c reads a, and is ready while a's stage is open: {a, c}, then {b}");
static_assert(kPlan<kUnrelated>.stages.size() == 3 && kPlan<kUnrelated>.stages[0].nodes.size() == 1 &&
                  kPlan<kUnrelated>.stages[2].through >= 0,
              "q alone; the square computed inside the window's reader, as before");
// (Node order is the builder's, not the source's: the fold of 7b comes second, so what is asked is the
// predicate itself — with the fold's stage open, the unrelated map does not join it. The gathering's state is set up
// as the pass sets it up: the graph's facts, then who reads whom.)
constexpr bool joins_after_the_fold(const Graph& g) {
    const loops::graph::Facts facts = loops::graph::facts_of(g);
    const loops::graph::Feedback feedback = loops::graph::find_feedback(g, facts);
    loops::planner::detail::Gathering at{};
    loops::planner::detail::begin(g, facts, feedback, at);
    std::size_t fold = 0;
    std::size_t map = 0;
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        (loops::graph::is_a_fold(g.nodes[i]) ? fold : map) = i;
    }
    loops::planner::detail::push(g, facts, feedback, fold, at);
    return loops::planner::detail::joins(g, facts, feedback, loops::planner::Gathered{}, map, at);
}
static_assert(!joins_after_the_fold(kUnrelatedMap), "a map over a fold's domain that reads nothing the fold stage reads does not join it");
constexpr int producers_inside(const loops::Schedule& d) {
    int inside = 0;
    for (const loops::Schedule::Stage& stage : d.stages) {
        inside += stage.through >= 0 && stage.nodes.size() == 3 ? 1 : 0;
    }
    return inside;
}
static_assert(producers_inside(kPlan<kTwoBoxes>) == 2 && producers_inside(kPlan<kTwoBoxesViewed>) == 2,
              "two unrelated box sums are no siblings, even sharing a weight they broadcast: each computes its own producer inside its window");
static_assert(kPlan<kKeepdims>.stages.size() == 2, "a sum and a mean that keeps its axes leave different results: no siblings");
static_assert(kPlan<kCycle>.stages.size() == 1,
              "the map before the cycle in its stage, and the map after it too: one multiply beside the chain costs less than a pass "
              "(Plan/23 04 4.3a, the trailing decision)");
static_assert(kPlan<kChain>.stages.size() == 2, "150 maps in a row: as many as a stage holds, and the rest");


// A map over the fold's result that reads no fold result is after the fold by its shape. The builder puts
// it first, so it is its own stage as planned; written after the fold into the fold's stage, it lowers.
constexpr loops::Schedule after_the_fold_by_hand() {
    const loops::Schedule& d = kPlan<kAfter>;
    loops::Schedule::Stage merged = d.stages[1];
    auto& members = merged.nodes;
    members = {};
    members.push_back(d.stages[1].nodes[0]);
    members.push_back(d.stages[0].nodes[0]);
    for (std::size_t k = 1; k < d.stages[1].nodes.size(); ++k) {
        members.push_back(d.stages[1].nodes[k]);
    }
    loops::Schedule by_hand{};
    by_hand.stages.push_back(merged);
    return by_hand;
}
constexpr loops::Schedule kAfterSchedule = after_the_fold_by_hand();
constexpr loops::Schedule::Stage kAfterMerged = kAfterSchedule.stages[0];
static_assert(kPlan<kAfter>.stages.size() == 2 &&
              loops::graph::side_of(kAfter, kAfterMerged, loops::graph::shape_of_group(kAfter, kAfterMerged), 1) == loops::graph::Side::After);
constexpr planned::Held kAfterByHandHeld{planned::program_of(kAfter, kAfterSchedule)};
constexpr const loops::Program& kAfterByHand = kAfterByHandHeld.program;

// A stage's shape rebuilt from its members alone (`graph::shape_of_group`, and the cycle of its first node in a cycle:
// a feed-forward part may stand before it) — what the passes after the gathering and the lowering plan from — is what
// pass one gathered; and the planner's schedule lowers (`kProgram` fails the build where it does not).
constexpr bool membership_agrees(const Graph& g) {
    const loops::graph::Facts facts = loops::graph::facts_of(g);
    const loops::graph::Feedback feedback = loops::graph::find_feedback(g, facts);
    loops::planner::Gathered gathered{};
    if (!loops::planner::gather_groups(g, facts, feedback, zen2, gathered)) {
        return false;
    }
    for (const loops::planner::OpenGroup& open : gathered.groups) {
        const loops::graph::GroupShape rebuilt = loops::graph::shape_of_group(g, open.stage);
        std::int32_t cycle = -1;
        for (std::size_t k = 0; k < open.stage.nodes.size() && cycle < 0; ++k) {
            cycle = feedback.cycle[static_cast<std::size_t>(open.stage.nodes[k])];
        }
        if (rebuilt != open.shape || cycle != open.cycle) {
            return false;
        }
    }
    return true;
}
template <const Graph& G>
constexpr bool kAgrees = membership_agrees(G) && !kProgram<G>.stages.empty();
static_assert(kAgrees<kDxDy> && kAgrees<kDxTimesDy> && kAgrees<kHarris> && kAgrees<kRowsLanes> && kAgrees<kRowsHigh> && kAgrees<kRowsAcross> &&
              kAgrees<kRownorm> && kAgrees<kReady> && kAgrees<kUnrelated> && kAgrees<kUnrelatedMap> && kAgrees<kTwoBoxes> &&
              kAgrees<kTwoBoxesViewed> && kAgrees<kAfter> && kAgrees<kKeepdims> && kAgrees<kCycle>);

// ---- running them -----------------------------------------------------------------------------------------
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

// Exact, or (`bound` > 0) within `bound` of the wanted value.
void expect(const char* what, std::size_t at, float got, float want, double bound = 0.0) {
    const bool wrong = bound > 0.0 ? !(std::fabs(static_cast<double>(got) - static_cast<double>(want)) <= bound) : got != want;
    if (wrong && ++g_wrong <= 10) {
        std::printf("  %s [%zu]: got %a, want %a\n", what, at, static_cast<double>(got), static_cast<double>(want));
    }
}

// Feeds go in the graph's input order, which is the builder's: bind them by name.
struct Feed {
    std::string_view name;
    const float* data;
};
loops::Feeds feeds_of(const Graph& g, std::initializer_list<Feed> by_name) {
    loops::Feeds feeds{};
    for (std::size_t i = 0; i < g.inputs.size(); ++i) {
        for (const Feed& f : by_name) {
            feeds[i] = g.inputs[i].name.view() == f.name ? f.data : feeds[i];
        }
    }
    return feeds;
}

template <const Graph& G>
std::vector<const float*> run(canaried::Workspace<kProgram<G>>& work, const loops::Feeds& feeds) {
    canaried::run<kProgram<G>>(feeds, work);
    std::vector<const float*> out;
    for (const std::int32_t b : kProgram<G>.outputs) {
        out.push_back(work.template data<float>(static_cast<std::size_t>(b)));
    }
    return out;
}

// The hand loops of harris, in the graph's order of operations: every sum from its first term.
struct Stencils {
    std::vector<float> dx;
    std::vector<float> dy;
};
Stencils stencils(const std::vector<float>& x, const std::vector<float>& wx, const std::vector<float>& wy) {
    Stencils s{std::vector<float>(std::size_t(kH - 2) * (kW - 2)), std::vector<float>(std::size_t(kH - 2) * (kW - 2))};
    for (int i = 0; i < kH - 2; ++i) {
        for (int j = 0; j < kW - 2; ++j) {
            float ax = 0.0f;
            float ay = 0.0f;
            for (int a = 0; a < 3; ++a) {
                for (int b = 0; b < 3; ++b) {
                    const float v = x[std::size_t(i + a) * kW + j + b];
                    ax = a + b == 0 ? v * wx[std::size_t(a) * 3 + b] : ax + v * wx[std::size_t(a) * 3 + b];
                    ay = a + b == 0 ? v * wy[std::size_t(a) * 3 + b] : ay + v * wy[std::size_t(a) * 3 + b];
                }
            }
            s.dx[std::size_t(i) * (kW - 2) + j] = ax;
            s.dy[std::size_t(i) * (kW - 2) + j] = ay;
        }
    }
    return s;
}

void check_harris() {
    const std::vector<float> x = noise(std::size_t(kH) * kW, 1);
    const std::vector<float> wx = noise(9, 2);
    const std::vector<float> wy = noise(9, 3);
    const auto feeds = [&](const Graph& g) { return feeds_of(g, {{"x", x.data()}, {"wx", wx.data()}, {"wy", wy.data()}}); };
    const Stencils s = stencils(x, wx, wy);
    constexpr int Wd = kW - 2;
    constexpr int Wo = kW - 4;
    {
        canaried::Workspace<kProgram<kDxDy>> work(loops::Sizes{});
        const auto out = run<kDxDy>(work, feeds(kDxDy));
        for (std::size_t k = 0; k < s.dx.size(); ++k) {
            expect("dx", k, out[0][k], s.dx[k]);
            expect("dy", k, out[1][k], s.dy[k]);
        }
    }
    {
        canaried::Workspace<kProgram<kDxTimesDy>> work(loops::Sizes{});
        const auto out = run<kDxTimesDy>(work, feeds(kDxTimesDy));
        for (std::size_t k = 0; k < s.dx.size(); ++k) {
            expect("dx*dy", k, out[0][k], s.dx[k] * s.dy[k]);
        }
    }
    canaried::Workspace<kProgram<kHarris>> work(loops::Sizes{});
    const auto out = run<kHarris>(work, feeds(kHarris));
    for (int i = 0; i < kH - 4; ++i) {
        for (int j = 0; j < Wo; ++j) {
            float sxx = 0.0f;
            float sxy = 0.0f;
            float syy = 0.0f;
            for (int a = 0; a < 3; ++a) {
                for (int b = 0; b < 3; ++b) {
                    const std::size_t at = std::size_t(i + a) * Wd + j + b;
                    const float u = s.dx[at];
                    const float v = s.dy[at];
                    sxx = a + b == 0 ? u * u : sxx + u * u;
                    sxy = a + b == 0 ? u * v : sxy + u * v;
                    syy = a + b == 0 ? v * v : syy + v * v;
                }
            }
            const float tr = sxx + syy;
            expect("harris", std::size_t(i) * Wo + j, out[0][std::size_t(i) * Wo + j], sxx * syy - sxy * sxy - 0.04f * tr * tr);
        }
    }
}

// A row's sum by hand, in double, and how far a float sum of it in any order may be from that.
template <const Graph& G>
void check_rows(const char* what, bool along_rows) {
    const std::vector<float> x = noise(std::size_t(kR) * kC, 4);
    canaried::Workspace<kProgram<G>> work(loops::Sizes{});
    const auto out = run<G>(work, loops::Feeds{x.data()});
    const int outer = along_rows ? kR : kC;
    const int inner = along_rows ? kC : kR;
    for (int o = 0; o < outer; ++o) {
        float sum = 0.0f;
        float max = 0.0f;
        double magnitude = 0.0;
        for (int k = 0; k < inner; ++k) {
            const float v = along_rows ? x[std::size_t(o) * kC + k] : x[std::size_t(k) * kC + o];
            sum = k == 0 ? v : sum + v;
            max = k == 0 ? v : (v > max ? v : max);
            magnitude += std::fabs(static_cast<double>(v));
        }
        expect(what, std::size_t(o), out[0][o], sum, along_rows ? inner * 1.2e-7 * magnitude : 0.0);
        expect(what, std::size_t(o), out[1][o], max);
    }
}

void check_rownorm_and_keepdims() {
    const std::vector<float> x = noise(std::size_t(kR) * kC, 5);
    canaried::Workspace<kProgram<kRownorm>> norm(loops::Sizes{});
    canaried::Workspace<kProgram<kKeepdims>> kept(loops::Sizes{});
    const auto y = run<kRownorm>(norm, loops::Feeds{x.data()});
    const auto sums = run<kKeepdims>(kept, loops::Feeds{x.data()});
    for (int i = 0; i < kR; ++i) {
        double sum = 0.0;
        double magnitude = 0.0;
        for (int j = 0; j < kC; ++j) {
            sum += x[std::size_t(i) * kC + j];
            magnitude += std::fabs(static_cast<double>(x[std::size_t(i) * kC + j]));
        }
        const double bound = kC * 1.2e-7 * magnitude;
        expect("keepdims sum", std::size_t(i), sums[0][i], static_cast<float>(sum), bound);
        expect("keepdims mean", std::size_t(i), sums[1][i], static_cast<float>(sum / kC), bound / kC + 1e-6);
        for (int j = 0; j < kC; ++j) {
            expect("rownorm", std::size_t(i) * kC + j, y[0][std::size_t(i) * kC + j],
                   static_cast<float>(x[std::size_t(i) * kC + j] - sum / kC), bound / kC + 1e-6);
        }
    }
}

void check_ready_and_unrelated() {
    const std::vector<float> x = noise(kN, 6);
    const std::vector<float> y = noise(5, 7);
    const std::vector<float> z = noise(kN, 8);
    {
        canaried::Workspace<kProgram<kReady>> work(loops::Sizes{});
        const auto out = run<kReady>(work, feeds_of(kReady, {{"x", x.data()}, {"y", y.data()}}));
        for (std::size_t k = 0; k < 5; ++k) {
            expect("ready b", k, out[0][k], y[k] * 3.0f);
        }
        for (std::size_t k = 0; k < kN; ++k) {
            expect("ready c", k, out[1][k], x[k] * 2.0f + 1.0f);
        }
    }
    canaried::Workspace<kProgram<kUnrelated>> work(loops::Sizes{});
    const auto out = run<kUnrelated>(work, feeds_of(kUnrelated, {{"z", z.data()}, {"w", y.data()}, {"x", x.data()}}));
    for (std::size_t k = 0; k < kN; ++k) {
        expect("unrelated q", k, out[0][k], z[k] * 3.0f);
    }
    for (std::size_t k = 0; k + 2 < kN; ++k) {
        expect("unrelated y", k, out[2][k], (x[k] * x[k] + x[k + 1] * x[k + 1]) + x[k + 2] * x[k + 2]);
    }
}

template <const Graph& G>
void check_two_boxes(const char* what, const std::vector<float>& a, const std::vector<float>& b, const std::vector<float>& v, float w) {
    canaried::Workspace<kProgram<G>> work(loops::Sizes{});
    const auto out = run<G>(work, feeds_of(G, {{"a", a.data()}, {"b", b.data()}, {"v", v.data()}, {"w", &w}}));
    for (std::size_t k = 0; k + 2 < kN; ++k) {
        expect(what, k, out[1][k], (a[k] * a[k] * w + a[k + 1] * a[k + 1] * w) + a[k + 2] * a[k + 2] * w);
        expect(what, k, out[2][k], (b[k] * b[k] * w + b[k + 1] * b[k + 1] * w) + b[k + 2] * b[k + 2] * w);
    }
}

void check_after_and_cycle() {
    const std::vector<float> x = noise(std::size_t(kR) * kC, 9);
    const std::vector<float> y = noise(kR, 10);
    const std::vector<float> s = noise(kN, 11);
    {
        canaried::Workspace<kAfterByHand> work(loops::Sizes{});
        canaried::run<kAfterByHand>(feeds_of(kAfter, {{"x", x.data()}, {"y", y.data()}}), work);
        const float* out[] = {work.data<float>(static_cast<std::size_t>(kAfterByHand.outputs[0]))};
        for (int i = 0; i < kR; ++i) {
            double sum = 0.0;
            double magnitude = 0.0;
            for (int j = 0; j < kC; ++j) {
                sum += x[std::size_t(i) * kC + j];
                magnitude += std::fabs(static_cast<double>(x[std::size_t(i) * kC + j]));
            }
            expect("after by shape", std::size_t(i), out[0][i], static_cast<float>(sum) + y[std::size_t(i)] * 2.0f, kC * 1.2e-7 * magnitude + 1e-6);
        }
    }
    canaried::Workspace<kProgram<kCycle>> work(loops::Sizes{});
    const auto out = run<kCycle>(work, loops::Feeds{s.data()});
    float state = 0.0f;
    for (std::size_t k = 0; k < kN; ++k) {
        state = s[k] * 2.0f + kPole * state;
        expect("cycle", k, out[0][k], state * 0.5f);
    }
}

} // namespace

int main() {
    check_harris();
    check_rows<kRowsLanes>("rows, lanes along them", true);
    check_rows<kRowsHigh>("rows, High", true);
    check_rows<kRowsAcross>("rows, lanes across them", false);
    check_rownorm_and_keepdims();
    check_ready_and_unrelated();
    const std::vector<float> a = noise(kN, 12);
    const std::vector<float> b = noise(kN, 13);
    const std::vector<float> v = noise(5, 14);
    const std::vector<float> w = noise(1, 15);
    check_two_boxes<kTwoBoxes>("two boxes", a, b, v, w[0]);
    check_two_boxes<kTwoBoxesViewed>("two boxes, a viewed weight", a, b, v, w[0]);
    check_after_and_cycle();
    if (g_wrong != 0) {
        std::printf("%d results are wrong\n", g_wrong);
        return 1;
    }
    std::printf("nest_grouping: every grouping as expected, every result as by hand\n");
    return 0;
}
