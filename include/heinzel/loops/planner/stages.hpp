#pragma once

// The planner's first pass decides which graph nodes share a stage. It gathers each stage's membership (its nodes, the
// domain its loops will walk, its fold, its cycle) and nothing about its loops, which the next pass plans from the
// membership alone. Refusals are values that start with `heinzel::compile:`, a node's with its operation as the user
// wrote it (`worded`). It names no operation: what a node is, and how it is written, come from the rule table.

#include <array>
#include <bit>
#include <cstdint>
#include <expected>
#include <string_view>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/ir/timing.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/graph/regions.hpp"
#include "heinzel/loops/graph/views.hpp"
#include "heinzel/loops/planner/chains.hpp"
#include "heinzel/loops/planner/gathering.hpp"
#include "heinzel/loops/planner/trail.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/schedule.hpp"
#include "heinzel/schedule/machine.hpp"
#include "heinzel/support/fail.hpp"
#include "heinzel/support/fixed_vector.hpp"

namespace heinzel::loops::planner {

// Is this a fold, or an order statistic? An order statistic makes an axis of its operand go away just as a fold does,
// so it gets a fold's stage: its producers inside the loop over that axis, and the maps over its result after it.
constexpr bool collapses_an_axis(const heinzel::Node& n) { return graph::is_a_fold(n) || graph::is_an_ordering(n); }

namespace detail {

// Why one of those cannot run here, a case of `why_it_cannot_run`; empty when it can.
constexpr Refusal why_not(const Graph& g, const heinzel::Node& n) {
    if (graph::is_an_ordering(n)) {
        return graph::selects_one_rank(g, n) ? "" : " is not one exact rank, which is not lowered yet";
    }
    if (graph::folds_over_time(g, n)) {
        return "heinzel::compile: this collapses the streamed axis, which a run here cannot do";
    }
    return graph::folds_a_missing_axis(g, n) ? "heinzel::compile: this collapses an axis the operand does not have" : "";
}

// Why a view cannot run here; empty when it can (and for what is no view). `statement`: whether it has one.
constexpr Refusal why_not_this_view(const Graph& g, const graph::Facts& facts, const heinzel::Node& n, bool statement) {
    if (!graph::is_an_affine_view(n) && !graph::folds_into_its_readers(n)) {
        return "";
    }
    if (const Refusal why = graph::why_no_address(g, facts, n); !why.empty()) {
        return why;
    }
    // A window reads its source in place, from storage, and a constant is stored nowhere. (A view of one WITH a
    // statement stores the constant itself, a fill; one read only by views and windows does not.)
    if (graph::folds_into_its_readers(n) && is_a_constant(g, facts.through[static_cast<std::size_t>(n.inputs[0])].source)) {
        return "heinzel::compile: a window over a constant is not lowered yet";
    }
    const bool stored_nowhere = !statement && is_a_result(g, n.output);
    return stored_nowhere ? "heinzel::compile: a window that is itself a result is not stored here yet" : "";
}

// Whether value `v` has no storage of its own: a constant, or a window (its readers read its source in place).
constexpr bool is_stored_nowhere(const Graph& g, const graph::Facts& facts, std::int32_t v) {
    const std::int32_t maker = facts.producer[static_cast<std::size_t>(v)];
    return is_a_constant(g, v) || (maker >= 0 && graph::folds_into_its_readers(g.nodes[static_cast<std::size_t>(maker)]));
}

// Why node `n` cannot run here at all; empty when it can. Asked of every node before any is gathered, in the order
// they are planned in, so which refusal a graph gets does not depend on the gathering. It gives the case alone, which
// `worded` puts after the operation: a sentence of its own (`heinzel::compile: …`, as a view's, which the lowering
// shares) after a colon, else the words that go on from the operation's name (" is not lowered yet").
constexpr Refusal why_it_cannot_run(const Graph& g, const graph::Facts& facts, const heinzel::Node& n, bool statement) {
    if (const Refusal why = detail::why_not_this_view(g, facts, n, statement); !why.empty() || !statement || graph::is_generated(n)) {
        return why;
    }
    if (graph::is_made_in_parts(n)) {
        return graph::parts_of(g, n).empty() ? "heinzel::compile: a pad or a join along an axis of symbolic extent is not lowered yet" : "";
    }
    if (graph::is_a_contraction(n) && (detail::is_stored_nowhere(g, facts, n.inputs[0]) || detail::is_stored_nowhere(g, facts, n.inputs[1]))) {
        return "heinzel::compile: a contraction of a constant or a window is not lowered yet"; // its operands are read from storage
    }
    const std::int32_t table = graph::gathers(n) == Gather::Table ? facts.read_as[static_cast<std::size_t>(n.inputs[1])] : kNoValue;
    if (table != kNoValue && detail::is_stored_nowhere(g, facts, table)) {
        return "heinzel::compile: a table read over a constant or a window is not lowered yet"; // a table is read from storage
    }
    if (table != kNoValue && g.values[static_cast<std::size_t>(table)].shape.rank != 2) {
        return "heinzel::compile: a table with axes besides its cells and time is not lowered yet";
    }
    if (planner::collapses_an_axis(n)) {
        return detail::why_not(g, n);
    }
    const bool mapped = graph::is_a_plain_map(n) || graph::hands_its_operand_on(n) || graph::is_an_affine_view(n) || graph::gathers(n) != Gather::None;
    // (a link runs only in its cycle, as `graph::find_feedback` makes sure, in a stage `plan_feedback_loops` lays out)
    return mapped || graph::is_a_link(n) || graph::is_a_contraction(n) ? "" : " is not lowered yet";
}

// Node `n`'s refusal as its user reads it: the operation as written, the case (`why_it_cannot_run`'s), and its
// operands' shapes as the analysis recorded them, last, so that a message cut at its length loses shapes, never the
// reason. No source line: every value of a graph would hold one, and equal graphs would differ.
constexpr Refusal worded(const Graph& g, const heinzel::Node& n, std::string_view why) {
    constexpr std::string_view compile = "heinzel::compile: ";
    MessageWriter m;
    m << compile;
    write_node(m, n);
    if (why.starts_with(compile)) {
        m << std::string_view{": "} << why.substr(compile.size());
    } else {
        m << why;
    }
    const std::size_t arity = rules_of(n.op).arity;
    for (std::size_t k = 0; k < arity; ++k) {
        const bool last = k + 1 == arity;
        m << std::string_view{k == 0 ? ": " : last ? " and " : ", "};
        write_shape(m, g.values[static_cast<std::size_t>(n.inputs[k])].shape);
    }
    const Diagnostic written = m.problem();
    return Refusal{written.message};
}

// The axes of `over` along which a read of a value of shape `held` moves, one bit per axis, with the two shapes aligned
// at their last axes. A broadcast axis sets no bit.
constexpr std::uint32_t moves_along(const Shape& held, const Shape& over) {
    std::uint32_t axes = 0;
    for (std::int32_t a = 0, lead = over.rank - held.rank; a < held.rank; ++a) {
        const Extent e = extent_at(held, a);
        axes |= e != literal_extent(1) && lead + a >= 0 && e == extent_at(over, lead + a) ? std::uint32_t{1} << (lead + a) : 0u;
    }
    return axes;
}

// Each value's run of lags 1, …, k that runs (`slots`: the lowering hands up to k samples of it on in registers,
// `loops/normalize.hpp` `find_state`; a lag past a gap is read from memory), and each unit's state: a register for
// every value it makes that is lagged, its newest sample, which every stage keeps (`state_depth`). Counted at the
// maker, so that lags of one value from two nodes count once.
constexpr void count_states(const Graph& g, Gathering& at) {
    std::vector<std::uint64_t> backs(g.nodes.size(), 0); // per maker: bit d − 1 for a lag of d samples
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        const std::int32_t d = samples_back(g.nodes[i]);
        if (at.runs[i] && at.lags[i] && at.storage_of[i] >= 0 && d >= 1 && d <= kMaxStateDepth) {
            backs[static_cast<std::size_t>(at.storage_of[i])] |= std::uint64_t{1} << (d - 1);
        }
    }
    for (std::size_t m = 0; m < g.nodes.size(); ++m) {
        at.slots[m] = at.runs[m] ? std::countr_one(backs[m]) : 0;
        at.unit_states[static_cast<std::size_t>(at.unit_of[m])] += at.slots[m] > 0 ? 1 : 0;
    }
}

// The domain a stage that node `i` opens walks, its axes the loops `plan_loops` plans: a fold's operand, the operand
// of the fold its cycle's sample holds, a contraction's result, or else what `i` makes.
constexpr const Shape& opened_over(const Graph& g, const graph::Feedback& feedback, std::size_t i, const Gathering& at) {
    const heinzel::Node& n = g.nodes[i];
    const bool folds = planner::collapses_an_axis(n) || graph::is_a_contraction(n);
    const std::int32_t sample = feedback.cycle[i] >= 0 ? at.fold_of[static_cast<std::size_t>(feedback.cycle[i])] : -1;
    const heinzel::Node& first = sample >= 0 ? g.nodes[static_cast<std::size_t>(sample)] : n; // (a cycle's first node may be over the result)
    const bool operand = (folds || sample >= 0) && !graph::is_a_contraction(first);
    return g.values[static_cast<std::size_t>(operand ? first.inputs[0] : n.output)].shape;
}

// The work gathered ahead of every unit with a loop (`next_unit`): each node that runs and makes, from timeless values
// alone, a value of no axis, so that a stage it opens walks no loop (`graph::ahead_of_the_loops`; the rewrite layer's
// mirror of the chaining keeps it out of a cycle's stage as timeless, `graph::detail::leads_into`). Its pieces are
// joined by what one hands another, each named by its first node.
constexpr void find_the_work_ahead(const Graph& g, const graph::Facts& facts, Gathering& at) {
    const std::vector<char> ahead = graph::ahead_of_the_loops(g, facts);
    std::vector<std::int32_t>& piece = at.ahead;
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        piece[i] = ahead[i] != 0 ? static_cast<std::int32_t>(i) : -1;
    }
    const auto root = [&](std::int32_t v) {
        for (; piece[static_cast<std::size_t>(v)] != v; v = piece[static_cast<std::size_t>(v)]) {
            piece[static_cast<std::size_t>(v)] = piece[static_cast<std::size_t>(piece[static_cast<std::size_t>(v)])];
        }
        return v;
    };
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        for (std::size_t k = 0; piece[i] >= 0 && k < rules_of(g.nodes[i].op).arity; ++k) {
            const std::int32_t m = facts.producer[static_cast<std::size_t>(detail::source_of(g, facts, i, k))];
            if (m >= 0 && piece[static_cast<std::size_t>(m)] >= 0) {
                const std::int32_t a = root(static_cast<std::int32_t>(i));
                const std::int32_t b = root(m);
                piece[static_cast<std::size_t>(a < b ? b : a)] = a < b ? a : b;
            }
        }
    }
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        piece[i] = piece[i] >= 0 ? root(static_cast<std::int32_t>(i)) : -1;
    }
}

