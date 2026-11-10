// Adversarial review of M1 (`Plan/archive/shared-outer-loop/02`, sibling folds and the related-first visit):
// its findings, fixed, as checks — the reproducers of `Plan/probes/review_m1/`. The review found no wrong
// result and no disagreement of planner and lowering (65 graphs, 33 bit-exact against the reference walk);
// what it found was schedules.
//
//   F1   a map of a producer's INPUT, after the fold that reads the producer through a window, was pulled
//        into the producer's stage of maps (it reads what the stage reads) — and a stage of maps with an
//        output read elsewhere is not computed inside the window: the square STORED, conv 4 M +27 %, a
//        blur +85 %, two boxes +75 %. What a stage reads relates a node to it only when it holds a fold.
//   F4a  dx and dy over two window NODES of one image were no siblings: relatedness looked up the read
//        value itself, not its source through views with no statement.

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"

#include "planned.hpp"

#include <cstdio>

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
        fail("nest_grouping_review_test: a graph did not resolve");
    }
    return g;
}

constexpr Graph related_map_after_the_window() {
    Input x{"x", {kN}};
    Input h{"h", {1, 3}};
    Expr y = ((x * x).unfold(0, 3) * h).sum(1);
    return analysed(finish(Output{"y"} = y, Output{"z"} = x + 1.0));
}
constexpr Graph related_map_after_the_blur() {
    Input x{"x", {kR, kC}};
    Input h{"h", {1, 3, 1}};
    Expr b = ((x * x).unfold(0, 3) * h).sum(1);
    return analysed(finish(Output{"b"} = b, Output{"e"} = x * 0.5));
}
constexpr Graph two_boxes_one_input() {
    Input a{"a", {kN}};
    Input w{"w", {1, 1}};
    Expr ya = ((a * a).unfold(0, 3) * w).sum(1);
    Expr yb = ((a * 2.0).unfold(0, 3) * w).sum(1);
    return analysed(finish(Output{"ya"} = ya, Output{"yb"} = yb));
}
constexpr Graph dx_dy_two_windows() {
    Input x{"x", {kH, kW}};
    Input wx{"wx", {1, 3, 1, 3}};
    Input wy{"wy", {1, 3, 1, 3}};
    Expr dx = (x.unfold2d(0, 3, 1, 3) * wx).sum({1, 3});
    Expr dy = (x.unfold2d(0, 3, 1, 3) * wy).sum({1, 3});
    return analysed(finish(Output{"dx"} = dx, Output{"dy"} = dy));
}

constexpr int windows_computed_inside(const loops::Schedule& d) {
    int inside = 0;
    for (const loops::Schedule::Stage& stage : d.stages) {
        inside += stage.through >= 0 ? 1 : 0;
    }
    return inside;
}

constexpr Graph kF1a = related_map_after_the_window();
constexpr Graph kF1b = related_map_after_the_blur();
constexpr Graph kF1c = two_boxes_one_input();
constexpr Graph kF4a = dx_dy_two_windows();
// The planner's schedule, planned once: what is asserted of it, and then that it lowers.
template <const Graph& G>
constexpr loops::Schedule kSchedule = planned::schedule_of(G, zen2);
static_assert(windows_computed_inside(kSchedule<kF1a>) == 1, "F1: the square still inside the window of its conv");
static_assert(windows_computed_inside(kSchedule<kF1b>) == 1, "F1: the square still inside the window of its blur");
static_assert(windows_computed_inside(kSchedule<kF1c>) == 2, "F1: two boxes over a*a and a*2, each its producer inside");
static_assert(kSchedule<kF4a>.stages.size() == 1, "F4a: dx and dy over two window nodes of one image are siblings");
static_assert(planned::refusal_of(kF1a, kSchedule<kF1a>).empty() && planned::refusal_of(kF1b, kSchedule<kF1b>).empty() &&
              planned::refusal_of(kF1c, kSchedule<kF1c>).empty() && planned::refusal_of(kF4a, kSchedule<kF4a>).empty());

} // namespace

int main() {
    std::printf("nest_grouping_review: every finding fixed (checked at compile time)\n");
    return 0;
}
