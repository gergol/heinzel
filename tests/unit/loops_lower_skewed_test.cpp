// The lowering of a skewed stage (`docs/sections-in-lanes.md`). The planner's schedules (`loops_plan_skewed_test`),
// planned on zen2, lower to programs the runner accepts (`well_formed`): the linked graphs at High
// (`support/linked_sections.hpp`) and the K = 8 Param cascade as `compile` plans it, the gather before the skewed one.
// In each skewed stage, the program's skew facts, as `support/skewed_sections.hpp` builds them by hand: the stage
// `skewed`, its time loop its Loop over time; the link a Map of `_chain` reading its keeper's State at slot 0,
// `shifted`, with no Open and no Close of its own; the fill's Load at lane 0; the last section's slice and its reshape
// at lane k − 1. LR8's and the cascade's programs are rows of `tests/goldens/loops_lower/` (`linked_lr8`,
// `linked_params_8`). Then each of the lowering's refusals by name, mostly on an edited schedule: a link in a stage not
// skewed (eight linked sections, all of them the output, in the eight's stage less its slice, which the planner refuses
// first); a skewed stage moving along time at no lane (those eight skewed, each stored every sample; LR8 with a state
// deeper than its registers, read from memory); one lowered into more than one stage (a pad of a tensor), or with no
// Vector along its sections; a link whose cycle is cut between two stages; and the eight's lanes in two chunks.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

#include "heinzel/loops/lower.hpp"
#include "heinzel/loops/print.hpp"
#include "heinzel/rewrite/rewritten.hpp"
#include "heinzel/rewrite/sections_in_lanes.hpp"
#include "heinzel/schedule/machine.hpp"

#include "corpus_graphs.hpp"
#include "linked_sections.hpp"
#include "loops_schedules.hpp"
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

// ---- the programs --------------------------------------------------------------------------------------------------

// Skewed stage s's facts, in the nodes from its root to the next stage's: its time loop a Loop over time; one shifted
// Compute, the link, reading slot 0, opened and closed by none; at lane 0 one Compute, at lane k − 1 two, and no other
// lane.
bool skewed_facts(const loops::Program& p, std::size_t s) {
    const loops::Stage& stage = p.stages[s];
    const std::int64_t k = literal_of(stage.axes[0]);
    const std::size_t end = s + 1 < p.stages.size() ? static_cast<std::size_t>(p.stages[s + 1].root) : p.nodes.size();
    const loops::Node& time = p.nodes[static_cast<std::size_t>(stage.time_loop)];
    bool ok = time.kind == loops::NodeKind::Loop && time.axis + 1 == static_cast<std::int32_t>(stage.axes.size());
    std::int32_t link = -1;
    std::int32_t first = 0;
    std::int32_t last = 0;
    std::int32_t others = 0;
    for (auto n = static_cast<std::size_t>(stage.root); n < end; ++n) {
        const loops::Node& here = p.nodes[n];
        ok = ok && !(here.shifted && (link >= 0 || here.slot != 0));
        link = here.shifted ? here.stmt : link;
        first += here.lane == 0 ? 1 : 0;
        last += here.lane == k - 1 ? 1 : 0;
        others += here.lane != -1 && here.lane != 0 && here.lane != k - 1 ? 1 : 0;
    }
    for (auto n = static_cast<std::size_t>(stage.root); n < end; ++n) {
        const loops::Node& here = p.nodes[n];
        ok = ok && !((here.kind == loops::NodeKind::Open || here.kind == loops::NodeKind::Close) && here.stmt == link);
    }
    if (!ok || link < 0) {
        return false;
    }
    const loops::Stmt& linked = p.stmts[static_cast<std::size_t>(stage.first + link)];
    return linked.kind == loops::StmtKind::Map && name_of(linked.op) == OpName{"_chain"} && first == 1 && last == 2 && others == 0;
}

// Program `p` as a row of `tests/goldens/loops_lower/`, as `loops_lower_test` pins its rows (`HEINZEL_REPIN=1` rewrites
// it, reviewed in `git diff`).
void in_goldens(const char* row, const loops::Program& p) {
    const std::string path = std::string{HEINZEL_LOOPS_LOWER_GOLDENS} + "/" + row + ".txt";
    const std::string got = loops::print(p);
    if (std::getenv("HEINZEL_REPIN") != nullptr) {
        std::ofstream(path) << got;
        return;
    }
    std::ifstream in(path);
    std::stringstream pinned;
    pinned << in.rdbuf();
    if (got != pinned.str()) {
        std::printf("%s lowered, not as %s:\n%s", row, path.c_str(), got.c_str());
    }
    check(got == pinned.str(), row);
}

