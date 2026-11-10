#pragma once

// The small hand-built trees the loop-tree IR tests share (`loops_ir_test`, `loops_walks_test`): a map in lanes, a
// fold with lanes across its outputs and a prefetch, an order statistic, state handed on in registers, a fold
// chunked along its own axis, a band, sibling folds with lanes along their own axis, a fold inside a feedback stage's
// sample (`fold_in_feedback.hpp`), and a few trees one rule breaks — and how a test holds `well_formed` to refusing a
// tree edited to break one rule, by name.

#include <cstdio>
#include <functional>
#include <memory>
#include <string_view>

#include "heinzel/loops/program.hpp"

#include "fold_in_feedback.hpp"
#include "hand_built_tree.hpp"
#include "well_formed.hpp"

namespace small_trees {

namespace loops = heinzel::loops;
namespace op = heinzel::op;
using namespace heinzel::loops; // (not `heinzel`: its graph `Node` would be ambiguous with the tree's)
using heinzel::Dim;
using heinzel::ElemType;
using heinzel::Extent;
using heinzel::OpCode;
using heinzel::ReduceOp;
using heinzel::Shape;
using namespace hand_built_tree;

// A: y = x * x over a run-time length, in lanes of 8 with the rest one at a time.
constexpr Program map_in_lanes() {
    Program p{};
    p.buffers.push_back(feed(Shape{Dim{0}}));
    p.buffers.push_back(made(Shape{Dim{0}}, 2));
    p.stmts.push_back(load(read(0, {0})));
    p.stmts.push_back(map(op::Mul, local(0), local(0), read(1, {0})));
    const std::int32_t lanes = add(p, loop(0, 8, Tail::Ones, 0), stage(p, {Dim{0}}, 2));
    const std::int32_t vector = add(p, walker(NodeKind::Vector, 0, 8), lanes);
    add(p, leaf(NodeKind::Compute, 0), vector);
    add(p, leaf(NodeKind::Compute, 1), vector);
    p.outputs.push_back(1);
    return p;
}

// B: y[c] = sum over r of x[c, r] — lanes across the 64 outputs in blocks of two lanes (a masked rest), the
// accumulators in registers, the next block's result lines asked for.
constexpr Program fold_across_outputs() {
    Program p{};
    p.buffers.push_back(feed(Shape{64, 32}));
    p.buffers.push_back(made(Shape{64}, 2));
    p.stmts.push_back(load(read(0, {0, 1})));
    p.stmts.push_back(fold(ReduceOp::Sum, local(0), read(1, {0, -1})));
    const std::int32_t block = add(p, loop(0, 16, Tail::Masked, 0), stage(p, {64, 32}, 2));
    const std::int32_t regs = add(p, registers(Role::Accumulator, 1, 8), block);
    add(p, Node{.kind = NodeKind::Prefetch, .link = block}, regs);
    add(p, leaf(NodeKind::Init, 1), add(p, walker(NodeKind::Vector, 0, 8), add(p, walker(NodeKind::Unroll, 0, 8), regs)));
    const std::int32_t update = add(p, walker(NodeKind::Vector, 0, 8), add(p, walker(NodeKind::Unroll, 0, 8), add(p, loop(1, 1), regs)));
    add(p, leaf(NodeKind::Compute, 0), update);
    add(p, leaf(NodeKind::Accumulate, 1), update);
    add(p, leaf(NodeKind::Finish, 1), add(p, walker(NodeKind::Vector, 0, 8), add(p, walker(NodeKind::Unroll, 0, 8), regs)));
    p.outputs.push_back(1);
    p.line_bytes = 64;
    return p;
}

// C: the median of three taps, per output: the taps gathered turn by turn, then ranked.
constexpr Program median_of_three() {
    Program p{};
    p.buffers.push_back(feed(Shape{Dim{0}}));
    p.buffers.push_back(made(Shape{Dim{0}}, 2));
    p.stmts.push_back(load(read(0, {0, -1})));
    p.stmts.push_back(loops::Stmt{.kind = StmtKind::Select, .node = 4, .type = ElemType::F32, .computed_in = ElemType::F32,
                                  .in = {local(0)}, .store = read(1, {0, -1}), .gathered = 3, .rank = 1});
    const std::int32_t regs = add(p, registers(Role::Gathered, 1, -1), add(p, loop(0, 1), stage(p, {Dim{0}, 3}, 2)));
    const std::int32_t turns = add(p, walker(NodeKind::Turns, 1, 1), regs);
    add(p, leaf(NodeKind::Compute, 0), turns);
    add(p, leaf(NodeKind::Collect, 1), turns);
    add(p, leaf(NodeKind::Rank, 1), regs);
    p.outputs.push_back(1);
    return p;
}

// D: four one-pole voices, y[v, t] = x[v, t] + y[v, t - 1], the state handed on in a register.
constexpr Program state_in_registers() {
    Program p{};
    p.buffers.push_back(feed(Shape{4, Dim{0}}));
    p.buffers.push_back(made(Shape{4, Dim{0}}, 2));
    p.buffers[1].prefix = 1;
    p.stmts.push_back(load(read(1, {0, 1}, 1)));
    p.stmts.push_back(map(op::Add, local(0), read(0, {0, 1}), read(1, {0, 1})));
    const std::int32_t regs = add(p, registers(Role::State, 1, -1), add(p, loop(0, 1), stage(p, {4, Dim{0}}, 2)));
    add(p, Node{.kind = NodeKind::Open, .stmt = 0, .keeps = 1}, regs);
    const std::int32_t time = add(p, loop(1, 1), regs);
    add(p, Node{.kind = NodeKind::Compute, .stmt = 0, .reads = 1}, time);
    add(p, Node{.kind = NodeKind::Compute, .stmt = 1, .keeps = 1}, time);
    p.outputs.push_back(1);
    return p;
}

// D with its state kept in its registers (`inside`: its Close in the time loop), the result stored apart by a third
// statement: what s1 keeps is read by its register alone, so its buffer holds its past alone, walked along no time.
enum class ClosedAt { After, Inside, Before }; // where the Close stands against the loop walking time

constexpr Program state_closed_at(ClosedAt at) {
    Program p{};
    p.buffers.push_back(feed(Shape{4, Dim{0}}));
    p.buffers.push_back(made(Shape{4, Dim{0}}, 2));
    p.buffers.push_back(made(Shape{4, Dim{0}}, 3));
    p.buffers[1].prefix = 1;
    p.buffers[1].only_past = true;
    p.stmts.push_back(load(read(1, {0, -1}, 1)));
    p.stmts.push_back(map(op::Add, local(0), read(0, {0, 1}), read(1, {0, -1})));
    p.stmts.push_back(load(local(1), read(2, {0, 1})));
    const std::int32_t regs = add(p, registers(Role::State, 1, -1), add(p, loop(0, 1), stage(p, {4, Dim{0}}, 3)));
    add(p, Node{.kind = NodeKind::Open, .stmt = 0, .keeps = 1}, regs);
    if (at == ClosedAt::Before) {
        add(p, Node{.kind = NodeKind::Close, .stmt = 0, .keeps = 1}, regs);
    }
    const std::int32_t time = add(p, loop(1, 1), regs);
    add(p, Node{.kind = NodeKind::Compute, .stmt = 0, .reads = 1}, time);
    add(p, Node{.kind = NodeKind::Compute, .stmt = 1, .keeps = 1, .closes = true}, time);
    add(p, Node{.kind = NodeKind::Compute, .stmt = 2}, time);
    if (at != ClosedAt::Before) {
        add(p, Node{.kind = NodeKind::Close, .stmt = 0, .keeps = 1}, at == ClosedAt::Inside ? time : regs);
    }
    p.outputs.push_back(2);
    return p;
}
constexpr Program state_closed() { return state_closed_at(ClosedAt::After); }
constexpr Program state_closed_inside() { return state_closed_at(ClosedAt::Inside); }
constexpr Program state_closed_before() { return state_closed_at(ClosedAt::Before); }

// E: a fold chunked along its own axis: between chunks its partial sums wait in its result.
constexpr Program chunked_fold() {
    Program p{};
    p.buffers.push_back(feed(Shape{8, 256}));
    p.buffers.push_back(made(Shape{8}, 2));
    p.stmts.push_back(load(read(0, {0, 1})));
    p.stmts.push_back(fold(ReduceOp::Sum, local(0), read(1, {0, -1})));
    Node chunks = loop(1, 64);
    chunks.chunks_fold = true;
    const std::int32_t regs = add(p, registers(Role::Accumulator, 1, -1), add(p, chunks, add(p, loop(0, 1), stage(p, {8, 256}, 2))));
    add(p, leaf(NodeKind::Init, 1), regs);
    const std::int32_t inner = add(p, loop(1, 1), regs);
    add(p, leaf(NodeKind::Compute, 0), inner);
    add(p, leaf(NodeKind::Accumulate, 1), inner);
    add(p, leaf(NodeKind::Finish, 1), add(p, Node{.kind = NodeKind::WhenLast}, regs));
    add(p, leaf(NodeKind::Partial, 1), add(p, Node{.kind = NodeKind::WhenNotLast}, regs));
    p.outputs.push_back(1);
    return p;
}

// F: two maps under one band: the first writes a band buffer of 4 rows the second reads.
constexpr Program band_of_two() {
    Program p{};
    p.buffers.push_back(feed(Shape{16, Dim{0}}));
    p.buffers.push_back(loops::Buffer{.type = ElemType::F32, .shape = Shape{16, 4}, .value = 1, .band = 0, .kept_axis = 1, .maker = 0, .reader = 1});
    p.buffers.push_back(made(Shape{16, Dim{0}}, 2));
    for (std::int32_t k = 0; k < 2; ++k) {
        p.stmts.push_back(load(read(k, {0, 1}), read(k + 1, {0, 1})));
        const std::int32_t rows = add(p, loop(1, 1), stage(p, {16, Dim{0}}, 1));
        add(p, leaf(NodeKind::Compute, 0), add(p, loop(0, 1), rows));
        p.stages[static_cast<std::size_t>(k)].band_axis = 1;
    }
    p.bands.push_back(loops::BandInfo{.first = 0, .count = 2, .step = 4});
    p.outputs.push_back(2);
    return p;
}

// G: a sum and a maximum of one operand, lanes along the axis they fold — blocks of two lanes, then whole
// lanes, then one at a time — both opened and finished outside the loop, with no chain.
constexpr Program sibling_folds_in_lanes() {
    Program p{};
    p.buffers.push_back(feed(Shape{64, Dim{0}}));
    p.buffers.push_back(made(Shape{1, Dim{0}}, 2));
    p.buffers.push_back(made(Shape{1, Dim{0}}, 3));
    p.stmts.push_back(load(read(0, {1, 0})));
    p.stmts.push_back(fold(ReduceOp::Sum, local(0), read(1, {1, -1})));
    p.stmts.push_back(fold(ReduceOp::Max, local(0), read(2, {1, -1})));
    const std::int32_t outer = add(p, loop(0, 1), stage(p, {Dim{0}, 64}, 3));
    const std::int32_t regs = add(p, registers(Role::Accumulator, 2, 7), add(p, registers(Role::Accumulator, 1, 7), outer));
    add(p, leaf(NodeKind::Init, 1), regs);
    add(p, leaf(NodeKind::Init, 2), regs);
    Node lanes = loop(1, 16, Tail::LanesThenOnes, 0);
    lanes.rest = 8;
    const std::int32_t update = add(p, walker(NodeKind::Vector, 1, 8), add(p, walker(NodeKind::Unroll, 1, 8), add(p, lanes, regs)));
    add(p, leaf(NodeKind::Compute, 0), update);
    add(p, leaf(NodeKind::Accumulate, 1), update);
    add(p, leaf(NodeKind::Accumulate, 2), update);
    add(p, leaf(NodeKind::Finish, 1), regs);
    add(p, leaf(NodeKind::Finish, 2), regs);
    p.outputs.push_back(1);
    p.outputs.push_back(2);
    return p;
}

// H: two strips of lanes in one stage, over two axes.
constexpr Program lanes_over_two_axes() {
    Program p{};
    p.buffers.push_back(feed(Shape{8, 8}));
    p.stmts.push_back(load(read(0, {0, 1})));
    p.stmts.push_back(load(read(0, {0, 1})));
    const std::int32_t root = stage(p, {8, 8}, 2);
    add(p, leaf(NodeKind::Compute, 0), add(p, walker(NodeKind::Vector, 0, 8), add(p, loop(0, 8, Tail::Ones, 0), add(p, loop(1, 1), root))));
    add(p, leaf(NodeKind::Compute, 1), add(p, walker(NodeKind::Vector, 1, 4), add(p, loop(1, 4, Tail::Ones, 1), add(p, loop(0, 1), root))));
    return p;
}

// B's sum with its Registers inside a chain: a strip of two columns, an Unroll of one, a set per body.
constexpr Program registers_in_a_chain() {
    Program p{};
    p.buffers.push_back(feed(Shape{8, 3}));
    p.buffers.push_back(made(Shape{8, 1}, 1));
    p.stmts.push_back(load(read(0, {0, 1})));
    p.stmts.push_back(fold(ReduceOp::Sum, local(0), read(1, {0, -1})));
    const std::int32_t rows = add(p, walker(NodeKind::Unroll, 0, 1), add(p, loop(0, 2, Tail::Ones, 0), stage(p, {8, 3}, 2)));
    const std::int32_t regs = add(p, registers(Role::Accumulator, 1, -1), rows);
    add(p, leaf(NodeKind::Init, 1), regs);
    const std::int32_t k = add(p, loop(1, 1), regs);
    add(p, leaf(NodeKind::Compute, 0), k);
    add(p, leaf(NodeKind::Accumulate, 1), k);
    add(p, leaf(NodeKind::Finish, 1), regs);
    p.outputs.push_back(1);
    return p;
}

// D with the lag's Open writing a State of its own statement instead of the one its Compute reads.
constexpr Program state_opened_wrongly() {
    Program p = state_in_registers();
    p.nodes.clear();
    add(p, Node{.kind = NodeKind::Stage}, -1);
    const std::int32_t regs = add(p, registers(Role::State, 0, -1), add(p, registers(Role::State, 1, -1), add(p, loop(0, 1), 0)));
    add(p, Node{.kind = NodeKind::Open, .stmt = 0, .keeps = 0}, regs);
    const std::int32_t time = add(p, loop(1, 1), regs);
    add(p, Node{.kind = NodeKind::Compute, .stmt = 0, .reads = 1}, time);
    add(p, Node{.kind = NodeKind::Compute, .stmt = 1, .keeps = 1}, time);
    return p;
}

// D's state read after the statement keeping it: z[v, t] = y[v, t - 1], y = x, the reader second, so that it would take
// this sample's y.
constexpr Program state_read_after_its_keeper() {
    Program p{};
    p.buffers.push_back(feed(Shape{4, Dim{0}}));
    p.buffers.push_back(made(Shape{4, Dim{0}}, 1));
    p.buffers.push_back(made(Shape{4, Dim{0}}, 2));
    p.buffers[1].prefix = 1;
    p.stmts.push_back(load(read(0, {0, 1}), read(1, {0, 1})));
    p.stmts.push_back(load(read(1, {0, 1}, 1), read(2, {0, 1})));
    const std::int32_t regs = add(p, registers(Role::State, 0, -1), add(p, loop(0, 1), stage(p, {4, Dim{0}}, 2)));
    add(p, Node{.kind = NodeKind::Open, .stmt = 1, .keeps = 0}, regs);
    const std::int32_t time = add(p, loop(1, 1), regs);
    add(p, Node{.kind = NodeKind::Compute, .stmt = 0, .keeps = 0}, time);
    add(p, Node{.kind = NodeKind::Compute, .stmt = 1, .reads = 0}, time);
    p.outputs.push_back(2);
    return p;
}

// A with a Clip loop over the lane axis between the strip and its Vector.
constexpr Program map_with_a_loop_between() {
    Program p = map_in_lanes();
    p.nodes.clear();
    add(p, Node{.kind = NodeKind::Stage}, -1);
    const std::int32_t vector = add(p, walker(NodeKind::Vector, 0, 8), add(p, loop(0, 8), add(p, loop(0, 8, Tail::Ones, 0), 0)));
    add(p, leaf(NodeKind::Compute, 0), vector);
    add(p, leaf(NodeKind::Compute, 1), vector);
    return p;
}

// I: a fold inside a feedback stage's sample (Plan/23 10), the plain bank of eight double voices in two lane bodies; and
// the own-lag bank over three, a masked tail.
constexpr Program fold_in_a_sample() { return fold_in_feedback::plain(fold_in_feedback::Voices{}); }
constexpr Program own_lag_of_three() {
    return fold_in_feedback::own_lag(fold_in_feedback::Voices{.bank = fold_in_feedback::Bank::OwnLag, .voices = 3, .lanes = 4, .bodies = 1});
}

// y[t] = Σ_v x[v, t] + y[t − 1], its fold's lanes along its own axis in a strip inside its Registers, which stand right
// below the loop handing y on: what the runner could run, but not the one shape of a fold in a sample.
constexpr Program fold_with_its_strip_in_a_sample() {
    Program p{};
    p.buffers.push_back(feed(Shape{4, Dim{0}}));
    p.buffers.push_back(made(Shape{Dim{0}}, 1));
    p.buffers[1].prefix = 1;
    p.stmts.push_back(load(read(1, {-1, 0}, 1)));
    p.stmts.push_back(load(read(0, {0, 1})));
    p.stmts.push_back(fold(ReduceOp::Sum, local(1), Access{}));
    p.stmts.push_back(map(op::Add, local(2), local(0), read(1, {-1, 0})));
    const std::int32_t state = add(p, registers(Role::State, 3, -1), stage(p, {4, Dim{0}}, 4));
    add(p, Node{.kind = NodeKind::Open, .stmt = 0, .keeps = 3}, state);
    const std::int32_t time = add(p, loop(1, 1), state);
    add(p, Node{.kind = NodeKind::Compute, .stmt = 0, .reads = 3}, time);
    const std::int32_t regs = add(p, registers(Role::Accumulator, 2, 8), time);
    add(p, leaf(NodeKind::Init, 2), regs);
    const std::int32_t lanes = add(p, walker(NodeKind::Vector, 0, 4), add(p, loop(0, 4, Tail::Ones, 0), regs));
    add(p, leaf(NodeKind::Compute, 1), lanes);
    add(p, leaf(NodeKind::Accumulate, 2), lanes);
    add(p, leaf(NodeKind::Finish, 2), regs);
    add(p, Node{.kind = NodeKind::Compute, .stmt = 3, .keeps = 3}, regs);
    p.outputs.push_back(1);
    return p;
}

// A tree built, then edited to break one rule: `well_formed` must refuse it with exactly `why`.
struct Refused {
    const char* what;
    Program (*build)();
    std::function<void(Program&)> change;
    std::string_view why;
};

inline std::unique_ptr<Program> made_by(Program (*build)()) { return std::unique_ptr<Program>(new Program(build())); }

// Did `well_formed` refuse it for that reason? Says what it did otherwise.
inline bool refused_as_named(const Refused& c) {
    const std::unique_ptr<Program> p = made_by(c.build);
    c.change(*p);
    const auto r = loops::well_formed(*p);
    if (!r && r.error() == c.why) {
        return true;
    }
    const std::string_view got = r ? std::string_view{"accepted"} : r.error().view();
    std::printf("%s: want \"%.*s\", got \"%.*s\"\n", c.what, static_cast<int>(c.why.size()), c.why.data(), static_cast<int>(got.size()), got.data());
    return false;
}

} // namespace small_trees
