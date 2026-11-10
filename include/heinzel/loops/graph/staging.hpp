#pragma once

// The cost model's staging of feedback cycles: the stages the planner will put a graph's cycles in
// (`detail::stages_of`, the planner's `planner/stages.hpp` mirrored), each priced from the graph (`feedback_cycles`),
// and the groups of cycles priced apart (`linked_cycles`). What the rewrite layer prices a rewrite of feedback by
// (`rewrite/feedback_sums.hpp`, `rewrite/sections_in_lanes.hpp`). It names no operation.

#include <cstdint>
#include <limits>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/ir/timing.hpp"
#include "heinzel/loops/graph/cycle_chains.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/schedule/cost.hpp"
#include "heinzel/schedule/machine.hpp"

namespace heinzel::loops::graph {

// Per cycle, the cycle it hands its values on to through memory (-1: none): the first, in the graph's order, that reads
// one of them through a lag, directly or through the cheap work leading into it (`target`: the planner's `find_chains`,
// the model's `detail::leads_into`); none where a cycle reads one of them this sample (`made_now`), which chains on
// as it reads the stage. A feedback stage whose cycles hand on to one cycle stores what one later stage reads, which a
// band can keep (`planner/bands.hpp` `linked`). One pass.
constexpr std::vector<std::int32_t> handed_to(const Graph& g, const Facts& facts, const Feedback& feedback, const std::vector<std::int32_t>& target) {
    const std::vector<std::int32_t> made = graph::made_now(g, facts, feedback);
    std::vector<std::int32_t> to(g.nodes.size(), -1);
    std::vector<char> now(g.nodes.size(), 0);
    for (std::size_t r = 0; r < g.nodes.size(); ++r) {
        const std::int32_t into = feedback.cycle[r] >= 0 ? feedback.cycle[r] : target[r];
        for (std::size_t k = 0; k < rules_of(g.nodes[r].op).arity && into >= 0; ++k) {
            const std::int32_t m = facts.producer[static_cast<std::size_t>(facts.read_as[static_cast<std::size_t>(g.nodes[r].inputs[k])])];
            const std::int32_t from = m >= 0 ? feedback.cycle[static_cast<std::size_t>(m)] : -1;
            if (from >= 0 && from != into) {
                std::int32_t& first = to[static_cast<std::size_t>(from)];
                first = made[static_cast<std::size_t>(m)] < 0 && (first < 0 || into < first) ? into : first;
                now[static_cast<std::size_t>(from)] |= made[static_cast<std::size_t>(m)] >= 0 ? 1 : 0;
            }
        }
    }
    for (std::size_t c = 0; c < g.nodes.size(); ++c) {
        to[c] = now[c] != 0 ? -1 : to[c];
    }
    return to;
}

namespace detail {

// Per node outside every cycle, the nearest cycle it leads into through cheap work (-1: none), as `find_chains` marks
// it: a cheap plain map or a hand-on, read by a cycle's node or by such work, and not timeless. Timeless work runs once
// a run, not a sample, in a stage of its own before the loops: ahead of them (no axis), or over the sections (`[k, 1]`,
// the gather of sections in lanes). One pass back, every reader after what it reads.
constexpr std::vector<std::int32_t> leads_into(const Graph& g, const Facts& facts, const Feedback& feedback) {
    const std::vector<char> timeless = graph::timeless_of(g);
    std::vector<std::int32_t> target(g.nodes.size(), -1);
    for (std::size_t r = g.nodes.size(); r-- > 0;) {
        const std::int32_t into = feedback.cycle[r] >= 0 ? feedback.cycle[r] : target[r];
        for (std::size_t k = 0; k < rules_of(g.nodes[r].op).arity && into >= 0; ++k) {
            const std::int32_t m = facts.producer[static_cast<std::size_t>(facts.read_as[static_cast<std::size_t>(g.nodes[r].inputs[k])])];
            const heinzel::Node* n = m >= 0 ? &g.nodes[static_cast<std::size_t>(m)] : nullptr;
            const bool cheap = n != nullptr && (graph::hands_its_operand_on(*n) || (graph::is_a_plain_map(*n) && rules_of(n->op).cost == CostClass::Cheap));
            const bool leads = cheap && feedback.cycle[static_cast<std::size_t>(m)] < 0 && timeless[static_cast<std::size_t>(n->output)] == 0;
            if (leads && (target[static_cast<std::size_t>(m)] < 0 || into < target[static_cast<std::size_t>(m)])) {
                target[static_cast<std::size_t>(m)] = into;
            }
        }
    }
    return target;
}

// Per node, whether it leads into a cycle (`target`) and makes the domain that cycle's stage walks: the cycle's shape
// or, where it folds its voices, its fold's operand (`planner/stages.hpp` `opened_over`). Only such work shares the
// cycle's stage: the planner joins a node to a stage, and a stage of maps to the feedback stage after it, only where it
// makes that domain (`joins`, `planner/trail.hpp` `as_it_closes`); other work leading in runs in a stage of its own.
constexpr std::vector<char> makes_the_domain(const Graph& g, const Feedback& feedback, const std::vector<std::int32_t>& target) {
    std::vector<std::int32_t> walks(g.nodes.size(), kNoValue); // per cycle: a value of the domain its stage walks
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        const heinzel::Node& n = g.nodes[i];
        const std::int32_t c = feedback.cycle[i];
        if (c >= 0 && (graph::is_a_fold(n) || static_cast<std::size_t>(c) == i)) { // (the node naming a cycle comes first)
            walks[static_cast<std::size_t>(c)] = graph::is_a_fold(n) ? n.inputs[0] : n.output;
        }
    }
    std::vector<char> makes(g.nodes.size(), 0);
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        const std::int32_t into = target[i];
        const Shape& made = g.values[static_cast<std::size_t>(g.nodes[i].output)].shape;
        makes[i] = into >= 0 && made == g.values[static_cast<std::size_t>(walks[static_cast<std::size_t>(into)])].shape;
    }
    return makes;
}

