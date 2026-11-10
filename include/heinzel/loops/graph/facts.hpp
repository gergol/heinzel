#pragma once

// What the planner and the lowering read off a graph again and again, worked out once per graph: per value, the node
// that makes it, the value a read of it finds, what it is read through and how far back anything reads it; per node,
// whether it is a statement. Asked of one value at a time, each was a scan of the graph, and a pass asking it of every
// node was quadratic; built here, each is one pass. The table is transient, sized to the graph, and never leaves the
// pass that builds it. It decides nothing and names no operation. Every pass reads the nodes in the order they stand,
// as the frontend builds them: a graph edited by hand so that a node reads what it or a later node makes is refused by
// both passes (`why_not`).

#include <cstdint>
#include <string_view>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/ir/timing.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/graph/views.hpp"

namespace heinzel::loops::graph {

struct Facts {
    std::vector<std::int32_t> producer{}; // per value: the node that makes it (-1: an input, a constant, a variable)
    std::vector<std::int32_t> read_as{};  // per value: what a read of it finds — a defined variable is its definition
    std::vector<char> statement{};        // per node: whether it is a statement
    std::vector<Viewed> through{};        // per value: it, read through every view with no statement
    std::vector<std::int32_t> deepest{};  // per value: how far back anything reads it (0: nothing reads its past)
    std::string_view why_not{};           // why the passes cannot read the nodes as they stand (empty: they can)
};

// Why a view has no address here yet; empty when it has one. Asked before anything is read through it, so that
// what cannot be lowered is refused by name and not half way.
constexpr std::string_view why_no_address(const Graph& g, const Facts& facts, const heinzel::Node& view) {
    const ViewAttr& how = view.attr.view;
    const Shape& to = g.values[static_cast<std::size_t>(view.output)].shape;
    const Shape& from = g.values[static_cast<std::size_t>(view.inputs[0])].shape;
    if (graph::folds_into_its_readers(view)) {
        return graph::windows_an_element_axis(g, view) ? "" : "heinzel::compile: a window over no element axis, or with a hop below one";
    }
    if (graph::keeps_elements_in_order(view)) {
        const std::int32_t under = facts.producer[static_cast<std::size_t>(view.inputs[0])];
        if (under >= 0 && graph::folds_into_its_readers(g.nodes[static_cast<std::size_t>(under)])) {
            return "heinzel::compile: new extents over a sliding window have no address here yet";
        }
        for (std::int32_t k = 1; k + 1 < to.rank; ++k) {
            if (!is_literal(extent_at(to, k))) {
                return "heinzel::compile: new extents, a symbolic one not outermost, have no address yet";
            }
        }
        return "";
    }
    if (how.kind != ViewKind::Transpose && (how.axis < 0 || how.axis + 1 >= from.rank)) {
        return "heinzel::compile: a view along an axis that is no element axis of its source";
    }
    if (how.kind == ViewKind::Flip && !is_literal(extent_at(to, how.axis))) {
        return "heinzel::compile: an axis of symbolic extent, reversed, has no address here yet";
    }
    return "";
}

namespace detail {

constexpr bool names_a_value(const Graph& g, std::int32_t v) { return v >= 0 && static_cast<std::size_t>(v) < g.values.size(); }

// Per value, the node that makes it, and what a read of it finds: a variable and its defining expression are one
// number, and only the first definition is made. A value made by two nodes is refused.
constexpr void makers_of(const Graph& g, Facts& facts) {
    std::vector<char> defined(g.values.size(), false);
    for (std::size_t v = 0; v < g.values.size(); ++v) {
        facts.read_as[v] = static_cast<std::int32_t>(v);
    }
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        const heinzel::Node& n = g.nodes[i];
        if (detail::names_a_value(g, n.output) && facts.producer[static_cast<std::size_t>(n.output)] >= 0) {
            facts.why_not = "heinzel::compile: a value is made by two nodes";
        } else if (detail::names_a_value(g, n.output)) {
            facts.producer[static_cast<std::size_t>(n.output)] = static_cast<std::int32_t>(i);
        }
        const std::int32_t v = n.attr.kind == AttrKind::Recurrence ? n.attr.recurrence.defines : kNoValue;
        if (detail::names_a_value(g, v) && !defined[static_cast<std::size_t>(v)]) {
            defined[static_cast<std::size_t>(v)] = true;
            facts.read_as[static_cast<std::size_t>(v)] = g.values[static_cast<std::size_t>(v)].category == ValueCategory::Variable ? n.output : v;
        }
    }
}

// An affine view is a statement, a Load of its source at the view's address, unless everything that reads it is a
// view reading through it in turn: then nothing is made, and those readers read its source. New extents over the same
// elements need them in memory order, so what such a view reads is always made. A window is never a statement, and
// anything that is no view always is. A node reading what it or a later node makes is refused (a variable has no maker,
// so a read of its past never is). The frontend stands a node before what it reads only where a loop reads a computed
// value's past (`frontend/reading_order.hpp`), and that refusal says how to write the loop.
constexpr void statements_of(const Graph& g, Facts& facts) {
    std::vector<char> read(g.values.size(), false);  // read at all
    std::vector<char> made(g.values.size(), false);  // read by what needs it made
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        const heinzel::Node& n = g.nodes[i];
        const bool reads_through = !graph::keeps_elements_in_order(n) && (graph::is_an_affine_view(n) || graph::folds_into_its_readers(n));
        for (std::size_t k = 0; k < rules_of(n.op).arity; ++k) {
            if (detail::names_a_value(g, n.inputs[k])) {
                read[static_cast<std::size_t>(n.inputs[k])] = true;
                made[static_cast<std::size_t>(n.inputs[k])] |= !reads_through;
                if (facts.producer[static_cast<std::size_t>(n.inputs[k])] >= static_cast<std::int32_t>(i)) {
                    facts.why_not = operand_comes_from_the_past(n, k)
                                        ? "heinzel::compile: feedback reads the past of an Expr; make it a Variable"
                                        : "heinzel::compile: a node reads what it or a later node makes";
                }
            }
        }
    }
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        const heinzel::Node& n = g.nodes[i];
        const std::size_t v = static_cast<std::size_t>(n.output);
        facts.statement[i] = !graph::is_an_affine_view(n) ? !graph::folds_into_its_readers(n) : is_a_result(g, n.output) || made[v] || !read[v];
    }
}

