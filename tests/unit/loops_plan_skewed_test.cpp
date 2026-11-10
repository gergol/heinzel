// The planner runs a stage holding a link skewed (`docs/sections-in-lanes.md`), in the voice layout (lanes
// along the sections, bodies of them unrolled, time innermost), holding its cycle and what leads from it to its last
// section's slice, and nothing else (`planner/feedback.hpp`, `runs_skewed`). Planned as written on zen2 and their
// schedules pinned:
//   - the linked graphs (`support/linked_sections.hpp`), in double at High: the TPT pair, LR8, the five peaks (two
//     bodies), the eight tanh sections and the pair into the eight (two skewed stages, the second's link reading the
//     first's slice);
//   - the K = 8 Param cascade as `compile` plans it: `lanes::raw(8)` merged by `sections_in_lanes` and given the rules
//     after it (the cycle in double, its last section converted down before the slice), the gather of its 40 Params
//     a stage of its own before the skewed one.
// LR8's and the cascade's schedules are rows of `tests/goldens/loops_plan/` (`linked_lr8`, `linked_params_8`).
// None is split among zen2's workers, and none is banded. Each refusal by name: no state handed on, no lanes, more
// sections than four lane bodies hold, sections read other than through the last one's slice (the eight tanh
// sections all the output), and a band asked to hold a skewed stage. (A cycle folding its sections every sample is
// `linked_sections_test`'s.)

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include "heinzel/loops/band_layout.hpp"
#include "heinzel/loops/planner/plan.hpp"
#include "heinzel/loops/print.hpp"
#include "heinzel/rewrite/rewritten.hpp"
#include "heinzel/rewrite/sections_in_lanes.hpp"
#include "heinzel/schedule/machine.hpp"

#include "hand_built_graph.hpp"
#include "linked_sections.hpp"
#include "planned.hpp"
#include "sections_in_lanes_graphs.hpp"

using namespace heinzel;
using namespace heinzel::testing::linked_sections;

namespace {

int g_wrong = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        ++g_wrong;
        std::printf("FAILED: %s\n", what);
    }
}

// The schedule of `g` planned on `m`, as text, or its refusal.
std::string planned_text(const Graph& g, const Machine& m) {
    const std::expected<loops::Schedule, loops::Refusal> s = loops::planner::schedule_of(g, m);
    return s ? loops::print(*s, g) : std::string(s.error().view());
}

void pinned(const char* what, const Graph& g, std::string_view listing) {
    const std::string got = planned_text(g, zen2);
    if (got != listing) {
        std::printf("%s planned:\n%s", what, got.c_str());
    }
    check(got == listing, what);
}

// ...as a row of `tests/goldens/loops_plan/`, its machine named first as `loops_plan_test` names it (`HEINZEL_REPIN=1`
// rewrites it, reviewed in `git diff`).
void in_goldens(const char* row, const Graph& g) {
    const std::string path = std::string{HEINZEL_LOOPS_PLAN_GOLDENS} + "/" + row + ".txt";
    const std::string got = "-- zen2\n" + planned_text(g, zen2);
    if (std::getenv("HEINZEL_REPIN") != nullptr) {
        std::ofstream(path) << got;
        return;
    }
    std::ifstream in(path);
    std::stringstream pinned;
    pinned << in.rdbuf();
    if (got != pinned.str()) {
        std::printf("%s planned, not as %s:\n%s", row, path.c_str(), got.c_str());
    }
    check(got == pinned.str(), row);
}

// ---- the schedules -------------------------------------------------------------------------------------------------

// zen2's line every schedule ends with.
constexpr std::string_view kWork = "parallel-work 262144 line-bytes 64\n";

