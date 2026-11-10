#pragma once

// The proof (Plan/26-value-ranges/03): every value's range, carried through an analysed graph from the ranges it
// states, and every operand held to its operation's domain. The rules are the rule table's range and domain columns
// (`ir/op_rules.hpp`), so nothing here names an operation; a monotone rule computes its ends with the operation's own
// writing (`runtime/compute.hpp`), so no arithmetic is written twice. It runs between `infer` and the rewrites, not
// inside `infer`: the rewrites analyse graphs again whose promises a debug build has turned into checks (`04`).
//
// Linear: one pass over the marks and the recurrences, then one in node order.
//
// 1. A port takes its declared range, a scalar constant its number, and anything else no node makes any number.
// 2. A node: each operand is checked against the row's domain, a miss being a finding, and the result follows the row's
//    range rule, narrowed by a fact on it: the range a composition's result always has (`frac` is `x - floor(x)`, in
//    [0, 1], though its operands' ranges taken alone say nothing), trusted as a port's range is, though never into no
//    number. After a finding the result is any number, so that no undefined range proves a promise downstream.
// 3. A row that reads its operand's past reads silence before the first run and after a reset: that operand is its range
//    or 0. A variable read in the past takes its definition's fact and promise; read at this sample, any number (inside
//    its own definition the promise would assume itself).
// 4. A promise narrows the result: proven when the result lies inside it, a finding when the two share no number. On a
//    recurrence's definition this is a proof by induction: every sample lies inside when every earlier one did, and
//    silence does.

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/ranges/interval.hpp"
#include "heinzel/runtime/compute.hpp"
#include "heinzel/support/fail.hpp"
#include "heinzel/support/fixed_vector.hpp"

namespace heinzel::ranges {

// What the proof makes of a promise: the computed range lies inside it, and no check is needed; the promise narrowed the
// computed range, and is checked (`04`); or the two share no number.
enum class Verdict : std::uint8_t { Proven, Narrowed, Contradicted };

// An operand outside its operation's domain, or a promise the proof contradicts.
struct Finding {
    std::int32_t value{kNoValue}; // the result it is about: the operation's, or the promised value
    std::int32_t node{-1};        // the operation; -1 for a promise on a value no node makes
    std::int32_t operand{-1};     // the operand outside the domain; -1 for a contradiction
    std::int32_t root{0};         // the earlier finding whose "any number" made this one; its own index for a root
    Range range{};                // that operand's range, or the computed range the promise contradicts (a constant's own)

    constexpr bool operator==(const Finding&) const = default;
};

struct Promised {
    std::int32_t value{kNoValue};
    Verdict verdict{Verdict::Narrowed};

