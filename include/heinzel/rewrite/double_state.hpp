#pragma once

// The rewrite layer's second rule (Decision 66, `Plan/23-audio-scheduler/03` 3.4; by the cost model since Plan/23 04
// 4.3b): at `Normal`, a feedback cycle computes its float values in double where the cost model prices its stage in
// double no dearer than in float (`loops/graph/cycle_chains.hpp` `cycle_stages`): its chain in double's latencies (a
// division or a core on it lengthens it), its voices in half the lanes, a comb's samples half as many at once, the
// conversions, of a float it reads and of what leaves it, issued beside it; and the stage it shares, gathered as the
// planner gathers cycles. Double is the more accurate, so a tie widens, and a tie is a difference the model does not
// resolve: within 3 %, the corpus's bar for a right pick (`kTie`). And where Normal promises double (Decision 66: one
// voice, cheap nodes, the past in registers), it keeps it whatever the price (`promised`). One filter waits on its
// chain, and a double multiply-add takes as long as a float one: the bench cascade runs 2.91 ns a sample either way,
// its error -135.7 dB in float and -142.1 dB in double, a 20 Hz low-pass's -44.6 dB and -151.9 dB
// (`Plan/probes/audio-iir/`, `forms.cpp`, `double_state.cpp`). The cheap work that carries one such cycle's value into
// another widens with them: converted around each cycle alone, a cascade's sections pass their output through float to
// the next (1.6x slower, no more accurate). Nothing is converted up: every node of the region reads one of its values,
// and a float operand beside a double one is read as double (the type rule's promotion; a constant at its double
// value, so a coefficient's float rounding goes too); a conversion of a double inside the region is read through
// (`eq8fb`'s loop around its bank). What leaves it is converted down; where that conversion runs is the planner's
// (`planner/trail.hpp`). It names no operation: the conversion is the rule table's row whose type its attribute states.

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/ir/timing.hpp"
#include "heinzel/loops/graph/cycle_chains.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/rewrite/feedback_sums.hpp"
#include "heinzel/schedule/cost.hpp"
#include "heinzel/schedule/machine.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel::rewrite {
namespace detail {

// How much dearer double may be priced and still tie: the model picks right where it picks within 3 % of the best
// (`Plan/23-audio-scheduler/04` 4.2), so it resolves no finer. A single filter's conversions issue beside its chain,
// priced 0.1-0.2 % dearer; it measures 3-5 % (`cost_corpus17.cpp`).
inline constexpr double kTie = 1.03;

// The conversion: the one row whose result type its attribute states.
constexpr OpCode conversion() {
    for (std::size_t i = 0; i < kOpCount; ++i) {
        if (kOpTable[i].rules.type == TypeRule::FromAttribute) {
            return OpCode{static_cast<std::uint8_t>(i)};
        }
    }
    fail("heinzel::rewrite: the rule table has no conversion");
    return OpCode{};
}

// Hands its operand on from an earlier sample, or as the variable it defines: a lag or a recurrence, not a conversion.
constexpr bool hands_on_unconverted(const Node& n) {
    return loops::graph::hands_its_operand_on(n) && rules_of(n.op).type != TypeRule::FromAttribute;
}

// A conversion of a double into a float, which the region reads through.
constexpr bool narrows(const Graph& g, const Node& n) {
    return loops::graph::converts(n) && g.values[static_cast<std::size_t>(n.inputs[0])].type == ElemType::F64;
}

// A map, or a link: a map but for its time column, which moves its operands' numbers between sections at this sample.
constexpr bool maps_or_links(const Node& n) { return loops::graph::is_a_plain_map(n) || loops::graph::is_a_link(n); }

// May run in double: a float value at `Normal` of one voice (a signal of time alone), made by a map or a link whose type
// is its operands' (so a double operand makes it double: not a choice, whose condition does not count), a lag, a
// recurrence, or a conversion of a double (read through). One voice stands in for the model: a voice bank in double
// costs what the model does not see, its lanes masked to the voices (JUCE's EQ over 2 and 4 channels 1.12-1.18x slower
// in double, one lane body either way and priced alike; one section over 2 and 4 voices 1.01-1.03x; `cost_corpus17.cpp`).
// A cycle over voices still widens its one-voice part (`eq8fb`'s loop around its bank). Eligibility (sections in
// lanes): a cycle holding a link counts as one voice (`linked`), the stand-in widened as Plan/23 05 has it. Its axis 0
// holds one voice's sections, which run skewed in the double lanes as the lanes' probe measured them; in float each
// section would lose the double it runs in today (a 20 Hz low-pass as users write it, -68.8 against -144.9 dB).
constexpr bool may_widen(const Graph& g, const Node& n, bool linked) {
    const Value& v = g.values[static_cast<std::size_t>(n.output)];
    const bool map = detail::maps_or_links(n) && rules_of(n.op).type == TypeRule::FromOperands;
    return (map || hands_on_unconverted(n) || narrows(g, n)) && v.type == ElemType::F32 && v.precision == numerics::Precision::Normal &&
           v.shape.rank == (linked ? 2 : 1) && !v.timeless;
}

// Cheap work: a hand-on, or a map or a link of the rule table's cheap class (no core, no root).
constexpr bool cheap(const Node& n) {
    return loops::graph::hands_its_operand_on(n) || (detail::maps_or_links(n) && rules_of(n.op).cost == CostClass::Cheap);
}

// Per cycle (by the node naming it), whether it holds a link.
constexpr std::vector<char> links_in(const Graph& g, const loops::graph::Feedback& feedback) {
    std::vector<char> holds(g.nodes.size(), 0);
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        if (const std::int32_t c = feedback.cycle[i]; c >= 0 && loops::graph::is_a_link(g.nodes[i])) {
            holds[static_cast<std::size_t>(c)] = 1;
        }
    }
    return holds;
}

