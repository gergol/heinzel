#pragma once

// The rewrite layer's first rule (Decision 66): in a feedback cycle a sum is evaluated in the order its terms become
// ready, so that only the term of the newest state is added on the cycle's chain, and that last addition is fused
// into a multiply-add where the machine has one (rounded as the build rounds: `_madd`, never a software core). Written `y = 0.3x + 0.4x[-1] + 0.2x[-2] + 0.5y[-1] - 0.2y[-2]`, a
// biquad puts a multiply, an add and a subtract between `y[n-1]` and `y[n]`; in this order one multiply-add (2.2 to
// 1.4 ns a sample on zen2, `Plan/probes/audio-iir/chain.cpp`). It changes rounding, so it fires only where the sum's
// precision grants reordering. It names no operation: what a node is to a sum is the rule table's `Algebra` column.

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
#include "heinzel/loops/graph/staging.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/rewrite/rebuilt.hpp"
#include "heinzel/schedule/machine.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel::rewrite {
namespace detail {

// The operation whose row plays `role`: the table names it, the rule does not.
constexpr OpCode op_with(Algebra role) {
    for (std::size_t i = 0; i < kOpCount; ++i) {
        if (kOpTable[i].rules.algebra == role) {
            return OpCode{static_cast<std::uint8_t>(i)};
        }
    }
    fail("heinzel::rewrite: the rule table has no row for a role the rewrite emits");
    return OpCode{};
}

constexpr Algebra algebra_of(const Node& n) { return rules_of(n.op).algebra; }

// One term of a sum: a value, or a product of two (`a`, `b`) folded into the sum by a multiply-add already.
struct Term {
    std::int32_t value{kNoValue};
    std::int32_t a{kNoValue};
    std::int32_t b{kNoValue};
    std::int32_t product{-1}; // the node making `value` where it is a product nothing else reads (-1: none)
    bool minus{false};
    std::int32_t key{0};  // when it is ready
    std::int32_t position{0};  // where it stood in the sum as written
    std::int32_t flip{kNoValue}; // subtracted, the factor of `product` the chain negates instead (`chain_of`)
};

// What the rule reads of the graph, worked out once: its facts and cycles, how many read each value (an output
// counts), when each node of a cycle is ready around it (`ready`, -1 until asked), whether it reads its cycle's past
// from memory (`far`, -1 until asked), per value the lags of it a cycle reads (`backs`, bit d − 1 for d samples), per
// node the latest node of any cycle it reads this sample through cheap work (`made_now`, as the planner chains cycles
// into one stage); per cycle whether that is ready at once (`early`, the order with a cycle's own newest state last),
// and whether any sum's term was made so (`upstream`); per value whether it is timeless (`timeless`: a constant, a Param or
// a tensor declared so, or what is computed sample by sample from those alone, the same at every sample).
struct Context {
    const Graph& g;
    loops::graph::Facts facts;
    loops::graph::Feedback feedback;
    std::vector<std::int32_t> readers;
    std::vector<std::int32_t> ready;
    std::vector<std::int32_t> reader{}; // per value: a node that reads it (the only one where `readers` is 1)
    std::vector<std::int32_t> far{};
    std::vector<std::uint64_t> backs{};
    std::vector<std::int32_t> made_now{};
    std::vector<char> timeless{};
    std::vector<char> early{};
    bool upstream{false};
};

constexpr Context context_of(const Graph& g, const std::vector<char>& early) {
    Context at{g, loops::graph::facts_of(g), {}, std::vector<std::int32_t>(g.values.size(), 0), std::vector<std::int32_t>(g.nodes.size(), -1)};
    at.feedback = loops::graph::find_feedback(g, at.facts);
    at.reader.assign(g.values.size(), -1);
    at.far.assign(g.nodes.size(), -1);
    at.backs.assign(g.values.size(), 0);
    at.made_now = loops::graph::made_now(g, at.facts, at.feedback);
    at.early = early;
    at.timeless = loops::graph::timeless_of(g);
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        const Node& n = g.nodes[i];
        const std::int32_t d = samples_back(n);
        if (at.feedback.cycle[i] >= 0 && loops::graph::hands_its_operand_on(n) && d > 0 && d <= 64) {
            at.backs[static_cast<std::size_t>(at.facts.read_as[static_cast<std::size_t>(n.inputs[0])])] |= std::uint64_t{1} << (d - 1);
        }
    }
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        const Node& n = g.nodes[i];
        for (std::size_t k = 0; k < rules_of(n.op).arity; ++k) {
            ++at.readers[static_cast<std::size_t>(n.inputs[k])];
            at.reader[static_cast<std::size_t>(n.inputs[k])] = static_cast<std::int32_t>(i);
        }
    }
    for (const NamedValue& out : g.outputs) {
        ++at.readers[static_cast<std::size_t>(out.value)];
    }
    return at;
}

