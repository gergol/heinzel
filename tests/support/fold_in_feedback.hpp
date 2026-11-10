#pragma once

// Feedback stages whose sample holds a fold (Plan/23 10), built by hand: a bank of voices in lanes inside a loop that
// sums them every sample and feeds the sum back. One shape, `sample_tree`: the voices' strip outside, one chunk; the
// State registers (a lane state sized by the chain, a one-voice state one body at width one) and their Opens; the time
// loop, and in each sample the part before the fold at width one, the fold's Registers with the lanes in their chain,
// its Finish and the part after it; the Closes after the time loop. The banks over it (`fold_in_feedback_test`'s graphs
// say what each computes, at High or Normal):
//   plain    u = x − g·y[n−1]; v = c0·u + c1·u[n−1] + a1·v[n−1]; y = Σv + d·u   (u and v kept in their registers)
//   direct   v = c0·x − c1·y[n−1] + a1·v[n−1]; y = Σv + d·x                    (the lanes read the loop directly)
//   own lag  u = x − g·s[n−1]; v = u + a1·v[n−1]; s = Σv (its Finish keeps it); y = d·s
//   TDF2     u = x − g·y[n−1]; o = c0·u + w[n−1]; y = Σo; w = c1·u − a1·o      (lanes after the fold)
// It includes only `ir/` and `loops/`, so `loops_alone` admits it (`loops_ir` pins a listing and its refusals).

#include <array>
#include <bit>
#include <cstdint>
#include <initializer_list>

#include "heinzel/loops/program.hpp"

#include "hand_built_tree.hpp"