constexpr std::int32_t maker_of(const loops::graph::Facts& facts, std::int32_t v) {
    return facts.producer[static_cast<std::size_t>(facts.read_as[static_cast<std::size_t>(v)])];
}

using Edges = std::vector<std::vector<std::int32_t>>;

// Adds to `seen` every node reached from it along `edges` through nodes `through` lets pass.
constexpr void reach(const Edges& edges, const std::vector<char>& through, std::vector<char>& seen) {
    std::vector<std::int32_t> queue{};
    for (std::size_t i = 0; i < seen.size(); ++i) {
        if (seen[i]) {
            queue.push_back(static_cast<std::int32_t>(i));
        }
    }
    for (std::size_t at = 0; at < queue.size(); ++at) {
        for (const std::int32_t next : edges[static_cast<std::size_t>(queue[at])]) {
            if (!seen[static_cast<std::size_t>(next)] && through[static_cast<std::size_t>(next)]) {
                seen[static_cast<std::size_t>(next)] = 1;
                queue.push_back(next);
            }
        }
    }
}

// Per cycle (by the node naming it), whether it may run in double at all: each of its nodes may widen or makes a double
// already, and one at least may widen.
constexpr std::vector<char> candidates(const Graph& g, const loops::graph::Feedback& feedback) {
    const std::vector<char> linked = detail::links_in(g, feedback);
    std::vector<char> all(g.nodes.size(), 1);
    std::vector<char> some(g.nodes.size(), 0);
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        if (const std::int32_t c = feedback.cycle[i]; c >= 0) {
            const bool widens = may_widen(g, g.nodes[i], linked[static_cast<std::size_t>(c)]);
            all[static_cast<std::size_t>(c)] = all[static_cast<std::size_t>(c)] && (widens || g.values[static_cast<std::size_t>(g.nodes[i].output)].type == ElemType::F64);
            some[static_cast<std::size_t>(c)] = some[static_cast<std::size_t>(c)] || widens;
        }
    }
    for (std::size_t c = 0; c < g.nodes.size(); ++c) {
        all[c] = all[c] && some[c];
    }
    return all;
}

