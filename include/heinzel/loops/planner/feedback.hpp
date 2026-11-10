#pragma once

// Plans a cycle's loops: a feedback stage walks time a sample at a time. It names no operation.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/planner/stages.hpp"
#include "heinzel/loops/schedule.hpp"
#include "heinzel/support/fixed_vector.hpp"

namespace heinzel::loops::planner {

namespace detail {

// Is a value stage `open` makes over its domain, its sections, read outside it, or an output? In a skewed stage the
// sections stand on different samples, so only its last section's slice leaves it, at that section's lane.
constexpr bool sections_leave(const Graph& g, const graph::Facts& facts, const OpenGroup& open) {
    std::vector<char> here(g.nodes.size(), 0); // per node: in the stage
    std::vector<char> over(g.nodes.size(), 0); // ...making a value over its sections
    for (const std::int32_t ni : open.stage.nodes) {
        const auto i = static_cast<std::size_t>(ni);
        const heinzel::Value& made = g.values[static_cast<std::size_t>(g.nodes[i].output)];
        here[i] = 1;
        over[i] = made.shape == open.shape.domain;
    }
    const auto made_over = [&](std::int32_t v) {
        const std::int32_t found = facts.read_as[static_cast<std::size_t>(v)];
        const std::int32_t p = facts.producer[static_cast<std::size_t>(found)];
        return p >= 0 && over[static_cast<std::size_t>(p)] != 0;
    };
    bool leave = false;
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        for (std::size_t k = 0; here[i] == 0 && k < rules_of(g.nodes[i].op).arity; ++k) {
            leave = leave || (g.nodes[i].inputs[k] >= 0 && made_over(g.nodes[i].inputs[k]));
        }
    }
    for (const NamedValue& out : g.outputs) {
        leave = leave || made_over(out.value);
    }
    return leave;
}

} // namespace detail

// Where the rest of a sample (at most one more axis, of a literal extent) fits one block of lane bodies, time runs
// innermost and the lowering hands the state on in registers instead of through a store and a load: 16 voices went from
// 0.25 to 0.13 ms on zen2. A wider sample keeps time outermost and its lanes inside: with time innermost a block would
// stride a whole sample per step (64 voices 3x slower, 1024 voices 30x), and more lanes per sample than a block holds
// are not bound by that latency anyway. Time runs innermost only where a lag of the cycle reads one sample back of what
// a later node of the stage makes (`graph::hands_state_on`): without that state, lanes outside the time loop have
// nothing to build a block for. A stage whose sample folds its voices (Plan/23 10) takes this layout or none: its fold
// restarts every sample, inside one chunk of its lanes, the voices' state in registers; refused by name where its cycle
// hands no state of one sample back on, where the machine has no lanes, or where the voices are not a literal count
// within four lane bodies (`compile` then plans the graph as written). A fold in a cycle (Plan/23 10) meets that here;
// a bank's sum joined to its sample (4.3d) never does, since the gathering lets nothing join such a stage that would
// break it (`sample_fits`). Eligibility: a stage holding a link (`graph::is_a_link`) runs skewed in the voice
// layout, holding its cycle and what leads from it to its last section's slice, and nothing else (`runs_skewed`): section
// j a sample behind section j - 1, so the link reads the section below as it stood a step before. Refused by name where
// that layout does not apply, as for a fold (the rewrite asks `fits_a_block` too, so it makes no such stage); where its
// cycle folds the sections every sample, which no skew runs: section 0 reads their fold a sample on;
// and where its sections are read other than through the last one's slice (`sections_leave`).
constexpr Refusal plan_feedback_loops(OpenGroup& open, const Graph& g, const graph::Facts& facts, std::int32_t lanes) {
    bool linked = false;
    for (const std::int32_t ni : open.stage.nodes) {
        linked = linked || graph::is_a_link(g.nodes[static_cast<std::size_t>(ni)]);
    }
    const std::int32_t rank = open.shape.domain.rank;
    const Extent voices = extent_at(open.shape.domain, 0);
    fixed_vector<Schedule::Loop, kMaxStageLoops>& loops = open.stage.loops;
    const bool state = graph::hands_state_on(g, facts, open.stage);
    const bool voice_layout = rank == 2 && graph::fits_a_block(voices, lanes) && state;
    if (linked && open.shape.fold >= 0) {
        return "heinzel::compile: sections linked in lanes run skewed, so no fold over them runs in their cycle";
    }
    if (linked && detail::sections_leave(g, facts, open)) {
        return "heinzel::compile: sections linked in lanes are read only through their last section's slice";
    }
    if (linked && !(voice_layout && lanes > 1)) {
        return !state       ? "heinzel::compile: sections linked in lanes need their state of one sample back, in registers"
               : lanes <= 1 ? "heinzel::compile: sections linked in lanes take lanes, which this machine has not"
                            : "heinzel::compile: sections linked in lanes take a literal count, four lane bodies at most";
    }
    if (open.shape.fold >= 0 && !(voice_layout && lanes > 1)) {
        return !state       ? "heinzel::compile: a sample folding its voices needs its state of one sample back, in registers"
               : lanes <= 1 ? "heinzel::compile: a sample folding its voices takes them in lanes, which this machine has not"
                            : "heinzel::compile: a sample folding its voices takes a literal count, four lane bodies at most";
    }
    open.stage.skewed = linked;
    if (voice_layout) {
        loops.push_back(lanes > 1 ? Schedule::Loop{.axis = 0, .step = lanes, .kind = LoopKind::Vector, .unroll = (voices.encoded + lanes - 1) / lanes}
                                  : Schedule::Loop{.axis = 0});
    } else if (rank >= 2) {
        for (const std::int32_t axis : graph::memory_order(rank)) {
            loops.push_back(Schedule::Loop{.axis = axis});
        }
        if (lanes > 1) {
            loops[loops.size() - 1] = Schedule::Loop{.axis = rank - 2, .step = lanes, .kind = LoopKind::Vector};
        }
        open.stage.inner_from = static_cast<std::int32_t>(loops.size());
        return {};
    }
    loops.push_back(Schedule::Loop{.axis = rank - 1});
    open.stage.inner_from = static_cast<std::int32_t>(loops.size()) - (state ? 1 : 0); // (time the inner loop where it hands state on)
    return {};
}

} // namespace heinzel::loops::planner