// Per cycle, the latest cycle whose value, through any work, reaches work in no feedback stage that it or work sharing
// its stage reads (-1: none): that work runs after that cycle's stage, which it cannot join then (`could_follow`).
constexpr std::vector<std::int32_t> waits_on(const Graph& g, const Facts& facts, const Feedback& feedback, const std::vector<std::int32_t>& target,
                                             const std::vector<char>& shares) {
    const std::size_t count = g.nodes.size();
    std::vector<std::int32_t> after(count, -1); // per node: the latest cycle it reads, through any work
    std::vector<std::int32_t> waits(count, -1);
    for (std::size_t i = 0; i < count; ++i) { // (every reader outside a cycle after what it reads)
        const std::int32_t owner = feedback.cycle[i] >= 0 ? feedback.cycle[i] : shares[i] ? target[i] : -1;
        for (std::size_t k = 0; k < rules_of(g.nodes[i].op).arity; ++k) {
            const std::int32_t p = facts.producer[static_cast<std::size_t>(facts.read_as[static_cast<std::size_t>(g.nodes[i].inputs[k])])];
            if (p < 0) {
                continue;
            }
            const std::size_t q = static_cast<std::size_t>(p);
            const std::int32_t in = feedback.cycle[q] >= 0 ? feedback.cycle[q] : shares[q] && target[q] > after[q] ? target[q] : after[q];
            after[i] = in > after[i] ? in : after[i];
            if (owner >= 0 && feedback.cycle[q] < 0 && !shares[q] && after[q] > waits[static_cast<std::size_t>(owner)]) {
                waits[static_cast<std::size_t>(owner)] = after[q];
            }
        }
    }
    return waits;
}

// Per cycle (by the node naming it), its stage alone with the work leading in that shares it, as the planner prices a
// unit chaining on (`cheaper_joined`): its counts (`alone`), that work's slots added, its longest path before its own.
constexpr std::vector<cost::Stage> led_into(const Graph& g, const Facts& facts, const Feedback& feedback, const std::vector<std::int32_t>& target,
                                            const std::vector<char>& shares, const std::vector<cost::Stage>& alone, const Machine& m) {
    std::vector<cost::Stage> led = alone;
    std::vector<double> path(g.nodes.size(), 0);
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        const heinzel::Node& n = g.nodes[i];
        if (feedback.cycle[i] >= 0 || !shares[i] || !facts.statement[i] || graph::is_generated(n)) {
            continue;
        }
        const OpCost own = cost::of(n.op, g.values[static_cast<std::size_t>(n.output)].type, m);
        for (std::size_t k = 0; k < detail::read_now(n); ++k) {
            const std::int32_t p = facts.producer[static_cast<std::size_t>(facts.read_as[static_cast<std::size_t>(n.inputs[k])])];
            const bool same = p >= 0 && shares[static_cast<std::size_t>(p)] && target[static_cast<std::size_t>(p)] == target[i];
            path[i] = same && path[static_cast<std::size_t>(p)] > path[i] ? path[static_cast<std::size_t>(p)] : path[i];
        }
        path[i] += own.latency;
        const std::size_t t = static_cast<std::size_t>(target[i]);
        led[t].depth = alone[t].depth + path[i] > led[t].depth ? alone[t].depth + path[i] : led[t].depth;
        led[t].slots += own.slots;
    }
    return led;
}