// Each stage: its nodes, the lanes along the sections (a body of four doubles, or two), time innermost, `skewed`. The
// cycle's lags of the link's value run before it, which hands them on (`lags_before_their_makers`); the slice of the last
// section and its reshape last, after the conversion down where the cycle is double. The Param cascade's gather stage
// (35 selects over [8, 1]) stands before it, a stage of maps like any other.
void schedules_hold() {
    const std::string pair = "s0 = n0 n2 n3 n4 n5 n6 n7 n8 n9 n10\n  loops a0 vector 4 | a1\n  skewed\n";
    pinned("the TPT pair", tpt_pair(Form::Linked), pair + std::string(kWork));
    in_goldens("linked_lr8", lr8(Form::Linked));
    pinned("the five peaks", peaks(Form::Linked),
           std::string("s0 = n1 n2 n0 n3 n4 n5 n6 n7 n8 n9 n10 n11 n12 n13 n14 n15 n16\n  loops a0 vector 4 x2 | a1\n  skewed\n") + std::string(kWork));
    pinned("the eight tanh sections", saturating_chain(Form::Linked),
           std::string("s0 = n0 n2 n3 n4 n5 n6 n7 n8 n9 n10\n  loops a0 vector 4 x2 | a1\n  skewed\n") + std::string(kWork));
    pinned("the pair into the eight", in_series(Form::Linked),
           pair + "s1 = n11 n13 n14 n15 n16 n17 n18 n19 n20 n21\n  loops a0 vector 4 x2 | a1\n  skewed\n" + std::string(kWork));
    const Graph merged = rewrite::sections_in_lanes(lanes::raw(8), zen2);
    in_goldens("linked_params_8", rewrite::detail::after_the_form(merged, zen2));
}

// ---- the refusals --------------------------------------------------------------------------------------------------

// k memoryless sections, y = tanh(0.5 u + 0.1): the link alone closes their cycle, so no state is handed on.
constexpr Graph memoryless(std::int32_t k) {
    return graph_of([k](Graph& g, std::int32_t x) {
        const auto outputs = variable(g, Shape{k, kLength});
        const auto u = add_node(g, op::Chain, x, outputs);
        const auto half = scalar(g, 0.5);
        const auto fed = add_node(g, op::Mul, half, u);
        const auto offset = scalar(g, 0.1);
        const auto shifted = add_node(g, op::Add, fed, offset);
        const auto bent = add_node(g, op::Tanh, shifted);
        settle(g, bent, outputs);
        return section_of(g, outputs, k - 1, Shape{kLength});
    });
}

// Nine tanh sections: one more than four bodies of the default machine's two double lanes.
constexpr Graph nine_tanh_sections() {
    return graph_of([](Graph& g, std::int32_t x) {
        constexpr std::array<double, 9> kNine{0.30, 0.35, 0.40, 0.45, 0.50, 0.55, 0.60, 0.65, 0.70};
        return linked_saturating(g, x, kNine);
    });
}

constexpr Machine kOneDoubleLane{.vector_bytes = 8};

void refusals_hold() {
    check(planned_text(memoryless(4), zen2) == "heinzel::compile: sections linked in lanes need their state of one sample back, in registers",
          "no state handed on");
    check(planned_text(lr8(Form::Linked), kOneDoubleLane) == "heinzel::compile: sections linked in lanes take lanes, which this machine has not",
          "no lanes");
    check(planned_text(nine_tanh_sections(), default_machine) ==
              "heinzel::compile: sections linked in lanes take a literal count, four lane bodies at most",
          "nine sections at two lanes");
    check(planned_text(saturating_chain(Form::Linked), default_machine).find("skewed") != std::string::npos, "eight at two lanes");
    check(planned_text(all_eight_out(), zen2) == "heinzel::compile: sections linked in lanes are read only through their last section's slice",
          "all eight sections the output");
    const Graph g = lr8(Form::Linked);
    const loops::Schedule s = planned::schedule_of(g, zen2);
    const loops::graph::Facts facts = loops::graph::facts_of(g);
    check(loops::why_not_in_any_band(g, facts, s.stages[0]) == "heinzel::compile: a band holds no skewed stage (each chunk would drain its sections)",
          "a band holding a skewed stage");
}

} // namespace

int main() {
    try {
        schedules_hold();
        refusals_hold();
    } catch (const heinzel::error& e) {
        std::printf("loops_plan_skewed: %.*s\n", static_cast<int>(e.message().size()), e.message().data());
        return 1;
    }
    return g_wrong == 0 ? 0 : 1;
}