// Normal's promise (Decision 66, Gerald's Q1 (b), the accuracy): a cycle over one voice whose nodes are cheap and whose
// past is all held in registers (every lag one of a run 1, 2, ... of a value's lags) computes in double, where the
// model prices that dearer too: a division on its chain (1.14x), three bands of one input summed (1.16x), six sections
// (priced 7 % dearer, measured 3 %; a declined 16th-order low-pass at 50 Hz is 2.2e-4 from long double in float, 3.5e-8
// in double) (`cost_corpus17.cpp`). Per cycle (by the node naming it); every other cycle is the model's.
constexpr std::vector<char> promised(const Graph& g, const loops::graph::Facts& facts, const loops::graph::Feedback& feedback) {
    const std::size_t count = g.nodes.size();
    const std::vector<char> linked = detail::links_in(g, feedback);
    const auto lag_of = [&](std::size_t i) { return feedback.cycle[i] >= 0 && hands_on_unconverted(g.nodes[i]) && samples_back(g.nodes[i]) > 0; };
    std::vector<std::uint64_t> backs(g.values.size(), 0); // per value: bit d - 1 for a lag of d samples a cycle reads
    for (std::size_t i = 0; i < count; ++i) {
        if (const std::int32_t d = samples_back(g.nodes[i]); lag_of(i) && d <= 64) {
            backs[static_cast<std::size_t>(facts.read_as[static_cast<std::size_t>(g.nodes[i].inputs[0])])] |= std::uint64_t{1} << (d - 1);
        }
    }
    std::vector<char> keeps(count, 1);
    for (std::size_t i = 0; i < count; ++i) {
        const Node& n = g.nodes[i];
        const bool held = !lag_of(i) ||
                          samples_back(n) <= std::countr_one(backs[static_cast<std::size_t>(facts.read_as[static_cast<std::size_t>(n.inputs[0])])]);
        if (const std::int32_t c = feedback.cycle[i]; c >= 0) {
            const bool widens = may_widen(g, n, linked[static_cast<std::size_t>(c)]);
            keeps[static_cast<std::size_t>(c)] = keeps[static_cast<std::size_t>(c)] && widens && !narrows(g, n) && cheap(n) && held;
        }
    }
    for (std::size_t c = 0; c < count; ++c) {
        keeps[c] = keeps[c] && feedback.cycle[c] == static_cast<std::int32_t>(c);
    }
    return keeps;
}

// Per node, whether it runs in double: what may widen of the cycles `qualifies` names, and the cheap work that may widen
// (of no cycle; the model prices cycles, and the planner chains cycles through cheap work only) both reached from them
// and reaching them. A lag outside reads the converted value:
// what a later stage reads of the past stays float, and the cycle's own past is in its registers (the voice's phase
// read a sample back by its sine: stored in double 7.5 % slower).
constexpr std::vector<char> region_of(const Graph& g, const loops::graph::Facts& facts, const loops::graph::Feedback& feedback,
                                      const std::vector<char>& qualifies) {
    const std::size_t count = g.nodes.size();
    Edges makers(count);
    Edges readers(count);
    for (std::size_t i = 0; i < count; ++i) {
        const Node& n = g.nodes[i];
        for (std::size_t k = 0; k < rules_of(n.op).arity; ++k) {
            if (const std::int32_t p = maker_of(facts, n.inputs[k]); p >= 0) {
                makers[i].push_back(p);
                readers[static_cast<std::size_t>(p)].push_back(static_cast<std::int32_t>(i));
            }
        }
    }
    const std::vector<char> linked = detail::links_in(g, feedback);
    std::vector<char> passes(count, 0);
    std::vector<char> from(count, 0);
    std::vector<char> to(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        const std::int32_t c = feedback.cycle[i];
        from[i] = to[i] = c >= 0 && qualifies[static_cast<std::size_t>(c)];
        passes[i] = from[i] || (c < 0 && may_widen(g, g.nodes[i], false) && cheap(g.nodes[i]));
    }
    detail::reach(readers, passes, from);
    detail::reach(makers, passes, to);
    std::vector<char> in(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        const std::int32_t c = feedback.cycle[i];
        in[i] = from[i] && to[i] && may_widen(g, g.nodes[i], c >= 0 && linked[static_cast<std::size_t>(c)]);
    }
    return in;
}