// Every value read through the views with no statement between it and the value that is made, each worked out once,
// from the one under it, in whatever order the nodes stand. A view with no address reads as itself here: every pass
// refuses it before anything is read through it.
constexpr void through_of(const Graph& g, Facts& facts) {
    std::vector<char> known(g.values.size(), false);
    std::vector<std::int32_t> above{}; // the views still to apply, the outermost first
    for (std::size_t first = 0; first < g.values.size(); ++first) {
        std::int32_t v = static_cast<std::int32_t>(first);
        while (!known[static_cast<std::size_t>(v)]) {
            const std::int32_t m = facts.producer[static_cast<std::size_t>(v)];
            if (m >= 0 && !facts.statement[static_cast<std::size_t>(m)] && graph::why_no_address(g, facts, g.nodes[static_cast<std::size_t>(m)]).empty()) {
                above.push_back(v);
                v = g.nodes[static_cast<std::size_t>(m)].inputs[0];
                continue;
            }
            facts.through[static_cast<std::size_t>(v)] = graph::read_as_itself(v);
            known[static_cast<std::size_t>(v)] = true;
        }
        for (; !above.empty(); above.pop_back()) {
            const std::size_t at = static_cast<std::size_t>(above.back());
            const heinzel::Node& view = g.nodes[static_cast<std::size_t>(facts.producer[at])];
            facts.through[at] = graph::through_view(g, view, facts.through[static_cast<std::size_t>(view.inputs[0])]);
            known[at] = true;
        }
    }
}

} // namespace detail

// The table for `g`, in a few passes over its nodes.
constexpr Facts facts_of(const Graph& g) {
    Facts facts{.producer = std::vector<std::int32_t>(g.values.size(), -1),
                .read_as = std::vector<std::int32_t>(g.values.size(), kNoValue),
                .statement = std::vector<char>(g.nodes.size(), false),
                .through = std::vector<Viewed>(g.values.size()),
                .deepest = std::vector<std::int32_t>(g.values.size(), 0)};
    detail::makers_of(g, facts);
    detail::statements_of(g, facts);
    detail::through_of(g, facts);
    // How far back anything reads each value: a delay line as far as its position may reach, plus one sample for its
    // far tap.
    for (const heinzel::Node& n : g.nodes) {
        const std::int32_t back = graph::hands_its_operand_on(n) ? samples_back(n) : graph::gathers(n) == Gather::Back ? graph::reach_of(n) + 1 : 0;
        const std::size_t v = back > 0 ? static_cast<std::size_t>(facts.read_as[static_cast<std::size_t>(n.inputs[0])]) : 0;
        facts.deepest[v] = back > facts.deepest[v] ? back : facts.deepest[v];
    }
    return facts;
}

// Per value of `g`, whether it is timeless: a constant, a Param or a tensor declared so, or what is computed sample by
// sample from those alone, the same at every sample. The rewrites and the planner read it.
constexpr std::vector<char> timeless_of(const Graph& g) {
    std::vector<char> timeless(g.values.size(), 0);
    for (std::size_t v = 0; v < g.values.size(); ++v) {
        timeless[v] = g.values[v].timeless || g.values[v].category == ValueCategory::Const ? 1 : 0;
    }
    for (const heinzel::Node& n : g.nodes) { // (in order: an operand made later is not known timeless yet, and stays so)
        const OpRules& r = rules_of(n.op);
        bool all = r.arity > 0 && r.time == TimeRule::SameSample && r.meaning == MeaningRule::PerSample && n.attr.kind != AttrKind::Recurrence;
        for (std::size_t k = 0; k < r.arity && all; ++k) {
            all = timeless[static_cast<std::size_t>(n.inputs[k])] != 0;
        }
        timeless[static_cast<std::size_t>(n.output)] = timeless[static_cast<std::size_t>(n.output)] || all ? 1 : 0;
    }
    return timeless;
}

// Per node of `g`, whether it is work ahead of the loops: it runs (a statement, no ramp, the planner's `runs`) and makes,
// from timeless values alone, a value of no axis. Timeless work is maps and hand-ons of timeless values, so no fold, no
// contraction, in no cycle (which reads its own past): a stage it opens walks what it makes (`planner/stages.hpp`
// `opened_over`), no loop. The planner gathers it before every unit with a loop (`next_unit`), so it leads into no
// cycle's stage in the planner's chaining (`planner/chains.hpp` `find_chains`); the rewrite layer's mirror of it
// (`cycle_chains.hpp` `leads_into`) keeps all timeless work out, which runs once a run.
constexpr std::vector<char> ahead_of_the_loops(const Graph& g, const Facts& facts) {
    const std::vector<char> timeless = graph::timeless_of(g);
    std::vector<char> ahead(g.nodes.size(), 0);
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        const std::size_t v = static_cast<std::size_t>(g.nodes[i].output);
        ahead[i] = facts.statement[i] && !graph::is_generated(g.nodes[i]) && timeless[v] != 0 && g.values[v].shape.rank == 0 ? 1 : 0;
    }
    return ahead;
}

} // namespace heinzel::loops::graph
