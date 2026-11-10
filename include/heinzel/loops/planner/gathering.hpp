#pragma once

// The state of the planner's first pass (`stages.hpp`) while it gathers the stages: the stage still open and the ones
// gathered, and what each node and value is to the open stage. `chains.hpp` reads it to chain feedback cycles onto one
// stage. It names no operation.

#include <array>
#include <cstdint>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/graph/regions.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/schedule.hpp"
#include "heinzel/schedule/cost.hpp"
#include "heinzel/schedule/machine.hpp"
#include "heinzel/support/fixed_vector.hpp"

namespace heinzel::loops::planner {

// A stage being gathered: its nodes so far, its shape (the domain its loops will walk, its fold if it has one, and what
// the fold collapses: the nodes after it are maps over its result), and the first feedback cycle it holds
// (`graph::Feedback`; -1: none). A stage that reads its own past walks time a sample at a time; it may hold several
// cycles, each chained onto the ones before it (`chains_on`). `counts`: the model's counts of it as it closed
// (`Gathering::last_cost`), which `kept.hpp` prices a stage with no loop by (a map later moved into its reader leaves
// them as they were: no stage with no loop gives or takes one).
struct OpenGroup {
    Schedule::Stage stage{};
    graph::GroupShape shape{};
    std::int32_t cycle{-1};
    cost::Stage counts{};
};

// Every stage, in the order they run. `no_room`: a stage was closed that a program has no room for. (`close` keeps
// at most `kMaxStages`, and `joins` at most `kMaxGroupStmts` nodes a stage.)
struct Gathered {
    std::vector<OpenGroup> groups{};
    bool no_room{false};
};

namespace detail {

// The open stage is done: into the list, if there is room, and a new one opens.
constexpr void close(Gathered& gathered, OpenGroup& open) {
    if (!open.stage.nodes.empty()) {
        if (gathered.groups.size() == kMaxStages) {
            gathered.no_room = true;
        } else {
            gathered.groups.push_back(open);
        }
    }
    open = OpenGroup{};
}

inline constexpr std::uint32_t kOverTheResult = std::uint32_t{1} << 31; // a read after the fold: along an axis it keeps

inline constexpr std::int32_t kOpen = -2; // `Gathering::made_in`: made by the stage still open

using Operands = std::array<std::int32_t, kMaxNodeInputs>;

using graph::kNoLag;

// The pass's state, sized to the graph; none of it leaves the pass. It records which nodes are visited and how many of
// each node's operands are still missing, so that readiness is one comparison, and what the open stage makes, reads
// and walks, so that whether a node is related to it (`related`) is a few lookups. Flags are `char`s: the bit proxies
// of `std::vector<bool>` cost the planner a tenth of its constant evaluation.
struct Gathering {
    OpenGroup open{};
    std::vector<char> runs{}; // it has a statement and is no ramp (whose storage is filled once)
    std::vector<char> visited{};
    std::vector<std::int32_t> missing{};
    // Who reads each node, once per operand: `reader[first_reader[m] .. first_reader[m + 1])`.
    std::vector<std::int32_t> first_reader{};
    std::vector<std::int32_t> reader{};
    std::vector<std::int32_t> no_loops{}; // ready, and no statement of their own: visited next
    // The units — a node, or a feedback cycle's consecutive nodes — by their first position in `feedback.order`: where
    // each node's unit starts, how many of its nodes still wait for an operand, and the units that wait for none, in
    // planning order.
    std::vector<std::int32_t> unit_of{};
    std::vector<std::int32_t> waiting{};
    std::vector<std::int32_t> ready{};
    std::vector<Operands> maker{}; // the node an operand is read from (-1: none, or its own cycle)
    // The node whose storage a node reads at another place or time: a view's source, a lag's, a gather's (-1: none).
    // Storage is written by a stage that has run, so such a read starts a stage of its own after the one that makes
    // what it reads (but in a cycle, where a lag reads what was stored a sample ago).
    std::vector<std::int32_t> storage_of{};
    std::vector<char> lags{};              // ...a lag, which a cycle's stage may hold after all
    std::vector<std::int32_t> made_in{};   // per value: the gathered stage that made it (-1: none; kOpen)
    std::vector<char> after{};             // an after value of the open stage: a fold's result, or a map over it
    // The axes of its domain along which the open stage reads a value (`kOverTheResult` after its fold), and reads
    // anything a gathered stage made.
    std::vector<std::uint32_t> walks{};
    std::array<std::uint32_t, kMaxStages> reads_from{};
    // What chains a later feedback cycle onto a feedback stage: per node, the cycle it leads into through cheap work
    // only (`target`, from `find_chains`; -1: none); per unit, how many of its nodes run and how many states it hands
    // on; per cycle, whether the open stage holds it; for the open stage, the states it hands on and the values from
    // outside it loads (`loaded`, one mark per value). A merge is bounded by the machine's lanes and registers.
    std::vector<char> held{};
    std::vector<char> loaded{};
    std::vector<std::int32_t> target{};
    std::vector<std::int32_t> hands_to{}; // per cycle: the cycle it hands its values on to (`graph::handed_to`; -1: none)
    std::int32_t open_hands_to{-1};       // the open stage's cycles'
    std::vector<std::int32_t> unit_runs{};
    std::vector<std::int32_t> unit_states{};
    std::vector<std::int32_t> held_now{};
    std::vector<std::int32_t> loaded_now{};
    std::vector<std::int32_t> leads{};  // per cycle: the chain nodes into it not gathered yet
    std::vector<std::int32_t> lead_states{};  // per cycle: their states
    // Per cycle, the widest element the chain nodes into it make; per unit, its nodes (`sample_fits`).
    std::vector<std::int32_t> lead_bytes{};
    std::vector<std::int32_t> unit_bytes{};
    std::int32_t states{0};
    std::vector<std::int32_t> slots{}; // per node: the run of lags of its value that runs (`count_states`)
    std::vector<std::int32_t> fold_of{}; // per cycle: the fold its sample holds (-1: none; Plan/23 10)
    std::vector<std::int32_t> link_of{}; // per cycle: the link it holds (-1: none)
    std::vector<char> to_the_slice{};    // per node: it leads from a linked cycle to its last section's slice, or is that
                                         //   slice (`find_the_slices`)
    // Per node, the piece of the work gathered ahead of every unit with a loop that it is in, named by the piece's first
    // node (-1: none; `find_the_work_ahead`, `next_unit`).
    std::vector<std::int32_t> ahead{};
    // What the cost model (`schedule/cost.hpp`) prices a merge by: per cycle, the slots and the depth (in latencies) of
    // the cheap work leading into it (`find_chains`); per unit, its own chain, depth and slots (`price_units`); per
    // node, its depth in the open stage; and the open stage's counts.
    std::vector<double> lead_slots{};
    std::vector<double> lead_depth{};
    std::vector<double> unit_chain{};
    std::vector<double> unit_depth{};
    std::vector<double> unit_slots{};
    std::vector<double> depth_at{};
    // Per unit and for the open stage, the shortest lag through which a cycle reads its own past: at least a vector's
    // lanes, its samples run that many at once (a comb's, an allpass's).
    std::vector<std::int32_t> unit_lag{};
    std::int32_t open_lag{kNoLag};
    cost::Stage open_cost{};  // the open stage's counts
    cost::Stage last_cost{};  // the last stage closed, priced
    // The work after the open feedback stage's cycles (`trail.hpp`): the slots of what joined the open stage so, and the
    // model's answer for the stage as it stands (its first node, the cycles it holds), per node whether it joins: a memo,
    // not state, hence mutable.
    double trail_slots{0};
    double once_slots{0}; // the open stage's slots that run once a sample, after its fold over voices (`folds_voices`)
    mutable std::vector<char> trail{};
    mutable std::vector<std::int32_t> trail_marked{};
    mutable std::int32_t trail_first{-1};
    mutable std::int32_t trail_cycles{-1};
    mutable std::int32_t trail_fold{-1};

