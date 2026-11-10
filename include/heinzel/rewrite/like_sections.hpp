#pragma once

// What sections in lanes (`sections_in_lanes.hpp`) recognises: runs of like sections, each reading the one before.
//
// A section is a cycle of `loops/graph/feedback.hpp` (one inside a larger cycle is part of that one, so no section of
// its own) and the work of no cycle feeding it from the section before: the maps, hand-ons and table reads it reads,
// now or in the past, back to the section before or to a timeless value (`graph::timeless_of`). *Like* (P5's,
// Plan/23-audio-scheduler/05, widened: `docs/sections-in-lanes.md`): the nodes of two sections pair one for one, the
// cycles' in node order and the work feeding them by what reads what, each pair one row of the rule table and
// attribute, one type, shape and precision; an operand of the section is its partner's, the section's input is the one
// before's (made outside the two), any other is a timeless value of one type, its value free. A run is maximal: from a
// cycle, as far as the next section is like and each is read outside only by the next. Linear: each node joins one
// section, and two sections pair twice at most (the last of a run, dropped from it, may begin the next). Names no
// operation.

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/ir/timing.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/schedule/machine.hpp"

namespace heinzel::rewrite {
namespace detail {

inline constexpr std::int32_t kMany = -2; // (more than one)

// May the node run in a section: a map, a hand-on (a lag, a recurrence, a conversion) or a table read, what a cycle runs
// (`feedback.hpp` `runs_in_the_cycle`) but a link or a fold, which run over more than one voice?
constexpr bool sectionable(const Node& n) {
    return loops::graph::is_a_plain_map(n) || loops::graph::hands_its_operand_on(n) || loops::graph::gathers(n) == loops::Gather::Table;
}

// What the rule reads of a graph, worked out once. Per value as read (`Facts::read_as`: a variable is its definition),
// the nodes reading it, once per operand, and whether it is an output. Per cycle, by the node naming it: its nodes in
// node order; how many nodes its section holds; the one value from outside its section it reads that is not timeless
// (`kNoValue`: none, `kMany`: more); the cycle whose section reads its value (-1: none, `kMany`: more). Per node: the cycle whose section holds it (a node of no cycle joins the first section to reach it),
// and its partners in the sections before and after it, once paired (-1: none).
struct Reading {
    loops::graph::Facts facts{};
    loops::graph::Feedback feedback{};
    std::vector<char> timeless{};
    std::vector<std::vector<std::int32_t>> readers{};
    std::vector<char> output{};
    std::vector<std::vector<std::int32_t>> members{};
    std::vector<std::int32_t> size{};
    std::vector<std::int32_t> input{};
    std::vector<std::int32_t> after{};
    std::vector<std::int32_t> owner{};
    std::vector<std::int32_t> before{};
    std::vector<std::int32_t> next{};
};

constexpr std::int32_t as_read(const Reading& r, std::int32_t v) { return r.facts.read_as[static_cast<std::size_t>(v)]; }
constexpr std::int32_t made_by(const Reading& r, std::int32_t v) { return r.facts.producer[static_cast<std::size_t>(as_read(r, v))]; }
constexpr std::int32_t cycle_of(const Reading& r, std::int32_t i) { return r.feedback.cycle[static_cast<std::size_t>(i)]; }

// Cycle `c`'s section: its nodes, and the maps, hand-ons and table reads of no cycle they read that are not timeless,
// each joining the first section to reach it; what is left from outside, not timeless, is its input. (A cycle holds
// what may run in a section but a link or a fold, and their sections are over more than one voice: `qualifies`.)
constexpr void gather_section(Reading& r, const Graph& g, std::int32_t c) {
    std::vector<std::int32_t> queue(r.members[static_cast<std::size_t>(c)]);
    std::int32_t outside = kNoValue;
    for (std::size_t at = 0; at < queue.size(); ++at) {
        const Node& n = g.nodes[static_cast<std::size_t>(queue[at])];
        for (std::size_t k = 0; k < rules_of(n.op).arity; ++k) {
            const std::int32_t v = as_read(r, n.inputs[k]);
            const std::int32_t p = made_by(r, v);
            const bool fresh = r.timeless[static_cast<std::size_t>(v)] == 0;
            if (p >= 0 && r.owner[static_cast<std::size_t>(p)] == c) {
                continue;
            }
            if (p >= 0 && fresh && cycle_of(r, p) < 0 && r.owner[static_cast<std::size_t>(p)] < 0 && sectionable(g.nodes[static_cast<std::size_t>(p)])) {
                r.owner[static_cast<std::size_t>(p)] = c;
                queue.push_back(p);
            } else if (fresh) {
                outside = outside == kNoValue || outside == v ? v : kMany;
            }
        }
    }
    r.size[static_cast<std::size_t>(c)] = static_cast<std::int32_t>(queue.size());
    r.input[static_cast<std::size_t>(c)] = outside;
}

constexpr Reading reading_of(const Graph& g) {
    const std::size_t count = g.nodes.size();
    Reading r{.facts = loops::graph::facts_of(g)};
    r.feedback = loops::graph::find_feedback(g, r.facts);
    if (!r.facts.why_not.empty() || !r.feedback.why_not.empty()) {
        return r; // (the planner refuses it, by name)
    }
    r.timeless = loops::graph::timeless_of(g);
    r.readers.assign(g.values.size(), {});
    r.output.assign(g.values.size(), 0);
    r.members.assign(count, {});
    r.size.assign(count, -1);
    r.input.assign(count, kNoValue);
    r.after.assign(count, -1);
    r.owner.assign(count, -1);
    r.before.assign(count, -1);
    r.next.assign(count, -1);
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t k = 0; k < rules_of(g.nodes[i].op).arity; ++k) {
            r.readers[static_cast<std::size_t>(as_read(r, g.nodes[i].inputs[k]))].push_back(static_cast<std::int32_t>(i));
        }
        if (const std::int32_t c = cycle_of(r, static_cast<std::int32_t>(i)); c >= 0) {
            r.members[static_cast<std::size_t>(c)].push_back(static_cast<std::int32_t>(i));
            r.owner[i] = c;
        }
    }
    for (const NamedValue& out : g.outputs) {
        r.output[static_cast<std::size_t>(as_read(r, out.value))] = 1;
    }
    for (std::size_t c = 0; c < count; ++c) {
        if (cycle_of(r, static_cast<std::int32_t>(c)) == static_cast<std::int32_t>(c)) {
            detail::gather_section(r, g, static_cast<std::int32_t>(c));
        }
    }
    for (std::size_t c = 0; c < count; ++c) {
        const std::int32_t p = r.input[c] >= 0 ? made_by(r, r.input[c]) : -1;
        const std::int32_t from = p >= 0 ? cycle_of(r, p) : -1;
        if (from >= 0 && from != static_cast<std::int32_t>(c)) {
            std::int32_t& after = r.after[static_cast<std::size_t>(from)];
            after = after == -1 ? static_cast<std::int32_t>(c) : kMany;
        }
    }
    return r;
}

