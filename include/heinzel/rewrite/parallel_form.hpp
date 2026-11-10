#pragma once

// The rewrite layer's third rule (Plan/23 03, 3.7): a linear filter in its parallel form. A chain of second-order
// sections (`linear_sections.hpp`), each reading the one before, computes one rational transfer function. Its partial
// fractions are a bank of second-order sections that each read the chain's input, plus a direct term; computed so, in
// double, a sample waits on one section's chain instead of each in turn, and with its output in float it is as accurate
// as the cascade in double (`Plan/probes/audio-iir/parallel_sweep.cpp`); wholly in double it is not, so a chain whose
// result is double stays a cascade (`kBankType`). `parallel_lanes.cpp`: KFR's eight sections 3.2 ns a sample by hand,
// its cascade 9.4. It fires where every value it takes apart grants reordering, on one voice, on chains of two or more
// sections whose result is narrower than the bank's type and whose partial fractions exist and amplify no more than
// `kMostAmplification` (`partial_fractions.hpp`; where poles crowd together, the fractions are large and cancel, and
// the bank's rounding would collapse its accuracy; a Linkwitz-Riley's repeated pole has none), and whose sections'
// values nothing outside reads but the last. Inside an enclosing loop (`u = x - 0.6 clamp(y[n-1])`, `y` the chain of
// `u`) the bank's sum lies on the
// loop, and its feedback stage folds the bank's voices every sample (Plan/23 10); there the form is taken only where the
// loop's chain does not grow (`priced.hpp`: on zen2 from four sections, the loop's one-voice part in double with the
// bank since P4 4.3b; never where no sample holds the bank's voices). Outside one it is taken wherever it applies: a stand-in for the cost model weighing it against the
// cascade (Plan/23 04), for two sections too, where the cascade's chain is as short. Names no operation: the rule
// table's columns find each it reads or makes.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/rewrite/feedback_sums.hpp"
#include "heinzel/rewrite/linear_sections.hpp"
#include "heinzel/rewrite/partial_fractions.hpp"
#include "heinzel/rewrite/priced.hpp"
#include "heinzel/schedule/machine.hpp"
#include "heinzel/sema/analyze.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel::rewrite {
namespace detail {

// The operation whose row carries attribute `kind`: the table names it, the rule does not.
constexpr OpCode op_carrying(AttrKind kind) {
    for (std::size_t i = 0; i < kOpCount; ++i) {
        if (kOpTable[i].rules.attr == kind) {
            return OpCode{static_cast<std::uint8_t>(i)};
        }
    }
    fail("heinzel::rewrite: the rule table has no row for an attribute the rewrite emits");
    return OpCode{};
}

// ---- the chain ------------------------------------------------------------------------------------------------

// The chains of sections, each a list of indexes into `sections`: a section's successor is the one section reading its
// value as its signal; a chain starts at a section whose signal no section makes.
constexpr std::vector<std::vector<std::size_t>> chains_of(const std::vector<Section>& sections) {
    std::size_t values = 0;
    for (const Section& s : sections) {
        values = static_cast<std::size_t>(s.variable) >= values ? static_cast<std::size_t>(s.variable) + 1 : values;
        values = static_cast<std::size_t>(s.input) >= values ? static_cast<std::size_t>(s.input) + 1 : values;
    }
    std::vector<std::int64_t> maker(values, -1);  // per value: the section making it
    std::vector<std::int64_t> next(values, -1);  // per value: the section reading it (-2: more)
    for (std::size_t s = 0; s < sections.size(); ++s) {
        maker[static_cast<std::size_t>(sections[s].variable)] = static_cast<std::int64_t>(s);
        std::int64_t& reader = next[static_cast<std::size_t>(sections[s].input)];
        reader = reader == -1 ? static_cast<std::int64_t>(s) : -2;
    }
    std::vector<std::vector<std::size_t>> chains{};
    for (std::size_t s = 0; s < sections.size(); ++s) {
        if (maker[static_cast<std::size_t>(sections[s].input)] >= 0) {
            continue;
        }
        std::vector<std::size_t> chain{s};
        while (next[static_cast<std::size_t>(sections[chain.back()].variable)] >= 0 && chain.size() <= sections.size()) {
            chain.push_back(static_cast<std::size_t>(next[static_cast<std::size_t>(sections[chain.back()].variable)]));
        }
        chains.push_back(chain);
    }
    return chains;
}

// May recurrence node `r` be a section's: over one voice, at a precision that grants reordering? Asked before its
// definition is read, so that a graph whose recurrences are not filters pays for nothing more.
constexpr bool may_be_a_section(const Graph& g, std::size_t r) {
    const Value& made = g.values[static_cast<std::size_t>(g.nodes[r].output)];
    return g.nodes[r].attr.kind == AttrKind::Recurrence && made.shape.rank == 1 && numerics::grants(made.precision).reorder;
}

// Does every node of section `s` grant reordering? The rule computes anew all it takes apart, so a value pinned `High`
// inside a section (`(b0 x).precision(High)` in a `Normal` sum) keeps the section as written.
constexpr bool all_reorder(const Graph& g, const Section& s) {
    for (const std::int32_t p : s.nodes) {
        if (!numerics::grants(g.values[static_cast<std::size_t>(g.nodes[static_cast<std::size_t>(p)].output)].precision).reorder) {
            return false;
        }
    }
    return true;
}

// The type a bank computes in; the chain's result must be narrower. Rounded to float, the bank keeps the cascade's
// accuracy (`parallel_sweep.cpp`); kept in double it may not, since a cascade's later sections filter an earlier one's
// rounding and in a bank nothing does (an 8th-order Butterworth high-pass at 30 Hz: 15 to 148 times the double
// cascade's error, `parallel_conditioning.cpp`).
inline constexpr ElemType kBankType = ElemType::F64;

// Does the chain stand alone: two sections or more over one length, at one precision, its result narrower than
// `kBankType`, its nodes' values read by its nodes alone but the last section's result, its variables lagged by its
// nodes alone, and no result of it the graph's output but the last?
constexpr bool stands_alone(const Graph& g, const Lookup& at, const std::vector<Section>& sections, const std::vector<std::size_t>& chain) {
    std::vector<char> mine(g.nodes.size(), 0);
    for (const std::size_t s : chain) {
        for (const std::int32_t p : sections[s].nodes) {
            mine[static_cast<std::size_t>(p)] = 1;
        }
    }
    const std::int32_t result = g.nodes[static_cast<std::size_t>(sections[chain.back()].recur)].output;
    const Value& last = g.values[static_cast<std::size_t>(result)];
    bool alone = chain.size() >= 2 && elem_size(last.type) < elem_size(kBankType);
    const auto read_within = [&](std::int32_t v) {
        for (const std::int32_t r : at.readers[static_cast<std::size_t>(v)]) {
            alone = alone && mine[static_cast<std::size_t>(r)] != 0;
        }
        for (const NamedValue& io : g.outputs) {
            alone = alone && io.value != v;
        }
    };
    for (const std::size_t s : chain) {
        const std::int32_t made = g.nodes[static_cast<std::size_t>(sections[s].recur)].output;
        alone = alone && g.values[static_cast<std::size_t>(made)].shape == last.shape && g.values[static_cast<std::size_t>(made)].precision == last.precision;
        read_within(sections[s].variable);
        for (const std::int32_t p : sections[s].nodes) {
            if (g.nodes[static_cast<std::size_t>(p)].output != result) {
                read_within(g.nodes[static_cast<std::size_t>(p)].output);
            }
        }
    }
    return alone;
}

// Does an enclosing loop run through the chain: a node outside it in one of its sections' cycles, as the planner finds
// them (`loops/graph/feedback.hpp`)?
constexpr bool in_a_loop(const Graph& g, const loops::graph::Feedback& feedback, const std::vector<Section>& sections, const std::vector<std::size_t>& chain) {
    std::vector<char> mine(g.nodes.size(), 0);
    std::vector<char> ours(g.nodes.size(), 0);
    for (const std::size_t s : chain) {
        for (const std::int32_t p : sections[s].nodes) {
            mine[static_cast<std::size_t>(p)] = 1;
        }
        const std::int32_t c = feedback.cycle[static_cast<std::size_t>(sections[s].recur)];
        ours[static_cast<std::size_t>(c < 0 ? sections[s].recur : c)] = 1;
    }
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        if (!mine[i] && feedback.cycle[i] >= 0 && ours[static_cast<std::size_t>(feedback.cycle[i])]) {
            return true;
        }
    }
    return false;
}