// Who reads whom, what each node reads storage of, and the units.
constexpr void begin(const Graph& g, const graph::Facts& facts, const graph::Feedback& feedback, Gathering& at) {
    const std::size_t nodes = g.nodes.size();
    detail::size_to(g, at);
    for (std::size_t i = 0; i < nodes; ++i) {
        const heinzel::Node& n = g.nodes[i];
        if (feedback.cycle[i] >= 0 && graph::is_a_fold(n)) { // (`find_feedback` admits one, not over time)
            at.fold_of[static_cast<std::size_t>(feedback.cycle[i])] = static_cast<std::int32_t>(i);
        }
        at.runs[i] = facts.statement[i] && !graph::is_generated(n);
        at.lags[i] = graph::hands_its_operand_on(n) && samples_back(n) > 0;
        const std::int32_t stored = graph::is_an_affine_view(n) && facts.statement[i] ? facts.through[static_cast<std::size_t>(n.inputs[0])].source
                                    : at.lags[i] || graph::gathers(n) != Gather::None
                                        ? facts.read_as[static_cast<std::size_t>(n.inputs[graph::gathers(n) == Gather::Table ? 1 : 0])]
                                        : kNoValue;
        at.storage_of[i] = stored == kNoValue ? -1 : facts.producer[static_cast<std::size_t>(stored)];
    }
    for (std::size_t i = 0; i < nodes; ++i) {
        for (std::size_t k = 0; k < kMaxNodeInputs; ++k) {
            at.maker[i][k] = -1;
            if (k >= rules_of(g.nodes[i].op).arity) {
                continue;
            }
            const std::int32_t m = facts.producer[static_cast<std::size_t>(facts.read_as[static_cast<std::size_t>(g.nodes[i].inputs[k])])];
            const bool own_cycle = m >= 0 && feedback.cycle[i] >= 0 && feedback.cycle[static_cast<std::size_t>(m)] == feedback.cycle[i];
            at.maker[i][k] = own_cycle ? -1 : m;
            at.missing[i] += at.maker[i][k] >= 0 ? 1 : 0;
            at.first_reader[static_cast<std::size_t>(at.maker[i][k] >= 0 ? at.maker[i][k] + 1 : 0)] += at.maker[i][k] >= 0 ? 1 : 0;
        }
    }
    for (std::size_t m = 0; m < nodes; ++m) {
        at.first_reader[m + 1] += at.first_reader[m];
    }
    detail::find_the_work_ahead(g, facts, at);
    for (std::size_t p = 0; p < feedback.order.size(); ++p) {
        const std::size_t i = static_cast<std::size_t>(feedback.order[p]);
        const bool continues = p > 0 && feedback.cycle[i] >= 0 && feedback.cycle[static_cast<std::size_t>(feedback.order[p - 1])] == feedback.cycle[i];
        at.unit_of[i] = continues ? at.unit_of[static_cast<std::size_t>(feedback.order[p - 1])] : static_cast<std::int32_t>(p);
        at.waiting[static_cast<std::size_t>(at.unit_of[i])] += at.runs[i] ? 1 : 0;
    }
    at.unit_runs = at.waiting;
    detail::count_states(g, at);
    detail::price_units(g, facts, feedback, at);
    std::vector<std::int32_t> placed(nodes, 0);
    for (std::size_t i = 0; i < nodes; ++i) {
        for (std::size_t k = 0; k < kMaxNodeInputs; ++k) {
            if (const std::int32_t m = at.maker[i][k]; m >= 0) {
                at.reader[static_cast<std::size_t>(at.first_reader[static_cast<std::size_t>(m)] + placed[static_cast<std::size_t>(m)]++)] =
                    static_cast<std::int32_t>(i);
            }
        }
    }
}