// The rewrite under way: the values, each widened value's conversion down (made once), the conversions to emit after
// each node, and per value what a region node reads instead (a narrowing conversion's operand).
struct Widening {
    const loops::graph::Facts& facts;
    const std::vector<char>& in;
    std::vector<Value> values;
    std::vector<std::int32_t> down;
    std::vector<std::int32_t> through;
    std::vector<std::vector<Node>> after;
};

constexpr bool widened(const Widening& w, std::int32_t v) {
    const std::int32_t p = maker_of(w.facts, v);
    return p >= 0 && w.in[static_cast<std::size_t>(p)] && w.through[static_cast<std::size_t>(v)] == kNoValue;
}

// Widened value `v` as float, for what reads it outside the region: converted once, after the node making it.
constexpr std::int32_t narrowed(Widening& w, std::int32_t v) {
    const std::int32_t made = w.facts.read_as[static_cast<std::size_t>(v)];
    std::int32_t& copy = w.down[static_cast<std::size_t>(made)];
    if (copy == kNoValue) {
        Value value = w.values[static_cast<std::size_t>(made)];
        value.type = ElemType::F32, value.category = ValueCategory::Internal;
        w.values.push_back(value);
        copy = static_cast<std::int32_t>(w.values.size() - 1);
        Node cast{.op = detail::conversion(), .output = copy};
        cast.inputs[0] = made;
        cast.attr.kind = AttrKind::Cast;
        cast.attr.cast.to = ElemType::F32;
        w.after[static_cast<std::size_t>(maker_of(w.facts, made))].push_back(cast);
    }
    return copy;
}

// `g` with the nodes `in` marks in double, and where each of its nodes now stands (-1: gone, a conversion the region
// read through and nothing else reads); none where the conversions would outgrow its capacities.
struct Widened {
    Graph graph;
    std::vector<std::int32_t> at;
};
constexpr std::optional<Widened> widen(const Graph& g, const loops::graph::Facts& facts, const std::vector<char>& in) {
    Widening w{facts, in, std::vector<Value>(g.values.begin(), g.values.end()), std::vector<std::int32_t>(g.values.size(), kNoValue),
               std::vector<std::int32_t>(g.values.size(), kNoValue), std::vector<std::vector<Node>>(g.nodes.size())};
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        if (in[i] && narrows(g, g.nodes[i])) {
            w.through[static_cast<std::size_t>(g.nodes[i].output)] = g.nodes[i].inputs[0];
        }
    }
    std::vector<Node> nodes(g.nodes.begin(), g.nodes.end());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        for (std::size_t k = 0; k < rules_of(nodes[i].op).arity; ++k) {
            std::int32_t& v = nodes[i].inputs[k];
            const std::int32_t through = w.through[static_cast<std::size_t>(v)];
            // (a node outside that makes a double already reads the double: only a float reader's type would change)
            const bool float_reader = g.values[static_cast<std::size_t>(nodes[i].output)].type != ElemType::F64;
            v = in[i] && through != kNoValue ? through : !in[i] && float_reader && detail::widened(w, v) ? detail::narrowed(w, v) : v;
        }
    }
    Graph out = g;
    for (NamedValue& o : out.outputs) {
        o.value = detail::widened(w, o.value) ? detail::narrowed(w, o.value) : o.value;
    }
    std::vector<char> read(w.values.size(), 0); // (a conversion the region reads through stays where anything else does)
    for (const Node& n : nodes) {
        for (std::size_t k = 0; k < rules_of(n.op).arity; ++k) {
            read[static_cast<std::size_t>(n.inputs[k])] = 1;
        }
    }
    for (const NamedValue& o : out.outputs) {
        read[static_cast<std::size_t>(o.value)] = 1;
    }
    for (std::size_t i = 0; i < nodes.size(); ++i) { // (after the copies: a conversion's result stays float)
        if (in[i] && !narrows(g, nodes[i])) {
            w.values[static_cast<std::size_t>(nodes[i].output)].type = ElemType::F64;
            if (nodes[i].attr.kind == AttrKind::Recurrence) {
                w.values[static_cast<std::size_t>(nodes[i].attr.recurrence.defines)].type = ElemType::F64;
            }
        }
    }
    std::vector<Node> ordered{};
    std::vector<std::int32_t> at(nodes.size(), -1);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (!(in[i] && narrows(g, nodes[i]) && !read[static_cast<std::size_t>(nodes[i].output)])) {
            at[i] = static_cast<std::int32_t>(ordered.size());
            ordered.push_back(nodes[i]);
        }
        ordered.insert(ordered.end(), w.after[i].begin(), w.after[i].end());
    }
    const std::optional<Graph> rebuilt = detail::with(out, ordered, w.values);
    return rebuilt ? std::optional<Widened>{Widened{*rebuilt, at}} : std::nullopt;
}