// ---- the bank -----------------------------------------------------------------------------------------------

// What the bank adds to a graph: its nodes, and the values beside the graph's.
struct Bank {
    std::vector<Node> nodes{};
    std::vector<Value> values{};
};

constexpr std::int32_t made(Bank& b, const Value& v) {
    b.values.push_back(v);
    return static_cast<std::int32_t>(b.values.size() - 1);
}

constexpr std::int32_t node(Bank& b, OpCode op, const OpAttr& attr, std::int32_t in0, std::int32_t in1, const Value& out) {
    Node n{};
    n.op = op, n.attr = attr, n.inputs[0] = in0, n.inputs[1] = in1, n.output = made(b, out);
    b.nodes.push_back(n);
    return n.output;
}

// A constant tensor of the bank's numbers, one a section: `[sections, 1]` of `kBankType`, its numbers in `h`'s pool.
constexpr std::int32_t tensor(Graph& h, Bank& b, const std::vector<double>& numbers, const Shape& column, numerics::Precision p) {
    OpAttr attr{};
    attr.kind = AttrKind::Constant;
    attr.generator = GeneratorAttr{column, kBankType, static_cast<std::int32_t>(h.constants.size())};
    for (const double number : numbers) {
        push_constant(h, number);
    }
    return node(b, op_carrying(AttrKind::Constant), attr, kNoValue, kNoValue, Value{kBankType, column, ValueCategory::Internal, 0, p, true});
}