// Node `i` waits for no operand any more. A node without a statement is visited next; a unit all of whose nodes are
// there becomes ready, and the ready units are kept in planning order.
constexpr void arrived(std::size_t i, Gathering& at) {
    if (!at.runs[i]) {
        at.no_loops.push_back(static_cast<std::int32_t>(i));
        return;
    }
    const std::int32_t unit = at.unit_of[i];
    if (--at.waiting[static_cast<std::size_t>(unit)] == 0) {
        std::size_t k = at.ready.size();
        at.ready.push_back(unit);
        for (; k > 0 && at.ready[k - 1] > unit; --k) {
            at.ready[k] = at.ready[k - 1];
        }
        at.ready[k] = unit;
    }
}

// Node `m` has run: what reads it has one operand fewer to wait for.
constexpr void visited(std::size_t m, Gathering& at) {
    at.visited[m] = true;
    for (std::int32_t r = at.first_reader[m]; r < at.first_reader[m + 1]; ++r) {
        const std::size_t i = static_cast<std::size_t>(at.reader[static_cast<std::size_t>(r)]);
        if (--at.missing[i] == 0) {
            detail::arrived(i, at);
        }
    }
}

// The open stage is done: what it made is now that stage's. As a feedback stage closes, whole, its feed joins it where
// the model prices that no dearer (`as_it_closes`). Its counts join the stage's raw, which `priced_open` prices.
constexpr void fold_in_the_feed(const Graph& g, Gathered& gathered, Gathering& at) {
    bool folds = false;
    detail::as_it_closes(g, gathered, at, folds);
    if (!folds) {
        return;
    }
    auto nodes = gathered.groups.back().stage.nodes;
    for (const std::int32_t ni : at.open.stage.nodes) {
        nodes.push_back(ni);
    }
    at.open.stage.nodes = nodes;
    at.open_cost.slots += at.last_cost.slots, at.open_cost.depth += at.last_cost.depth;
    gathered.groups.pop_back(); // (its values' stage index is the merged stage's)
}