// ---- like sections -------------------------------------------------------------------------------------------

// Do nodes `a` and `b` do the same: one row and attribute (the variable a recurrence defines aside), a result of one
// type, shape, precision and kind?
constexpr bool alike_nodes(const Graph& g, const Node& a, const Node& b) {
    OpAttr x = a.attr;
    x.recurrence.defines = b.attr.recurrence.defines;
    const Value& va = g.values[static_cast<std::size_t>(a.output)];
    const Value& vb = g.values[static_cast<std::size_t>(b.output)];
    return a.op == b.op && x == b.attr && va.type == vb.type && va.shape == vb.shape && va.precision == vb.precision &&
           va.category == vb.category && va.timeless == vb.timeless;
}

// The pairing of a section `a` with the section `b` after it, under way: `b`'s nodes paired so far; what `a`'s section
// reads where `b`'s reads `a`'s value (its input, found here where `a` begins a run); whether `b` reads it at this sample.
struct Pairing {
    std::int32_t a{-1};
    std::int32_t b{-1};
    std::int32_t input{kNoValue};
    std::vector<std::int32_t> work{};
    bool now{false};
};

// Pairs node `a` with node `b` of the section after it, unless either has another partner. Each section has one
// section after it, and a pairing no run keeps is undone (`unpaired`), so a node has one partner each way at most and
// the partners are one table for the whole graph.
constexpr bool pair(Reading& r, Pairing& p, std::int32_t a, std::int32_t b) {
    std::int32_t& next = r.next[static_cast<std::size_t>(a)];
    std::int32_t& before = r.before[static_cast<std::size_t>(b)];
    if (next == b && before == a) {
        return true;
    }
    if (next >= 0 || before >= 0) {
        return false;
    }
    next = b;
    before = a;
    p.work.push_back(b);
    return true;
}