// The bank's nodes, reading signal `input` now through `now`: v = c0 x + c1 x[n-1] + (-a1) v[n-1] + (-a2) v[n-2] over
// the sections, summed over them, the direct term added, converted to `like`'s narrower type. Its result.
constexpr std::int32_t bank_nodes(Graph& h, Bank& b, const Expansion& e, std::int32_t input, std::int32_t now, const Value& like) {
    const numerics::Precision p = like.precision;
    const Extent sections = literal_extent(static_cast<std::int32_t>(e.c0.size()));
    const Value wide{kBankType, Shape{sections, extent_at(like.shape, 0)}, ValueCategory::Internal, 0, p, false};
    const Value one{kBankType, like.shape, ValueCategory::Internal, 0, p, false};
    const Value x = h.values[static_cast<std::size_t>(now)];
    const Shape column{sections, literal_extent(1)};
    const std::int32_t c0 = tensor(h, b, e.c0, column, p);
    const std::int32_t c1 = tensor(h, b, e.c1, column, p);
    const std::int32_t a1 = tensor(h, b, e.a1, column, p);
    const std::int32_t a2 = tensor(h, b, e.a2, column, p);
    OpAttr back{};
    OpAttr recur{};
    OpAttr sum{};
    OpAttr none{};
    back.kind = AttrKind::Delay, back.delay.samples = 1;
    const OpCode delay = op_carrying(AttrKind::Delay);
    const OpCode mul = op_with(Algebra::Product);
    const OpCode add = op_with(Algebra::Add);
    const std::int32_t x1 = node(b, delay, back, input, kNoValue, Value{x.type, x.shape, ValueCategory::Internal, 0, x.precision, false});
    const std::int32_t v = made(b, Value{kBankType, wide.shape, ValueCategory::Variable, 0, p, false});
    const std::int32_t v1 = node(b, delay, back, v, kNoValue, wide);
    back.delay.samples = 2;
    const std::int32_t v2 = node(b, delay, back, v, kNoValue, wide);
    const std::int32_t fed = node(b, add, none, node(b, mul, none, c0, now, wide), node(b, mul, none, c1, x1, wide), wide);
    const std::int32_t once = node(b, add, none, fed, node(b, mul, none, a2, v2, wide), wide);
    const std::int32_t def = node(b, add, none, once, node(b, mul, none, a1, v1, wide), wide);
    recur.kind = AttrKind::Recurrence, recur.recurrence.defines = v;
    sum.kind = AttrKind::Reduce, sum.reduce = ReduceAttr{ReduceOp::Sum, Axes{0}, false};
    std::int32_t out = node(b, op_carrying(AttrKind::Reduce), sum, node(b, op_carrying(AttrKind::Recurrence), recur, def, kNoValue, wide), kNoValue, one);
    if (e.direct != 0) {
        const std::int32_t d = made(b, Value{kBankType, Shape{}, ValueCategory::Const, e.direct, p, false});
        out = node(b, add, none, out, node(b, mul, none, d, now, one), one);
    }
    OpAttr cast{};
    cast.kind = AttrKind::Cast, cast.cast.to = like.type;
    return node(b, op_carrying(AttrKind::Cast), cast, out, kNoValue, Value{like.type, like.shape, ValueCategory::Internal, 0, p, false});
}

// A chain the rule may take: its sections, in order, and its bank.
struct Candidate {
    std::vector<std::size_t> chain{};
    Expansion e{};
};

