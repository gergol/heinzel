#pragma once

// Skewed stages built by hand (`docs/sections-in-lanes.md`): sections linked in lanes as the lowering lowers
// them, for `loops_skewed_test` and the runner's speed probe. A stage's statement 0 reads the fill (at lane 0); the
// link `u = _chain(x, V)` (V's State read one lane up) and each section's arithmetic over [k, time] follow, and last the
// slice, which stores the last section's value (at lane k − 1). A value whose past is read keeps it in a State, its
// keeper closing it into a buffer of its past alone, its readers before it; V, where only the link reads it, is opened
// by none. The tree is the voice layout (`loops a0 vector 4 xB | a1`): the sections' strip, the States, their Opens, the
// time loop and a step of B bodies of four lanes in it, their Closes.
//   - the linked graphs (`linked_sections.hpp`), in double at High: the TPT pair, LR8, the five peaks (their
//     coefficients [5, 1] feeds), the eight tanh sections, and the pair into the eight (two skewed stages);
//   - a Param cascade of k direct-form-I biquads as the merge and the rules after it leave it, at Normal: x and y in
//     float, the cycle in double, summed in the rule's order (the link's term last, fused), its five coefficients
//     [k, 1] floats, from feeds or from a stage before it that gathers 5k float Params by the gather's selects.

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

#include "heinzel/ir/ir.hpp"
#include "heinzel/loops/program.hpp"

#include "hand_built_tree.hpp"
#include "linked_sections.hpp"

