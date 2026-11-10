#pragma once

// Chaining feedback cycles onto one stage (Decision 65): the nodes that lead from one cycle into the next, what bounds
// a merge, and where in such a stage a one-sample lag of what it makes runs. The planner's first pass (`stages.hpp`)
// asks it while it gathers. It names no operation.

#include <bit>
#include <cstdint>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/ir/timing.hpp"
#include "heinzel/loops/graph/cycle_chains.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/graph/staging.hpp"
#include "heinzel/loops/planner/gathering.hpp"
#include "heinzel/loops/program.hpp"

namespace heinzel::loops::planner::detail {

// What node `n` costs on machine `m`, computed in the type it makes.
constexpr OpCost cost_of(const Graph& g, const heinzel::Node& n, const Machine& m) {
    return cost::of(n.op, g.values[static_cast<std::size_t>(n.output)].type, m);
}

// The nodes that lead into a feedback cycle through cheap work only: a cheap plain map or a hand-on, read by a cycle's
// node or by another such node. Between two cycles they may run in the cycles' stage, a sample at a time, where its
// window hides their latency, or apart in lanes; the cost model decides (`cheaper_joined`; ten maps deep stood in for
// it before, Decision 65). Each is marked with the cycle it leads into (`target`), and each cycle with the slots, the
// depth, in latencies, and the widest element made of what leads into it. A node that leads into several cycles (an
// allpass's `-in` skips its cycle, so its input reaches every later one) is marked with the nearest, the first in
// order. One pass forward for the depths, one back for the marks, since every reader outside a cycle comes after what
// it reads (and its mark is final by then), one to count each cycle's.
constexpr void find_chains(const Graph& g, const graph::Feedback& feedback, Gathering& at) {
    const std::size_t count = g.nodes.size();
    std::vector<double> depth(count, 0);
    std::vector<char> kind(count, 0); // a cheap plain map or a hand-on, outside every cycle
    for (std::size_t i = 0; i < count; ++i) {
        const heinzel::Node& n = g.nodes[i];
        const bool map = graph::is_a_plain_map(n) && rules_of(n.op).cost == CostClass::Cheap;
        // (work gathered ahead, `next_unit`, runs in a stage of its own before every loop: it leads into no cycle's stage,
        // as in the rewrite layer's mirror, `graph::detail::leads_into`, which prices work marked here that makes
        // another shape than the cycle's stage walks, which `joins` keeps out of it, in a stage of its own)
        kind[i] = feedback.cycle[i] < 0 && at.ahead[i] < 0 && (map || graph::hands_its_operand_on(n));
        if (kind[i] == 0 || (at.lags[i] && samples_back(n) > 0)) {
            continue; // (a lag reads the sample before: a run through it starts afresh)
        }
        for (std::size_t k = 0; k < kMaxNodeInputs; ++k) {
            const std::int32_t m = at.maker[i][k];
            if (m >= 0 && kind[static_cast<std::size_t>(m)]) {
                depth[i] = depth[static_cast<std::size_t>(m)] > depth[i] ? depth[static_cast<std::size_t>(m)] : depth[i];
            }
        }
        depth[i] += map ? cost_of(g, n, at.machine).latency : 0;
    }
    for (std::size_t r = count; r-- > 0;) {
        const std::int32_t into = feedback.cycle[r] >= 0 ? feedback.cycle[r] : at.target[r];
        for (std::size_t k = 0; k < kMaxNodeInputs && into >= 0; ++k) {
            const std::int32_t maker = at.maker[r][k];
            const std::size_t m = static_cast<std::size_t>(maker);
            if (maker >= 0 && kind[m] && (at.target[m] < 0 || into < at.target[m])) {
                at.target[m] = into;
            }
        }
    }
    for (std::size_t m = 0; m < count; ++m) {
        if (const std::int32_t into = at.target[m]; into >= 0) {
            const std::size_t c = static_cast<std::size_t>(into);
            at.leads[c] += at.runs[m] ? 1 : 0;
            at.lead_states[c] += at.unit_states[static_cast<std::size_t>(at.unit_of[m])];
            at.lead_slots[c] += at.runs[m] ? cost_of(g, g.nodes[m], at.machine).slots : 0;
            const std::int32_t bytes = at.runs[m] ? loops::bytes_of(g.values[static_cast<std::size_t>(g.nodes[m].output)].type) : 0;
            at.lead_bytes[c] = bytes > at.lead_bytes[c] ? bytes : at.lead_bytes[c];
            at.lead_depth[c] = depth[m] > at.lead_depth[c] ? depth[m] : at.lead_depth[c];
        }
    }
}

// Each unit's own chain, depth and slots, for the cost model, and its widest element made, for the lanes of a stage
// that takes it (`sample_fits`): a node's are its own; a cycle's chain is the cost model's (`graph::cycle_chains`), and
// its depth its longest path. The paths are within the unit, whose nodes `feedback.order` lists in the order they run.
constexpr void price_units(const Graph& g, const graph::Facts& facts, const graph::Feedback& feedback, Gathering& at) {
    std::vector<double> path(g.nodes.size(), 0);
    for (std::size_t q = 0; q < feedback.order.size(); ++q) {
        const std::size_t i = static_cast<std::size_t>(feedback.order[q]);
        const std::size_t u = static_cast<std::size_t>(at.unit_of[i]);
        const OpCost c = at.runs[i] ? cost_of(g, g.nodes[i], at.machine) : OpCost{0, 0};
        double before = 0;
        for (std::size_t k = 0; k < rules_of(g.nodes[i].op).arity && !at.lags[i]; ++k) {
            const std::int32_t m = facts.producer[static_cast<std::size_t>(facts.read_as[static_cast<std::size_t>(g.nodes[i].inputs[k])])];
            before = m >= 0 && at.unit_of[static_cast<std::size_t>(m)] == at.unit_of[i] && path[static_cast<std::size_t>(m)] > before
                         ? path[static_cast<std::size_t>(m)] : before;
        }
        path[i] = before + c.latency;
        at.unit_depth[u] = path[i] > at.unit_depth[u] ? path[i] : at.unit_depth[u];
        at.unit_slots[u] += c.slots;
        const std::int32_t bytes = at.runs[i] ? loops::bytes_of(g.values[static_cast<std::size_t>(g.nodes[i].output)].type) : 0;
        at.unit_bytes[u] = bytes > at.unit_bytes[u] ? bytes : at.unit_bytes[u];
    }
    const std::vector<double> chain = graph::cycle_chains(g, facts, feedback, at.machine);
    const std::vector<std::int32_t> nearest = graph::nearest_pasts(g, facts, feedback);
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        if (feedback.cycle[i] >= 0) {
            at.unit_chain[static_cast<std::size_t>(at.unit_of[i])] = chain[i];
            at.unit_lag[static_cast<std::size_t>(at.unit_of[i])] = nearest[i];
        }
    }
}