// Operand `k` of node `nb` of section `b`, `vb`, and of its partner, `va` (both as read): like where both are made by
// their sections (the makers partners), where `vb` is `a`'s value and `va` `a`'s input, or where both are timeless and
// of one type. From the cycles' nodes, paired in node order, a node of a cycle pairs with one of a cycle and the work
// feeding it with work of no cycle: a node is in its cycle by what it reads (a lag of a variable defined later), and its
// partner reads the partners.
constexpr bool operands_alike(Reading& r, const Graph& g, Pairing& p, const Node& nb, std::size_t k, std::int32_t vb, std::int32_t va) {
    const std::int32_t qb = made_by(r, vb);
    const std::int32_t qa = made_by(r, va);
    if (qb >= 0 && r.owner[static_cast<std::size_t>(qb)] == p.b) {
        return qa >= 0 && detail::pair(r, p, qa, qb);
    }
    if (vb == r.input[static_cast<std::size_t>(p.b)]) {
        p.now = p.now || !operand_comes_from_the_past(nb, k);
        if (p.input == kNoValue && r.timeless[static_cast<std::size_t>(va)] == 0) {
            p.input = va;
        }
        return va == p.input;
    }
    const bool timeless = r.timeless[static_cast<std::size_t>(vb)] != 0 && r.timeless[static_cast<std::size_t>(va)] != 0;
    return timeless && g.values[static_cast<std::size_t>(vb)].type == g.values[static_cast<std::size_t>(va)].type;
}

// The sections of cycles `a` and `b`, `b`'s reading `a`'s value, paired node for node: the cycles' nodes in node order,
// the work feeding them by what reads what. `first`: `a` begins a run, so its section is what pairs with `b`'s, and
// its input what it reads where `b` reads `a`; otherwise its input is known, and all of its section pairs, since each
// of its nodes is reached from its cycle and reads none from outside but that input and timeless values. The nodes of
// `a`'s section in node order, `b`'s, and `a`'s input; none where the two are not like, `b` reads `a` only in the past,
// or `a`'s input is made by a node paired with one of `b`'s (where `b` reads `a`'s value, `a` reads its own).
struct Paired {
    std::vector<std::int32_t> nodes{};
    std::vector<std::int32_t> later{};
    std::int32_t input{kNoValue};
};

// The nodes `later` and their partners before them unpaired, so that each may pair afresh: a pairing found unlike, or
// one no run keeps, since a section found the last of one run may begin the next.
constexpr void unpaired(Reading& r, const std::vector<std::int32_t>& later) {
    for (const std::int32_t b : later) {
        std::int32_t& a = r.before[static_cast<std::size_t>(b)];
        r.next[static_cast<std::size_t>(a)] = -1;
        a = -1;
    }
}

constexpr std::optional<Paired> paired(Reading& r, const Graph& g, std::int32_t a, std::int32_t b, bool first) {
    const std::vector<std::int32_t>& ma = r.members[static_cast<std::size_t>(a)];
    const std::vector<std::int32_t>& mb = r.members[static_cast<std::size_t>(b)];
    Pairing p{.a = a, .b = b, .input = first ? kNoValue : r.input[static_cast<std::size_t>(a)]};
    bool like = ma.size() == mb.size();
    for (std::size_t i = 0; i < mb.size() && like; ++i) {
        like = detail::pair(r, p, ma[i], mb[i]);
    }
    for (std::size_t at = 0; at < p.work.size() && like; ++at) {
        const Node& nb = g.nodes[static_cast<std::size_t>(p.work[at])];
        const Node& na = g.nodes[static_cast<std::size_t>(r.before[static_cast<std::size_t>(p.work[at])])];
        like = detail::alike_nodes(g, na, nb);
        for (std::size_t k = 0; k < rules_of(nb.op).arity && like; ++k) {
            like = detail::operands_alike(r, g, p, nb, k, as_read(r, nb.inputs[k]), as_read(r, na.inputs[k]));
        }
    }
    Paired out{.later = p.work, .input = p.input};
    for (const std::int32_t w : p.work) {
        out.nodes.push_back(r.before[static_cast<std::size_t>(w)]);
    }
    like = like && p.now && p.input >= 0 && static_cast<std::int32_t>(p.work.size()) == r.size[static_cast<std::size_t>(b)];
    // The input comes from outside the pair: a value of a node paired here is made in the linked cycle, after the link
    // that would read it.
    like = like && std::find(out.nodes.begin(), out.nodes.end(), made_by(r, p.input)) == out.nodes.end();
    std::sort(out.nodes.begin(), out.nodes.end());
    if (!like) {
        detail::unpaired(r, p.work);
        return std::nullopt;
    }
    return out;
}