// The graph's feedback stages priced as the planner gathers cycles (Plan/23 04 4.3a, g): merged into one stage where
// that is no dearer than each apart. A merged stage holds the longest chain, every slot, load and store, the depths one
// after another, the most lane bodies and the fewest lanes along time; one cycle widened beside others costs what the
// stage it shares costs more (Freeverb's eight combs, each a tie alone, 1.43x slower merged: `cost_corpus17.cpp`).
struct Gathered {
    cost::Stage merged{.lanes = 1e30};
    double apart{0};
};
constexpr Gathered joined(const Gathered& a, const Gathered& b) {
    const cost::Stage &x = a.merged;
    const cost::Stage &y = b.merged;
    const double bodies = x.bodies > y.bodies ? x.bodies : y.bodies;
    const auto summed = [&](double p, double q) { return (p * x.bodies + q * y.bodies) / bodies; };
    const cost::Stage s{.chain = x.chain > y.chain ? x.chain : y.chain, .depth = x.depth + y.depth, .slots = summed(x.slots, y.slots),
                        .loads = summed(x.loads, y.loads), .stores = summed(x.stores, y.stores), .bodies = bodies,
                        .lanes = x.lanes < y.lanes ? x.lanes : y.lanes, .bytes = x.bytes + y.bytes};
    return {s, a.apart + b.apart};
}
constexpr double priced(const Gathered& a, const Machine& m) {
    const double merged = cost::cycles(a.merged, m);
    return merged < a.apart ? merged : a.apart;
}
constexpr Gathered alone(const cost::Stage& s, const Machine& m) { return {s, cost::cycles(s, m)}; }