constexpr void close_open(const Graph& g, const graph::Facts& facts, Gathered& gathered, Gathering& at) {
    detail::fold_in_the_feed(g, gathered, at);
    at.last_cost = detail::priced_open(g, at);
    at.open.counts = at.last_cost;
    detail::lags_before_their_makers(g, at);
    detail::in_three_parts(g, facts, at);
    const std::int32_t index = static_cast<std::int32_t>(gathered.groups.size());
    for (std::size_t v = 0; v < at.made_in.size(); ++v) {
        at.made_in[v] = at.made_in[v] == kOpen ? index : at.made_in[v];
        at.after[v] = false;
        at.walks[v] = 0;
    }
    at.reads_from = {};
    for (const std::int32_t c : at.held_now) {
        at.held[static_cast<std::size_t>(c)] = false;
    }
    for (const std::int32_t v : at.loaded_now) {
        at.loaded[static_cast<std::size_t>(v)] = false;
    }
    at.held_now.clear();
    at.loaded_now.clear();
    at.states = 0;
    at.open_cost = {};
    at.trail_slots = 0, at.once_slots = 0;
    at.open_lag = kNoLag;
    at.open_hands_to = -1;
    at.open.stage.state_depth = at.open.cycle >= 0 ? detail::state_depth(g, at) : kMaxStateDepth;
    detail::close(gathered, at.open);
}