namespace fold_in_feedback {

using namespace heinzel::loops;
using heinzel::Dim;
using heinzel::ElemType;
using heinzel::Extent;
using heinzel::ReduceOp;
using heinzel::Shape;
namespace op = heinzel::op;
namespace hb = hand_built_tree;

enum class Bank : std::uint8_t { Plain, Direct, OwnLag, Tdf2 };

// Which bank, over how many voices of which type, in how many lane bodies of how many lanes.
struct Voices {
    Bank bank{Bank::Plain};
    std::int32_t voices{8};
    std::int32_t lanes{4};
    std::int32_t bodies{2};
    ElemType type{ElemType::F64};
    bool high{true}; // High (a strict fold), or Normal
};

inline constexpr double kGain = 0.125; // g
inline constexpr double kDirect = 0.5; // d

// Voice k's coefficients: c0, c1 and a1, a different number a voice, each a float (the loop stays bounded over 16).
constexpr double c0(std::int32_t k) { return 0.0625 + 0.0078125 * k; }
constexpr double c1(std::int32_t k) { return -0.03125 + 0.00390625 * k; }
constexpr double a1(std::int32_t k) { return (k % 2 == 0 ? 0.375 : -0.25) + 0.0078125 * k; }

// A state the sample hands on: the statement keeping it, the one reading it, whether it is a lane state, and whether its
// registers alone hold it (closed after the time loop).
struct Kept {
    std::int32_t keeper{-1};
    std::int32_t reader{-1};
    bool lanes{false};
    bool closes{false};
};

// Where the parts of a stage's `count` statements lie: before `lanes` the part before the fold, from `after` the part
// after it, between them the lanes (the fold among them).
struct Parts {
    std::int32_t lanes{0};
    std::int32_t after{0};
    std::int32_t fold{0};
    std::int32_t count{0};
    std::array<Kept, 4> kept{};
    std::int32_t states{0};
};

constexpr Stmt typed(Stmt st, const Voices& v) {
    st.type = st.computed_in = v.type;
    st.precision = st.kind == StmtKind::Load ? heinzel::numerics::Precision::Inherit
                   : v.high                  ? heinzel::numerics::Precision::High
                                             : heinzel::numerics::Precision::Normal;
    st.strict = st.kind == StmtKind::Fold && v.high;
    return st;
}
constexpr Access number(double x) { return Access{.source = Source::Immediate, .immediate = std::bit_cast<std::uint64_t>(x)}; }

// A constant [voices, 1] of `coefficient`, read along the voices.
constexpr std::int32_t constants(Program& p, const Voices& v, double (*coefficient)(std::int32_t)) {
    const auto first = static_cast<std::int32_t>(p.constants.size());
    for (std::int32_t k = 0; k < v.voices; ++k) {
        p.constants.push_back(std::bit_cast<std::uint64_t>(coefficient(k)));
    }
    p.buffers.push_back(Buffer{.type = v.type, .shape = Shape{v.voices, 1}, .home = Home::Constant, .value = 100 + first, .first = first});
    return static_cast<std::int32_t>(p.buffers.size() - 1);
}

constexpr std::int32_t buffer(Program& p, const Voices& v, Shape shape, std::int32_t prefix, bool only_past) {
    p.buffers.push_back(Buffer{.type = v.type, .shape = shape, .value = static_cast<std::int32_t>(p.buffers.size()), .prefix = prefix, .only_past = only_past});
    return static_cast<std::int32_t>(p.buffers.size() - 1);
}

// ---- the tree ------------------------------------------------------------------------------------------------

// The chain of the lanes under `parent`: an Unroll by the lanes where there are bodies, then the Vector. Its innermost.
constexpr std::int32_t chain(Program& p, const Voices& v, std::int32_t parent) {
    const std::int32_t at = v.bodies > 1 ? hb::add(p, hb::walker(NodeKind::Unroll, 0, v.lanes), parent) : parent;
    return hb::add(p, hb::walker(NodeKind::Vector, 0, v.lanes), at);
}

// Statement r's leaf: a Compute reading a State where it is a reader, keeping its value where it is a keeper.
constexpr Node leaf_of(const Parts& s, std::int32_t r) {
    Node n{.kind = NodeKind::Compute, .stmt = r};
    for (std::int32_t k = 0; k < s.states; ++k) {
        const Kept& kept = s.kept[static_cast<std::size_t>(k)];
        n.reads = kept.reader == r ? kept.keeper : n.reads;
        n.keeps = kept.keeper == r ? r : n.keeps;
        n.closes = n.closes || (kept.keeper == r && kept.closes);
    }
    return n;
}

// Each Open (`close`: Close) of the states whose lanes are `lanes` under `parent`, in their readers' order.
constexpr void opened(Program& p, const Parts& s, std::int32_t parent, bool lanes, bool close) {
    for (std::int32_t r = 0; r < s.count; ++r) {
        for (std::int32_t k = 0; k < s.states; ++k) {
            const Kept& kept = s.kept[static_cast<std::size_t>(k)];
            if (kept.reader == r && kept.lanes == lanes && (!close || kept.closes)) {
                hb::add(p, Node{.kind = close ? NodeKind::Close : NodeKind::Open, .stmt = r, .keeps = kept.keeper}, parent);
            }
        }
    }
}

constexpr bool any(const Parts& s, bool lanes, bool closes) {
    bool found = false;
    for (std::int32_t k = 0; k < s.states; ++k) {
        found = found || (s.kept[static_cast<std::size_t>(k)].lanes == lanes && (!closes || s.kept[static_cast<std::size_t>(k)].closes));
    }
    return found;
}

// One sample: before, the fold's Registers with its Init, the lanes in the chain and the Accumulate, its Finish, after.
// Returns the chain's head, which sizes the fold's registers and the lane states'.
constexpr std::int32_t one_sample(Program& p, const Voices& v, const Parts& s, std::int32_t time) {
    for (std::int32_t r = 0; r < s.lanes; ++r) {
        hb::add(p, leaf_of(s, r), time);
    }
    Node fold = hb::registers(Role::Accumulator, s.fold, -1);
    fold.opens_first = true;
    const std::int32_t regs = hb::add(p, fold, time);
    hb::add(p, hb::leaf(NodeKind::Init, s.fold), regs);
    const auto head = static_cast<std::int32_t>(p.nodes.size());
    const std::int32_t lanes = chain(p, v, regs);
    for (std::int32_t r = s.lanes; r < s.after; ++r) {
        hb::add(p, r == s.fold ? hb::leaf(NodeKind::Accumulate, r) : leaf_of(s, r), lanes);
    }
    Node finish = leaf_of(s, s.fold);
    finish.kind = NodeKind::Finish;
    hb::add(p, finish, regs);
    for (std::int32_t r = s.after; r < s.count; ++r) {
        hb::add(p, leaf_of(s, r), regs);
    }
    p.nodes[static_cast<std::size_t>(regs)].link = head;
    return head;
}

// The stage over the last `s.count` statements, in this file's one shape.
constexpr void sample_tree(Program& p, const Voices& v, const Parts& s) {
    const std::int32_t root = hb::stage(p, {Extent{v.voices}, Dim{0}}, s.count);
    std::int32_t at = hb::add(p, hb::loop(0, v.lanes * v.bodies, Tail::Masked, 0), root);
    std::array<std::int32_t, 4> state{};
    for (std::int32_t k = 0; k < s.states; ++k) { // (in their keepers' order)
        at = state[static_cast<std::size_t>(k)] = hb::add(p, hb::registers(Role::State, s.kept[static_cast<std::size_t>(k)].keeper, -1), at);
    }
    if (any(s, true, false)) {
        opened(p, s, chain(p, v, at), true, false);
    }
    opened(p, s, at, false, false);
    const std::int32_t head = one_sample(p, v, s, hb::add(p, hb::loop(1, 1), at));
    if (any(s, true, true)) {
        opened(p, s, chain(p, v, at), true, true);
    }
    opened(p, s, at, false, true);
    for (std::int32_t k = 0; k < s.states; ++k) {
        p.nodes[static_cast<std::size_t>(state[static_cast<std::size_t>(k)])].link = s.kept[static_cast<std::size_t>(k)].lanes ? head : -1;
    }
}

// ---- the banks -----------------------------------------------------------------------------------------------

// x, y, then the constants c0, c1, a1: buffers 0 to 4.
constexpr void the_ports(Program& p, const Voices& v, bool closed_y) {
    p.buffers.push_back(Buffer{.type = v.type, .shape = Shape{Dim{0}}, .home = Home::Feed, .feed = 0, .value = 0});
    buffer(p, v, Shape{Dim{0}}, 1, closed_y);
    constants(p, v, c0);
    constants(p, v, c1);
    constants(p, v, a1);
    p.outputs.push_back(1);
}

constexpr Stmt load(const Voices& v, Access from, Access to = {}) { return typed(hb::load(from, to), v); }
constexpr Stmt map(const Voices& v, heinzel::OpCode o, Access a, Access b, Access to = {}) { return typed(hb::map(o, a, b, to), v); }

// plain: u = x − g·y[n−1]; v = c0·u + c1·u[n−1] + a1·v[n−1]; y = Σv + d·u. Buffers: u 5, v 6 (each its past alone).
constexpr Program plain(Voices v) {
    Program p{};
    the_ports(p, v, false);
    const std::int32_t u = buffer(p, v, Shape{Dim{0}}, 1, true);
    const std::int32_t bank = buffer(p, v, Shape{v.voices, Dim{0}}, 1, true);
    const std::initializer_list<Stmt> stmts = {
        load(v, hb::read(1, {-1, 0}, 1)), load(v, hb::read(u, {-1, -1}, 1)), map(v, op::Mul, number(kGain), hb::local(0)), load(v, hb::read(0, {-1, 0})),
        map(v, op::Sub, hb::local(3), hb::local(2), hb::read(u, {-1, -1})),                                            // s4 u
        load(v, hb::read(2, {0, -1})), map(v, op::Mul, hb::local(5), hb::local(4)), load(v, hb::read(3, {0, -1})),        // s5 – s7
        map(v, op::Mul, hb::local(7), hb::local(1)), map(v, op::Add, hb::local(6), hb::local(8)),                       // s8, s9
        load(v, hb::read(bank, {0, -1}, 1)), load(v, hb::read(4, {0, -1})), map(v, op::Mul, hb::local(11), hb::local(10)), // s10 – s12
        map(v, op::Add, hb::local(9), hb::local(12), hb::read(bank, {0, -1})),                                       // s13 v
        typed(hb::fold(ReduceOp::Sum, hb::local(13), Access{}), v),                                              // s14 Σv
        map(v, op::Mul, number(kDirect), hb::local(4)), map(v, op::Add, hb::local(14), hb::local(15), hb::read(1, {-1, 0})), // s15, s16 y
    };
    for (const Stmt& st : stmts) {
        p.stmts.push_back(st);
    }
    Parts s{.lanes = 5, .after = 15, .fold = 14, .count = 17, .states = 3};
    s.kept = {Kept{4, 1, false, true}, Kept{13, 10, true, true}, Kept{16, 0, false, false}};
    sample_tree(p, v, s);
    return p;
}

// direct: v = c0·x − c1·y[n−1] + a1·v[n−1]; y = Σv + d·x, x read in the lanes and again after the fold. Buffer: v 5.
constexpr Program direct(Voices v) {
    Program p{};
    the_ports(p, v, false);
    const std::int32_t bank = buffer(p, v, Shape{v.voices, Dim{0}}, 1, true);
    const std::initializer_list<Stmt> stmts = {
        load(v, hb::read(1, {-1, 0}, 1)),                                                                           // s0 y[n−1]
        load(v, hb::read(2, {0, -1})), load(v, hb::read(0, {-1, 0})), map(v, op::Mul, hb::local(1), hb::local(2)),   // s1 – s3
        load(v, hb::read(3, {0, -1})), map(v, op::Mul, hb::local(4), hb::local(0)), map(v, op::Sub, hb::local(3), hb::local(5)), // s4 – s6
        load(v, hb::read(bank, {0, -1}, 1)), load(v, hb::read(4, {0, -1})), map(v, op::Mul, hb::local(8), hb::local(7)),  // s7 – s9
        map(v, op::Add, hb::local(6), hb::local(9), hb::read(bank, {0, -1})),                                       // s10 v
        typed(hb::fold(ReduceOp::Sum, hb::local(10), Access{}), v),                                             // s11 Σv
        load(v, hb::read(0, {-1, 0})), map(v, op::Mul, number(kDirect), hb::local(12)),                             // s12, s13
        map(v, op::Add, hb::local(11), hb::local(13), hb::read(1, {-1, 0})),                                        // s14 y
    };
    for (const Stmt& st : stmts) {
        p.stmts.push_back(st);
    }
    Parts s{.lanes = 1, .after = 12, .fold = 11, .count = 15, .states = 2};
    s.kept = {Kept{10, 7, true, true}, Kept{14, 0, false, false}};
    sample_tree(p, v, s);
    return p;
}

// own lag: u = x − g·s[n−1]; v = u + a1·v[n−1]; s = Σv, kept by its Finish; y = d·s. Buffers: s 5, v 6. (A masked
// tail's missing lanes hold u, which its Finish must leave out.)
constexpr Program own_lag(Voices v) {
    Program p{};
    the_ports(p, v, false);
    const std::int32_t sum = buffer(p, v, Shape{Dim{0}}, 1, true);
    const std::int32_t bank = buffer(p, v, Shape{v.voices, Dim{0}}, 1, true);
    const std::initializer_list<Stmt> stmts = {
        load(v, hb::read(sum, {-1, -1}, 1)), map(v, op::Mul, number(kGain), hb::local(0)), load(v, hb::read(0, {-1, 0})), // s0 – s2
        map(v, op::Sub, hb::local(2), hb::local(1)),                                                                 // s3 u
        load(v, hb::read(bank, {0, -1}, 1)), load(v, hb::read(4, {0, -1})), map(v, op::Mul, hb::local(5), hb::local(4)), // s4 – s6
        map(v, op::Add, hb::local(3), hb::local(6), hb::read(bank, {0, -1})),                                       // s7 v
        typed(hb::fold(ReduceOp::Sum, hb::local(7), hb::read(sum, {-1, -1})), v),                                   // s8 s = Σv
        map(v, op::Mul, number(kDirect), hb::local(8), hb::read(1, {-1, 0})),                                       // s9 y
    };
    for (const Stmt& st : stmts) {
        p.stmts.push_back(st);
    }
    Parts s{.lanes = 4, .after = 9, .fold = 8, .count = 10, .states = 2};
    s.kept = {Kept{7, 4, true, true}, Kept{8, 0, false, true}};
    sample_tree(p, v, s);
    return p;
}

// TDF2: u = x − g·y[n−1]; o = c0·u + w[n−1]; y = Σo, kept by its Finish; w = c1·u − a1·o, after the fold. Buffer: w 5.
constexpr Program tdf2(Voices v) {
    Program p{};
    the_ports(p, v, false);
    const std::int32_t w = buffer(p, v, Shape{v.voices, Dim{0}}, 1, true);
    const std::initializer_list<Stmt> stmts = {
        load(v, hb::read(1, {-1, 0}, 1)), map(v, op::Mul, number(kGain), hb::local(0)), load(v, hb::read(0, {-1, 0})), // s0 – s2
        map(v, op::Sub, hb::local(2), hb::local(1)),                                                                 // s3 u
        load(v, hb::read(2, {0, -1})), map(v, op::Mul, hb::local(4), hb::local(3)),                                 // s4, s5
        load(v, hb::read(w, {0, -1}, 1)), map(v, op::Add, hb::local(5), hb::local(6)),                              // s6, s7 o
        typed(hb::fold(ReduceOp::Sum, hb::local(7), hb::read(1, {-1, 0})), v),                                 // s8 y = Σo
        load(v, hb::read(3, {0, -1})), map(v, op::Mul, hb::local(9), hb::local(3)),                                 // s9, s10
        load(v, hb::read(4, {0, -1})), map(v, op::Mul, hb::local(11), hb::local(7)),                                // s11, s12
        map(v, op::Sub, hb::local(10), hb::local(12), hb::read(w, {0, -1})),                                        // s13 w
    };
    for (const Stmt& st : stmts) {
        p.stmts.push_back(st);
    }
    Parts s{.lanes = 4, .after = 14, .fold = 8, .count = 14, .states = 2};
    s.kept = {Kept{8, 0, false, false}, Kept{13, 6, true, true}};
    sample_tree(p, v, s);
    return p;
}

constexpr Program bank(Voices v) {
    return v.bank == Bank::Plain ? plain(v) : v.bank == Bank::Direct ? direct(v) : v.bank == Bank::OwnLag ? own_lag(v) : tdf2(v);
}

} // namespace fold_in_feedback