// Each cycle of `g` (by the node naming it) priced as the cost model prices its stage alone (`cycle_stages`), in `g`
// and as it runs in `wide`, all of `may` in double; `wide.at` says where each node of `g` stands there.
struct Priced {
    std::vector<cost::Stage> narrow;
    std::vector<cost::Stage> wide;
    std::vector<char> both; // a cycle priced both ways
};
constexpr Priced priced_both_ways(const Graph& g, const loops::graph::Facts& facts, const loops::graph::Feedback& feedback, const std::vector<char>& may,
                                  const Widened& wide, const Machine& m) {
    Priced p{loops::graph::cycle_stages(g, facts, feedback, m), {}, std::vector<char>(g.nodes.size(), 0)};
    p.wide = p.narrow;
    const loops::graph::Facts wide_facts = loops::graph::facts_of(wide.graph);
    const loops::graph::Feedback wide_feedback = loops::graph::find_feedback(wide.graph, wide_facts);
    if (!wide_facts.why_not.empty() || !wide_feedback.why_not.empty()) {
        return p;
    }
    const std::vector<cost::Stage> there = loops::graph::cycle_stages(wide.graph, wide_facts, wide_feedback, m);
    for (std::size_t c = 0; c < g.nodes.size(); ++c) {
        const std::int32_t at = may[c] && wide.at[c] >= 0 ? wide_feedback.cycle[static_cast<std::size_t>(wide.at[c])] : -1;
        p.both[c] = at >= 0;
        p.wide[c] = at >= 0 ? there[static_cast<std::size_t>(at)] : p.narrow[c];
    }
    return p;
}

// Union-find over nodes: the root of `i`'s set.
constexpr std::int32_t root_of(std::vector<std::int32_t>& up, std::int32_t i) {
    while (up[static_cast<std::size_t>(i)] != i) {
        up[static_cast<std::size_t>(i)] = up[static_cast<std::size_t>(up[static_cast<std::size_t>(i)])];
        i = up[static_cast<std::size_t>(i)];
    }
    return i;
}

// Per node, the set of cycles that widen together: the cycles of `alone` that the region `in` links (a cascade's
// sections pass doubles, never a float between them: 1.6x slower so), by a node of it; every other cycle a set of its
// own.
constexpr std::vector<std::int32_t> sets_of(const Graph& g, const loops::graph::Facts& facts, const loops::graph::Feedback& feedback,
                                            const std::vector<char>& alone, const std::vector<char>& in) {
    std::vector<std::int32_t> up(g.nodes.size(), 0);
    const auto links = [&](std::size_t i) { return in[i] || (feedback.cycle[i] >= 0 && alone[static_cast<std::size_t>(feedback.cycle[i])]); };
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        up[i] = static_cast<std::int32_t>(i);
        for (std::size_t k = 0; k < rules_of(g.nodes[i].op).arity && links(i); ++k) {
            if (const std::int32_t p = maker_of(facts, g.nodes[i].inputs[k]); p >= 0 && links(static_cast<std::size_t>(p))) {
                up[static_cast<std::size_t>(detail::root_of(up, p))] = detail::root_of(up, static_cast<std::int32_t>(i));
            }
        }
    }
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        up[i] = links(i) ? detail::root_of(up, static_cast<std::int32_t>(i)) : static_cast<std::int32_t>(i);
    }
    return up;
}