// Does node `i` read, through views with no statement, an after value of the open stage?
constexpr bool reads_an_after_value(const Graph& g, const graph::Facts& facts, std::size_t i, const Gathering& at) {
    for (std::size_t k = 0; k < rules_of(g.nodes[i].op).arity; ++k) {
        if (at.after[static_cast<std::size_t>(detail::source_of(g, facts, i, k))]) {
            return true;
        }
    }
    return false;
}

// Two kinds of node the open fold stage takes although a scan of the graph's order would not offer them to it, only
// where its fold is a plain fold, the stage is no cycle and the node is related to it: a sibling fold over the same
// domain, collapsing the same axes to the same result, and a producer inside its loops, a map over its domain that
// reads nothing the fold's loops have not closed over yet.
constexpr bool joins_a_fold_group(const Graph& g, const graph::Facts& facts, std::size_t i, const Gathering& at) {
    const graph::GroupShape& open = at.open.shape;
    const heinzel::Node& n = g.nodes[i];
    if (open.fold < 0 || at.open.cycle >= 0 || !graph::is_a_fold(g.nodes[static_cast<std::size_t>(open.fold)]) || detail::reads_an_after_value(g, facts, i, at) ||
        !detail::related(g, facts, i, at)) {
        return false;
    }
    const Shape& produced = g.values[static_cast<std::size_t>(n.output)].shape;
    if (!graph::is_a_fold(n)) {
        return !planner::collapses_an_axis(n) && produced == open.domain && produced != open.folded;
    }
    return graph::collapsed_axes(n, open.domain.rank) == open.collapsed && g.values[static_cast<std::size_t>(n.inputs[0])].shape == open.domain &&
           produced == open.folded;
}