// When value `v` is ready around cycle `c`, as a key: 0 for what is ready as the sample starts (what reads no cycle this
// sample); for the cycle's state `d` samples back, 1000 - d (the older, the earlier); a product or a negation with its
// later factor (the multiply overlaps: `c y[n-1]` is the term of `y[n-1]`); anything else a cycle computes this sample,
// this one or another, or what reads that, 1000 plus the (latest) node making it. A lag closing the cycle reads what
// the cycle makes; a lag of an earlier node is ready when that node was, its samples before, so `y[n-1][n-1]` is as
// old as `y[n-2]`. In a stage holding both (Decision 65), another cycle's output arrives late: a cascade section adds
// it last (four sections by hand 3.92 -> 2.95 ns a sample, as written 3.60), or with what is ready at once (`early`)
// where its stage costs less so (`ordered_feedback_sums`). And a sum the cycle computes is ready
// after the states it adds: a TPT filter's `bp = g hp + s1[n-1]` fuses `g hp` (two stages by hand 5.33 -> 5.04, the
// second alone fused 5.50; `Plan/probes/cascade-order`).
constexpr std::int32_t ready_key(Context& at, std::int32_t c, std::int32_t v) {
    const std::int32_t p = at.facts.producer[static_cast<std::size_t>(at.facts.read_as[static_cast<std::size_t>(v)])];
    if (p < 0 || at.feedback.cycle[static_cast<std::size_t>(p)] != c) {
        const std::int32_t made = p < 0 ? -1 : at.made_now[static_cast<std::size_t>(p)];
        at.upstream = at.upstream || made >= 0;
        return made < 0 || at.early[static_cast<std::size_t>(c)] ? 0 : 1000 + made;
    }
    std::int32_t& key = at.ready[static_cast<std::size_t>(p)];
    if (key >= 0) {
        return key;
    }
    const Node& n = at.g.nodes[static_cast<std::size_t>(p)];
    if (loops::graph::hands_its_operand_on(n) && samples_back(n) > 0) {
        const std::int32_t d = samples_back(n);
        const std::int32_t of = at.facts.producer[static_cast<std::size_t>(at.facts.read_as[static_cast<std::size_t>(n.inputs[0])])];
        const std::int32_t then = of < 0 ? 0 : of > p ? 1000 : detail::ready_key(at, c, n.inputs[0]); // (an earlier node: no cycle)
        key = then >= 1000 ? 1000 - d : then > d ? then - d : then > 0 ? 1 : 0;
    } else if (const Algebra role = detail::algebra_of(n); n.attr.kind == AttrKind::Recurrence || (role != Algebra::Product && role != Algebra::Negate)) {
        key = 1000 + p;
    } else {
        std::int32_t latest = 0;
        for (std::size_t k = 0; k < rules_of(n.op).arity; ++k) {
            const std::int32_t operand = ready_key(at, c, n.inputs[k]);
            latest = operand > latest ? operand : latest;
        }
        key = latest;
    }
    return key;
}