// In a feedback stage, a lag of what the stage makes earlier runs just before the node making it, where it is one of a
// run of lags 1, …, k of that value in the stage. The lowering then hands the value on in registers, as it hands on a
// cycle's own state (`loops/normalize.hpp` `find_state`), where read after its maker it would be stored and loaded
// back samples later; a lag past a gap is read from memory either way and stays. Between cycles, clang joined two such
// loads of adjacent samples into one wider load, which the store of the sample before cannot forward to (a
// three-section cascade over a run-time length 6.7 → 2.9 ns a sample on zen2, bit for bit); inside a cycle, a lag of a
// lag (`y[n-1][n-1]`, Decision 66) put the store-to-load path on the cycle's chain (a biquad 2.55 → 1.41 ns). A lag
// that closes a cycle reads a node after it and stays. Its readers stay after it. Two passes to find the lags and one
// to lay the stage out again.
constexpr void lags_before_their_makers(const Graph& g, Gathering& at) {
    auto& nodes = at.open.stage.nodes;
    if (at.open.cycle < 0) {
        return;
    }
    std::vector<std::int32_t> at_position(g.nodes.size(), -1);
    std::vector<std::int32_t> first(g.nodes.size(), -1);
    std::vector<std::int32_t> next(g.nodes.size(), -1);
    std::vector<std::int32_t> last(g.nodes.size(), -1);
    std::vector<char> moves(g.nodes.size(), 0);
    std::vector<std::uint64_t> backs(g.nodes.size(), 0); // per maker: bit d − 1 for a lag of d samples the stage reads
    for (const std::int32_t ni : nodes) {
        const std::size_t i = static_cast<std::size_t>(ni);
        const std::int32_t d = samples_back(g.nodes[i]);
        if (at.lags[i] && at.storage_of[i] >= 0 && d >= 1 && d <= kMaxStateDepth) {
            backs[static_cast<std::size_t>(at.storage_of[i])] |= std::uint64_t{1} << (d - 1);
        }
    }
    for (std::size_t k = 0; k < nodes.size(); ++k) {
        const std::size_t i = static_cast<std::size_t>(nodes[k]);
        at_position[i] = static_cast<std::int32_t>(k);
        const std::int32_t maker = at.storage_of[i];
        const bool in_a_run = maker >= 0 && samples_back(g.nodes[i]) <= std::countr_one(backs[static_cast<std::size_t>(maker)]);
        if (at.lags[i] && in_a_run && at_position[static_cast<std::size_t>(maker)] >= 0) {
            moves[i] = 1; // (its maker stands before it: queue it there, in order)
            std::int32_t& tail = last[static_cast<std::size_t>(maker)];
            (tail < 0 ? first[static_cast<std::size_t>(maker)] : next[static_cast<std::size_t>(tail)]) = static_cast<std::int32_t>(i);
            tail = static_cast<std::int32_t>(i);
        }
    }
    auto laid = nodes;
    laid.clear();
    for (const std::int32_t ni : nodes) {
        for (std::int32_t lag = first[static_cast<std::size_t>(ni)]; lag >= 0; lag = next[static_cast<std::size_t>(lag)]) {
            laid.push_back(lag);
        }
        if (moves[static_cast<std::size_t>(ni)] == 0) {
            laid.push_back(ni);
        }
    }
    nodes = laid;
}

