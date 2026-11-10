#pragma once

// A feedback cycle's chain, the cost model's measure of how long a sample waits on its own past (Plan/23 04, 4.3a): its
// longest path, in latencies, from a lag of a value it makes to the node making that value, over the lag's samples (a
// link of sections, which reads the element before, a lag of one step: `kinds.hpp` `is_a_link`). Worked out from the
// graph alone, so that the planner, which prices a stage by it (`planner/chains.hpp`), and the rewrite layer, which
// takes a rewrite on a cycle only where it does not lengthen it (`rewrite/priced.hpp`), measure alike. And a cycle's whole stage priced alone, from the graph (4.3b): what double state weighs a cycle's type by
// (`rewrite/double_state.hpp`). It names no operation: a node's latency is its rule-table class's (`schedule/cost.hpp`).

#include <bit>
#include <cstdint>
#include <limits>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/ir/timing.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/schedule/cost.hpp"
#include "heinzel/schedule/machine.hpp"

namespace heinzel::loops::graph {
namespace detail {

// What a node adds to a path through it: its latency. A fold in a cycle folds a feedback stage's voices every sample
// (Plan/23 10): its lane bodies added, then its lanes folded, as many lanes as the machine's vector holds of the cycle's
// `widest` element (as the planner lays them out, `planner/loops.hpp`). Voices no stage holds in a sample — not a
// literal count, no lanes, more than `kMostSampleBodies` bodies — never run so: their chain has no end.
constexpr double latency_of(const Graph& g, const heinzel::Node& n, std::int32_t widest, const Machine& m) {
    const double own = cost::of(n.op, g.values[static_cast<std::size_t>(n.output)].type, m).latency;
    if (!graph::is_a_fold(n)) {
        return own;
    }
    const Shape& operand = g.values[static_cast<std::size_t>(n.inputs[0])].shape;
    std::int64_t voices = 1;
    bool literal = true;
    for (std::int32_t a = 0; a < operand.rank; ++a) {
        const Extent e = extent_at(operand, a);
        literal = literal && !(graph::collapses(n, a) && !is_literal(e));
        voices *= graph::collapses(n, a) && is_literal(e) ? literal_of(e) : 1;
    }
    const std::int64_t lanes = m.vector_bytes / widest;
    const std::int64_t bodies = lanes > 1 ? (voices + lanes - 1) / lanes : 0;
    if (!literal || lanes < 2 || bodies > kMostSampleBodies) {
        return std::numeric_limits<double>::infinity();
    }
    return cost::lanes_folded(own, lanes, bodies, m);
}

// What reading operand `k` adds to a path through node `n`: a conversion where the type rule reads it in the type `n`
// makes and that is another (a condition is read as it is), costing what the conversion row's class costs.
constexpr double converted(const Graph& g, const heinzel::Node& n, std::size_t k, OpCode conversion, const Machine& m) {
    const TypeRule rule = rules_of(n.op).type;
    const ElemType made = g.values[static_cast<std::size_t>(n.output)].type;
    const bool promotes = rule == TypeRule::FromOperands || (rule == TypeRule::FromOperandsExceptFirst && k > 0);
    return promotes && g.values[static_cast<std::size_t>(n.inputs[k])].type != made ? cost::of(conversion, made, m).latency : 0;
}

// The conversion: the one row that converts its operand into the type its attribute states.
constexpr OpCode conversion_op() {
    for (std::size_t i = 0; i < kOpCount; ++i) {
        heinzel::Node n{};
        n.op = OpCode{static_cast<std::uint8_t>(i)};
        if (graph::converts(n)) {
            return n.op;
        }
    }
    return OpCode{};
}

// How many of node `n`'s operands, from the first, a path through it runs through, read at this sample: none of a
// lag's (it reads another sample), a link's operand 0 alone (its operand 1 is the element before, which the skewed
// stage made a step earlier).
constexpr std::size_t read_now(const heinzel::Node& n) {
    const bool lag = graph::hands_its_operand_on(n) && samples_back(n) > 0;
    return lag ? 0 : graph::is_a_link(n) ? 1 : rules_of(n.op).arity;
}

} // namespace detail

// Each node's cycle's chain (0 for a node of none). A path runs through what the cycle computes in the sample, a lag
// starting afresh (it reads another sample), an operand read in another type converted on the way. A link is a lag of
// one step: its path starts afresh there too, its own latency on it (the lanes shifted along the sections). One pass
// over a cycle's nodes, which `feedback.order` lists together in the order they run, for each lag or link of a value
// it makes.
constexpr std::vector<double> cycle_chains(const Graph& g, const Facts& facts, const Feedback& feedback, const Machine& m) {
    const std::size_t count = g.nodes.size();
    const auto cycle_at = [&](std::size_t q) { return feedback.cycle[static_cast<std::size_t>(feedback.order[q])]; };
    const auto maker_of = [&](std::int32_t v) { return facts.producer[static_cast<std::size_t>(facts.read_as[static_cast<std::size_t>(v)])]; };
    const auto lag = [&](std::size_t i) { return graph::hands_its_operand_on(g.nodes[i]) && samples_back(g.nodes[i]) > 0; };
    std::vector<double> chain(count, 0);
    std::vector<double> dist(count, -1);
    std::vector<std::int32_t> widest(count, 4);  // per cycle: its widest element
    std::vector<std::int32_t> first(count, -1);  // per cycle: its first position
    for (std::size_t q = 0; q < feedback.order.size(); ++q) {
        if (const std::int32_t c = cycle_at(q); c >= 0) {
            const std::int32_t bytes = loops::bytes_of(g.values[static_cast<std::size_t>(g.nodes[static_cast<std::size_t>(feedback.order[q])].output)].type);
            std::int32_t& wide = widest[static_cast<std::size_t>(c)];
            wide = bytes > wide ? bytes : wide;
            first[static_cast<std::size_t>(c)] = first[static_cast<std::size_t>(c)] < 0 ? static_cast<std::int32_t>(q) : first[static_cast<std::size_t>(c)];
        }
    }
    const OpCode conversion = detail::conversion_op();
    for (const std::int32_t li : feedback.order) {
        const std::size_t l = static_cast<std::size_t>(li);
        const std::int32_t c = feedback.cycle[l];
        const bool link = graph::is_a_link(g.nodes[l]);
        const std::int32_t maker = lag(l) ? maker_of(g.nodes[l].inputs[0]) : link ? maker_of(g.nodes[l].inputs[1]) : -1;
        if (c < 0 || maker < 0 || feedback.cycle[static_cast<std::size_t>(maker)] != c) {
            continue;
        }
        const std::size_t cycle = static_cast<std::size_t>(c);
        for (std::size_t q = static_cast<std::size_t>(first[cycle]); q < feedback.order.size() && cycle_at(q) == c; ++q) {
            const std::size_t i = static_cast<std::size_t>(feedback.order[q]);
            const heinzel::Node& n = g.nodes[i];
            double before = i == l ? 0 : -1;
            for (std::size_t k = 0; k < detail::read_now(n); ++k) {
                const std::int32_t p = maker_of(n.inputs[k]);
                const bool within = p >= 0 && feedback.cycle[static_cast<std::size_t>(p)] == c && dist[static_cast<std::size_t>(p)] >= 0;
                const double through = within ? dist[static_cast<std::size_t>(p)] + detail::converted(g, n, k, conversion, m) : -1;
                before = through > before ? through : before;
            }
            const bool runs = facts.statement[i] && !graph::is_generated(n) && (i != l || link);
            dist[i] = before < 0 ? -1 : before + (runs ? detail::latency_of(g, n, widest[cycle], m) : 0);
        }
        const double through = dist[static_cast<std::size_t>(maker)] / (link ? 1 : samples_back(g.nodes[l]));
        chain[cycle] = through > chain[cycle] ? through : chain[cycle];
    }
    std::vector<double> of(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        of[i] = feedback.cycle[i] >= 0 ? chain[static_cast<std::size_t>(feedback.cycle[i])] : 0;
    }
    return of;
}

namespace detail {

// The widest element node `n` makes (`reads` false) or reads or makes, in bytes.
constexpr std::int64_t widest_of(const Graph& g, const heinzel::Node& n, bool reads) {
    std::int64_t widest = loops::bytes_of(g.values[static_cast<std::size_t>(n.output)].type);
    for (std::size_t k = 0; k < rules_of(n.op).arity && reads; ++k) {
        const std::int64_t b = loops::bytes_of(g.values[static_cast<std::size_t>(n.inputs[k])].type);
        widest = b > widest ? b : widest;
    }
    return widest;
}

// The lane bodies a value of `shape` runs in a sample: its voices (its literal extents but time) over `lanes`; one for
// one voice.
constexpr double bodies_of(const Shape& shape, std::int64_t lanes) {
    std::int64_t voices = 1;
    for (std::int32_t a = 0; a + 1 < shape.rank; ++a) {
        voices *= is_literal(extent_at(shape, a)) ? literal_of(extent_at(shape, a)) : lanes;
    }
    return voices > 1 ? static_cast<double>((voices + lanes - 1) / lanes) : 1;
}

// Per cycle (by the node naming it): the lanes the machine's vector holds of the widest element it makes, which its
// voices run in, and of the widest it reads or makes, which a comb's samples run in; whether it holds voices.
struct Widths {
    std::vector<std::int64_t> voice_lanes;
    std::vector<std::int64_t> time_lanes;
    std::vector<char> voices;
};
constexpr Widths widths_of(const Graph& g, const Feedback& feedback, const Machine& m) {
    const std::size_t count = g.nodes.size();
    std::vector<std::int64_t> made(count, 4);
    std::vector<std::int64_t> any(count, 4);
    Widths w{std::vector<std::int64_t>(count, 1), std::vector<std::int64_t>(count, 1), std::vector<char>(count, 0)};
    for (std::size_t i = 0; i < count; ++i) {
        if (const std::int32_t c = feedback.cycle[i]; c >= 0) {
            const std::size_t at = static_cast<std::size_t>(c);
            made[at] = detail::widest_of(g, g.nodes[i], false) > made[at] ? detail::widest_of(g, g.nodes[i], false) : made[at];
            any[at] = detail::widest_of(g, g.nodes[i], true) > any[at] ? detail::widest_of(g, g.nodes[i], true) : any[at];
            w.voices[at] = w.voices[at] || g.values[static_cast<std::size_t>(g.nodes[i].output)].shape.rank > 1;
        }
    }
    for (std::size_t c = 0; c < count; ++c) {
        w.voice_lanes[c] = m.vector_bytes / made[c] > 1 ? m.vector_bytes / made[c] : 1;
        w.time_lanes[c] = m.vector_bytes / any[c] > 1 ? m.vector_bytes / any[c] : 1;
    }
    return w;
}

} // namespace detail

// Each cycle's stage priced alone, by its nodes (as `cycle_chains`), the counts the cost model prices a feedback stage
// by (`schedule/cost.hpp`): its chain; its depth, one sample's longest path; its slots, each node's in its lane bodies,
// a fold's across its lanes, a conversion's for each operand read in another type and for each conversion just after
// it of what it makes; a load of each value it reads from outside, a store; its voices in lane bodies (a link's k
// sections k voices), as many lanes as the machine's vector holds of its widest element; one voice whose nearest past is a vector's lanes back runs that
// many samples at once (`nearest_pasts`). The rewrite layer weighs a cycle's type by it (`rewrite/double_state.hpp`).
constexpr std::vector<cost::Stage> cycle_stages(const Graph& g, const Facts& facts, const Feedback& feedback, const Machine& m) {
    const std::size_t count = g.nodes.size();
    const auto maker_of = [&](std::int32_t v) { return facts.producer[static_cast<std::size_t>(facts.read_as[static_cast<std::size_t>(v)])]; };
    const auto cycle_of = [&](std::int32_t node) { return node >= 0 ? feedback.cycle[static_cast<std::size_t>(node)] : -1; };
    const std::vector<double> chains = graph::cycle_chains(g, facts, feedback, m);
    const std::vector<std::int32_t> nearest = graph::nearest_pasts(g, facts, feedback);
    const detail::Widths w = detail::widths_of(g, feedback, m);
    const OpCode conversion = detail::conversion_op();
    const double converts = cost::of(conversion, ElemType::F64, m).slots;
    std::vector<cost::Stage> stage(count);
    std::vector<double> path(count, 0);
    std::vector<double> slots(count, 0);
    std::vector<std::int32_t> loaded(g.values.size(), -1); // per value: the cycle loading it
    for (const std::int32_t ni : feedback.order) {
        const std::size_t i = static_cast<std::size_t>(ni);
        const heinzel::Node& n = g.nodes[i];
        const std::int32_t c = feedback.cycle[i];
        if (c < 0) { // (a conversion of what a cycle makes, just after it: the cycle's)
            if (const std::int32_t from = graph::converts(n) ? cycle_of(maker_of(n.inputs[0])) : -1; from >= 0) {
                const std::size_t at = static_cast<std::size_t>(from);
                slots[at] += converts * detail::bodies_of(g.values[static_cast<std::size_t>(n.output)].shape, w.voice_lanes[at]);
            }
            continue;
        }
        const std::size_t at = static_cast<std::size_t>(c);
        const std::int64_t lanes = w.voice_lanes[at];
        const double bodies = detail::bodies_of(g.values[static_cast<std::size_t>(n.output)].shape, lanes);
        const bool runs = facts.statement[i] && !graph::is_generated(n);
        double before = 0;
        for (std::size_t k = 0; k < detail::read_now(n); ++k) {
            const std::int32_t p = maker_of(n.inputs[k]);
            const std::int32_t v = facts.read_as[static_cast<std::size_t>(n.inputs[k])];
            const bool constant = g.values[static_cast<std::size_t>(v)].category == ValueCategory::Const;
            const double through = constant ? 0 : detail::converted(g, n, k, conversion, m);
            slots[at] += through > 0 ? converts * bodies : 0;
            if (cycle_of(p) == c) {
                before = path[static_cast<std::size_t>(p)] + through > before ? path[static_cast<std::size_t>(p)] + through : before;
            } else if (!constant && loaded[static_cast<std::size_t>(v)] != c) {
                loaded[static_cast<std::size_t>(v)] = c;
                stage[at].loads += 1;
            }
        }
        path[i] = before + (runs ? detail::latency_of(g, n, static_cast<std::int32_t>(m.vector_bytes / lanes), m) : 0);
        stage[at].depth = path[i] > stage[at].depth ? path[i] : stage[at].depth;
        if (runs && graph::is_a_fold(n)) { // (its operand's bodies added, then log2(lanes) steps of a shuffle and an add)
            const double steps = static_cast<double>(std::bit_width(static_cast<std::uint64_t>(lanes)) - 1);
            slots[at] += detail::bodies_of(g.values[static_cast<std::size_t>(n.inputs[0])].shape, lanes) - 1 + 2 * steps;
        } else {
            slots[at] += runs ? cost::of(n.op, g.values[static_cast<std::size_t>(n.output)].type, m).slots * bodies : 0;
        }
        stage[at].bodies = bodies > stage[at].bodies ? bodies : stage[at].bodies;
    }
    for (std::size_t c = 0; c < count; ++c) {
        cost::Stage& s = stage[c];
        s.chain = chains[c], s.slots = slots[c] / s.bodies, s.stores = 1;
        s.lanes = w.voices[c] || nearest[c] < w.time_lanes[c] ? 1 : static_cast<double>(w.time_lanes[c]);
    }
    return stage;
}

// Per node, the latest node of any cycle it reads this sample through cheap work (-1: none): through no lag and no
// expensive map, as the planner chains cycles into one stage (`planner/chains.hpp` `find_chains`, Decision 65).
constexpr std::vector<std::int32_t> made_now(const Graph& g, const Facts& facts, const Feedback& feedback) {
    std::vector<std::int32_t> made(g.nodes.size(), -1);
    for (std::size_t i = 0; i < g.nodes.size(); ++i) { // (what a node reads but through a lag was made before it)
        const heinzel::Node& n = g.nodes[i];
        const bool lag = graph::hands_its_operand_on(n) && samples_back(n) > 0;
        const bool cheap = graph::hands_its_operand_on(n) || (graph::is_a_plain_map(n) && rules_of(n.op).cost == CostClass::Cheap);
        for (std::size_t k = 0; k < rules_of(n.op).arity && cheap && !lag && feedback.cycle[i] < 0; ++k) {
            const std::int32_t q = facts.producer[static_cast<std::size_t>(facts.read_as[static_cast<std::size_t>(n.inputs[k])])];
            const std::int32_t then = q >= 0 && static_cast<std::size_t>(q) < i ? made[static_cast<std::size_t>(q)] : -1;
            made[i] = then > made[i] ? then : made[i];
        }
        made[i] = feedback.cycle[i] >= 0 && !lag ? static_cast<std::int32_t>(i) : made[i];
    }
    return made;
}

} // namespace heinzel::loops::graph