// ---- runs -------------------------------------------------------------------------------------------------

// How an operand from outside a run's sections is merged over them: one value all read (or one number, all constants);
// the constants as one constant tensor; anything else gathered (a chain of `where`).
enum class Merge : std::uint8_t { Shared, Constants, Gathered };
struct Outside {
    std::size_t at{0};
    std::size_t operand{0};
    Merge how{Merge::Shared};
};

// A run of like sections, to be one linked cycle: its cycles (by the nodes naming them) and each one's section, node
// for node (section j's t-th node the partner of section j - 1's), the first's input, where in a section stands the
// value the next one reads, and its operands from outside.
struct Run {
    std::vector<std::int32_t> cycles{};
    std::vector<std::vector<std::int32_t>> sections{};
    std::int32_t input{kNoValue};
    std::size_t out{0};
    std::vector<Outside> outside{};
};

// Does section `j` of `run` hold node `i`? The first holds what paired with the second's nodes; each other all its own.
constexpr bool holds(const Reading& r, const Run& run, std::size_t j, std::int32_t i) {
    const auto at = static_cast<std::size_t>(i);
    if (r.owner[at] != run.cycles[j]) {
        return false;
    }
    return j > 0 || (r.next[at] >= 0 && r.owner[static_cast<std::size_t>(r.next[at])] == run.cycles[1]);
}

// Is section `j` of `run` read outside only at the run's end? Its values by its own nodes, none an output, but the one
// the next section reads (at `run.out`): that by the next section's nodes too, or, where `j` is the last, by any node
// after node `end`, and as an output.
constexpr bool read_at_its_end(const Reading& r, const Graph& g, const Run& run, std::size_t j, std::int32_t end) {
    const bool last = j + 1 == run.cycles.size();
    for (std::size_t t = 0; t < run.sections[j].size(); ++t) {
        const auto v = static_cast<std::size_t>(as_read(r, g.nodes[static_cast<std::size_t>(run.sections[j][t])].output));
        const bool result = t == run.out;
        if (r.output[v] != 0 && !(result && last)) {
            return false;
        }
        for (const std::int32_t reader : r.readers[v]) {
            const bool onward = result && (last ? reader > end : detail::holds(r, run, j + 1, reader));
            if (!detail::holds(r, run, j, reader) && !onward) {
                return false;
            }
        }
    }
    return true;
}

// The run's last node, after which its linked cycle stands.
constexpr std::int32_t end_of(const Run& run) {
    std::int32_t end = -1;
    for (const std::vector<std::int32_t>& section : run.sections) {
        for (const std::int32_t i : section) {
            end = i > end ? i : end;
        }
    }
    return end;
}