// Does value `v` read cycle `c`'s past from memory, through a lag that closes the cycle past a gap in its run of lags,
// or maps over one? The lowering hands a run of lags 1, …, k of a value on in registers (`loops/normalize.hpp`
// `find_state`) and reads a lag past a gap back from memory; a sum in ready order would put that load on the cycle's
// chain (a biquad's `y[n-2]` from memory: by hand 2.16 -> 2.27 ns a sample, in a register 1.34), so the rule leaves
// such a sum as written. A value made this sample is its own sum's business.
constexpr bool reads_far_back(Context& at, std::int32_t c, std::int32_t v) {
    const std::int32_t p = at.facts.producer[static_cast<std::size_t>(at.facts.read_as[static_cast<std::size_t>(v)])];
    if (p < 0 || at.feedback.cycle[static_cast<std::size_t>(p)] != c) {
        return false;
    }
    std::int32_t& far = at.far[static_cast<std::size_t>(p)];
    if (far >= 0) {
        return far != 0;
    }
    const Node& n = at.g.nodes[static_cast<std::size_t>(p)];
    bool deep = false;
    if (loops::graph::hands_its_operand_on(n) && samples_back(n) > 0) {
        const std::int32_t of = at.facts.producer[static_cast<std::size_t>(at.facts.read_as[static_cast<std::size_t>(n.inputs[0])])];
        const std::uint64_t run = at.backs[static_cast<std::size_t>(at.facts.read_as[static_cast<std::size_t>(n.inputs[0])])];
        deep = of > p ? samples_back(n) > std::countr_one(run) : of >= 0 && detail::reads_far_back(at, c, n.inputs[0]);
    } else if (n.attr.kind != AttrKind::Recurrence) {
        for (std::size_t k = 0; k < rules_of(n.op).arity && !deep; ++k) {
            deep = detail::reads_far_back(at, c, n.inputs[k]);
        }
    }
    far = deep ? 1 : 0;
    return deep;
}

// Laid out and rounded alike: a node of another precision is a term, so that only what grants reordering moves.
constexpr bool alike(const Graph& g, std::int32_t v, std::int32_t w) {
    const Value& a = g.values[static_cast<std::size_t>(v)];
    const Value& b = g.values[static_cast<std::size_t>(w)];
    return a.type == b.type && a.shape == b.shape && a.timeless == b.timeless && a.precision == b.precision;
}

// A sum being taken apart: its root's cycle and value, its terms, and the nodes inside it that go.
struct Sum {
    std::int32_t cycle{-1};
    std::int32_t root{-1};
    std::vector<Term> terms{};
    std::vector<std::int32_t> inside{};
};

constexpr void take(Context& at, Sum& sum, std::int32_t v, bool minus);

// Node `ni`, a sum node of the cycle (or of none, read by the sum alone), taken apart into `sum`'s terms.
constexpr void open_up(Context& at, Sum& sum, std::int32_t ni, bool minus) {
    const Node& n = at.g.nodes[static_cast<std::size_t>(ni)];
    switch (algebra_of(n)) {
    case Algebra::Add:
        detail::take(at, sum, n.inputs[0], minus), detail::take(at, sum, n.inputs[1], minus);
        break;
    case Algebra::Subtract:
        detail::take(at, sum, n.inputs[0], minus), detail::take(at, sum, n.inputs[1], !minus);
        break;
    case Algebra::Negate:
        detail::take(at, sum, n.inputs[0], !minus);
        break;
    default: // ProductAdd: a * b + c, its sum first, as it is computed
        detail::take(at, sum, n.inputs[2], minus);
        sum.terms.push_back(Term{.a = n.inputs[0], .b = n.inputs[1], .minus = minus});
        break;
    }
}