namespace skewed_sections {

using namespace hand_built_tree;
using heinzel::numerics::Precision;
namespace op = heinzel::op;
namespace ls = heinzel::testing::linked_sections;

inline constexpr std::int32_t kLanes = 4; // doubles a body
inline constexpr ElemType kF32 = ElemType::F32;
inline constexpr ElemType kF64 = ElemType::F64;

// ---- statements and buffers --------------------------------------------------------------------------------------

constexpr Stmt loaded(ElemType type, Access from, Access to = {}) {
    return Stmt{.kind = StmtKind::Load, .type = type, .computed_in = type, .in = {from}, .store = to};
}
constexpr Stmt mapped(OpCode code, ElemType type, ElemType in, Precision precision, Access a, Access b = {}, Access c = {}, Access to = {}) {
    return Stmt{.kind = StmtKind::Map, .op = code, .type = type, .computed_in = in, .precision = precision, .in = {a, b, c}, .store = to};
}
// In double, at High (the linked graphs).
constexpr Stmt at_high(OpCode code, Access a, Access b = {}, Access c = {}, Access to = {}) { return mapped(code, kF64, kF64, Precision::High, a, b, c, to); }
// In double, at Normal (the Param cascade's cycle).
constexpr Stmt at_normal(OpCode code, Access a, Access b = {}, Access c = {}, Access to = {}) {
    return mapped(code, kF64, kF64, Precision::Normal, a, b, c, to);
}
constexpr Access number(double v) { return Access{.source = Source::Immediate, .immediate = std::bit_cast<std::uint64_t>(v)}; }

// Over the stage's domain [k, time]: a signal at this sample; one number a section, or a State's past `back` samples
// ago; the link's operand 1, V one section down.
constexpr Access at_this_sample(std::int32_t b) { return read(b, {-1, 0}); }
constexpr Access per_section(std::int32_t b, std::int32_t back = 0) { return read(b, {0, -1}, back); }
constexpr Access one_section_down(std::int32_t b) {
    Access a = read(b, {0, -1});
    a.offset[0] = -1;
    return a;
}

constexpr std::int32_t pushed(Program& p, Buffer b) {
    const auto at = static_cast<std::int32_t>(p.buffers.size());
    b.value = at;
    p.buffers.push_back(b);
    return at;
}
constexpr Buffer fed(ElemType type, Shape shape, std::int32_t feed) { return Buffer{.type = type, .shape = shape, .home = Home::Feed, .feed = feed}; }
constexpr Buffer signal(ElemType type) { return Buffer{.type = type, .shape = Shape{Dim{0}}}; }
// A State's past alone, `prefix` samples of each of k sections.
constexpr Buffer past_of(std::int32_t k, std::int32_t prefix) { return Buffer{.type = kF64, .shape = Shape{k, Dim{0}}, .prefix = prefix, .only_past = true}; }
// One double a section, a constant tensor [k, 1].
constexpr std::int32_t constants(Program& p, std::span<const double> numbers) {
    const auto first = static_cast<std::int32_t>(p.constants.size());
    for (const double n : numbers) {
        p.constants.push_back(std::bit_cast<std::uint64_t>(n));
    }
    const auto k = static_cast<std::int32_t>(numbers.size());
    return pushed(p, Buffer{.type = kF64, .shape = Shape{k, 1}, .home = Home::Constant, .first = first});
}

// ---- the skewed stage's tree -------------------------------------------------------------------------------------

// A value whose past the stage keeps in a State: its keeper, its deepest lag, and the statement reading each slot (-1:
// none; V where only the link reads it has none).
struct Kept {
    std::int32_t keeper{-1};
    std::int32_t slots{1};
    std::array<std::int32_t, 2> readers{-1, -1};
};
// k sections; the link's statement, which reads V's State, made by statement `v`; the slice's statement; at most two
// States.
struct Skew {
    std::int32_t k{0};
    std::int32_t link{1};
    std::int32_t v{-1};
    std::int32_t slice{-1};
    std::array<Kept, 2> kept{};
};
// The lanes' chain under `parent`: an Unroll by four where there are two bodies or more, then the Vector.
constexpr std::int32_t lanes_of(Program& p, std::int32_t bodies, std::int32_t parent) {
    const std::int32_t unrolled = bodies > 1 ? add(p, walker(NodeKind::Unroll, 0, kLanes), parent) : parent;
    return add(p, walker(NodeKind::Vector, 0, kLanes), unrolled);
}

// Each read State slot's Open (or Close), in statement order, under the lanes outside the time loop.
constexpr void opened_or_closed(Program& p, const Skew& s, NodeKind kind, std::int32_t bodies, std::int32_t states) {
    std::array<Node, 4> leaves{};
    std::size_t count = 0;
    for (const Kept& kept : s.kept) {
        for (std::size_t slot = 0; slot < kept.readers.size(); ++slot) {
            if (kept.keeper >= 0 && kept.readers[slot] >= 0) {
                leaves[count++] = Node{.kind = kind, .stmt = kept.readers[slot], .keeps = kept.keeper, .slot = static_cast<std::int32_t>(slot)};
            }
        }
    }
    std::sort(leaves.begin(), leaves.begin() + static_cast<std::ptrdiff_t>(count), [](const Node& a, const Node& b) { return a.stmt < b.stmt; });
    const std::int32_t lanes = lanes_of(p, bodies, states);
    for (std::size_t n = 0; n < count; ++n) {
        add(p, leaves[n], lanes);
    }
}

// Statement i's Compute: the fill and the slice at a lane, the link reading V shifted, a State's reader or keeper.
constexpr Node compute_of(const Skew& s, std::int32_t i) {
    Node n{.kind = NodeKind::Compute, .stmt = i};
    n.lane = i == 0 ? 0 : i == s.slice ? s.k - 1 : -1;
    for (const Kept& kept : s.kept) {
        for (std::size_t slot = 0; kept.keeper >= 0 && slot < kept.readers.size(); ++slot) {
            n.reads = kept.readers[slot] == i ? kept.keeper : n.reads;
            n.slot = kept.readers[slot] == i ? static_cast<std::int32_t>(slot) : n.slot;
        }
        n.keeps = kept.keeper == i ? i : n.keeps;
        n.closes = n.closes || kept.keeper == i;
    }
    n.reads = i == s.link ? s.v : n.reads;
    n.shifted = i == s.link;
    return n;
}

// The last `count` statements as a skewed stage over [k, time].
constexpr void skewed_stage(Program& p, const Skew& s, std::int32_t count) {
    const std::int32_t bodies = (s.k + kLanes - 1) / kLanes;
    const std::int32_t width = bodies * kLanes;
    const std::int32_t root = stage(p, {s.k, Dim{0}}, count);
    const std::int32_t strip = add(p, loop(0, width, width > s.k ? Tail::Masked : Tail::Ones, 0), root);
    std::int32_t states = strip;
    for (const Kept& kept : s.kept) {
        Node held = registers(Role::State, kept.keeper, -1);
        held.slots = kept.slots;
        states = kept.keeper >= 0 ? add(p, held, states) : states;
    }
    opened_or_closed(p, s, NodeKind::Open, bodies, states);
    const std::int32_t time = add(p, loop(1, 1), states);
    const auto head = static_cast<std::int32_t>(p.nodes.size());
    const std::int32_t step = lanes_of(p, bodies, time);
    for (std::int32_t i = 0; i < count; ++i) {
        add(p, compute_of(s, i), step);
    }
    opened_or_closed(p, s, NodeKind::Close, bodies, states);
    for (auto n = static_cast<std::size_t>(root); n < p.nodes.size(); ++n) {
        p.nodes[n].link = p.nodes[n].kind == NodeKind::Registers ? head : p.nodes[n].link;
    }
    Stage& skewed = p.stages[p.stages.size() - 1];
    skewed.time_loop = time;
    skewed.skewed = true;
}

// ---- the linked graphs' sections, in double at High -------------------------------------------------------------

// The TPT pair (`linked_tpt`), its fill read from `in`, its slice stored into `out`: e = u − s[n − 1], v = G e,
// y = v + s[n − 1], s = y + v. V is y, which only the link reads.
constexpr void tpt_stage(Program& p, std::int32_t in, std::int32_t out) {
    const std::int32_t s = pushed(p, past_of(2, 1));
    const std::int32_t y = pushed(p, past_of(2, 0));
    const std::int32_t gain = constants(p, ls::kTptGains);
    p.stmts.push_back(loaded(kF64, at_this_sample(in)));
    p.stmts.push_back(at_high(op::Chain, local(0), one_section_down(y)));
    p.stmts.push_back(loaded(kF64, per_section(s, 1)));
    p.stmts.push_back(at_high(op::Sub, local(1), local(2)));
    p.stmts.push_back(loaded(kF64, per_section(gain)));
    p.stmts.push_back(at_high(op::Mul, local(4), local(3)));
    p.stmts.push_back(at_high(op::Add, local(5), local(2), {}, per_section(y)));
    p.stmts.push_back(at_high(op::Add, local(6), local(5), {}, per_section(s)));
    p.stmts.push_back(loaded(kF64, local(6), at_this_sample(out)));
    const Kept own{.keeper = 6};
    const Kept state{.keeper = 7, .readers = {2, -1}};
    skewed_stage(p, Skew{.k = 2, .v = 6, .slice = 8, .kept = {own, state}}, 9);
}

// k direct-form-I biquads (`linked_biquads`): y = b0 u + b1 u[n − 1] + b2 u[n − 2] − a1 y[n − 1] − a2 y[n − 2], summed in
// that order, coefficient i from the [k, 1] buffer `c[i]`.
constexpr void biquad_stage(Program& p, std::int32_t k, std::int32_t in, std::int32_t out, const std::array<std::int32_t, 5>& c) {
    const std::int32_t u = pushed(p, past_of(k, 2));
    const std::int32_t y = pushed(p, past_of(k, 2));
    p.stmts.push_back(loaded(kF64, at_this_sample(in)));
    p.stmts.push_back(loaded(kF64, per_section(u, 1)));
    p.stmts.push_back(loaded(kF64, per_section(u, 2)));
    p.stmts.push_back(at_high(op::Chain, local(0), one_section_down(y), {}, per_section(u)));
    p.stmts.push_back(loaded(kF64, per_section(y, 1)));
    p.stmts.push_back(loaded(kF64, per_section(y, 2)));
    constexpr std::array<std::int32_t, 5> kTimes{3, 1, 2, 4, 5}; // what each coefficient multiplies: u, its lags, y's
    for (std::int32_t i = 0; i < 5; ++i) { // s6 … s15: b0 u, b1 u[n − 1], b2 u[n − 2], a1 y[n − 1], a2 y[n − 2]
        p.stmts.push_back(loaded(kF64, per_section(c[static_cast<std::size_t>(i)])));
        p.stmts.push_back(at_high(op::Mul, local(6 + 2 * i), local(kTimes[static_cast<std::size_t>(i)])));
    }
    p.stmts.push_back(at_high(op::Add, local(7), local(9)));
    p.stmts.push_back(at_high(op::Add, local(16), local(11)));
    p.stmts.push_back(at_high(op::Sub, local(17), local(13)));
    p.stmts.push_back(at_high(op::Sub, local(18), local(15), {}, per_section(y)));
    p.stmts.push_back(loaded(kF64, local(19), at_this_sample(out)));
    const Kept link{.keeper = 3, .slots = 2, .readers = {1, 2}};
    const Kept own{.keeper = 19, .slots = 2, .readers = {4, 5}};
    skewed_stage(p, Skew{.k = k, .link = 3, .v = 19, .slice = 20, .kept = {link, own}}, 21);
}

// The eight tanh sections (`linked_saturating`): y = tanh(0.5 u + d y[n − 1] + 0.1).
constexpr void saturating_stage(Program& p, std::int32_t in, std::int32_t out) {
    const std::int32_t y = pushed(p, past_of(8, 1));
    const std::int32_t d = constants(p, ls::kFeedback);
    p.stmts.push_back(loaded(kF64, at_this_sample(in)));
    p.stmts.push_back(at_high(op::Chain, local(0), one_section_down(y)));
    p.stmts.push_back(loaded(kF64, per_section(y, 1)));
    p.stmts.push_back(at_high(op::Mul, number(0.5), local(1)));
    p.stmts.push_back(loaded(kF64, per_section(d)));
    p.stmts.push_back(at_high(op::Mul, local(4), local(2)));
    p.stmts.push_back(at_high(op::Add, local(3), local(5)));
    p.stmts.push_back(at_high(op::Add, local(6), number(0.1)));
    p.stmts.push_back(at_high(op::Tanh, local(7), {}, {}, per_section(y)));
    p.stmts.push_back(loaded(kF64, local(8), at_this_sample(out)));
    const Kept own{.keeper = 8, .readers = {2, -1}};
    skewed_stage(p, Skew{.k = 8, .v = 8, .slice = 9, .kept = {own, Kept{}}}, 10);
}

// A program over x (feed 0, double) and y, both [time], what `body` makes between them.
template <class Body>
constexpr Program from_x_to_y(Body body) {
    Program p{};
    const std::int32_t x = pushed(p, fed(kF64, Shape{Dim{0}}, 0));
    const std::int32_t y = pushed(p, signal(kF64));
    body(p, x, y);
    p.outputs.push_back(y);
    return p;
}

constexpr Program tpt_pair() {
    return from_x_to_y([](Program& p, std::int32_t x, std::int32_t y) { tpt_stage(p, x, y); });
}

// Each coefficient's k numbers, one constant tensor each.
template <std::size_t K>
constexpr std::array<std::int32_t, 5> constant_columns(Program& p, const std::array<ls::Coefficients, K>& sections) {
    std::array<std::int32_t, 5> c{};
    for (std::size_t i = 0; i < c.size(); ++i) {
        std::array<double, K> column{};
        for (std::size_t j = 0; j < K; ++j) {
            column[j] = sections[j][i];
        }
        c[i] = constants(p, column);
    }
    return c;
}

constexpr Program lr8() {
    return from_x_to_y([](Program& p, std::int32_t x, std::int32_t y) {
        const std::array<std::int32_t, 5> c = constant_columns(p, ls::kLr8);
        biquad_stage(p, 4, x, y, c);
    });
}

// The peaks' coefficients, [5, 1] feeds 1 to 5 (b0 b1 b2 a1 a2, as the graph's inputs after x).
constexpr Program peaks() {
    return from_x_to_y([](Program& p, std::int32_t x, std::int32_t y) {
        std::array<std::int32_t, 5> c{};
        for (std::int32_t i = 0; i < 5; ++i) {
            c[static_cast<std::size_t>(i)] = pushed(p, fed(kF64, Shape{5, 1}, 1 + i));
        }
        biquad_stage(p, 5, x, y, c);
    });
}

constexpr Program saturating_chain() {
    return from_x_to_y([](Program& p, std::int32_t x, std::int32_t y) { saturating_stage(p, x, y); });
}

// The pair into the eight: the pair's slice a workspace signal the eight's fill reads.
constexpr Program in_series() {
    return from_x_to_y([](Program& p, std::int32_t x, std::int32_t y) {
        const std::int32_t pair = pushed(p, signal(kF64));
        tpt_stage(p, x, pair);
        saturating_stage(p, pair, y);
    });
}

// ---- the Param cascade, at Normal --------------------------------------------------------------------------------

// Coefficient i of a section, as the stage reads them: b0 b1 b2 na1 na2 (na1 = −a1, na2 = −a2: `feedback_sums`' sum).
inline constexpr std::size_t kCoefficients = 5;

// A section as the sums' rule orders it (`rewrite_feedback_sums_test`, and the hand lanes, `skewed_lanes.cpp`):
// v = madd(b0, u, ((b2 u[n − 2] + na2 v[n − 2]) + b1 u[n − 1]) + na1 v[n − 1]), the cycle in double, the float
// coefficients converted where they are read, v converted down to float before the slice.
constexpr void param_stage(Program& p, std::int32_t k, std::int32_t in, std::int32_t out, const std::array<std::int32_t, 5>& c) {
    const std::int32_t u = pushed(p, past_of(k, 2));
    const std::int32_t v = pushed(p, past_of(k, 2));
    p.stmts.push_back(loaded(kF32, at_this_sample(in)));
    p.stmts.push_back(loaded(kF64, per_section(u, 1)));
    p.stmts.push_back(loaded(kF64, per_section(u, 2)));
    p.stmts.push_back(at_normal(op::Chain, local(0), one_section_down(v), {}, per_section(u)));
    p.stmts.push_back(loaded(kF64, per_section(v, 1)));
    p.stmts.push_back(loaded(kF64, per_section(v, 2)));
    p.stmts.push_back(loaded(kF32, per_section(c[2])));
    p.stmts.push_back(at_normal(op::Mul, local(6), local(2)));
    p.stmts.push_back(loaded(kF32, per_section(c[4])));
    p.stmts.push_back(at_normal(op::Mul, local(8), local(5)));
    p.stmts.push_back(at_normal(op::Add, local(7), local(9)));
    p.stmts.push_back(loaded(kF32, per_section(c[1])));
    p.stmts.push_back(at_normal(op::Mul, local(11), local(1)));
    p.stmts.push_back(at_normal(op::Add, local(10), local(12)));
    p.stmts.push_back(loaded(kF32, per_section(c[3])));
    p.stmts.push_back(at_normal(op::Mul, local(14), local(4)));
    p.stmts.push_back(at_normal(op::Add, local(13), local(15)));
    p.stmts.push_back(loaded(kF32, per_section(c[0])));
    p.stmts.push_back(at_normal(op::MultiplyAdd, local(17), local(3), local(16), per_section(v)));
    p.stmts.push_back(loaded(kF32, local(18)));
    p.stmts.push_back(loaded(kF32, local(19), at_this_sample(out)));
    const Kept link{.keeper = 3, .slots = 2, .readers = {1, 2}};
    const Kept own{.keeper = 18, .slots = 2, .readers = {4, 5}};
    skewed_stage(p, Skew{.k = k, .link = 3, .v = 18, .slice = 20, .kept = {link, own}}, 21);
}

// The Params' feeds: coefficient i of section j is feed 1 + i k + j, one float.
constexpr std::int32_t param_feed(std::int32_t k, std::size_t i, std::int32_t j) { return 1 + static_cast<std::int32_t>(i) * k + j; }

// The gather: a stage over [k, 1] before the cascade gathers each coefficient's k Params into a [k, 1] float buffer,
// acc = where(e_j, p_j, acc) for j = 1 … k − 1 from acc = p_0, e_j the one-hot double masks; the buffers, which it
// returns, are what the cascade reads. Its tree: the one sample, a strip of four lanes over the sections.
constexpr std::array<std::int32_t, 5> gathered(Program& p, std::int32_t k) {
    std::array<std::int32_t, 5> c{};
    for (std::size_t i = 0; i < c.size(); ++i) {
        c[i] = pushed(p, Buffer{.type = kF32, .shape = Shape{k, 1}});
    }
    const auto first = static_cast<std::int32_t>(p.stmts.size());
    for (std::int32_t j = 1; j < k; ++j) {
        std::array<double, 8> one_hot{};
        one_hot[static_cast<std::size_t>(j)] = 1;
        const std::int32_t mask = constants(p, std::span<const double>(one_hot.data(), static_cast<std::size_t>(k)));
        p.stmts.push_back(loaded(kF64, per_section(mask)));
    }
    for (std::size_t i = 0; i < c.size(); ++i) {
        std::int32_t acc = static_cast<std::int32_t>(p.stmts.size()) - first;
        const std::int32_t section_0 = pushed(p, fed(kF32, Shape{}, param_feed(k, i, 0)));
        p.stmts.push_back(loaded(kF32, read(section_0, {-1, -1})));
        for (std::int32_t j = 1; j < k; ++j) {
            const std::int32_t param = pushed(p, fed(kF32, Shape{}, param_feed(k, i, j)));
            p.stmts.push_back(loaded(kF32, read(param, {-1, -1})));
            const Access to = j == k - 1 ? read(c[i], {0, 1}) : Access{};
            const auto here = static_cast<std::int32_t>(p.stmts.size()) - first;
            p.stmts.push_back(mapped(op::Select, kF32, kF64, Precision::Normal, local(j - 1), local(here - 1), local(acc), to));
            acc = here;
        }
    }
    const auto count = static_cast<std::int32_t>(p.stmts.size()) - first;
    const std::int32_t root = stage(p, {k, 1}, count);
    const std::int32_t sample = add(p, loop(1, 1), root);
    const std::int32_t strip = add(p, loop(0, kLanes, k % kLanes != 0 ? Tail::Masked : Tail::Ones, 0), sample);
    const std::int32_t lanes = add(p, walker(NodeKind::Vector, 0, kLanes), strip);
    for (std::int32_t s = 0; s < count; ++s) {
        add(p, leaf(NodeKind::Compute, s), lanes);
    }
    return c;
}

enum class Coefficients : std::uint8_t { Fed, Gathered };

// k sections over x (feed 0, float) into y (float); fed, coefficient i is the [k, 1] float feed 1 + i.
constexpr Program param_cascade(std::int32_t k, Coefficients from) {
    Program p{};
    const std::int32_t x = pushed(p, fed(kF32, Shape{Dim{0}}, 0));
    std::array<std::int32_t, 5> c{};
    if (from == Coefficients::Fed) {
        for (std::int32_t i = 0; i < 5; ++i) {
            c[static_cast<std::size_t>(i)] = pushed(p, fed(kF32, Shape{k, 1}, 1 + i));
        }
    } else {
        c = gathered(p, k);
    }
    const std::int32_t y = pushed(p, signal(kF32));
    param_stage(p, k, x, y, c);
    p.outputs.push_back(y);
    return p;
}

} // namespace skewed_sections