// The cycles that widen. First each alone: promised, or its stage in double no dearer than in float, within the
// model's resolution (`kTie`). Then those the region links as one set, the sets taken in graph order while the graph's
// feedback stages, gathered as the planner gathers them, cost no more with the set in double than in float (a promise
// in double), again within `kTie`, the sets taken before it included (Freeverb's eight combs, each a tie alone, 1.43x
// slower merged: `cost_corpus17.cpp`); a set holding a promised cycle is taken.
constexpr std::vector<char> taken(const Graph& g, const loops::graph::Facts& facts, const loops::graph::Feedback& feedback, const Priced& p,
                                  const Machine& m) {
    const std::size_t count = g.nodes.size();
    const std::vector<char> promise = detail::promised(g, facts, feedback);
    std::vector<char> alone(count, 0);
    for (std::size_t c = 0; c < count; ++c) {
        alone[c] = p.both[c] && (promise[c] || cost::cycles(p.wide[c], m) <= kTie * cost::cycles(p.narrow[c], m));
    }
    const std::vector<std::int32_t> set = detail::sets_of(g, facts, feedback, alone, detail::region_of(g, facts, feedback, alone));
    std::vector<Gathered> as_float(count);
    std::vector<Gathered> as_double(count);
    std::vector<std::int32_t> sets{}; // (by their roots, in the order of their first cycles)
    std::vector<char> seen(count, 0);
    std::vector<char> widens(count, 0);
    std::vector<char> kept(count, 0);  // a set holding a promised cycle
    for (std::size_t c = 0; c < count; ++c) {
        const std::size_t r = static_cast<std::size_t>(set[c]);
        if (feedback.cycle[c] == static_cast<std::int32_t>(c)) {
            if (!seen[r]) {
                seen[r] = 1;
                sets.push_back(static_cast<std::int32_t>(r));
            }
            widens[r] = alone[c], kept[r] = kept[r] || (alone[c] && promise[c]);
            as_float[r] = detail::joined(as_float[r], detail::alone(p.narrow[c], m));
            as_double[r] = detail::joined(as_double[r], detail::alone(alone[c] ? p.wide[c] : p.narrow[c], m));
        }
    }
    std::vector<Gathered> after(sets.size() + 1); // (the sets from k on: in float, but a promise in double)
    for (std::size_t k = sets.size(); k-- > 0;) {
        const std::size_t r = static_cast<std::size_t>(sets[k]);
        after[k] = detail::joined(kept[r] ? as_double[r] : as_float[r], after[k + 1]);
    }
    const double budget = kTie * detail::priced(after[0], m);
    Gathered before{};
    for (std::size_t k = 0; k < sets.size(); ++k) {
        const std::size_t r = static_cast<std::size_t>(sets[k]);
        const Gathered with = detail::joined(before, as_double[r]);
        widens[r] = widens[r] && (kept[r] || detail::priced(detail::joined(with, after[k + 1]), m) <= budget);
        before = widens[r] ? with : detail::joined(before, as_float[r]);
    }
    std::vector<char> takes(count, 0);
    for (std::size_t c = 0; c < count; ++c) {
        takes[c] = alone[c] && widens[static_cast<std::size_t>(set[c])];
    }
    return takes;
}

} // namespace detail

// Analysed graph `g` with its feedback cycles at `Normal` in double where machine `m` runs them so no slower by the cost
// model, and the work between them; `g` itself where none does. Normal's promise is a floor: where the conversions of
// every candidate, widened to be priced, or of the cycles taken outgrow the graph's capacities, the promised cycles
// widen alone, as before the model priced them (a biquad beside ten `tanh` cycles and maps up to 127 values: the
// review of P4); `g` itself where even those outgrow them.
constexpr Graph double_state(const Graph& g, const Machine& m) {
    const loops::graph::Facts facts = loops::graph::facts_of(g);
    const loops::graph::Feedback feedback = loops::graph::find_feedback(g, facts);
    if (!facts.why_not.empty() || !feedback.why_not.empty()) {
        return g; // (the planner refuses it, by name)
    }
    const std::vector<char> may = detail::candidates(g, feedback);
    if (std::find(may.begin(), may.end(), 1) == may.end()) {
        return g;
    }
    const auto promise_alone = [&] {
        const std::vector<char> promise = detail::promised(g, facts, feedback);
        if (std::find(promise.begin(), promise.end(), 1) == promise.end()) {
            return g;
        }
        const std::optional<detail::Widened> kept = detail::widen(g, facts, detail::region_of(g, facts, feedback, promise));
        return kept ? kept->graph : g;
    };
    const std::optional<detail::Widened> all = detail::widen(g, facts, detail::region_of(g, facts, feedback, may));
    if (!all) {
        return promise_alone();
    }
    const std::vector<char> takes = detail::taken(g, facts, feedback, detail::priced_both_ways(g, facts, feedback, may, *all, m), m);
    if (takes == may) {
        return all->graph;
    }
    if (std::find(takes.begin(), takes.end(), 1) == takes.end()) {
        return g;
    }
    const std::optional<detail::Widened> some = detail::widen(g, facts, detail::region_of(g, facts, feedback, takes));
    return some ? some->graph : promise_alone();
}

} // namespace heinzel::rewrite