// Stage `s` with unit `u` chained on, as the planner sums them on a join (`cheaper_joined`): the longer chain, the
// paths one after the other, the slots and loads added.
constexpr cost::Stage chained(cost::Stage s, const cost::Stage& u) {
    s.chain = u.chain > s.chain ? u.chain : s.chain;
    s.depth += u.depth, s.slots += u.slots, s.loads += u.loads, s.stores = 1;
    s.bodies = u.bodies > s.bodies ? u.bodies : s.bodies;
    s.lanes = u.lanes < s.lanes ? u.lanes : s.lanes;
    return s;
}

// Does the cycle at `q` in the feedback order read a cycle of the open stage: this sample, through cheap work and no lag
// (`made_now`), or, `lagged`, through any lag too, as the planner's `reads_the_open` reads what the stage makes.
constexpr bool reads_the_open(const Graph& g, const Facts& facts, const Feedback& feedback, const std::vector<std::int32_t>& made,
                              const std::vector<std::int32_t>& stage, std::int32_t open, std::size_t q, bool lagged) {
    const auto at = [](std::int32_t v) { return static_cast<std::size_t>(v); };
    const std::int32_t c = feedback.cycle[at(feedback.order[q])];
    for (std::size_t r = q; r < feedback.order.size() && feedback.cycle[at(feedback.order[r])] == c; ++r) {
        const heinzel::Node& n = g.nodes[at(feedback.order[r])];
        for (std::size_t k = 0; k < rules_of(n.op).arity && (lagged || !(graph::hands_its_operand_on(n) && samples_back(n) > 0)); ++k) {
            const std::int32_t v = facts.read_as[at(n.inputs[k])];
            const std::int32_t p = facts.producer[at(lagged ? facts.through[at(v)].source : v)];
            const std::int32_t from = p < 0 ? -1 : lagged && feedback.cycle[at(p)] >= 0 ? p : made[at(p)];
            if (from >= 0 && feedback.cycle[at(from)] != c && stage[at(feedback.cycle[at(from)])] == open) {
                return true;
            }
        }
    }
    return false;
}