// The graph with each chain of `taken` replaced by its bank, placed where the chain's last node stood (after all it
// reads, before all that reads it); what read a chain's result reads its bank's. Several at once are the graph each
// makes in turn. None where the result would not fit the graph's capacities or `infer` refuses it.
constexpr std::optional<Graph> with_banks(const Graph& g, const Lookup& at, const std::vector<Section>& sections, const std::vector<Candidate>& taken) {
    std::size_t numbers = g.constants.size();
    for (const Candidate& t : taken) {
        numbers += 4 * t.e.c0.size();
    }
    if (numbers > kMaxConstants) {
        return std::nullopt;
    }
    Graph h = g;
    Bank b{{}, std::vector<Value>(g.values.begin(), g.values.end())};
    std::vector<char> mine(g.nodes.size(), 0);
    std::vector<std::int32_t> renamed(g.values.size(), kNoValue);  // (a chain's result: its bank's)
    std::vector<std::int32_t> after(g.nodes.size(), -1);  // (a chain's last node: its bank)
    std::vector<std::size_t> ends{0};                                                        // (bank k's nodes: `ends[k]` to `ends[k + 1]`)
    for (std::size_t k = 0; k < taken.size(); ++k) {
        std::size_t last = 0;
        for (const std::size_t s : taken[k].chain) {
            for (const std::int32_t p : sections[s].nodes) {
                mine[static_cast<std::size_t>(p)] = 1;
                last = static_cast<std::size_t>(p) > last ? static_cast<std::size_t>(p) : last;
            }
        }
        const Section& head = sections[taken[k].chain.front()];
        const std::int32_t result = g.nodes[static_cast<std::size_t>(sections[taken[k].chain.back()].recur)].output;
        const std::int32_t now = head.present != kNoValue                                        ? head.present
                                 : at.present[static_cast<std::size_t>(head.input)] != kNoValue ? at.present[static_cast<std::size_t>(head.input)]
                                                                                                 : head.input;
        renamed[static_cast<std::size_t>(result)] = bank_nodes(h, b, taken[k].e, head.input, now, g.values[static_cast<std::size_t>(result)]);
        after[last] = static_cast<std::int32_t>(k);
        ends.push_back(b.nodes.size());
    }
    const auto read = [&](std::int32_t v) { return v >= 0 && renamed[static_cast<std::size_t>(v)] != kNoValue ? renamed[static_cast<std::size_t>(v)] : v; };
    std::vector<Node> nodes{};
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        Node n = g.nodes[i];
        for (std::size_t k = 0; k < rules_of(n.op).arity; ++k) {
            n.inputs[k] = read(n.inputs[k]);
        }
        if (!mine[i]) {
            nodes.push_back(n);
        }
        if (const std::int32_t k = after[i]; k >= 0) {
            nodes.insert(nodes.end(), b.nodes.begin() + static_cast<std::ptrdiff_t>(ends[static_cast<std::size_t>(k)]),
                         b.nodes.begin() + static_cast<std::ptrdiff_t>(ends[static_cast<std::size_t>(k) + 1]));
        }
    }
    for (NamedValue& io : h.outputs) {
        io.value = read(io.value);
    }
    std::optional<Graph> rewritten = detail::with(h, nodes, b.values);
    if (rewritten && !infer(*rewritten).ok) {
        return std::nullopt;
    }
    return rewritten;
}

// Which of `loops`, chains inside a loop, pay on `m`: each where its loop's chain, the graph as the planner will be
// given it, does not grow with the banks (`priced.hpp`). Priced together, cycle by cycle: a bank stands where its chain
// stood, so the k-th cycle after is the k-th before. Where the planner refuses them together, or their cycles do not
// pair, each alone (one that still does not is declined); and one bank a loop, since a sample holds one fold.
constexpr std::vector<char> paying(const Graph& g, const Lookup& at, const std::vector<Section>& sections, const loops::graph::Feedback& feedback,
                                   const std::vector<Candidate>& loops, const Machine& m) {
    std::vector<char> takes(loops.size(), 0);
    std::vector<std::size_t> place(g.nodes.size(), 0); // (per node: how many cycles open before it)
    for (std::size_t i = 1; i < g.nodes.size(); ++i) {
        place[i] = place[i - 1] + (feedback.cycle[i - 1] == static_cast<std::int32_t>(i - 1) ? 1 : 0);
    }
    const std::size_t cycles = g.nodes.empty() ? 0 : place.back() + (feedback.cycle[g.nodes.size() - 1] == static_cast<std::int32_t>(g.nodes.size() - 1) ? 1 : 0);
    const std::optional<std::vector<double>> before = loops.empty() ? std::nullopt : chains_as_planned(g, m);
    const auto loop_of = [&](const Candidate& t) { return feedback.cycle[static_cast<std::size_t>(sections[t.chain.front()].recur)]; };
    const auto priced = [&](const std::vector<Candidate>& together, std::size_t from) {
        const std::optional<Graph> h = with_banks(g, at, sections, together);
        const std::optional<std::vector<double>> after = h ? chains_as_planned(*h, m) : std::nullopt;
        if (!before || !after || before->size() != cycles || after->size() != cycles) {
            return false;
        }
        for (std::size_t k = 0; k < together.size(); ++k) {
            const std::size_t c = place[static_cast<std::size_t>(loop_of(together[k]))];
            takes[from + k] = (*after)[c] <= (*before)[c];
        }
        return true;
    };
    if (!priced(loops, 0)) {
        for (std::size_t k = 0; k < loops.size(); ++k) {
            priced(std::vector<Candidate>{loops[k]}, k);
        }
    }
    for (std::size_t k = 0; k < loops.size(); ++k) {
        for (std::size_t j = 0; j < k && takes[k]; ++j) {
            takes[k] = !(takes[j] && loop_of(loops[j]) == loop_of(loops[k]));
        }
    }
    return takes;
}