// The run that begins with cycle `c`, as long as its sections are like and each is read outside only by the next, and
// there only where the first is read by the second (`run.out`); then, while its last is read outside but at its end, a
// section shorter. Of one section where none follows. The pairings past its last section are undone.
constexpr Run run_from(Reading& r, const Graph& g, std::int32_t c) {
    Run run{.cycles = {c}};
    std::vector<std::vector<std::int32_t>> pairings{};
    for (std::int32_t at = c; r.after[static_cast<std::size_t>(at)] >= 0;) {
        const std::int32_t b = r.after[static_cast<std::size_t>(at)];
        const bool first = run.cycles.size() == 1;
        const std::optional<Paired> p = detail::paired(r, g, at, b, first);
        if (!p) {
            break;
        }
        pairings.push_back(p->later);
        if (first) {
            const std::int32_t value = made_by(r, r.input[static_cast<std::size_t>(b)]); // (a node of `at`'s cycle: paired)
            run.sections.push_back(p->nodes);
            run.input = p->input;
            run.out = static_cast<std::size_t>(std::find(p->nodes.begin(), p->nodes.end(), value) - p->nodes.begin());
        }
        std::vector<std::int32_t> partners{};
        for (const std::int32_t i : run.sections.back()) {
            partners.push_back(r.next[static_cast<std::size_t>(i)]);
        }
        run.cycles.push_back(b);
        run.sections.push_back(partners);
        if (!detail::read_at_its_end(r, g, run, run.cycles.size() - 2, -1)) {
            run.cycles.pop_back();
            run.sections.pop_back();
            break;
        }
        at = b;
    }
    while (run.cycles.size() >= 2 && !detail::read_at_its_end(r, g, run, run.cycles.size() - 1, detail::end_of(run))) {
        run.cycles.pop_back();
        run.sections.pop_back();
    }
    for (std::size_t j = run.cycles.size() - 1; j < pairings.size(); ++j) {
        detail::unpaired(r, pairings[j]);
    }
    return run;
}

// Of one number a sample: every extent 1, as a Param's or a timeless tensor's of one number.
constexpr bool one_number(const Shape& s) {
    for (std::int32_t a = 0; a < s.rank; ++a) {
        if (!(extent_at(s, a) == literal_extent(1))) {
            return false;
        }
    }
    return true;
}

// The k values the sections read at operand `operand` of their node `at`.
constexpr std::vector<std::int32_t> outside_values(const Graph& g, const Run& run, std::size_t at, std::size_t operand) {
    std::vector<std::int32_t> values{};
    for (const std::vector<std::int32_t>& section : run.sections) {
        values.push_back(g.nodes[static_cast<std::size_t>(section[at])].inputs[operand]);
    }
    return values;
}

// How the k values one operand reads from outside are merged; none where some must be gathered and one is more than
// one number a sample.
constexpr std::optional<Merge> merge_of(const Graph& g, const std::vector<std::int32_t>& values) {
    const Value& first = g.values[static_cast<std::size_t>(values[0])];
    bool shared = true;
    bool constants = true;
    bool gatherable = true;
    for (const std::int32_t v : values) {
        const Value& each = g.values[static_cast<std::size_t>(v)];
        const bool constant = each.category == ValueCategory::Const;
        const bool same_number = constant && first.category == ValueCategory::Const && each.type == first.type &&
                                 std::bit_cast<std::uint64_t>(each.constant) == std::bit_cast<std::uint64_t>(first.constant);
        shared = shared && (v == values[0] || same_number);
        constants = constants && constant;
        gatherable = gatherable && one_number(each.shape);
    }
    if (shared || constants) {
        return shared ? Merge::Shared : Merge::Constants;
    }
    return gatherable ? std::optional<Merge>{Merge::Gathered} : std::nullopt;
}

// Is all the past `run`'s linked cycle reads held in registers: each lag of a value one of a run 1, 2, … of that
// value's lags (a bit each in a 64-bit word, as the lowering counts them; the link's read fills no gap, as it opens no
// slot), and standing before the node making that value? The skewed stage reads nothing along time from memory but at
// one lane (`loops/validate.hpp` `check_links`), and the lowering holds a lag in registers only where it stands before
// the statement storing what it reads (`loops/implied.hpp` `find_state`); else the stage would be refused, and
// `compile` would plan the graph as written, every rule lost. The planner lays each lag of a run just before its maker
// (`planner/chains.hpp` `lags_before_their_makers`), but the lags of a lag it moves stay where they were, after it: a
// lag of a lag is held only where the inner one reads what is made after it and so stays, a cycle's own state
// (`y1 = y[n-1]`, then `y1[n-1]`). So a lag past a gap (RBJ's band-pass, b1 = 0, reads x[n - 2] without x[n - 1]) is
// declined, and so is a lag of a lag of the input, of the work feeding the cycle, or of an older lag.
constexpr bool past_in_registers(const Reading& r, const Graph& g, const Run& run) {
    std::vector<std::uint64_t> backs(g.values.size(), 0); // per value as read: bit d - 1 for a lag of d samples
    const auto lag = [&g](std::int32_t i) {
        const Node& n = g.nodes[static_cast<std::size_t>(i)];
        return loops::graph::hands_its_operand_on(n) && samples_back(n) > 0;
    };
    for (const std::int32_t i : run.sections[0]) {
        const Node& n = g.nodes[static_cast<std::size_t>(i)];
        if (const std::int32_t d = samples_back(n); lag(i) && d <= 64) {
            backs[static_cast<std::size_t>(as_read(r, n.inputs[0]))] |= std::uint64_t{1} << (d - 1);
        }
    }
    for (const std::int32_t i : run.sections[0]) {
        const Node& n = g.nodes[static_cast<std::size_t>(i)];
        const std::int32_t v = as_read(r, n.inputs[0]);
        const std::int32_t inner = v == run.input ? -1 : made_by(r, v); // (the input's lags read the link)
        const bool moved = inner >= 0 && lag(inner) && loops::graph::cycle_closes_at(g, r.facts, static_cast<std::size_t>(inner)) < 0;
        if (lag(i) && (samples_back(n) > std::countr_one(backs[static_cast<std::size_t>(v)]) || moved)) {
            return false;
        }
    }
    return true;
}

