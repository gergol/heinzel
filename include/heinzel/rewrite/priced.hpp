#pragma once

// What a rewrite on a feedback cycle is priced by (Plan/23 10, Decision 71): it is taken only where the cycle's chain,
// priced in latencies as the cost model prices it (`loops/graph/cycle_chains.hpp`), does not grow. Inside a loop the
// loop's period is its chain: the parallel form puts one multiply-add, the fold across the lanes and the conversions on
// it where the cascade puts each section's newest multiply-add (`Plan/probes/audio-iir/fold_in_a_loop.cpp`). Each graph
// is priced as the planner will be given it, the rules after the parallel form run on it: a sum's order decides which
// multiply-add lies on the chain, double state which conversions do. It names no operation.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/loops/graph/cycle_chains.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/rewrite/double_state.hpp"
#include "heinzel/rewrite/feedback_sums.hpp"
#include "heinzel/schedule/machine.hpp"

namespace heinzel::rewrite {

// The rules after the parallel form, in the order they run (`rewritten.hpp`): a feedback sum in ready order, fused where
// the machine has the multiply-add; a cycle in double where the cost model prices it no dearer.
constexpr Graph sums_and_state(const Graph& g, const Machine& m) { return rewrite::double_state(rewrite::ordered_feedback_sums(g, m), m); }

// Each feedback cycle's chain in `g` as the planner will be given it, in the order the cycles run; none where the
// planner refuses its feedback. Cycles are named by the node opening their span, so the order is their names'.
constexpr std::optional<std::vector<double>> chains_as_planned(const Graph& g, const Machine& m) {
    const Graph planned = rewrite::sums_and_state(g, m);
    const loops::graph::Facts facts = loops::graph::facts_of(planned);
    const loops::graph::Feedback feedback = loops::graph::find_feedback(planned, facts);
    if (!facts.why_not.empty() || !feedback.why_not.empty()) {
        return std::nullopt;
    }
    const std::vector<double> of = loops::graph::cycle_chains(planned, facts, feedback, m);
    std::vector<double> chains{};
    for (std::size_t i = 0; i < planned.nodes.size(); ++i) {
        if (feedback.cycle[i] == static_cast<std::int32_t>(i)) {
            chains.push_back(of[i]);
        }
    }
    return chains;
}

} // namespace heinzel::rewrite