// Value `v` read by the sum: a sum node that nothing else reads, of the cycle or of none, is opened up, anything else is
// a term. (The part of a sum that reads nothing of its cycle, `.5a + .1a[n-1]` beside `b`'s own states, is a node of
// no cycle, but the sum's as written: its terms are ready at other times, and the last may be the one to fuse.)
constexpr void take(Context& at, Sum& sum, std::int32_t v, bool minus) {
    const std::int32_t p = at.facts.producer[static_cast<std::size_t>(v)];
    const std::int32_t of = p >= 0 ? at.feedback.cycle[static_cast<std::size_t>(p)] : -1;
    const bool same = p >= 0 && (of == sum.cycle || of < 0) && at.readers[static_cast<std::size_t>(v)] == 1 && detail::alike(at.g, v, sum.root);
    const Algebra role = p >= 0 ? detail::algebra_of(at.g.nodes[static_cast<std::size_t>(p)]) : Algebra::None;
    if (same && (role == Algebra::Add || role == Algebra::Subtract || role == Algebra::Negate || role == Algebra::ProductAdd)) {
        sum.inside.push_back(p);
        detail::open_up(at, sum, p, minus);
        return;
    }
    sum.terms.push_back(Term{.value = v, .product = same && role == Algebra::Product ? p : -1, .minus = minus});
}


constexpr bool is_a_sum(Algebra role) { return role == Algebra::Add || role == Algebra::Subtract || role == Algebra::ProductAdd; }

// Is node `ri` the root of a sum this rule may reorder: a sum node of a cycle, at a precision that grants reordering,
// that no enclosing sum of its cycle takes apart?
constexpr bool is_a_root(const Context& at, std::size_t ri) {
    const Node& n = at.g.nodes[ri];
    const std::int32_t c = at.feedback.cycle[ri];
    const std::int32_t v = n.output;
    if (c < 0 || !detail::is_a_sum(detail::algebra_of(n)) || !numerics::grants(at.g.values[static_cast<std::size_t>(v)].precision).reorder) {
        return false;
    }
    const std::int32_t r = at.reader[static_cast<std::size_t>(v)];
    if (at.readers[static_cast<std::size_t>(v)] != 1 || r < 0 || at.feedback.cycle[static_cast<std::size_t>(r)] != c) {
        return true;
    }
    const Algebra above = detail::algebra_of(at.g.nodes[static_cast<std::size_t>(r)]);
    const bool taken = (detail::is_a_sum(above) || above == Algebra::Negate) && detail::alike(at.g, v, at.g.nodes[static_cast<std::size_t>(r)].output);
    return !taken;
}

constexpr bool is_constant(const Graph& g, std::int32_t v) { return g.values[static_cast<std::size_t>(v)].category == ValueCategory::Const; }

// The terms in the order they become ready, of those ready together the added first (so that the chain stands upright
// as early as it can: `build_chain`), written order kept among equals; whether any moved.
constexpr bool in_ready_order(std::vector<Term>& terms) {
    for (std::size_t i = 0; i < terms.size(); ++i) {
        terms[i].position = static_cast<std::int32_t>(i);
    }
    std::sort(terms.begin(), terms.end(), [](const Term& l, const Term& r) {
        return l.key != r.key ? l.key < r.key : l.minus != r.minus ? r.minus : l.position < r.position;
    });
    bool moved = false;
    for (std::size_t i = 0; i < terms.size(); ++i) {
        moved = moved || terms[i].position != static_cast<std::int32_t>(i);
    }
    return moved;
}

struct Chain {
    std::vector<Node> nodes{};
    std::vector<std::int32_t> gone{}; // the nodes it replaces, the root's apart
    std::vector<Node> ahead{};        // what it negates of a timeless factor, put right after that factor is made
};

// Factor `v` negated, a value appended to `values`: the factor a subtracted product is fused with. A constant is
// negated as it is copied; a timeless factor by a negation of its own (exact, so the multiply-add of `-a` rounds as
// the constant's does; ready as the sample starts, off the cycle's chain). That stands right after the factor is made,
// not with the chain: there, the planner runs it, a stage of no time, between the chain's feed and its cycle, which
// cuts them apart (`biquad3ch`'s Param rows at C8 on zen2 1.10-1.22x slower).
constexpr std::int32_t negated(const Graph& g, std::vector<Value>& values, std::int32_t v, std::vector<Node>& ahead) {
    Value minus = values[static_cast<std::size_t>(v)];
    if (detail::is_constant(g, v)) {
        minus.constant = -minus.constant;
    } else {
        minus.category = ValueCategory::Internal, minus.constant = 0, minus.timeless = false; // (as the frontend makes `-a`)
        ahead.push_back(Node{.op = detail::op_with(Algebra::Negate), .inputs = {v, kNoValue, kNoValue}, .output = static_cast<std::int32_t>(values.size())});
    }
    values.push_back(minus);
    return static_cast<std::int32_t>(values.size() - 1);
}