// A feedback stage whose sample folds its voices (Plan/23 10; a bank's sum joined to it, 4.3d) runs in three parts
// (`loops/normalize.hpp` `sample_tree`): what runs over the fold's result and does not read it in its sample (a phase
// beside the bank, 4.3g), then what runs over its operand (the fold among them), then the rest over its result. A stable
// partition, which every read keeps in order: the cycle's facts let nothing over the operand read the fold's result in
// its sample (`graph::find_feedback`), and a fold after the bank reads only what the bank made.
constexpr void in_three_parts(const Graph& g, const graph::Facts& facts, Gathering& at) {
    if (at.open.cycle < 0 || at.open.shape.fold < 0) {
        return;
    }
    auto& nodes = at.open.stage.nodes;
    const std::int32_t fold = at.open.shape.fold;
    const Shape& result = g.values[static_cast<std::size_t>(g.nodes[static_cast<std::size_t>(fold)].output)].shape;
    std::vector<char> after(g.nodes.size(), 0);
    std::vector<std::int32_t> part(nodes.size(), 1);
    for (std::size_t k = 0; k < nodes.size(); ++k) { // (in the stage's order, every node after what it reads now)
        const std::size_t i = static_cast<std::size_t>(nodes[k]);
        after[i] = nodes[k] == fold;
        for (std::size_t o = 0; o < rules_of(g.nodes[i].op).arity && !at.lags[i]; ++o) {
            const std::int32_t m = facts.producer[static_cast<std::size_t>(detail::source_of(g, facts, i, o))];
            after[i] = after[i] || (m >= 0 && after[static_cast<std::size_t>(m)]);
        }
        part[k] = nodes[k] == fold || g.values[static_cast<std::size_t>(g.nodes[i].output)].shape != result ? 1 : after[i] ? 2 : 0;
    }
    auto laid = nodes;
    laid.clear();
    for (const std::int32_t which : {0, 1, 2}) {
        for (std::size_t k = 0; k < nodes.size(); ++k) {
            if (part[k] == which) {
                laid.push_back(nodes[k]);
            }
        }
    }
    nodes = laid;
}

// The lanes the open stage runs a sample's voices in, joined by work making elements `bytes` wide: as many as the
// machine's vector holds of its widest element made (`planner/loops.hpp` `widest_element`).
constexpr std::int64_t voice_lanes(const Graph& g, const Gathering& at, std::int64_t bytes = 4) {
    std::int64_t widest = bytes > 4 ? bytes : 4;
    for (const std::int32_t ni : at.open.stage.nodes) {
        const std::int64_t b = loops::bytes_of(g.values[static_cast<std::size_t>(g.nodes[static_cast<std::size_t>(ni)].output)].type);
        widest = b > widest ? b : widest;
    }
    return at.machine.vector_bytes / widest > 1 ? at.machine.vector_bytes / widest : 1;
}