// Per cycle, the stage it runs in, named by that stage's first cycle: a cycle joins the open stage where the two are of
// one group (`group`: a group is priced apart), it waits on no work run after that stage (`waits_on`), the two walk one
// shape (what a stage takes makes its domain: the planner's `joins`), neither folds its voices every sample nor holds a
// link (each runs in a stage of its own: `chains_on`), their nodes and the work leading in, times their lane bodies,
// stay within `Machine::merge_work` (Decision 65; the planner's `still_fits`), and it reads a cycle of the stage this
// sample, directly or through cheap work (`made_now`), or else the model prices the stage no dearer with it than the two
// apart: the planner chains independent recurrences on too, where their chains overlap (`chains_on`, `cheaper_joined`),
// and hands its values on to the cycle the stage's cycles hand theirs to, or either to none (`keeps_one_reader`).
constexpr std::vector<std::int32_t> stages_of(const Graph& g, const Facts& facts, const Feedback& feedback, const std::vector<std::int32_t>& target,
                                              const std::vector<char>& shares, const std::vector<cost::Stage>& alone, const std::vector<std::int32_t>& group,
                                              const Machine& m) {
    const std::size_t count = g.nodes.size();
    const std::vector<std::int32_t> made = graph::made_now(g, facts, feedback);
    const std::vector<std::int32_t> waits = detail::waits_on(g, facts, feedback, target, shares);
    const std::vector<cost::Stage> led = detail::led_into(g, facts, feedback, target, shares, alone, m);
    const std::vector<std::int32_t> to = graph::handed_to(g, facts, feedback, target);
    const auto at = [](std::int32_t v) { return static_cast<std::size_t>(v); };
    std::vector<std::int32_t> stage(count, -1);
    std::vector<std::int32_t> size(count, 0);
    std::vector<char> apart(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        if (const std::int32_t c = feedback.cycle[i] >= 0 ? feedback.cycle[i] : shares[i] ? target[i] : -1; c >= 0) {
            size[at(c)] += 1;
            apart[at(c)] |= feedback.cycle[i] >= 0 && (graph::is_a_fold(g.nodes[i]) || graph::is_a_link(g.nodes[i]));
        }
    }
    const auto shape_of = [&](std::int32_t c) -> const Shape& { return g.values[at(g.nodes[at(c)].output)].shape; };
    std::int32_t open = -1;
    std::int32_t open_to = -1; // (the cycle the open stage's cycles hand on to)
    std::int32_t held = 0;
    double bodies = 1;
    cost::Stage priced{}; // (the open stage's counts, summed as the planner sums them)
    for (std::size_t q = 0; q < feedback.order.size(); ++q) {
        const std::int32_t c = feedback.cycle[at(feedback.order[q])];
        if (c < 0 || stage[at(c)] >= 0) {
            continue;
        }
        const bool reads = detail::reads_the_open(g, facts, feedback, made, stage, open, q, false);
        const cost::Stage joined = detail::chained(priced, led[at(c)]);
        const bool cheaper = cost::cycles(joined, m) <= cost::cycles(priced, m) + cost::cycles(led[at(c)], m);
        const double wide = alone[at(c)].bodies > bodies ? alone[at(c)].bodies : bodies;
        const bool one_reader = to[at(c)] < 0 || open_to < 0 || to[at(c)] == open_to || detail::reads_the_open(g, facts, feedback, made, stage, open, q, true);
        const bool joins = open >= 0 && group[at(c)] == group[at(open)] && waits[at(c)] < open && (reads || cheaper) && one_reader && !apart[at(c)] &&
                           !apart[at(open)] && shape_of(c) == shape_of(open) && (held + size[at(c)]) * wide <= m.merge_work;
        open_to = joins && open_to >= 0 ? open_to : to[at(c)];
        open = joins ? open : c;
        held = (joins ? held : 0) + size[at(c)];
        bodies = joins ? wide : alone[at(c)].bodies;
        priced = joins ? joined : led[at(c)];
        stage[at(c)] = open;
    }
    return stage;
}

// Node `i` of a feedback stage, on a path `path` long through it, joins the stage's counts `into`: its depth; the slots
// of work leading in; a cycle's own counts (`alone`) once, at the node naming it (`c`: its cycle, -1 none).
constexpr void joins_cycles(cost::Stage& into, const Graph& g, std::size_t i, std::int32_t c, double path, bool runs, const std::vector<cost::Stage>& alone,
                            const Machine& m) {
    const heinzel::Node& n = g.nodes[i];
    into.depth = path > into.depth ? path : into.depth;
    into.slots += c < 0 && runs ? cost::of(n.op, g.values[static_cast<std::size_t>(n.output)].type, m).slots : 0;
    if (c >= 0 && static_cast<std::size_t>(c) == i) {
        const cost::Stage& own = alone[i];
        into.chain = own.chain > into.chain ? own.chain : into.chain;
        into.depth = own.depth > into.depth ? own.depth : into.depth; // (a fold's lanes, which the path leaves out)
        into.slots += own.slots, into.loads += own.loads, into.stores = 1;
        into.bodies = own.bodies > into.bodies ? own.bodies : into.bodies;
        into.lanes = own.lanes < into.lanes ? own.lanes : into.lanes;
    }
}

// Node `n`, on a path `path` long through a stage of maps (no cycle), joins its counts `into` as the planner lays such a
// stage out (`planner/chains.hpp` `priced_open`): its slots; voices in lane bodies, one voice's samples in a vector's
// lanes of its widest element; a store.
constexpr void joins_maps(cost::Stage& into, const Graph& g, const heinzel::Node& n, double path, const Machine& m) {
    const Shape& made = g.values[static_cast<std::size_t>(n.output)].shape;
    const std::int64_t widest = detail::widest_of(g, n, true);
    const std::int64_t lanes = m.vector_bytes / widest > 1 ? m.vector_bytes / widest : 1;
    const double along = made.rank > 1 ? 1 : static_cast<double>(lanes);
    into.depth = path > into.depth ? path : into.depth;
    into.slots += cost::of(n.op, g.values[static_cast<std::size_t>(n.output)].type, m).slots;
    into.stores = 1;
    into.bodies = detail::bodies_of(made, lanes) > into.bodies ? detail::bodies_of(made, lanes) : into.bodies;
    into.lanes = along < into.lanes ? along : into.lanes;
}