    constexpr bool operator==(const Promised&) const = default;
};

struct Proof {
    fixed_vector<Range, kMaxValues> values{};     // each value's range, by value
    fixed_vector<Finding, kMaxValues> findings{}; // in node order, so the first is a root; at most one per value
    fixed_vector<Promised, kMaxRanges> promises{}; // each promise of `Graph::ranges`, in its order
};

namespace detail {

struct Walk {
    const Graph& g;
    Proof& out;
    std::vector<std::int32_t> declared; // per value: its port's mark in `Graph::ranges`, or its fact's, or -1
    std::vector<std::int32_t> promise;  // per value: its promise's mark, or -1
    std::vector<std::int32_t> past;     // per variable: the definition its recurrence binds, or -1
    std::vector<std::int32_t> cause;    // per value that is any number: the finding that made it so, or -1
    std::vector<Verdict> verdict;       // per promised value
};

constexpr std::size_t at(std::int32_t v) { return static_cast<std::size_t>(v); }

// The marks by value, and each recurrence's variable bound to its definition. A hand-built graph's promise on a port is
// refused, as the frontend refuses one: the walk would narrow by it, and no check reads a value no node makes.
constexpr void gather(Walk& w) {
    for (std::size_t i = 0; i < w.g.ranges.size(); ++i) {
        const RangeMark& m = w.g.ranges[i];
        if (m.value < 0 || at(m.value) >= w.g.values.size()) {
            fail("heinzel::ranges: a range names no value of its graph");
        }
        if (m.promised && w.g.values[at(m.value)].category == ValueCategory::Input) {
            fail(kPromiseOnAPort);
        }
        std::int32_t& mark = m.promised ? w.promise[at(m.value)] : w.declared[at(m.value)];
        mark = mark < 0 ? static_cast<std::int32_t>(i) : mark;
    }
    for (const Node& n : w.g.nodes) {
        if (n.attr.kind == AttrKind::Recurrence && n.attr.recurrence.defines != kNoValue) {
            w.past[at(n.attr.recurrence.defines)] = n.inputs[0];
        }
    }
}

constexpr void record(Walk& w, const Finding& f) {
    w.out.findings.push_back(f);
    w.out.values[at(f.value)] = whole();
    w.cause[at(f.value)] = f.root;
}

// Rule 4: a promise on `v`, set from `node` (-1 where no node makes it). A constant is decided by comparing it, as a
// check would: a NaN or an infinity is any number as a range, which a promise would narrow, and no check reads it. A
// promise so proven narrows that any number (+inf in `ge(0)` is then [0, inf)); a finite constant stays its number.
constexpr void settle(Walk& w, std::int32_t v, std::int32_t node) {
    const std::int32_t mark = w.promise[at(v)];
    if (mark < 0) {
        return;
    }
    const Range& promised = w.g.ranges[at(mark)].range;
    const Value& value = w.g.values[at(v)];
    const bool constant = value.category == ValueCategory::Const;
    Range& r = w.out.values[at(v)];
    if (constant ? holds(promised, value.constant) : inside(r, promised)) {
        w.verdict[at(v)] = Verdict::Proven;
        r = constant ? narrowed(r, promised) : r;
        return;
    }
    const Range both = narrowed(r, promised);
    if (!constant && both.count > 0) {
        w.verdict[at(v)] = Verdict::Narrowed;
        r = both;
        w.cause[at(v)] = -1;
        return;
    }
    w.verdict[at(v)] = Verdict::Contradicted;
    // A NaN or an infinity names itself (`-inf`), where its range says any number; a finite constant's is its number.
    const Range found = constant && !finite(value.constant) ? span(value.constant, value.constant) : r;
    record(w, Finding{v, node, -1, static_cast<std::int32_t>(w.out.findings.size()), found});
}

// Rule 1: the values no node makes.
constexpr void seed(Walk& w) {
    for (std::size_t v = 0; v < w.g.values.size(); ++v) {
        const Value& value = w.g.values[v];
        if (value.category == ValueCategory::Const) {
            w.out.values[v] = point(value.constant);
        } else if (value.category == ValueCategory::Input && w.declared[v] >= 0) {
            w.out.values[v] = w.g.ranges[at(w.declared[v])].range;
        }
        if (value.category == ValueCategory::Const || value.category == ValueCategory::Input) {
            settle(w, static_cast<std::int32_t>(v), -1);
        }
    }
}

// `r` narrowed by `by`, or `r` where the two share no number: a fact the rules contradict (as `frac`'s [0, 1) was, where
// x - floor(x) rounds to 1) must not leave a value no number, which every domain and every promise would hold.
constexpr Range narrowed_or_kept(const Range& r, const Range& by) {
    const Range both = narrowed(r, by);
    return both.count == 0 ? r : both;
}

// `r` narrowed by a fact on `v` (rule 2).
constexpr Range narrowed_by_fact(const Walk& w, std::int32_t v, const Range& r) {
    const std::int32_t mark = w.declared[at(v)];
    return mark < 0 ? r : narrowed_or_kept(r, w.g.ranges[at(mark)].range);
}

// What every sample of a definition holds, before the walk reaches it: its fact and its promise (rule 3). Where they
// share no number the promise, which a debug build checks: the definition is then refused, or its fact was wrong.
constexpr Range marked(const Walk& w, std::int32_t definition) {
    const Range fact = narrowed_by_fact(w, definition, whole());
    const std::int32_t mark = w.promise[at(definition)];
    return mark < 0 ? fact : narrowed_or_kept(w.g.ranges[at(mark)].range, fact);
}

// Operand `k` as the row reads it (rule 3).
constexpr Range operand(const Walk& w, const Node& n, std::size_t k, const OpRules& rules) {
    const std::size_t v = at(n.inputs[k]);
    if (k != 0 || !reaches_into_the_past(rules.time)) {
        return w.out.values[v];
    }
    const bool variable = w.g.values[v].category == ValueCategory::Variable;
    const Range read = !variable ? w.out.values[v] : (w.past[v] < 0 ? whole() : marked(w, w.past[v]));
    return unite(read, point(0.0));
}

// The finding whose "any number" reached operands [first, end), or -1: only where each of them that is any number is so
// because of an earlier finding.
constexpr std::int32_t inherited(const Walk& w, const Node& n, const Range* in, std::size_t first, std::size_t end) {
    std::int32_t found = -1;
    for (std::size_t k = first; k < end; ++k) {
        if (!is_whole(in[k])) {
            continue;
        }
        const std::int32_t c = w.cause[at(n.inputs[k])];
        if (c < 0) {
            return -1;
        }
        found = found < 0 || c < found ? c : found;
    }
    return found;
}

// Where operand 0 must lie, for a domain about operand 0 alone; any number for the others.
constexpr Range first_operand_domain(Domain d) {
    switch (d) {
    case Domain::NonNegative: return span(0, kInf);
    case Domain::Positive: return span(0, kInf, true);
    case Domain::Unit: return span(-1, 1);
    default: return whole();
    }
}

// The operand outside the row's domain, or -1. A domain holds when every piece lies inside it.
constexpr int missed(Domain d, const Range* in) {
    switch (d) {
    case Domain::Anywhere:
        return -1;
    case Domain::NonZeroSecond:
        return holds(in[1], 0.0) ? 1 : -1;
    case Domain::NonNegative:
    case Domain::Positive:
    case Domain::Unit:
        return inside(in[0], first_operand_domain(d)) ? -1 : 0;
    case Domain::OneBranch:
        return within_one_branch(in[0]) ? -1 : 0;
    case Domain::NotBothZero:
        return holds(in[0], 0.0) && holds(in[1], 0.0) ? 1 : -1;
    case Domain::PowerBase:
        return inside(in[0], span(0, kInf, may_be_negative(in[1]))) ? -1 : 0;
    case Domain::Unclassified:
        break;
    }
    fail("heinzel::ranges: an operation with no domain reached the proof");
}

// A product, or a square where one value is on both sides.
constexpr Range times(const Range& a, const Range& b, bool same) { return same ? each(a, square) : pairs(a, b, product); }

// How many terms a sum has, from literal extents: their product, or -1 where one is known only later.
constexpr std::int64_t literal(Extent e) { return is_no_extent(e) || is_symbol(e) ? -1 : literal_of(e); }

constexpr std::int64_t counted(const Shape& s, const Axes& axes) {
    std::int64_t count = 1;
    for (std::int32_t j = 0; j < axes.rank; ++j) {
        const std::int64_t e = axes.axes[j] < 0 || axes.axes[j] >= s.rank ? -1 : literal(extent_at(s, axes.axes[j]));
        if (e < 0) {
            return -1;
        }
        count *= e;
    }
    return count;
}

// The axes of two operands broadcast together, right-aligned: an extent of 1 takes the other's.
constexpr Shape joined(const Shape& a, const Shape& b) {
    Shape s{};
    s.rank = a.rank > b.rank ? a.rank : b.rank;
    for (std::int32_t k = 0; k < s.rank; ++k) {
        const std::int32_t ia = a.rank - s.rank + k;
        const std::int32_t ib = b.rank - s.rank + k;
        const Extent ea = ia >= 0 ? extent_at(a, ia) : literal_extent(1);
        const Extent eb = ib >= 0 ? extent_at(b, ib) : literal_extent(1);
        set_extent(s, k, literal(ea) == 1 ? eb : ea);
    }
    return s;
}

// `count` terms, each within the term's hull; where the count is not known, the sign the terms share.
constexpr Range terms(const Range& term, std::int64_t count) {
    const Range h = hull(term);
    if (h.count == 0 || count >= 1) {
        return each(h, [&](const Interval& p) { return scaled(p, static_cast<double>(count)); });
    }
    if (h.pieces[0].lo >= 0) {
        return span(0, kInf);
    }
    return h.pieces[0].hi <= 0 ? span(-kInf, 0) : whole();
}

constexpr Range folded(ReduceOp op, const Range& in, std::int64_t count) {
    switch (op) {
    case ReduceOp::Sum:
        return terms(in, count);
    case ReduceOp::Mean:
    case ReduceOp::Max:
    case ReduceOp::Min:
        return hull(in);
    case ReduceOp::Prod:
        break;
    }
    return whole();
}

// A sum of terms, its count read off the attribute the row carries. An overlap-add sums between none and
// ceil(window / hop) frames at an element: fewer at the edges, none where the hop is past the window.
constexpr Range summed(const Graph& g, const Node& n, const Range* in) {
    const Shape& a = g.values[at(n.inputs[0])].shape;
    switch (n.attr.kind) {
    case AttrKind::Reduce:
        return folded(n.attr.reduce.op, in[0], counted(a, n.attr.reduce.axes));
    case AttrKind::Contract:
        return terms(times(in[0], in[1], n.inputs[0] == n.inputs[1]), counted(joined(a, g.values[at(n.inputs[1])].shape), n.attr.contract.axes));
    case AttrKind::MatMul: { // a[i,k] times b[k,j]: no squares, even of one value
        const std::int32_t k = n.attr.matmul.a_contract;
        return terms(times(in[0], in[1], false), k >= 0 && k < a.rank ? literal(extent_at(a, k)) : -1);
    }
    case AttrKind::OverlapAdd: {
        const std::int64_t frames = (n.attr.overlap.window + n.attr.overlap.hop - 1) / n.attr.overlap.hop;
        return hull(unite(terms(in[0], frames < 1 ? 1 : frames), point(0.0)));
    }
    default:
        break;
    }
    fail("heinzel::ranges: a sum whose attribute does not say what it sums");
}

// A ramp counts its elements from 0; a constant holds its own numbers.
constexpr Range generated(const Graph& g, const Node& n) {
    const std::int32_t count = numbers_per_sample(n.attr.generator.shape);
    if (count < 1) {
        return whole();
    }
    if (n.attr.kind == AttrKind::Iota) {
        return span(0, count - 1);
    }
    Range r = nothing();
    for (std::int32_t e = 0; e < count; ++e) {
        const double c = std::bit_cast<double>(g.constants[at(n.attr.generator.first + e)]);
        r = is_whole(point(c)) ? whole() : hull(unite(r, point(c)));
    }
    return r;
}

// A join's operands, and a pad's fill where it has margins (a join's pad fields are at their defaults: none).
constexpr Range united(const Node& n, const OpRules& rules, const Range* in) {
    Range r = in[0];
    for (std::size_t k = 1; k < rules.arity; ++k) {
        r = unite(r, in[k]);
    }
    if (n.attr.view.pad_before + n.attr.view.pad_after > 0) {
        r = unite(r, point(std::bit_cast<double>(n.attr.view.pad_bits)));
    }
    return r;
}

constexpr Range by_rule(const Graph& g, const Node& n, const OpRules& rules, const Range* in) {
    const auto f = [&](double x) { return compute_as<double>(n.op, FnAccuracy::Exact, x); };
    const auto f2 = [&](double x, double y) { return compute_as<double>(n.op, FnAccuracy::Exact, x, y); };
    // f's limits, where its domain reaches that infinity (sqrt has none below).
    const auto monotone = [&](bool strict, bool decreasing) {
        const Range d = first_operand_domain(rules.domain);
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double below = d.pieces[0].lo == -kInf ? f(-kInf) : nan;
        const double above = d.pieces[d.count - 1].hi == kInf ? f(kInf) : nan;
        return each(in[0], [&](const Interval& p) { return mapped(p, f, strict, decreasing, below, above); });
    };
    switch (rules.range) {
    case RangeRule::Same: return in[0];
    case RangeRule::Covering: return hull(in[0]);
    case RangeRule::CoveringTable: return hull(in[1]);
    case RangeRule::Sum: return pairs(in[0], in[1], sum);
    case RangeRule::Difference: return pairs(in[0], in[1], difference);
    case RangeRule::Negation: return each(in[0], negated);
    case RangeRule::Product: return times(in[0], in[1], n.inputs[0] == n.inputs[1]);
    case RangeRule::ProductSum: return pairs(times(in[0], in[1], n.inputs[0] == n.inputs[1]), in[2], sum);
    case RangeRule::Quotient: return times(in[0], each(in[1], reciprocal), false);
    case RangeRule::Increasing: return monotone(false, false);
    case RangeRule::StrictlyIncreasing: return monotone(true, false);
    case RangeRule::StrictlyDecreasing: return monotone(true, true);
    case RangeRule::EachIncreasing:
        return pairs(in[0], in[1], [&](const Interval& a, const Interval& b) { return each_increasing(a, b, f2); });
    case RangeRule::Magnitude: return each(in[0], magnitude);
    case RangeRule::Unit: return span(-1, 1);
    case RangeRule::Truth: return span(0, 1);
    case RangeRule::Angle: return span(-kPiAbove, kPiAbove);
    case RangeRule::NonNegative: return span(0, kInf);
    case RangeRule::Positive: return span(0, kInf, true);
    case RangeRule::Choice: return unite(in[1], in[2]);
    case RangeRule::Union: return united(n, rules, in);
    case RangeRule::Summed: return summed(g, n, in);
    case RangeRule::Generated: return generated(g, n);
    case RangeRule::Whole: return whole();
    case RangeRule::Unclassified: break;
    }
    fail("heinzel::ranges: an operation with no range rule reached the proof");
}

// Rule 2, then rule 4 on the result.
constexpr void step(Walk& w, std::size_t ni) {
    const Node& n = w.g.nodes[ni];
    const OpRules rules = rules_of(n.op);
    Range in[kMaxNodeInputs]{};
    for (std::size_t k = 0; k < rules.arity; ++k) {
        in[k] = operand(w, n, k, rules);
    }
    if (const int k = missed(rules.domain, in); k >= 0) {
        const std::size_t first = rules.domain == Domain::NotBothZero ? 0 : static_cast<std::size_t>(k);
        const std::int32_t root = inherited(w, n, in, first, static_cast<std::size_t>(k) + 1);
        const auto self = static_cast<std::int32_t>(w.out.findings.size());
        record(w, Finding{n.output, static_cast<std::int32_t>(ni), k, root < 0 ? self : root, in[k]});
    } else {
        const Range r = narrowed_by_fact(w, n.output, by_rule(w.g, n, rules, in));
        w.out.values[at(n.output)] = r;
        w.cause[at(n.output)] = is_whole(r) ? inherited(w, n, in, 0, rules.arity) : -1;
    }
    settle(w, n.output, static_cast<std::int32_t>(ni));
}

} // namespace detail

// Each value's range, each finding, and each promise's verdict, of an analysed graph.
constexpr Proof proof(const Graph& g) {
    Proof out{};
    const std::size_t values = g.values.size();
    detail::Walk w{g, out, std::vector<std::int32_t>(values, -1), std::vector<std::int32_t>(values, -1),
                   std::vector<std::int32_t>(values, -1), std::vector<std::int32_t>(values, -1),
                   std::vector<Verdict>(values, Verdict::Narrowed)};
    for (std::size_t v = 0; v < values; ++v) {
        out.values.push_back(whole());
    }
    detail::gather(w);
    detail::seed(w);
    for (std::size_t ni = 0; ni < g.nodes.size(); ++ni) {
        detail::step(w, ni);
    }
    for (const RangeMark& m : g.ranges) {
        if (m.promised) {
            out.promises.push_back(Promised{m.value, w.verdict[detail::at(m.value)]});
        }
    }
    return out;
}


// ---- what a finding says --------------------------------------------------------------------------------------
// A message holds 95 characters, so a finding names the operation as the user writes it, the operand and its range,
// and leaves the remedy to `docs/limitations.md`. A long range of four pieces may be cut.

namespace detail {

// Pieces apart only by one-number holes, as one comparator list: its bounds, then each hole as ne(c).
constexpr void write_group(MessageWriter& out, const Range& r, std::int8_t first, std::int8_t last) {
    const Interval& left = r.pieces[first];
    const Interval& right = r.pieces[last];
    std::string_view between{};
    const auto comparator = [&](std::string_view name, double bound) {
        out << between << name << std::string_view{"("} << bound << std::string_view{")"};
        between = std::string_view{", "};
    };
    if (left.lo != -kInf) {
        comparator(left.lo_open ? "gt" : "ge", left.lo);
    }
    if (right.hi != kInf) {
        comparator(right.hi_open ? "lt" : "le", right.hi);
    }
    for (std::int8_t k = first; k < last; ++k) {
        comparator("ne", r.pieces[k].hi);
    }
    if (between.empty()) {
        out << std::string_view{"any number"};
    }
}

} // namespace detail

// A range as the user writes one: a comparator list, a one-number hole as ne(c), a single number as itself, pieces
// farther apart joined by `or`. (A single number is a group of its own: a piece beside it at a one-number hole would
// touch it, and be joined.)
constexpr void write_range(MessageWriter& out, const Range& r) {
    if (r.count == 0) {
        out << std::string_view{"no number"};
    }
    for (std::int8_t i = 0; i < r.count;) {
        std::int8_t last = i;
        while (last + 1 < r.count && r.pieces[last].hi == r.pieces[last + 1].lo) {
            ++last;
        }
        out << std::string_view{i == 0 ? "" : " or "};
        if (last == i && !(r.pieces[i].lo < r.pieces[i].hi)) { // (also a NaN constant's piece, which a contradiction names)
            out << r.pieces[i].lo;
        } else {
            detail::write_group(out, r, i, last);
        }
        i = static_cast<std::int8_t>(last + 1);
    }
}

namespace detail {

// What an operand outside a domain may do, up to the range that follows.
constexpr std::string_view what_it_may_do(Domain d, bool negative_exponent) {
    switch (d) {
    case Domain::NonZeroSecond: return " may divide by zero: the divisor is ";
    case Domain::NonNegative: return " may see a number below 0: its operand is ";
    case Domain::Positive: return " may see a number <= 0: its operand is ";
    case Domain::Unit: return " may see a number outside ge(-1), le(1): its operand is ";
    case Domain::OneBranch: return " may reach a pole: its operand is ";
    case Domain::NotBothZero: return " may see 0 and 0: its operands are ";
    case Domain::PowerBase:
        return negative_exponent ? " may see a base <= 0 with a negative exponent: the base is " : " may see a negative base: the base is ";
    case Domain::Anywhere:
    case Domain::Unclassified: break;
    }
    return " may leave its domain: its operand is ";
}

} // namespace detail

// One finding, worded.
constexpr Diagnostic message(const Graph& g, const Proof& p, const Finding& f) {
    MessageWriter out;
    out << std::string_view{"heinzel: "};
    if (f.operand < 0) {
        out << std::string_view{"promise("};
        for (const RangeMark& m : g.ranges) {
            if (m.promised && m.value == f.value) {
                write_range(out, m.range);
                break;
            }
        }
        out << std::string_view{") contradicts the proof: "};
        write_range(out, f.range);
        return out.problem();
    }
    const Node& n = g.nodes[detail::at(f.node)];
    const Domain d = rules_of(n.op).domain;
    write_op(out, n.op);
    out << detail::what_it_may_do(d, d == Domain::PowerBase && may_be_negative(p.values[detail::at(n.inputs[1])]));
    if (d == Domain::NotBothZero) {
        write_range(out, p.values[detail::at(n.inputs[0])]);
        out << std::string_view{" and "};
    }
    write_range(out, f.range);
    return out.problem();
}

// The first finding in node order, worded: a root, since a finding follows only from earlier ones.
constexpr Diagnostic refusal(const Graph& g) {
    const Proof p = proof(g);
    return p.findings.empty() ? Diagnostic{} : message(g, p, p.findings[0]);
}

} // namespace heinzel::ranges