// Does the open stage keep a sample the planner lays out once work making elements `bytes` wide joins it? A feedback
// stage that folds its voices takes one block of lane bodies or none (`plan_feedback_loops`), so every way into one
// asks: a cycle chaining on with the work into it (`chains_on`), the work after its cycles (`cheapest_cut`), its feed
// (`as_it_closes`); the fold that makes one asks `graph::fits_a_block` itself (`fold_joins`).
constexpr bool sample_fits(const Graph& g, const Gathering& at, std::int64_t bytes) {
    if (at.open.cycle < 0 || at.open.shape.fold < 0) {
        return true;
    }
    const std::int64_t lanes = detail::voice_lanes(g, at, bytes);
    return lanes > 1 && graph::fits_a_block(extent_at(at.open.shape.domain, 0), lanes);
}

// The lane bodies the open stage will run a sample's voices in (one for a single voice); a symbolic count of voices
// counts as a stage's worth of work (`Machine::merge_work`).
constexpr std::int64_t bodies_of(const Graph& g, const Gathering& at) {
    const Shape& domain = at.open.shape.domain;
    std::int64_t voices = 1;
    for (std::int32_t a = 0; a + 1 < domain.rank; ++a) {
        const Extent e = extent_at(domain, a);
        voices *= is_literal(e) ? literal_of(e) : at.machine.merge_work;
    }
    const std::int64_t lanes = detail::voice_lanes(g, at);
    return (voices + lanes - 1) / lanes;
}

// What the unit at position `p` of `feedback.order` loads that the open stage loads not yet: each value it reads from
// outside the stage, a constant apart (an immediate), once.
constexpr std::int32_t new_loads(const Graph& g, const graph::Facts& facts, const graph::Feedback& feedback, std::size_t p, const Gathering& at) {
    std::vector<std::int32_t> seen{};
    const std::int32_t unit = static_cast<std::int32_t>(p);
    for (std::size_t q = p; q < feedback.order.size() && at.unit_of[static_cast<std::size_t>(feedback.order[q])] == unit; ++q) {
        const std::size_t i = static_cast<std::size_t>(feedback.order[q]);
        for (std::size_t k = 0; k < rules_of(g.nodes[i].op).arity && at.runs[i]; ++k) {
            const std::int32_t v = detail::source_of(g, facts, i, k);
            bool counted = at.loaded[static_cast<std::size_t>(v)] || at.made_in[static_cast<std::size_t>(v)] == kOpen ||
                           g.values[static_cast<std::size_t>(v)].category == ValueCategory::Const;
            for (const std::int32_t w : seen) {
                counted = counted || w == v;
            }
            if (!counted) {
                seen.push_back(v);
            }
        }
    }
    return static_cast<std::int32_t>(seen.size());
}

// Does the open feedback stage still fit with the `nodes` and `states` the unit at `p` adds? Its work a sample (nodes
// times lane bodies) within `Machine::merge_work`; its handed-on states times its bodies within the machine's registers
// (beyond, they spill: a bank of 32 voices in eight sections ran 1.16-1.26x slower merged, the review of Decision 65);
// its nodes and its statements (nodes and loads) within a stage's capacity. The first two stand in for the cost model
// (Plan/23 04 4.3c, `cost_corpus18.cpp`): no count of registers the model could add fits the measured points, and the
// model alone merges wide banks into stages 1.12-1.14x slower than the work bound cuts them.
constexpr bool still_fits(const Graph& g, const graph::Facts& facts, const graph::Feedback& feedback, std::size_t p, std::int32_t nodes,
                          std::int32_t states, const Gathering& at) {
    const std::int64_t bodies = detail::bodies_of(g, at);
    const std::int64_t size = static_cast<std::int64_t>(at.open.stage.nodes.size()) + nodes;
    const std::int64_t loads = static_cast<std::int64_t>(at.loaded_now.size()) + detail::new_loads(g, facts, feedback, p, at);
    return size * bodies <= at.machine.merge_work && (at.states + states) * bodies <= at.machine.registers &&
           size <= static_cast<std::int64_t>(graph::kMaxGroupStmts) && size + loads <= static_cast<std::int64_t>(kMaxStageStmts);
}