// Does `run` qualify on machine `m`: two sections or more, within a block of the double lanes, its past held in
// registers, every node of a section over one voice (so its input too, which the pairing found not timeless) at a
// precision that grants reordering, each operand from outside merged (filled into `run.outside`), and the constants
// its merge adds to the pool, `numbers` numbers so far, within `kMaxConstants`? Then `numbers` grows by them. A run
// whose merge does not fit stays as written, and the others are linked all the same.
constexpr bool qualifies(const Reading& r, const Graph& g, Run& run, const Machine& m, std::size_t& numbers) {
    const std::int64_t lanes = m.vector_bytes / 8;
    const auto k = static_cast<std::int32_t>(run.cycles.size());
    if (k < 2 || lanes < 2 || !loops::graph::fits_a_block(literal_extent(k), lanes) || !detail::past_in_registers(r, g, run)) {
        return false;
    }
    bool gathers = false;
    std::size_t added = 0;
    for (std::size_t t = 0; t < run.sections[0].size(); ++t) {
        const Node& n = g.nodes[static_cast<std::size_t>(run.sections[0][t])];
        const Value& made = g.values[static_cast<std::size_t>(n.output)];
        if (made.shape.rank != 1 || !numerics::grants(made.precision).reorder) {
            return false;
        }
        for (std::size_t operand = 0; operand < rules_of(n.op).arity; ++operand) {
            const std::int32_t maker = made_by(r, n.inputs[operand]);
            if ((maker >= 0 && detail::holds(r, run, 0, maker)) || as_read(r, n.inputs[operand]) == run.input) {
                continue;
            }
            const std::optional<Merge> how = detail::merge_of(g, detail::outside_values(g, run, t, operand));
            if (!how) {
                return false;
            }
            run.outside.push_back(Outside{t, operand, *how});
            gathers = gathers || *how == Merge::Gathered;
            added += *how == Merge::Constants ? static_cast<std::size_t>(k) : 0;
        }
    }
    const std::size_t grown = numbers + added + (gathers ? static_cast<std::size_t>(k * (k - 1)) : 0);
    if (grown > kMaxConstants) {
        return false;
    }
    numbers = grown;
    return true;
}

// The runs to link, in node order: from each cycle that no run holds yet, the run beginning there, where it qualifies,
// the pool counted as the runs before it grew it. A run that does not qualify holds its cycles all the same: a run is
// maximal, so none of its parts is taken alone.
constexpr std::vector<Run> runs_of(Reading& r, const Graph& g, const Machine& m) {
    std::vector<Run> runs{};
    std::vector<char> held(g.nodes.size(), 0);
    std::size_t numbers = g.constants.size();
    for (std::size_t c = 0; c < g.nodes.size(); ++c) {
        if (cycle_of(r, static_cast<std::int32_t>(c)) != static_cast<std::int32_t>(c) || held[c] != 0) {
            continue;
        }
        Run run = detail::run_from(r, g, static_cast<std::int32_t>(c));
        for (const std::int32_t cycle : run.cycles) {
            held[static_cast<std::size_t>(cycle)] = 1;
        }
        if (detail::qualifies(r, g, run, m, numbers)) {
            runs.push_back(run);
        }
    }
    return runs;
}

} // namespace detail
} // namespace heinzel::rewrite