// The model's feedback stages by their first cycles, with their groups (`named`); per cycle, the work leading in of
// another shape than its stage walks, a stage of maps of its own (`apart`).
struct Staged {
    std::vector<cost::Stage> shared;
    std::vector<cost::Stage> apart;
    std::vector<std::int32_t> named;
};

// `g`'s stages as `feedback_cycles` prices them. Each path as the planner counts one (`count_cost`), a lag's afresh: in a
// feedback stage through what it shares, in work apart through that work.
constexpr Staged staged(const Graph& g, const Facts& facts, const Feedback& feedback, const std::vector<std::int32_t>& group, const Machine& m) {
    const std::size_t count = g.nodes.size();
    const std::vector<cost::Stage> alone = graph::cycle_stages(g, facts, feedback, m);
    const std::vector<std::int32_t> target = detail::leads_into(g, facts, feedback);
    const std::vector<char> shares = detail::makes_the_domain(g, feedback, target);
    const std::vector<std::int32_t> stage = detail::stages_of(g, facts, feedback, target, shares, alone, group, m);
    const auto at = [](std::int32_t v) { return static_cast<std::size_t>(v); };
    const auto stage_at = [&](std::int32_t node) {
        const std::int32_t c = node < 0 ? -1 : feedback.cycle[at(node)] >= 0 ? feedback.cycle[at(node)] : shares[at(node)] ? target[at(node)] : -1;
        return c >= 0 ? stage[at(c)] : -1;
    };
    const auto apart_at = [&](std::int32_t node) { return node < 0 || feedback.cycle[at(node)] >= 0 || shares[at(node)] ? -1 : target[at(node)]; };
    const cost::Stage none{.lanes = std::numeric_limits<double>::infinity()};
    Staged out{std::vector<cost::Stage>(count, none), std::vector<cost::Stage>(count, none), std::vector<std::int32_t>(count, -1)};
    std::vector<double> path(count, 0);
    std::vector<std::int32_t> loaded(g.values.size(), -1); // per value: the cycle whose work apart loads it
    const OpCode conversion = detail::conversion_op();
    for (const std::int32_t ni : feedback.order) {
        const std::size_t i = at(ni);
        const heinzel::Node& n = g.nodes[i];
        const std::int32_t s = stage_at(ni);
        const std::int32_t t = apart_at(ni);
        if (s < 0 && t < 0) {
            continue;
        }
        double before = 0;
        for (std::size_t k = 0; k < detail::read_now(n); ++k) {
            const std::int32_t p = facts.producer[at(facts.read_as[at(n.inputs[k])])];
            const bool with = s >= 0 ? stage_at(p) == s : apart_at(p) == t;
            const double through = with ? path[at(p)] + detail::converted(g, n, k, conversion, m) : 0;
            before = through > before ? through : before;
        }
        const bool runs = facts.statement[i] && !graph::is_generated(n);
        path[i] = before + (runs ? detail::latency_of(g, n, 4, m) : 0);
        if (s >= 0) {
            detail::joins_cycles(out.shared[at(s)], g, i, feedback.cycle[i], path[i], runs, alone, m);
            out.named[at(s)] = feedback.cycle[i] >= 0 ? group[i] : out.named[at(s)];
        } else if (runs) {
            cost::Stage& into = out.apart[at(t)];
            detail::joins_maps(into, g, n, path[i], m);
            for (std::size_t k = 0; k < rules_of(n.op).arity; ++k) { // (each value from outside it once, a constant apart)
                const std::int32_t v = facts.read_as[at(n.inputs[k])];
                const bool outside = apart_at(facts.producer[at(v)]) != t && g.values[at(v)].category != ValueCategory::Const;
                into.loads += outside && loaded[at(v)] != t ? 1 : 0;
                loaded[at(v)] = outside ? t : loaded[at(v)];
            }
        }
    }
    return out;
}

} // namespace detail