// May node `i` join the open stage (rather than close it and open the next)?
constexpr bool joins(const Graph& g, const graph::Facts& facts, const graph::Feedback& feedback, const Gathered& gathered, std::size_t i,
                     const Gathering& at) {
    const OpenGroup& open = at.open;
    const heinzel::Node& n = g.nodes[i];
    if (open.stage.nodes.empty()) {
        return true;
    }
    // A piece of the work gathered ahead is a stage of its own (`next_unit`), so that one band's knob math can be
    // skipped apart from another's while its knobs rest (`docs/knob-math.md`).
    if (at.ahead[i] != at.ahead[static_cast<std::size_t>(open.stage.nodes[0])]) {
        return false;
    }
    const std::int32_t c = feedback.cycle[i];
    const bool sample = detail::folds_each_sample(at);
    const bool held = c == open.cycle || (c >= 0 && at.held[static_cast<std::size_t>(c)] != 0);
    if (detail::runs_skewed(at)) { // (its cycle, then what leads from it to its last section's slice, and the slice)
        return (held || at.to_the_slice[i] != 0) && open.stage.nodes.size() < graph::kMaxGroupStmts;
    }
    const bool joined = held || detail::chains_on(g, facts, feedback, i, at) || detail::trails(g, facts, feedback, gathered, i, at) ||
                        detail::fold_joins(g, facts, i, at);
    if (!joined || open.stage.nodes.size() == graph::kMaxGroupStmts || graph::is_made_in_parts(n) || graph::is_a_contraction(n)) {
        return false;
    }
    if (planner::collapses_an_axis(n)) {
        return open.shape.fold < 0 ? open.shape.domain == g.values[static_cast<std::size_t>(n.inputs[0])].shape : detail::joins_a_fold_group(g, facts, i, at);
    }
    // A read of storage the open stage makes (`storage_of`) comes after the stage.
    const std::int32_t stored = at.storage_of[i];
    if (stored >= 0 && at.made_in[static_cast<std::size_t>(g.nodes[static_cast<std::size_t>(stored)].output)] == kOpen && !(at.lags[i] && open.cycle >= 0)) {
        return false;
    }
    const Shape& produced = g.values[static_cast<std::size_t>(n.output)].shape;
    if (sample) { // (its cycle's nodes run over the fold's operand or its result, `graph::find_feedback` says)
        return c == open.cycle;
    }
    if (c < 0 && detail::folds_voices(g, at) && produced == open.shape.folded) { // (what follows a fold of voices: the model's call)
        return detail::trails(g, facts, feedback, gathered, i, at);
    }
    return (produced == (open.shape.fold >= 0 ? open.shape.folded : open.shape.domain) && detail::keeps_lanes(g, i, at)) ||
           detail::joins_a_fold_group(g, facts, i, at);
}

// Node `i`, costing `cost`, joins the open stage's counts: its slots, its depth (its longest path in latencies from what
// the stage reads, a lag starting afresh), and its cycle's chain.
constexpr void count_cost(const Graph& g, const graph::Facts& facts, const graph::Feedback& feedback, std::size_t i, OpCost cost, Gathering& at) {
    double before = 0;
    for (std::size_t k = 0; k < rules_of(g.nodes[i].op).arity && !at.lags[i]; ++k) {
        const std::int32_t source = detail::source_of(g, facts, i, k);
        const std::int32_t m = facts.producer[static_cast<std::size_t>(source)];
        const bool open = m >= 0 && at.made_in[static_cast<std::size_t>(source)] == kOpen;
        before = open && at.depth_at[static_cast<std::size_t>(m)] > before ? at.depth_at[static_cast<std::size_t>(m)] : before;
    }
    at.depth_at[i] = before + cost.latency;
    at.open_cost.depth = at.depth_at[i] > at.open_cost.depth ? at.depth_at[i] : at.open_cost.depth;
    at.open_cost.slots += cost.slots;
    const std::size_t u = static_cast<std::size_t>(at.unit_of[i]);
    const double chain = feedback.cycle[i] >= 0 ? at.unit_chain[u] : 0;
    at.open_cost.chain = chain > at.open_cost.chain ? chain : at.open_cost.chain;
    at.open_lag = feedback.cycle[i] >= 0 && at.unit_lag[u] < at.open_lag ? at.unit_lag[u] : at.open_lag;
}