// How many samples of each value's past the open feedback stage hands on in registers: as many as keep all its state,
// every value's run of lags cut at that depth, times its lane bodies, within half the machine's registers, the rest
// for the sample's working set; at least the newest, the cycle's own chain. A second slot pays in a light stage, where
// the load of `y[n-2]` would be on the chain (a biquad 2.40 -> 1.58 ms), and costs in a heavy one, where the load has
// slack and the register is wanted (eight sections 9.4 -> 7.8 ns a sample, 32 voices of four 18.4 -> 15.5, four
// sections alike; `Plan/probes/state-depth`). No depth where it would cut no run. Half the registers is a stand-in for
// the cost model, measured (Plan/23 04 4.3c): a spill term priced on the registers held picks no better, since stages
// holding as many registers fare opposite ways with a second slot (JUCE's EQ over 16 channels 0.90x, 16 voices of six
// sections in two stages 1.05-1.07x). A skewed stage cuts no run: it reads nothing along time from memory but at one
// lane (`loops/validate.hpp` `check_links`), and eight third-order sections cut at two fell back to the graph as written.
constexpr std::int32_t state_depth(const Graph& g, const Gathering& at) {
    if (detail::runs_skewed(at)) {
        return kMaxStateDepth;
    }
    const std::int64_t bodies = detail::bodies_of(g, at);
    const std::int64_t room = at.machine.registers / 2;
    for (std::int32_t depth = 1; depth < kMaxStateDepth; ++depth) {
        std::int64_t held = 0; // (the state at one sample deeper)
        bool longer = false;
        for (const std::int32_t m : at.open.stage.nodes) {
            const std::int32_t run = at.slots[static_cast<std::size_t>(m)];
            held += run < depth + 1 ? run : depth + 1;
            longer = longer || run > depth;
        }
        if (!longer) {
            break;
        }
        if (held * bodies > room) {
            return depth;
        }
    }
    return kMaxStateDepth;
}

// Is the open feedback stage cheaper with cycle `c`, whose unit starts at position `p` of `feedback.order`, and the
// cheap work leading into it not gathered yet joined, than closed before them, the work in lanes and the cycle in a
// stage of its own, each handing its value on (a store and a load)? The cost model's question (`schedule/cost.hpp`),
// answered once per cycle and open stage, so that the work into a cycle joins whole or not at all.
// The lanes along time a stage of the open stage's voices runs in, its cycles reading their own past no nearer than
// `lag` and its widest element `bytes` wide: one voice whose cycles read no nearer than a vector's lanes runs that many
// samples at once (a comb's, an allpass's), voices in lanes or a nearer past one at a time.
constexpr double lanes_along_time(std::int32_t lag, std::int64_t bytes, const Gathering& at) {
    const std::int64_t vector = at.machine.vector_bytes / bytes > 1 ? at.machine.vector_bytes / bytes : 1;
    return at.open.shape.domain.rank > 1 || lag < vector ? 1.0 : static_cast<double>(vector);
}

// The widest element node `i` reads or makes, in bytes: a conversion of a double runs in a double's lanes.
constexpr std::int64_t widest_of(const Graph& g, std::size_t i) {
    std::int64_t widest = loops::bytes_of(g.values[static_cast<std::size_t>(g.nodes[i].output)].type);
    for (std::size_t k = 0; k < rules_of(g.nodes[i].op).arity; ++k) {
        const std::int64_t b = loops::bytes_of(g.values[static_cast<std::size_t>(g.nodes[i].inputs[k])].type);
        widest = b > widest ? b : widest;
    }
    return widest;
}

// The open stage's counts, priced: its loads (each value from outside once), a store, its lane bodies and lanes.
constexpr cost::Stage priced_open(const Graph& g, const Gathering& at) {
    std::int64_t widest = 4;
    for (const std::int32_t ni : at.open.stage.nodes) {
        const std::int64_t b = detail::widest_of(g, static_cast<std::size_t>(ni));
        widest = b > widest ? b : widest;
    }
    cost::Stage open = at.open_cost;
    open.loads = static_cast<double>(at.loaded_now.size()), open.stores = 1, open.bodies = static_cast<double>(detail::bodies_of(g, at));
    open.lanes = detail::lanes_along_time(at.open_lag, widest, at);
    if (detail::folds_voices(g, at)) { // (once a sample: its lanes folded, log2(lanes) steps of a shuffle and an add, and what follows)
        // Voices fewer than a lane fill none: the planner walks them one at a time (`planner/loops.hpp` `put_the_lanes`).
        const std::int64_t lanes = detail::voice_lanes(g, at);
        std::int64_t voices = 1;
        for (std::int32_t a = 0; a < at.open.shape.domain.rank; ++a) {
            const Extent e = extent_at(at.open.shape.domain, a);
            voices *= (at.open.shape.collapsed >> a & 1u) == 0 ? 1 : is_literal(e) ? literal_of(e) : lanes;
        }
        const bool scalar = voices < lanes;
        const double steps = scalar ? 0 : static_cast<double>(std::bit_width(static_cast<std::uint64_t>(lanes)) - 1);
        open.bodies = scalar ? static_cast<double>(voices) : open.bodies;
        open.slots += (at.once_slots + 2 * steps) / open.bodies - at.once_slots;
        open.depth += steps * (at.machine.shuffle.latency + at.machine.add.latency);
    }
    return open;
}