// What a step adding values `a` and `b` makes, laid out as the root `like` but shaped by numpy's rule from the innermost
// axis out, as wide as the wider, timeless if both are: `infer` of the rebuilt graph agrees (a bank's step over two of
// its mono terms stays mono).
constexpr Value step_value(const std::vector<Value>& values, std::int32_t a, std::int32_t b, std::int32_t like) {
    const Value& l = values[static_cast<std::size_t>(a)];
    const Value& r = values[static_cast<std::size_t>(b)];
    Value out = values[static_cast<std::size_t>(like)];
    out.shape = Shape{};
    out.shape.rank = l.shape.rank > r.shape.rank ? l.shape.rank : r.shape.rank;
    for (std::int32_t k = 0; k < out.shape.rank; ++k) {
        const std::int32_t il = l.shape.rank - out.shape.rank + k;
        const std::int32_t ir = r.shape.rank - out.shape.rank + k;
        const Extent el = il >= 0 ? extent_at(l.shape, il) : literal_extent(1);
        const Extent er = ir >= 0 ? extent_at(r.shape, ir) : literal_extent(1);
        set_extent(out.shape, k, el == literal_extent(1) ? er : el);
    }
    out.type = l.type == ElemType::F64 || r.type == ElemType::F64 ? ElemType::F64 : ElemType::F32;
    out.timeless = l.timeless && r.timeless;
    return out;
}

// The chain over `sum`'s terms in their order: each step an add or a subtract of the next term, the last a
// multiply-add where `fresh` (a product nothing else reads, fused; subtracted, its factor `flip` negated) or where the
// last term is one already; each step's value one of `spare`'s (from the back: the inside nodes were taken outermost
// first, the chain is built innermost first, so a chain rebuilt keeps its values), the last the root's own. While every
// term taken is subtracted, the running value stands for the sum negated, and the first added turns it (`t - acc`,
// rounded as the sum in that order); a first term with a factor to `flip` is led by its product, that factor negated.
constexpr Chain build_chain(const Graph& g, const Sum& sum, bool fresh, const std::vector<std::int32_t>& spare, std::vector<Value>& values) {
    Chain chain{.gone = sum.inside};
    const Term& first = sum.terms[0];
    std::int32_t acc = first.value;
    bool negative = first.minus;
    if (first.flip != kNoValue) {
        Node lead = g.nodes[static_cast<std::size_t>(first.product)];
        (lead.inputs[0] == first.flip ? lead.inputs[0] : lead.inputs[1]) = detail::negated(g, values, first.flip, chain.ahead);
        chain.nodes.push_back(lead);
        chain.gone.push_back(first.product);
        negative = false;
    }
    std::size_t next = 0;
    for (std::size_t i = 1; i < sum.terms.size(); ++i) {
        const Term& t = sum.terms[i];
        const bool last = i + 1 == sum.terms.size();
        const std::int32_t out = last ? sum.root : spare[spare.size() - 1 - next++];
        if (!last) {
            values[static_cast<std::size_t>(out)] = detail::step_value(values, acc, t.value, sum.root);
        }
        Node step{.output = out};
        if (last && (fresh || t.value == kNoValue)) {
            std::int32_t a = t.a;
            std::int32_t b = t.b;
            if (fresh) {
                const Node& product = g.nodes[static_cast<std::size_t>(t.product)];
                a = product.inputs[0], b = product.inputs[1];
                chain.gone.push_back(t.product);
            }
            if (t.minus) {
                (a == t.flip ? a : b) = detail::negated(g, values, t.flip, chain.ahead);
            }
            step.op = detail::op_with(Algebra::ProductAdd);
            step.inputs[0] = a, step.inputs[1] = b, step.inputs[2] = acc;
        } else {
            const bool turns = negative && !t.minus;
            step.op = detail::op_with(negative == t.minus ? Algebra::Add : Algebra::Subtract);
            step.inputs[0] = turns ? t.value : acc, step.inputs[1] = turns ? acc : t.value;
            negative = negative && t.minus;
        }
        chain.nodes.push_back(step);
        acc = out;
    }
    return chain;
}

