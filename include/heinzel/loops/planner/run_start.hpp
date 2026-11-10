#pragma once

// A run's first sample apart (Plan/33 S3). A stage that reads a run's start (`_first`: 1 at a run's first sample, else
// 0) runs that sample apart: its time loop's first iteration stands at the run's start and the others past it, where
// the start is a 0 the compiler knows, so what only a start computes (`dsp::smoothed`'s blend and its division) drops
// out of the samples after it. The rule: a stage is peeled where one of its nodes reads what a run-start node makes. No
// band holds a peeled stage nor does a worker split it (`band_layout.hpp`, `parallel.hpp`), and the lowering refuses one
// in lanes, until a benchmark asks. It names no operation.

#include <cstddef>
#include <cstdint>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/schedule.hpp"

namespace heinzel::loops::planner {

constexpr void peel_where_runs_start(Schedule& s, const Graph& g, const graph::Facts& facts) {
    for (Schedule::Stage& stage : s.stages) {
        for (const std::int32_t ni : stage.nodes) {
            const heinzel::Node& n = g.nodes[static_cast<std::size_t>(ni)];
            for (std::size_t k = 0; k < rules_of(n.op).arity; ++k) {
                const std::int32_t maker = facts.producer[static_cast<std::size_t>(facts.read_as[static_cast<std::size_t>(n.inputs[k])])];
                stage.peeled = stage.peeled || (maker >= 0 && graph::marks_a_run_start(g.nodes[static_cast<std::size_t>(maker)]));
            }
        }
    }
}

} // namespace heinzel::loops::planner
