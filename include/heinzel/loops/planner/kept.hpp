#pragma once

// Which stages are kept (`docs/knob-math.md`): a stage with no loop that works out values from Params alone, as a
// filter's coefficients from its knobs, runs only in a run where a Param it watches changed, its results kept in their
// workspace buffers in between, where the cost model prices it above checking those Params (`cost::kept`). One pass
// over the final schedule, in stage order. It names no operation.

#include <array>
#include <bit>
#include <cstdint>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/planner/gathering.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/schedule.hpp"
#include "heinzel/schedule/cost.hpp"
#include "heinzel/schedule/machine.hpp"

namespace heinzel::loops::planner {

// A bit per named input, in the feeds' order.
using Watched = std::array<std::uint64_t, kWatchWords>;

namespace detail {

// What the pass knows of the graph and of the stages it has passed: per node, whether it is work ahead of the loops and
// the stage it runs in (-1: none passed yet); per value, the place among the named inputs of a Param, an input of no
// axes that `Executor` copies in by value each run (`ports.hpp` `is_param`), -2 for any other input, a tensor read where
// it lies, whose numbers may change with no call, -1 for what is no input; per stage passed, whether it is eligible
// and the Params it watches.
struct Passed {
    std::vector<char> ahead{};
    std::vector<std::int32_t> stage_of{};
    std::vector<std::int32_t> param{};
    std::vector<char> eligible{};
    std::vector<Watched> watching{};
};

constexpr Passed begin_passing(const Schedule& s, const Graph& g, const graph::Facts& facts) {
    Passed at{.ahead = graph::ahead_of_the_loops(g, facts), .stage_of = std::vector<std::int32_t>(g.nodes.size(), -1),
              .param = std::vector<std::int32_t>(g.values.size(), -1), .eligible = std::vector<char>(s.stages.size(), 0),
              .watching = std::vector<Watched>(s.stages.size())};
    for (std::size_t i = 0; i < g.inputs.size(); ++i) {
        const std::size_t v = static_cast<std::size_t>(g.inputs[i].value);
        at.param[v] = g.values[v].shape.rank == 0 ? static_cast<std::int32_t>(i) : -2;
    }
    return at;
}

constexpr bool in_a_band(const Schedule& s, std::size_t k) {
    const auto at = static_cast<std::int32_t>(k);
    bool in = false;
    for (const Schedule::Band& band : s.bands) {
        in = in || (at >= band.first && at - band.first < band.count);
    }
    return in;
}

// Is stage `k` of `s` eligible, its nodes' stage noted: no loop and no band, every node work ahead of the loops that
// makes no output (an output may be written in the caller's memory, which a skipped stage would leave unwritten), every
// input it reads a Param, every value a node makes that it reads made by it or by an earlier eligible stage? Its
// watched Params, into `at.watching[k]`: those it reads, and those its makers watch.
constexpr bool eligible(const Schedule& s, std::size_t k, const Graph& g, const graph::Facts& facts, Passed& at) {
    const Schedule::Stage& stage = s.stages[k];
    for (const std::int32_t ni : stage.nodes) {
        at.stage_of[static_cast<std::size_t>(ni)] = static_cast<std::int32_t>(k);
    }
    bool ok = stage.loops.empty() && !detail::in_a_band(s, k);
    Watched& watched = at.watching[k];
    for (const std::int32_t ni : stage.nodes) {
        const std::size_t i = static_cast<std::size_t>(ni);
        ok = ok && at.ahead[i] != 0 && !is_a_result(g, g.nodes[i].output);
        for (std::size_t q = 0; ok && q < rules_of(g.nodes[i].op).arity; ++q) {
            const std::size_t v = static_cast<std::size_t>(detail::source_of(g, facts, i, q));
            const std::int32_t maker = facts.producer[v];
            const std::int32_t from = maker >= 0 ? at.stage_of[static_cast<std::size_t>(maker)] : -1;
            const bool earlier = from >= 0 && static_cast<std::size_t>(from) < k && at.eligible[static_cast<std::size_t>(from)] != 0;
            ok = at.param[v] != -2 && (maker < 0 || from == static_cast<std::int32_t>(k) || earlier); // (the loop runs while ok)
            if (at.param[v] >= 0) {
                const auto p = static_cast<std::size_t>(at.param[v]);
                watched[p / 64] |= std::uint64_t{1} << (p % 64);
            }
            for (std::size_t w = 0; earlier && w < kWatchWords; ++w) {
                watched[w] |= at.watching[static_cast<std::size_t>(from)][w];
            }
        }
    }
    return ok;
}

constexpr std::int32_t count_of(const Watched& watched) {
    std::int32_t count = 0;
    for (const std::uint64_t word : watched) {
        count += std::popcount(word);
    }
    return count;
}

} // namespace detail

// Decides which stages of `s` are kept, and the Params each watches, in one pass in stage order, once the stages and the
// bands are final. A stage is kept where it is eligible (`detail::eligible`) and the cost model prices it
// above checking the Params it watches: its price from its counts as the gathering closed it (`counted`, by its first
// node; no pass after the gathering moves a node into or out of a stage with no loop). Where it is not kept, it watches
// nothing in the schedule.
constexpr void keep_what_pays(Schedule& s, const Graph& g, const graph::Facts& facts, const std::vector<cost::Stage>& counted, const Machine& m) {
    detail::Passed at = detail::begin_passing(s, g, facts);
    for (std::size_t k = 0; k < s.stages.size(); ++k) {
        Schedule::Stage& stage = s.stages[k];
        at.eligible[k] = detail::eligible(s, k, g, facts, at) ? 1 : 0;
        const std::int32_t watches = detail::count_of(at.watching[k]);
        const cost::Stage& counts = counted[static_cast<std::size_t>(stage.nodes[0])];
        stage.kept = at.eligible[k] != 0 && cost::kept(counts, watches, m);
        stage.watched = stage.kept ? at.watching[k] : Watched{};
    }
}

} // namespace heinzel::loops::planner