// `g` planned on zen2 and lowered: well formed, its skewed stages those at `skewed`, each holding its facts; the program
// a row of the goldens where `row` names one.
void lowers(const char* what, const Graph& g, std::initializer_list<std::size_t> skewed, const char* row = nullptr) {
    const loops::Schedule s = planned::schedule_of(g, zen2);
    const std::unique_ptr<loops::Program> p = std::make_unique<loops::Program>();
    const std::expected<void, loops::Refusal> r = loops::lower_into(g, s, *p);
    const std::expected<void, loops::Refusal> w = r ? loops::well_formed(*p) : r;
    if (!w) {
        std::printf("%s: %.*s\n", what, static_cast<int>(w.error().view().size()), w.error().view().data());
        check(false, what);
        return;
    }
    std::size_t count = 0;
    bool ok = true;
    for (std::size_t k = 0; k < p->stages.size(); ++k) {
        count += p->stages[k].skewed ? 1 : 0;
        ok = ok && (!p->stages[k].skewed || (std::find(skewed.begin(), skewed.end(), k) != skewed.end() && skewed_facts(*p, k)));
    }
    if (!ok || count != skewed.size()) {
        std::printf("%s lowered:\n%s", what, loops::print(*p).c_str());
    }
    check(ok && count == skewed.size(), what);
    if (row != nullptr) {
        in_goldens(row, *p);
    }
}

void programs_hold() {
    lowers("the TPT pair", tpt_pair(Form::Linked), {0});
    lowers("LR8", lr8(Form::Linked), {0}, "linked_lr8");
    lowers("the five peaks", peaks(Form::Linked), {0});
    lowers("the eight tanh sections", saturating_chain(Form::Linked), {0});
    lowers("the pair into the eight", in_series(Form::Linked), {0, 1});
    const Graph merged = rewrite::sections_in_lanes(lanes::raw(8), zen2);
    lowers("the K = 8 Param cascade", rewrite::detail::after_the_form(merged, zen2), {1}, "linked_params_8");
}

// ---- the refusals --------------------------------------------------------------------------------------------------

void refused(const char* what, const Graph& g, const loops::Schedule& s, std::string_view why) {
    const loops::Refusal got = planned::refusal_of(g, s);
    if (got.view() != why) {
        std::printf("%s: refused \"%.*s\"\n", what, static_cast<int>(got.view().size()), got.view().data());
    }
    check(got.view() == why, what);
}

inline constexpr std::string_view kAtOneLane = "heinzel::compile: a skewed stage reads and stores what moves along time at one lane only";

// A [4, 1] tensor padded along its sections to [9, 1], in three parts (`graph::is_made_in_parts`).
Graph padded_tensor() {
    OpAttr attr{};
    attr.kind = AttrKind::View;
    attr.view.kind = ViewKind::Pad;
    attr.view.axis = 0;
    attr.view.pad_before = 2;
    attr.view.pad_after = 3;
    const Expr x = Input{"x", Signal{Shape{4, 1}}};
    return corpus::analysed(finish(Output{"y"} = heinzel::detail::append_unary(x, op::Pad, attr)));
}

void refusals_hold() {
    const Graph g = lr8(Form::Linked);
    const loops::Schedule as_planned = planned::schedule_of(g, zen2);
    const Graph eight = saturating_chain(Form::Linked);
    const loops::Schedule eight_planned = planned::schedule_of(eight, zen2);
    const Graph whole = all_eight_out();
    loops::Schedule whole_planned = eight_planned;
    whole_planned.stages[0].nodes = loops_schedules::stage({0, 2, 3, 4, 5, 6, 7, 8}).nodes;
    loops::Schedule plain = whole_planned;
    plain.stages[0].skewed = false;
    refused("a link in a stage not skewed", whole, plain, "heinzel::compile: a link stands only in a skewed stage");
    refused("all eight sections an output", whole, whole_planned, kAtOneLane);
    loops::Schedule shallow = as_planned;
    shallow.stages[0].state_depth = 1;
    refused("a state deeper than its registers", g, shallow, kAtOneLane);
    loops::Schedule serial = as_planned;
    serial.stages[0] = loops_schedules::planned(as_planned.stages[0], {loops_schedules::serial(0), loops_schedules::serial(1)}, 1);
    serial.stages[0].skewed = true;
    refused("no Vector along the sections", g, serial, "heinzel::compile: a skewed stage runs its sections in lanes, a Vector along axis 0");
    loops::Schedule cut = as_planned;
    loops::Schedule::Stage rest = as_planned.stages[0];
    cut.stages[0].nodes = loops_schedules::stage({6, 7, 5}).nodes;
    rest.nodes = loops_schedules::stage({8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21}).nodes;
    cut.stages.push_back(rest);
    refused("a link's cycle cut between two stages", g, cut, "heinzel::compile: a link reads what its own group makes: its cycle runs in one group");
    const Graph pad = padded_tensor();
    loops::Schedule::Stage parts = loops_schedules::planned(loops_schedules::stage({0}), {loops_schedules::serial(1), loops_schedules::lanes(0, 4)}, 2);
    parts.skewed = true;
    refused("a skewed stage in parts", pad, loops_schedules::schedule({parts}), "heinzel::compile: a skewed stage lowers into more than one stage");
    loops::Schedule chunks = eight_planned;
    chunks.stages[0].loops[0].unroll = 1;
    refused("the eight's lanes in two chunks", eight, chunks, "heinzel::compile: a skewed stage holds its sections in one chunk of lanes, a literal count");
}

} // namespace

int main() {
    try {
        programs_hold();
        refusals_hold();
    } catch (const heinzel::error& e) {
        std::printf("loops_lower_skewed: %.*s\n", static_cast<int>(e.message().size()), e.message().data());
        return 1;
    }
    return g_wrong == 0 ? 0 : 1;
}