// Node `i` joins the open stage: its domain and fold are known, and what it makes and reads is recorded.
constexpr void push(const Graph& g, const graph::Facts& facts, const graph::Feedback& feedback, std::size_t i, Gathering& at) {
    graph::GroupShape& open = at.open.shape;
    const heinzel::Node& n = g.nodes[i];
    const Shape& produced = g.values[static_cast<std::size_t>(n.output)].shape;
    const bool folds = planner::collapses_an_axis(n) || graph::is_a_contraction(n);
    if (at.open.stage.nodes.empty()) {
        open.domain = detail::opened_over(g, feedback, i, at);
    }
    const bool after = folds || (open.fold >= 0 && (open.domain == open.folded || produced == open.folded));
    if (folds && open.fold < 0) {
        open.fold = static_cast<std::int32_t>(i);
        open.folded = produced;
        open.collapsed = graph::collapsed_axes(n, open.domain.rank);
    }
    const bool over_the_result = !folds && after;
    for (std::size_t k = 0; k < rules_of(n.op).arity; ++k) {
        const std::int32_t v = n.inputs[k];
        const std::int32_t source = detail::source_of(g, facts, i, k);
        const std::int32_t from = at.made_in[static_cast<std::size_t>(source)];
        const std::uint32_t axes = detail::moves_along(g.values[static_cast<std::size_t>(v)].shape, over_the_result ? open.folded : open.domain);
        const std::uint32_t moves = over_the_result && axes != 0 ? kOverTheResult : axes;
        at.walks[static_cast<std::size_t>(source)] |= g.values[static_cast<std::size_t>(v)].category == ValueCategory::Const ? 0u : moves;
        if (from >= 0 && static_cast<std::size_t>(from) < kMaxStages) {
            at.reads_from[static_cast<std::size_t>(from)] |= moves;
        }
        if (from != kOpen && !at.loaded[static_cast<std::size_t>(source)] && g.values[static_cast<std::size_t>(source)].category != ValueCategory::Const) {
            at.loaded[static_cast<std::size_t>(source)] = true; // (a load of what the stage reads from outside it)
            at.loaded_now.push_back(source);
        }
    }
    at.states += at.slots[i] > 0 ? 1 : 0;
    const OpCost cost = at.runs[i] ? detail::cost_of(g, n, at.machine) : OpCost{0, 0};
    if (const std::int32_t c = at.target[i]; c >= 0) { // (one chain node into `c` fewer to come)
        --at.leads[static_cast<std::size_t>(c)];
        at.lead_states[static_cast<std::size_t>(c)] -= at.slots[i] > 0 ? 1 : 0;
        at.lead_slots[static_cast<std::size_t>(c)] -= cost.slots;
    }
    detail::count_cost(g, facts, feedback, i, cost, at);
    at.trail_slots += at.trail[i] != 0 && at.open.cycle >= 0 ? cost.slots : 0; // (work after its cycles, `trails`)
    at.once_slots += over_the_result ? cost.slots : 0;
    at.made_in[static_cast<std::size_t>(n.output)] = kOpen;
    at.after[static_cast<std::size_t>(n.output)] = after;
    const std::int32_t c = feedback.cycle[i];
    at.open.cycle = at.open.stage.nodes.empty() ? c : at.open.cycle; // (a stage chained on keeps its first cycle)
    at.open_hands_to = c >= 0 && at.open_hands_to < 0 ? at.hands_to[static_cast<std::size_t>(c)] : at.open_hands_to;
    at.open.stage.nodes.push_back(static_cast<std::int32_t>(i));
    if (c >= 0 && !at.held[static_cast<std::size_t>(c)]) {
        at.held[static_cast<std::size_t>(c)] = true;
        at.held_now.push_back(c);
    }
}

// Node `i` runs: it joins the open stage, or closes it and opens the next. A value made in parts is a stage of its
// own, which lowers to one loop nest per part.
constexpr void gather(const Graph& g, const graph::Facts& facts, const graph::Feedback& feedback, std::size_t i, Gathered& gathered, Gathering& at) {
    if (graph::is_made_in_parts(g.nodes[i]) || !detail::joins(g, facts, feedback, gathered, i, at)) {
        detail::close_open(g, facts, gathered, at);
    }
    detail::push(g, facts, feedback, i, at);
    if (graph::is_made_in_parts(g.nodes[i])) {
        detail::close_open(g, facts, gathered, at);
    }
}