    Machine machine{};
    // Per cycle, the model's answer for the open stage that asked (its first node), so that a cycle and the work into
    // it get one answer however often `joins` asks: a memo, not state, hence mutable.
    mutable std::vector<std::int32_t> asked_by{};
    mutable std::vector<char> answer{};
};

// The state sized to `g`.
constexpr void size_to(const Graph& g, Gathering& at) {
    const std::size_t nodes = g.nodes.size();
    const std::size_t values = g.values.size();
    for (std::vector<char>* per_node : {&at.runs, &at.visited, &at.lags, &at.held, &at.to_the_slice}) {
        per_node->assign(nodes, false);
    }
    for (std::vector<std::int32_t>* per_node : {&at.missing, &at.unit_of, &at.waiting, &at.unit_runs, &at.unit_states, &at.slots}) {
        per_node->assign(nodes, 0);
    }
    at.target.assign(nodes, -1);
    at.fold_of.assign(nodes, -1);
    at.link_of.assign(nodes, -1);
    at.ahead.assign(nodes, -1);
    at.unit_lag.assign(nodes, kNoLag);
    at.asked_by.assign(nodes, -1);
    at.trail.assign(nodes, 0);
    at.answer.assign(nodes, 0);
    for (std::vector<double>* per_node : {&at.lead_slots, &at.lead_depth, &at.unit_chain, &at.unit_depth, &at.unit_slots, &at.depth_at}) {
        per_node->assign(nodes, 0);
    }
    at.leads.assign(nodes, 0);
    at.lead_states.assign(nodes, 0);
    at.lead_bytes.assign(nodes, 0);
    at.unit_bytes.assign(nodes, 0);
    at.loaded.assign(values, false);
    at.storage_of.assign(nodes, -1);
    at.first_reader.assign(nodes + 1, 0);
    at.reader.assign(nodes * kMaxNodeInputs, 0);
    at.maker.assign(nodes, Operands{});
    at.made_in.assign(values, -1);
    at.after.assign(values, false);
    at.walks.assign(values, 0);
}

// The value operand `k` of node `i` is read from: through views with no statement, what reads them reads their source.
constexpr std::int32_t source_of(const Graph& g, const graph::Facts& facts, std::size_t i, std::size_t k) {
    return facts.through[static_cast<std::size_t>(facts.read_as[static_cast<std::size_t>(g.nodes[i].inputs[k])])].source;
}

// The axes of the open stage's domain that its fold keeps, as bits, with `kOverTheResult` set: a read after the fold,
// over its result, walks only those. (A contraction's contracted axis lies outside its domain: it keeps them all.)
constexpr std::uint32_t kept_axes(const OpenGroup& open) {
    const std::uint32_t domain = (std::uint32_t{1} << open.shape.domain.rank) - 1;
    return kOverTheResult | (domain & ~open.shape.collapsed);
}

// Is node `i` related to the open stage? It is when it reads what the stage makes, or, when the stage holds a fold,
// when it reads at the points the fold keeps a value the stage reads too, or one made by a stage the open stage reads
// from. A weight read along the taps, or broadcast, relates nothing. Only a fold stage is related by what it reads: a
// map pulled into a stage of maps by a shared input could cost a cheap producer its place inside the window that reads
// it (`planner/inline.hpp` takes only a stage of maps).
constexpr bool related(const Graph& g, const graph::Facts& facts, std::size_t i, const Gathering& at) {
    const bool reads_relate = at.open.shape.fold >= 0;
    const std::uint32_t kept = detail::kept_axes(at.open);
    for (std::size_t k = 0; k < rules_of(g.nodes[i].op).arity; ++k) {
        const std::int32_t source = detail::source_of(g, facts, i, k);
        const std::int32_t from = at.made_in[static_cast<std::size_t>(source)];
        if (from == kOpen || (reads_relate && (at.walks[static_cast<std::size_t>(source)] & kept) != 0) ||
            (reads_relate && from >= 0 && static_cast<std::size_t>(from) < kMaxStages && (at.reads_from[static_cast<std::size_t>(from)] & kept) != 0)) {
            return true;
        }
    }
    return false;
}

// Does the open stage fold its voices every sample (a cycle holding a fold, Plan/23 10)? Nothing joins it but its
// cycle, and it joins nothing: a stand-in for the cost model, which would price what may share its sample.
constexpr bool folds_each_sample(const Gathering& at) { return at.open.cycle >= 0 && at.fold_of[static_cast<std::size_t>(at.open.cycle)] >= 0; }

// Does the open stage hold a link (`graph::is_a_link`), so that it runs its sections skewed in lanes, each a sample
// behind the one before (`plan_feedback_loops`)? Nothing joins it but its cycle and what leads from that to
// its last section's slice, then the slice (`find_the_slices`), and it joins nothing: no feed, no other cycle, no work
// after the slice, which runs in stages of its own, in lanes (and `in_the_reader.hpp` moves maps only between stages
// of maps along time, so a stage gathering its coefficients before it, over [k, 1], stays its own). A named
// stand-in for the price of the work after its cycles (`trail.hpp`; `docs/sections-in-lanes.md`).
constexpr bool runs_skewed(const Gathering& at) { return at.open.cycle >= 0 && at.link_of[static_cast<std::size_t>(at.open.cycle)] >= 0; }

// Does the open stage fold a signal's voices, keeping time and dropping the voices (a bank's sum), on its own or in a
// bank's sample (4.3d)? It walks time a sample at a time, its lanes along the voices, so what runs over the fold's result
// runs once a sample at width one (Plan/23 04 4.3e). (A cycle that holds its fold is P10's, `folds_each_sample`.)
constexpr bool folds_voices(const Graph& g, const Gathering& at) {
    const graph::GroupShape& shape = at.open.shape;
    if (detail::folds_each_sample(at) || shape.fold < 0 || !graph::is_a_fold(g.nodes[static_cast<std::size_t>(shape.fold)]) || shape.domain.rank < 2) {
        return false;
    }
    const heinzel::Node& fold = g.nodes[static_cast<std::size_t>(shape.fold)];
    const bool keeps_time = (shape.collapsed & (std::uint32_t{1} << (shape.domain.rank - 1))) == 0;
    return keeps_time && !g.values[static_cast<std::size_t>(fold.inputs[0])].timeless && shape.folded.rank < shape.domain.rank;
}

} // namespace detail
} // namespace heinzel::loops::planner