// The factor of `product` a subtracted term negates to be added: a constant first, else a timeless one (none: kNoValue).
constexpr std::int32_t factor_to_negate(const Context& at, const Node& product) {
    const std::int32_t a = product.inputs[0];
    const std::int32_t b = product.inputs[1];
    const auto timeless = [&](std::int32_t v) { return at.timeless[static_cast<std::size_t>(v)] != 0; };
    return detail::is_constant(at.g, a) ? a : detail::is_constant(at.g, b) ? b : timeless(a) ? a : timeless(b) ? b : kNoValue;
}

// Whether the chain over `terms`, in ready order, stands upright where it must (`build_chain`): at a last step that
// fuses (`fresh`), subtracts or is a multiply-add already. A term added before it turns a chain that starts subtracted;
// where none does, the first term is led by its product with a factor negated (`flip`), if it has one to negate.
constexpr bool stands_upright(const Context& at, std::vector<Term>& terms, bool fresh) {
    const Term& last = terms.back();
    bool turns = !last.minus && !fresh && last.value != kNoValue; // (the last step turns it: `t - acc`)
    for (std::size_t i = 1; i + 1 < terms.size(); ++i) {
        turns = turns || !terms[i].minus;
    }
    Term& first = terms[0];
    if (first.minus && !turns && first.product >= 0) {
        first.flip = detail::factor_to_negate(at, at.g.nodes[static_cast<std::size_t>(first.product)]);
    }
    return !first.minus || turns || first.flip != kNoValue;
}

// How the sum rooted at node `ri` is rebuilt (empty: it stays as written). The chain adds its terms left to right in
// the order they are ready; where the last is a product (one factor timeless, if it is subtracted) and the machine
// fuses, the last step is a multiply-add. Eligibility: a feedback sum's last term `- a·y`, `a` timeless, fuses as a
// multiply-add of `-a` (a constant negated as it is copied, anything else timeless by a `_neg` off the chain; Plan 25,
// step 2); the sign of its first term does not keep it as written (`stands_upright`). The chain's values are those of
// the nodes it replaces, its last the root's.
constexpr Chain chain_of(Context& at, std::size_t ri, bool fuses, std::vector<Value>& values) {
    const Graph& g = at.g;
    Sum sum{.cycle = at.feedback.cycle[ri], .root = g.nodes[ri].output};
    detail::open_up(at, sum, static_cast<std::int32_t>(ri), false);
    for (const Term& t : sum.terms) {
        const bool deep = t.value != kNoValue ? detail::reads_far_back(at, sum.cycle, t.value)
                                              : detail::reads_far_back(at, sum.cycle, t.a) || detail::reads_far_back(at, sum.cycle, t.b);
        // (A term narrower than the sum: as written it is added to a wider running sum, in ready order two such
        // terms first would be added in their own type, a rounding no precision grants.)
        const bool narrower = t.value != kNoValue && g.values[static_cast<std::size_t>(t.value)].type != g.values[static_cast<std::size_t>(sum.root)].type;
        if (deep || narrower) {
            return {};
        }
    }
    for (Term& t : sum.terms) {
        const std::int32_t ka = t.value != kNoValue ? detail::ready_key(at, sum.cycle, t.value) : detail::ready_key(at, sum.cycle, t.a);
        const std::int32_t kb = t.value != kNoValue ? ka : detail::ready_key(at, sum.cycle, t.b);
        t.key = ka > kb ? ka : kb;
    }
    std::vector<Term>& terms = sum.terms;
    const bool moved = detail::in_ready_order(terms);
    const std::size_t count = terms.size();
    for (std::size_t i = 0; i + 1 < count; ++i) {
        if (terms[i].value == kNoValue) {
            return {}; // a multiply-add already there but not last: it would need a product of its own
        }
    }
    Term& last = terms[count - 1];
    const Node* product = last.product >= 0 ? &g.nodes[static_cast<std::size_t>(last.product)] : nullptr;
    last.flip = product != nullptr && last.minus ? detail::factor_to_negate(at, *product) : kNoValue;
    const bool fresh = fuses && product != nullptr && (!last.minus || last.flip != kNoValue);
    std::vector<std::int32_t> spare{};
    for (const std::int32_t ni : sum.inside) {
        spare.push_back(g.nodes[static_cast<std::size_t>(ni)].output);
    }
    if (fresh) {
        spare.push_back(product->output);
    }
    // (Two terms only reordered, no product fused, gain nothing: one add either way.)
    if (count < 2 || !detail::stands_upright(at, terms, fresh) || (last.value == kNoValue && last.minus) || (!moved && !fresh) ||
        (count == 2 && !fresh) || spare.size() + 2 < count) {
        return {};
    }
    return detail::build_chain(g, sum, fresh, spare, values);
}