// What runs next, among the ready units (a node, or a whole feedback cycle). The work gathered ahead (`ahead`), work of
// no axis made from timeless values alone, comes before every unit with a loop, the open stage's piece of it first: a
// stage with no loop between two with loops keeps them from chaining, as a filter section's coefficients worked out
// from its knobs did between it and the section before (an 8-band EQ of RBJ peaks ran as eight loops, 19.1 ns a sample,
// against 9.4 with its coefficients set as Params). Of the units so taken, the first in planning order that
// may join the open stage and is related to it, or else the first. A scan of the graph's order would visit the same,
// so a graph in which nothing related waits behind an unrelated node keeps its stages. Returns the unit's first
// position in `feedback.order`, taken out of `ready`.
constexpr std::expected<std::size_t, Refusal> next_unit(const Graph& g, const graph::Facts& facts, const graph::Feedback& feedback, const Gathered& gathered,
                                                        Gathering& at) {
    if (at.ready.empty()) { // (an invariant the order of feedback keeps: no case reaches it)
        return std::unexpected("heinzel::compile: no node is ready to run, which the order of feedback promises never happens");
    }
    const std::int32_t open = at.open.stage.nodes.empty() ? -1 : at.ahead[static_cast<std::size_t>(at.open.stage.nodes[0])];
    // (how early a ready unit is taken: 2 the open stage's piece of the work ahead, 1 other work ahead, 0 the rest)
    const auto earliness = [&](std::size_t r) {
        const std::int32_t piece = at.ahead[static_cast<std::size_t>(feedback.order[static_cast<std::size_t>(at.ready[r])])];
        return piece < 0 ? 0 : piece == open ? 2 : 1;
    };
    int earliest = 0;
    for (std::size_t r = 0; r < at.ready.size(); ++r) {
        earliest = earliness(r) > earliest ? earliness(r) : earliest;
    }
    std::size_t k = at.ready.size();
    for (std::size_t r = 0; r < at.ready.size(); ++r) {
        if (earliness(r) != earliest) {
            continue;
        }
        k = k == at.ready.size() ? r : k;
        const std::size_t i = static_cast<std::size_t>(feedback.order[static_cast<std::size_t>(at.ready[r])]);
        // (a cycle joins only by chaining on, which asks whether it relates)
        const bool relates = feedback.cycle[i] >= 0 || detail::related(g, facts, i, at) || detail::trails(g, facts, feedback, gathered, i, at);
        if (!at.open.stage.nodes.empty() && relates && detail::joins(g, facts, feedback, gathered, i, at)) {
            k = r;
            break;
        }
    }
    const std::size_t p = static_cast<std::size_t>(at.ready[k]);
    for (std::size_t j = k; j + 1 < at.ready.size(); ++j) { // (in order, so the rest shifts down)
        at.ready[j] = at.ready[j + 1];
    }
    at.ready.pop_back();
    return p;
}

} // namespace detail

// The pass: the stages, in the order they run — or why the graph cannot run here. The machine bounds what one feedback
// stage may take on (`still_fits`).
constexpr std::expected<void, Refusal> gather_groups(const Graph& g, const graph::Facts& facts, const graph::Feedback& feedback, const Machine& m,
                                                     Gathered& gathered) {
    for (const std::int32_t ni : feedback.order) {
        const std::size_t i = static_cast<std::size_t>(ni);
        if (const Refusal why = detail::why_it_cannot_run(g, facts, g.nodes[i], facts.statement[i] != 0); !why.empty()) {
            const Refusal named = detail::worded(g, g.nodes[i], why.view());
            return std::unexpected(named);
        }
    }
    detail::Gathering at{};
    at.machine = m;
    detail::begin(g, facts, feedback, at);
    detail::find_chains(g, feedback, at);
    at.hands_to = graph::handed_to(g, facts, feedback, at.target);
    detail::find_the_slices(g, feedback, at);
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        if (at.missing[i] == 0) {
            detail::arrived(i, at);
        }
    }
    for (std::size_t left = g.nodes.size(); left > 0;) {
        // No loops of its own: a view's readers read its source, a ramp's its storage.
        while (!at.no_loops.empty()) {
            const std::size_t i = static_cast<std::size_t>(at.no_loops.back());
            at.no_loops.pop_back();
            detail::visited(i, at);
            --left;
        }
        if (left == 0) {
            break;
        }
        const std::expected<std::size_t, Refusal> p = detail::next_unit(g, facts, feedback, gathered, at);
        if (!p) {
            return std::unexpected(p.error());
        }
        const std::int32_t unit = static_cast<std::int32_t>(*p);
        for (std::size_t q = *p; q < feedback.order.size() && at.unit_of[static_cast<std::size_t>(feedback.order[q])] == unit; ++q) {
            const std::size_t i = static_cast<std::size_t>(feedback.order[q]);
            if (at.runs[i]) { // (what has no statement is visited from `no_loops`)
                detail::gather(g, facts, feedback, i, gathered, at);
                detail::visited(i, at);
                --left;
            }
        }
    }
    detail::close_open(g, facts, gathered, at);
    return {};
}

} // namespace heinzel::loops::planner