constexpr bool cheaper_joined(const Graph& g, std::size_t p, std::int32_t c, const Gathering& at) {
    const std::size_t lead = static_cast<std::size_t>(c);
    if (at.asked_by[lead] == at.open.stage.nodes[0]) {
        return at.answer[lead] != 0;
    }
    const cost::Stage open = detail::priced_open(g, at);
    const double bodies = open.bodies;
    const double slots = at.lead_slots[lead];
    const bool voices = at.open.shape.domain.rank > 1;
    const auto lanes = [&](std::int32_t lag) { return detail::lanes_along_time(lag, 4, at); }; // (a float's lanes)
    cost::Stage joined = open;
    joined.chain = at.unit_chain[p] > open.chain ? at.unit_chain[p] : open.chain;
    joined.depth = open.depth + at.lead_depth[lead] + at.unit_depth[p];
    joined.slots += slots + at.unit_slots[p];
    joined.lanes = lanes(at.unit_lag[p] < at.open_lag ? at.unit_lag[p] : at.open_lag);
    const cost::Stage unit{.chain = at.unit_chain[p], .depth = at.unit_depth[p], .slots = at.unit_slots[p], .loads = 1, .stores = 1, .bodies = bodies,
                           .lanes = lanes(at.unit_lag[p])};
    const cost::Stage work{.slots = slots, .loads = 1, .stores = 1, .bodies = voices ? bodies : 1, .lanes = lanes(kNoLag)};
    const double closed = cost::cycles(open, at.machine) + (slots > 0 ? cost::cycles(work, at.machine) : 0) + cost::cycles(unit, at.machine);
    const bool yes = cost::cycles(joined, at.machine) <= closed;
    at.asked_by[lead] = at.open.stage.nodes[0];
    at.answer[lead] = yes;
    return yes;
}

// Could cycle `c` follow a chain node into the open stage? Only if it waits on nothing but what has run and the chain
// nodes that lead into it (whose own operands are held to the same): else the node would give up its lanes, stored for
// a stage after it, for nothing.
constexpr bool could_follow(const graph::Feedback& feedback, std::int32_t c, const Gathering& at) {
    std::vector<std::int32_t> waiting{};
    const std::int32_t unit = at.unit_of[static_cast<std::size_t>(c)];
    for (std::size_t q = static_cast<std::size_t>(unit); q < feedback.order.size() && at.unit_of[static_cast<std::size_t>(feedback.order[q])] == unit; ++q) {
        waiting.push_back(feedback.order[q]);
    }
    for (std::size_t w = 0; w < waiting.size(); ++w) { // (a chain node joins `waiting` once: it is marked once)
        for (std::size_t k = 0; k < kMaxNodeInputs; ++k) {
            const std::int32_t m = at.maker[static_cast<std::size_t>(waiting[w])][k];
            if (m < 0 || at.visited[static_cast<std::size_t>(m)]) {
                continue;
            }
            if (at.target[static_cast<std::size_t>(m)] != c) {
                return false;
            }
            bool queued = false;
            for (const std::int32_t x : waiting) {
                queued = queued || x == m;
            }
            if (!queued) {
                waiting.push_back(m);
            }
        }
    }
    return true;
}

// Does node `i` read what the open stage makes?
constexpr bool reads_the_open(const Graph& g, const graph::Facts& facts, std::size_t i, const Gathering& at) {
    for (std::size_t k = 0; k < rules_of(g.nodes[i].op).arity; ++k) {
        if (at.made_in[static_cast<std::size_t>(detail::source_of(g, facts, i, k))] == kOpen) {
            return true;
        }
    }
    return false;
}