// What a sample of `g`'s feedback stages costs, in cycles (infinite where the planner refuses its feedback), priced as
// the planner will stage them: each cycle's counts as `cycle_stages` prices it alone, the stages the cycles share
// (`detail::stages_of`), the cheap work leading into a cycle in its stage where it makes the domain that stage walks,
// else in a stage of maps of its own, and a stage's depth one sample's whole path through it, as the planner counts it
// (`planner/stages.hpp` `count_cost`). It mirrors the planner's staging, not its every call (its cost call on a cycle
// that reads the stage, `cheaper_joined`; on a stage's feed, `as_it_closes`: the first cycle's feed stays in its stage,
// as `Machine::window` was fitted; the registers' bound): a price that tells two orders of one graph's sums apart.
// Priced by group: a stage holds one group's cycles, and its price, and work apart, go to that group's `group[i]` (of
// `groups`; -1 outside every cycle: `linked_cycles`, in this graph or in one it was rewritten from).
constexpr std::vector<double> feedback_cycles(const Graph& g, const Machine& m, const std::vector<std::int32_t>& group, std::size_t groups) {
    const Facts facts = graph::facts_of(g);
    const Feedback feedback = graph::find_feedback(g, facts);
    if (!facts.why_not.empty() || !feedback.why_not.empty()) {
        return std::vector<double>(groups, std::numeric_limits<double>::infinity());
    }
    const detail::Staged s = detail::staged(g, facts, feedback, group, m);
    std::vector<double> total(groups, 0);
    for (std::size_t c = 0; c < g.nodes.size(); ++c) {
        if (s.shared[c].stores > 0) {
            total[static_cast<std::size_t>(s.named[c])] += cost::cycles(s.shared[c], m);
        }
        if (s.apart[c].stores > 0) {
            total[static_cast<std::size_t>(group[c])] += cost::cycles(s.apart[c], m);
        }
    }
    return total;
}

// What a sample of `g`'s feedback stages costs, in cycles: all of them, as one group.
constexpr double feedback_cycles(const Graph& g, const Machine& m) {
    return loops::graph::feedback_cycles(g, m, std::vector<std::int32_t>(g.nodes.size(), 0), 1)[0];
}

// Per node, the group of cycles it is one of (-1: none), named by one of them: cycles linked by one reading another
// this sample through cheap work (`made_now`), so that a graph's stages price apart group by group, priced as no stage
// holding two groups (`detail::stages_of`). Linked as a union-find, each group named by its least cycle.
constexpr std::vector<std::int32_t> linked_cycles(const Graph& g, const Facts& facts, const Feedback& feedback) {
    const std::size_t count = g.nodes.size();
    const std::vector<std::int32_t> made = graph::made_now(g, facts, feedback);
    std::vector<std::int32_t> name(count);
    std::vector<std::int32_t> group(count, -1);
    for (std::size_t i = 0; i < count; ++i) {
        name[i] = static_cast<std::int32_t>(i);
    }
    const auto root = [&](std::int32_t c) {
        for (; name[static_cast<std::size_t>(c)] != c; c = name[static_cast<std::size_t>(c)]) {
            name[static_cast<std::size_t>(c)] = name[static_cast<std::size_t>(name[static_cast<std::size_t>(c)])]; // (halved)
        }
        return c;
    };
    for (std::size_t i = 0; i < count; ++i) {
        const heinzel::Node& n = g.nodes[i];
        for (std::size_t k = 0; k < rules_of(n.op).arity && feedback.cycle[i] >= 0 && !(graph::hands_its_operand_on(n) && samples_back(n) > 0); ++k) {
            const std::int32_t p = facts.producer[static_cast<std::size_t>(facts.read_as[static_cast<std::size_t>(n.inputs[k])])];
            const std::int32_t from = p >= 0 ? made[static_cast<std::size_t>(p)] : -1;
            if (from >= 0) {
                const std::int32_t a = root(feedback.cycle[i]);
                const std::int32_t b = root(feedback.cycle[static_cast<std::size_t>(from)]);
                name[static_cast<std::size_t>(a > b ? a : b)] = a < b ? a : b;
            }
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        group[i] = feedback.cycle[i] >= 0 ? root(feedback.cycle[i]) : -1;
    }
    return group;
}

} // namespace heinzel::loops::graph