// Every feedback sum of analysed graph `g` in the order its terms are ready (`early`, per cycle: another cycle's output
// made this sample with what is ready at once), its last product fused where `fuses`; `g` itself where none moves. The
// chain of a sum stands where its root stood, after all its terms, so no node reads a later one. `upstream`: some sum's
// term is another cycle's output made this sample, so that the other order may differ. `from`: per node of the result,
// the node of `g` it stands for (a chain its root's; -1 for a negation of a timeless factor).
constexpr Graph in_order(const Graph& g, bool fuses, const std::vector<char>& early, bool& upstream, std::vector<std::int32_t>& from) {
    Context at = detail::context_of(g, early);
    from.resize(g.nodes.size());
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        from[i] = static_cast<std::int32_t>(i);
    }
    if (!at.facts.why_not.empty() || !at.feedback.why_not.empty()) {
        return g; // (the planner refuses it, by name)
    }
    std::vector<Value> values(g.values.begin(), g.values.end());
    std::vector<Chain> chains(g.nodes.size());
    std::vector<char> gone(g.nodes.size(), 0);
    bool any = false;
    for (std::size_t ri = 0; ri < g.nodes.size(); ++ri) {
        if (detail::is_a_root(at, ri)) {
            chains[ri] = detail::chain_of(at, ri, fuses, values);
            for (const std::int32_t ni : chains[ri].gone) {
                gone[static_cast<std::size_t>(ni)] = 1;
            }
            any = any || !chains[ri].nodes.empty();
        }
    }
    upstream = at.upstream;
    if (!any) {
        return g;
    }
    std::vector<Node> nodes{};
    std::vector<std::int32_t> stands_for{};
    std::vector<std::vector<Node>> after(g.nodes.size());
    for (const Chain& chain : chains) {
        for (const Node& n : chain.ahead) {
            const std::int32_t p = at.facts.producer[static_cast<std::size_t>(n.inputs[0])];
            (p < 0 ? nodes : after[static_cast<std::size_t>(p)]).push_back(n);
        }
    }
    stands_for.resize(nodes.size(), -1);
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        if (!chains[i].nodes.empty()) {
            nodes.insert(nodes.end(), chains[i].nodes.begin(), chains[i].nodes.end());
        } else if (!gone[i]) {
            nodes.push_back(g.nodes[i]);
        }
        stands_for.resize(nodes.size(), static_cast<std::int32_t>(i));
        nodes.insert(nodes.end(), after[i].begin(), after[i].end());
        stands_for.resize(nodes.size(), -1);
    }
    std::optional<Graph> out = detail::with(g, nodes, values);
    from = out ? stands_for : from;
    return out.value_or(g);
}

// The same, every cycle's sums in one order.
constexpr Graph in_order(const Graph& g, bool fuses, bool early, bool& upstream) {
    std::vector<std::int32_t> from{};
    return detail::in_order(g, fuses, std::vector<char>(g.nodes.size(), early ? 1 : 0), upstream, from);
}