// Whether each chain inside a loop (in order) pays on `m`, decided once for all of them (`paying`).
constexpr std::vector<char> decided_in_loops(const Graph& g, const Lookup& at, const std::vector<Section>& sections, const loops::graph::Feedback& feedback,
                                             const std::vector<std::vector<std::size_t>>& chains, const Machine& m) {
    std::vector<Candidate> loops{};
    std::vector<std::size_t> slot{};
    std::size_t count = 0;
    for (const std::vector<std::size_t>& chain : chains) {
        if (!in_a_loop(g, feedback, sections, chain)) {
            continue;
        }
        if (const std::optional<Expansion> e = stands_alone(g, at, sections, chain) ? expansion_of(sections, chain) : std::nullopt) {
            loops.push_back(Candidate{chain, *e});
            slot.push_back(count);
        }
        ++count;
    }
    const std::vector<char> pays = paying(g, at, sections, feedback, loops, m);
    std::vector<char> takes(count, 0);
    for (std::size_t k = 0; k < loops.size(); ++k) {
        takes[slot[k]] = pays[k];
    }
    return takes;
}

// The graph with one chain in its parallel form; none where no chain qualifies. Whether each chain inside a loop pays
// on `m` is decided once, in the first pass, for all of them (`takes`, in order; one taken leaves it).
constexpr std::optional<Graph> one_in_parallel(const Graph& g, const Machine& m, std::optional<std::vector<char>>& takes) {
    const Lookup at = lookup_of(g);
    std::vector<Section> sections{};
    for (std::size_t r = 0; r < g.nodes.size(); ++r) {
        if (may_be_a_section(g, r)) {
            if (Section s = section_of(g, at, r); s.valid && all_reorder(g, s)) {
                sections.push_back(s);
            }
        }
    }
    if (sections.size() < 2) {
        return std::nullopt;
    }
    const loops::graph::Facts facts = loops::graph::facts_of(g);
    const loops::graph::Feedback feedback = loops::graph::find_feedback(g, facts);
    if (!facts.why_not.empty() || !feedback.why_not.empty()) {
        return std::nullopt; // (the planner refuses it, by name)
    }
    const std::vector<std::vector<std::size_t>> chains = chains_of(sections);
    if (!takes) {
        takes = decided_in_loops(g, at, sections, feedback, chains, m);
    }
    std::size_t k = 0;
    for (const std::vector<std::size_t>& chain : chains) {
        const bool loop = in_a_loop(g, feedback, sections, chain);
        if ((loop && !(*takes)[k++]) || !stands_alone(g, at, sections, chain)) {
            continue;
        }
        if (const std::optional<Expansion> e = expansion_of(sections, chain)) {
            if (std::optional<Graph> rewritten = with_banks(g, at, sections, std::vector<Candidate>{Candidate{chain, *e}})) {
                if (loop) {
                    takes->erase(takes->begin() + static_cast<std::ptrdiff_t>(k - 1));
                }
                return rewritten;
            }
        }
    }
    return std::nullopt;
}

} // namespace detail

// Every linear filter of analysed graph `g` that qualifies in its parallel form on machine `m`, a bank in double; `g`
// itself where none does. A bank is no chain, so a second pass finds nothing.
constexpr Graph parallel_form(const Graph& g, const Machine& m) {
    Graph out = g;
    std::optional<std::vector<char>> takes{};
    for (std::optional<Graph> next = detail::one_in_parallel(out, m, takes); next; next = detail::one_in_parallel(out, m, takes)) {
        out = *next;
    }
    return out;
}

} // namespace heinzel::rewrite