// Does the open feedback stage keep one later stage reading what it stores, with cycle `c` chained on at node `i`? A
// cycle that reads nothing of the stage is taken on only where it hands its values on, through memory, to the cycle the
// stage's cycles hand theirs to, or either hands them to none (`graph::handed_to`): else the stage stores values for two
// later stages and loses the band that would keep them for the next. Freeverb's two rooms, their input shared, were cut at the work
// bound with three of the right room's combs among the left's: 30.4 ns a sample, the rooms apart 19.0 (zen2,
// `Plan/probes/chain-merge`).
constexpr bool keeps_one_reader(const Graph& g, const graph::Facts& facts, const graph::Feedback& feedback, std::size_t i, std::int32_t c,
                                const Gathering& at) {
    const std::int32_t to = at.hands_to[static_cast<std::size_t>(c)];
    if (to < 0 || at.open_hands_to < 0 || to == at.open_hands_to || detail::reads_the_open(g, facts, i, at)) {
        return true;
    }
    const std::int32_t unit = at.unit_of[static_cast<std::size_t>(c)];
    for (std::size_t q = static_cast<std::size_t>(unit); q < feedback.order.size() && at.unit_of[static_cast<std::size_t>(feedback.order[q])] == unit; ++q) {
        if (detail::reads_the_open(g, facts, static_cast<std::size_t>(feedback.order[q]), at)) {
            return true;
        }
    }
    return false;
}

// Does node `i` chain a later cycle onto the open feedback stage? A node that leads into a cycle through cheap work
// (`find_chains`) does where that cycle could follow it; the first node of a cycle does with its whole cycle. Either
// only where the stage still fits and the cost model prices it no dearer joined (`cheaper_joined`), whether the cycle
// reads the stage or hands it nothing: independent recurrences in one stage overlap their chains (Plan/23 04 4.3g,
// `Plan/probes/audio-iir/cost_corpus14.cpp`). The stage then computes what the split stages would have, in the same
// order, one sample at a time. A cycle that folds its voices every sample (Plan/23 10) joins nothing, so it never
// follows, and neither does the work leading into it: else that work would give up its lanes for nothing (a one-pole,
// 13 to 17 cheap maps and `eq8fb`'s loop: 1.25-1.39x). Nor does a cycle holding a link, which runs skewed in a stage of
// its own (`runs_skewed`).
constexpr bool chains_on(const Graph& g, const graph::Facts& facts, const graph::Feedback& feedback, std::size_t i, const Gathering& at) {
    const std::int32_t c = feedback.cycle[i] >= 0 ? feedback.cycle[i] : at.target[i]; // (its cycle, or the one it leads into)
    const bool alone = c >= 0 && (at.fold_of[static_cast<std::size_t>(c)] >= 0 || at.link_of[static_cast<std::size_t>(c)] >= 0);
    if (at.open.cycle < 0 || alone || detail::folds_each_sample(at) || detail::runs_skewed(at)) { // (stand-ins for the cost model)
        return false;
    }
    const std::size_t p = static_cast<std::size_t>(at.unit_of[i]);
    if (feedback.cycle[i] < 0) {
        // (the node, the chain nodes after it and the cycle they lead into, all of them: so that where the first
        // fits, the rest does, and no node joins that its cycle could not follow)
        if (c < 0) {
            return false;
        }
        const std::size_t into = static_cast<std::size_t>(at.unit_of[static_cast<std::size_t>(c)]);
        const std::int64_t lead = at.lead_bytes[static_cast<std::size_t>(c)];
        const std::int64_t bytes = lead > at.unit_bytes[into] ? lead : at.unit_bytes[into];
        return detail::could_follow(feedback, c, at) && detail::keeps_one_reader(g, facts, feedback, i, c, at) && detail::sample_fits(g, at, bytes) &&
               detail::still_fits(g, facts, feedback, into, at.leads[static_cast<std::size_t>(c)] + at.unit_runs[into],
                                  at.lead_states[static_cast<std::size_t>(c)] + at.unit_states[into], at) &&
               detail::cheaper_joined(g, into, c, at);
    }
    return detail::keeps_one_reader(g, facts, feedback, i, c, at) && detail::sample_fits(g, at, at.unit_bytes[p]) &&
           detail::still_fits(g, facts, feedback, p, at.unit_runs[p], at.unit_states[p], at) && detail::cheaper_joined(g, p, c, at);
}

} // namespace heinzel::loops::planner::detail