// What `re`, rewritten from `g`, costs a sample, per group of `g`'s cycles (`group`, by `linked_cycles`): each node of
// `re` in the group of the node of `g` it stands for (`from`).
constexpr std::vector<double> priced(const Graph& re, const Machine& m, const std::vector<std::int32_t>& group, const std::vector<std::int32_t>& from) {
    std::vector<std::int32_t> of(re.nodes.size(), -1);
    for (std::size_t i = 0; i < re.nodes.size(); ++i) {
        of[i] = from[i] >= 0 ? group[static_cast<std::size_t>(from[i])] : -1;
    }
    return loops::graph::feedback_cycles(re, m, of, group.size());
}

} // namespace detail

// How much cheaper the model must price the order with each section's own newest state last before the rule takes it:
// a stand-in for the model's error near the flip, where it puts the two orders within a few per cent and picks the
// slower. Through the runner on zen2 (`Plan/probes/cascade-order/runner_orders.cpp`, five-term sections), two sections
// priced 8.05 against 11.10 cycles and ran 0.71x; three priced 12.24 against 12.62 and ran 1.10x (one voice), 0.98x (two
// voices), 1.25x (eight in lanes; the bench's `biquad3ch` C8 1.12-1.25x). The suspect: zen2's issue bound,
// `fp_per_cycle` 2, which leaves out its add pipes beside the multiply-adds, so the longer cascade's price climbs too
// fast (backlog: recalibrate it, and the margin goes).
inline constexpr double kEarlyMargin = 0.10;

// Every feedback sum of analysed graph `g` in the order its terms are ready, its last product fused where machine `m`
// has the multiply-add. Where a sum adds another cycle's output made this sample (a cascade's section before), the two
// orders trade the stage's bounds (Plan 25, step 0.5): that output last (Decision 66) keeps a sample's path through the
// stage short and puts three steps on each section's own cycle; the section's own newest state last puts one
// multiply-add there and lengthens the path, which the core's window overlaps with the next samples' only so far
// (`Plan/probes/cascade-order/sections.cpp`: on zen2 the second order wins up to three sections, the first from four).
// Eligibility: a feedback sum whose terms include another cycle's output of this sample takes the order its stage
// costs clearly less in (`kEarlyMargin`). One sum's order moves the path of every stage it shares, and stages form only
// among cycles linked by what they read this sample (`loops::graph::linked_cycles`), so each such group is priced whole
// and apart: all its sums in one order against all in the other (`loops::graph::feedback_cycles`, stage by stage). An
// independent cascade beside it keeps its own order.
constexpr Graph ordered_feedback_sums(const Graph& g, const Machine& m) {
    const std::size_t count = g.nodes.size();
    bool upstream = false;
    std::vector<std::int32_t> from_late{};
    std::vector<std::int32_t> from_early{};
    const Graph late = detail::in_order(g, m.fma, std::vector<char>(count, 0), upstream, from_late);
    if (!upstream) {
        return late;
    }
    const Graph early = detail::in_order(g, m.fma, std::vector<char>(count, 1), upstream, from_early);
    const loops::graph::Facts facts = loops::graph::facts_of(g);
    const std::vector<std::int32_t> group = loops::graph::linked_cycles(g, facts, loops::graph::find_feedback(g, facts));
    const std::vector<double> b = detail::priced(early, m, group, from_early);
    const std::vector<double> f = detail::priced(late, m, group, from_late);
    std::vector<char> takes(count, 0);
    bool any = false;
    bool every = true;  // (every group whose orders differ takes B)
    for (std::size_t c = 0; c < count; ++c) {
        takes[c] = b[c] <= (1 - kEarlyMargin) * f[c] && b[c] != f[c] ? 1 : 0;
        any = any || takes[c];
        every = every && (takes[c] || b[c] == f[c]);
    }
    if (!any || every) {
        return any ? early : late;
    }
    std::vector<char> by_cycle(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        by_cycle[i] = group[i] >= 0 ? takes[static_cast<std::size_t>(group[i])] : 0;
    }
    return detail::in_order(g, m.fma, by_cycle, upstream, from_late);
}

} // namespace heinzel::rewrite
