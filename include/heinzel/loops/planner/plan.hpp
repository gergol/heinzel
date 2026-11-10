#pragma once

// The planner turns a graph and a machine into a schedule, by measured rules: for each stage an ordered list of loops
// and whatever else it chose, plus the bands. The lowering takes the schedule and is its legality check. One function
// body decides (`schedule_of`) and the others return its answers, so they cannot disagree. Refusals are values that
// start with `heinzel::compile:`, as the graph facts' and the lowering's do. It names no operation.

#include <cstdint>
#include <expected>
#include <utility>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/graph/views.hpp"
#include "heinzel/loops/lower.hpp"
#include "heinzel/loops/planner/bands.hpp"
#include "heinzel/loops/planner/in_the_reader.hpp"
#include "heinzel/loops/planner/inline.hpp"
#include "heinzel/loops/planner/kept.hpp"
#include "heinzel/loops/planner/loops.hpp"
#include "heinzel/loops/planner/parallel.hpp"
#include "heinzel/loops/planner/run_start.hpp"
#include "heinzel/loops/planner/stages.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/schedule.hpp"
#include "heinzel/schedule/cost.hpp"
#include "heinzel/schedule/machine.hpp"

namespace heinzel::loops {

// A planned graph: its schedule, and the program the schedule lowers to.
struct Planned {
    Schedule schedule{};
    Program program{};
};

namespace planner {
namespace detail {

// The stage's contraction (null: none).
constexpr const heinzel::Node* contraction_of(const Schedule::Stage& stage, const Graph& g) {
    const heinzel::Node* product = nullptr;
    for (const std::int32_t ni : stage.nodes) {
        const heinzel::Node& n = g.nodes[static_cast<std::size_t>(ni)];
        product = graph::is_a_contraction(n) ? &n : product;
    }
    return product;
}

// A relaid operand is copied by a stage of its own, one per value however many stages read the copy, and so is a turned
// one (a constant's turned copy needs no stage, but is counted: never fewer stages than the lowering makes). Where the
// program has no room for all the copies, the later stages read their operand as it was fed: a turned one by lanes
// along the contracted axis again, its split dropped. Slower, but refusing the graph would be worse.
constexpr void keep_the_copies_there_is_room_for(Schedule& s, const Graph& g, const graph::Facts& facts, const Machine& m, std::size_t nests) {
    // Both in blocks of how many columns.
    std::vector<std::int32_t> copied(g.values.size(), 0);
    std::vector<std::int32_t> copied_turned(g.values.size(), 0);
    std::vector<char> turned(g.values.size(), 0);
    std::size_t stages = nests; // (a value made in parts is a nest per part: `nests_of`)
    for (Schedule::Stage& stage : s.stages) {
        const heinzel::Node* product = contraction_of(stage, g);
        if (product == nullptr) {
            continue;
        }
        if (stage.turned >= 0 && !turned[static_cast<std::size_t>(product->inputs[static_cast<std::size_t>(stage.turned)])]) {
            if (stages == kMaxStages) {
                stage.loops = {}, stage.inner_from = 0, stage.relaid = -1, stage.relaid_block = 0, stage.turned = -1, stage.fetches = false, stage.parallel = -1;
                planner::plan_contraction_loops(stage, g, facts, *product, m, false);
                continue;
            }
            turned[static_cast<std::size_t>(product->inputs[static_cast<std::size_t>(stage.turned)])] = 1;
            ++stages;
        }
        if (stage.relaid < 0) {
            continue;
        }
        std::vector<std::int32_t>& done = stage.relaid == stage.turned ? copied_turned : copied;
        const auto operand = static_cast<std::size_t>(product->inputs[static_cast<std::size_t>(stage.relaid)]);
        if (done[operand] == stage.relaid_block) {
            continue;
        }
        if (stages == kMaxStages) {
            stage.relaid = -1;
        } else {
            done[operand] = stage.relaid_block;
            ++stages;
        }
    }
}

// How many stages a schedule's planned stages lower to: one each, except that a value made in parts is one per non-empty
// part. The copies' stages, relaid and turned, are counted on top (`keep_the_copies_there_is_room_for`).
constexpr std::size_t nests_of(const Schedule& s, const Graph& g) {
    std::size_t nests = 0;
    for (const Schedule::Stage& stage : s.stages) {
        const heinzel::Node& first = g.nodes[static_cast<std::size_t>(stage.nodes[0])];
        if (stage.nodes.size() != 1 || !graph::is_made_in_parts(first)) {
            nests += 1;
            continue;
        }
        for (const graph::Part& part : graph::parts_of(g, first)) {
            nests += part.length > 0 ? 1 : 0;
        }
    }
    return nests;
}

// Knob math kept (`docs/knob-math.md`, "Switch"): at 0 no stage is kept, every file of one program agreeing on it.
#ifndef HEINZEL_KNOB_MATH
#define HEINZEL_KNOB_MATH 1
#endif

// The passes over the whole schedule (producers computed inside their windows, a run's start peeled, bands, splits among
// workers); then the stages are counted, and those with no loop that pay for it kept (`counted`: each stage's counts as
// the gathering closed it, by its first node). Where each value is made and read is worked out once its stages are
// final, after the first.
constexpr std::expected<void, Refusal> finish(Schedule& s, bool no_room, const Graph& g, const graph::Facts& facts, const std::vector<cost::Stage>& counted,
                                              const Machine& m) {
    planner::compute_cheap_producers_inside_their_windows(s, g, facts, m);
    planner::peel_where_runs_start(s, g, facts);
    const graph::ValuePlaces at = graph::where_values_are_made(g, facts, s.stages);
    const planner::BandLayouts layouts = planner::band_where_it_pays(s, g, facts, at, m);
    planner::split_among_workers(s, g, facts, at, m, layouts);
    if (no_room || detail::nests_of(s, g) > kMaxStages) {
        return std::unexpected("heinzel::compile: more loop nests than kMaxStages; rebuild with -DHEINZEL_MAX_STAGES=N");
    }
    detail::keep_the_copies_there_is_room_for(s, g, facts, m, detail::nests_of(s, g));
    if constexpr (HEINZEL_KNOB_MATH != 0) {
        planner::keep_what_pays(s, g, facts, counted, m);
    }
    return {};
}

} // namespace detail

// The planner proper: it decides which nodes share a stage (and which maps run in their reader's), then each stage's
// loops from its membership, then runs the passes over the whole schedule. It returns the schedule, or why there is none.
constexpr std::expected<Schedule, Refusal> schedule_of(const Graph& g, const Machine& m) {
    const graph::Facts facts = graph::facts_of(g);
    if (!facts.why_not.empty()) {
        return std::unexpected(facts.why_not);
    }
    const graph::Feedback feedback = graph::find_feedback(g, facts);
    if (g.nodes.empty() || !feedback.why_not.empty()) {
        return std::unexpected(g.nodes.empty() ? Refusal{"heinzel::compile: a graph with no node has nothing to run"} : feedback.why_not);
    }
    // A result is stored where the node making it runs; a gradient its loss does not reach is a constant no node makes.
    for (const NamedValue& out : g.outputs) {
        if (facts.producer[static_cast<std::size_t>(out.value)] < 0) {
            return std::unexpected("heinzel::compile: an output that no node makes (an input, a constant) is not lowered yet");
        }
    }
    Gathered gathered{};
    if (const std::expected<void, Refusal> r = planner::gather_groups(g, facts, feedback, m, gathered); !r) {
        return std::unexpected(r.error());
    }
    if (!gathered.no_room) {
        planner::compute_maps_in_their_reader(gathered, g, facts, m);
    }
    std::expected<Schedule, Refusal> result{std::in_place};
    std::vector<cost::Stage> counted(g.nodes.size());
    for (OpenGroup& open : gathered.groups) {
        if (const Refusal why = planner::plan_loops(open, g, facts, m); !why.empty()) {
            return std::unexpected(why);
        }
        counted[static_cast<std::size_t>(open.stage.nodes[0])] = open.counts;
        result->stages.push_back(open.stage);
    }
    if (const std::expected<void, Refusal> r = detail::finish(*result, gathered.no_room, g, facts, counted, m); !r) {
        return std::unexpected(r.error());
    }
    return result;
}

} // namespace planner

// Plans and lowers once, into `result`: the schedule and the program it lowers to. A schedule the lowering refuses is
// the planner's refusal, with the lowering's message. The result is about 130 KB, so it is made where its caller holds
// it, never as a temporary on the stack (`compile.hpp`'s `plan` makes it in its `Planning`).
constexpr void plan_and_lower_into(std::expected<Planned, Refusal>& result, const Graph& g, const Machine& m) {
    std::expected<Schedule, Refusal> d = planner::schedule_of(g, m);
    if (!d) {
        result = std::unexpected(d.error());
        return;
    }
    result.emplace();
    result->schedule = *std::move(d);
    if (const std::expected<void, Refusal> r = loops::lower_into(g, result->schedule, result->program); !r) {
        result = std::unexpected(r.error());
    }
}

// The same, returned: a run-time caller short of stack holds it on the heap.
constexpr std::expected<Planned, Refusal> plan_and_lower(const Graph& g, const Machine& m) {
    std::expected<Planned, Refusal> result{};
    loops::plan_and_lower_into(result, g, m);
    return result;
}

// Does this graph plan and lower on this machine? (Tests assert with it that a graph does not.)
constexpr bool lowers(const Graph& g, const Machine& m) { return loops::plan_and_lower(g, m).has_value(); }

} // namespace heinzel::loops
