// What a loop tree's walks keep (`tests/support/well_formed_walks.hpp`, `Plan/archive/13-loop-tree/02` §4.2): trees
// the runner would run wrong or past an end — lanes not side by side, a relaid buffer's lanes across its blocks,
// a point of the domain not made, a masked strip at width one, a loop re-walking its axis, a past its buffer does
// not keep, a split two workers would share, a band buffer made outside its band, bands that overlap or hold no
// stage, a kept stage that could leave its results stale or watch the wrong Params (`well_formed_kept.hpp`), a skewed
// stage whose sections would run on the wrong samples (`well_formed_skew.hpp`) — each refused by name, and the trees
// the rules must keep accepting. Built with `tests/support/small_trees.hpp`; nothing but `loops/` and its allowlist.

#include <bit>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

#include "heinzel/loops/print.hpp"
#include "heinzel/loops/program.hpp"

#include "small_trees.hpp"
#include "well_formed.hpp"

using namespace small_trees;

namespace {

int g_wrong = 0;

// A delay line: y[t] = a[t - p[t]], a = x stored with six samples of silence before it (a reach of five, and the
// far tap), p a second feed.
constexpr Program delay_line() {
    Program p{};
    p.buffers.push_back(feed(Shape{Dim{0}}));
    Buffer position = feed(Shape{Dim{0}});
    position.feed = 1;
    p.buffers.push_back(position);
    Buffer a = made(Shape{Dim{0}}, 2);
    a.prefix = 6;
    p.buffers.push_back(a);
    p.buffers.push_back(made(Shape{Dim{0}}, 3));
    Access source = read(2, {0});
    source.indexed_axis = 0;
    p.stmts.push_back(load(read(0, {0}), read(2, {0})));
    p.stmts.push_back(load(read(1, {0})));
    p.stmts.push_back(loops::Stmt{.kind = StmtKind::Gather, .node = 4, .type = ElemType::F32, .computed_in = ElemType::F32, .precision = kResolved,
                                  .in = {source, local(1)}, .store = read(3, {0}), .gather = Gather::Back, .reach = 5});
    const std::int32_t time = add(p, loop(0, 1), stage(p, {Dim{0}}, 3));
    for (std::int32_t s = 0; s < 3; ++s) {
        add(p, leaf(NodeKind::Compute, s), time);
    }
    p.outputs.push_back(3);
    return p;
}

// y[t] = x[t] + x[t - 1] over chunks of four samples, split along time at its time loop: its first sample a head
// sample, run against the silence before the feed.
constexpr Program lag_of_a_feed() {
    Program p{};
    p.buffers.push_back(feed(Shape{Dim{0}}));
    p.buffers.push_back(made(Shape{Dim{0}}, 1));
    p.stmts.push_back(load(read(0, {0}, 1)));
    p.stmts.push_back(map(op::Add, local(0), read(0, {0}), read(1, {0})));
    const std::int32_t time = add(p, loop(0, 1), add(p, loop(0, 4), stage(p, {Dim{0}}, 2)));
    add(p, leaf(NodeKind::Compute, 0), time);
    add(p, leaf(NodeKind::Compute, 1), time);
    p.stages[0].head = 1, p.stages[0].time_loop = time, p.stages[0].split = time;
    p.outputs.push_back(1);
    return p;
}

// A sum over eight numbers of which the tree walks none: every sample would fold its one point.
constexpr Program sum_over_nothing() {
    Program p{};
    p.buffers.push_back(feed(Shape{Dim{0}}));
    p.buffers.push_back(made(Shape{Dim{0}}, 1));
    p.stmts.push_back(load(read(0, {-1, 0})));
    p.stmts.push_back(fold(ReduceOp::Sum, local(0), read(1, {-1, 0})));
    const std::int32_t regs = add(p, registers(Role::Accumulator, 1, -1), add(p, loop(1, 1), stage(p, {8, Dim{0}}, 2)));
    add(p, leaf(NodeKind::Init, 1), regs);
    add(p, leaf(NodeKind::Compute, 0), regs);
    add(p, leaf(NodeKind::Accumulate, 1), regs);
    add(p, leaf(NodeKind::Finish, 1), regs);
    p.outputs.push_back(1);
    return p;
}

// A masked strip over the lanes whose chunk one leaf runs in lanes, another a sample at a time: past the ragged end
// the second would run all the same.
constexpr Program half_in_lanes() {
    Program p{};
    p.buffers.push_back(feed(Shape{Dim{0}}));
    p.buffers.push_back(made(Shape{Dim{0}}, 1));
    p.stmts.push_back(load(read(0, {0})));
    p.stmts.push_back(load(read(0, {0}), read(1, {0})));
    const std::int32_t strip = add(p, loop(0, 8, Tail::Masked, 0), stage(p, {Dim{0}}, 2));
    add(p, leaf(NodeKind::Compute, 0), add(p, walker(NodeKind::Vector, 0, 8), strip));
    add(p, leaf(NodeKind::Compute, 1), add(p, walker(NodeKind::Unroll, 0, 1), strip));
    p.outputs.push_back(1);
    return p;
}

// A copy into a buffer relaid in blocks of 16, walked a block at a time, each block in two masked strips of 8.
constexpr Program relaid_in_whole_blocks() {
    Program p{};
    p.buffers.push_back(feed(Shape{Dim{0}}));
    Buffer r = made(Shape{Dim{0}}, 1);
    r.blocked_axis = 0, r.block = 16;
    p.buffers.push_back(r);
    p.stmts.push_back(load(read(0, {0}), read(1, {0})));
    const std::int32_t strip = add(p, loop(0, 8, Tail::Masked, 0), add(p, loop(0, 16), stage(p, {Dim{0}}, 1)));
    add(p, leaf(NodeKind::Compute, 0), add(p, walker(NodeKind::Vector, 0, 8), strip));
    p.outputs.push_back(1);
    return p;
}

// Two kept stages of no axes, then a map over time: k = p * q from Params p and q, c = k + 1 (the 1 a constant's one
// number), y[t] = x[t] * c. The second watches what the first does: its maker's Params.
constexpr Program kept_knobs() {
    Program p{};
    p.buffers.push_back(feed(Shape{}));
    Buffer q = feed(Shape{});
    q.feed = 1;
    p.buffers.push_back(q);
    Buffer x = feed(Shape{Dim{0}});
    x.feed = 2;
    p.buffers.push_back(x);
    p.buffers.push_back(made(Shape{}, 3));
    p.buffers.push_back(made(Shape{}, 4));
    p.buffers.push_back(made(Shape{Dim{0}}, 5));
    p.buffers.push_back(Buffer{.type = ElemType::F32, .shape = Shape{}, .home = Home::Constant, .value = 6, .first = 0});
    p.constants.push_back(std::bit_cast<std::uint64_t>(1.0));
    p.stmts.push_back(load(read(0, {})));
    p.stmts.push_back(load(read(1, {})));
    p.stmts.push_back(map(op::Mul, local(0), local(1), read(3, {})));
    const std::int32_t first = stage(p, {}, 3);
    for (std::int32_t s = 0; s < 3; ++s) {
        add(p, leaf(NodeKind::Compute, s), first);
    }
    p.stmts.push_back(load(read(3, {})));
    p.stmts.push_back(load(read(6, {})));
    p.stmts.push_back(map(op::Add, local(0), local(1), read(4, {})));
    const std::int32_t second = stage(p, {}, 3);
    for (std::int32_t s = 0; s < 3; ++s) {
        add(p, leaf(NodeKind::Compute, s), second);
    }
    p.stmts.push_back(load(read(2, {0})));
    p.stmts.push_back(load(read(4, {-1})));
    p.stmts.push_back(map(op::Mul, local(0), local(1), read(5, {0})));
    const std::int32_t time = add(p, loop(0, 1), stage(p, {Dim{0}}, 3));
    for (std::int32_t s = 0; s < 3; ++s) {
        add(p, leaf(NodeKind::Compute, s), time);
    }
    for (std::size_t s = 0; s < 2; ++s) {
        p.stages[s].kept = true;
        p.stages[s].watched[0] = 0b11;
    }
    p.outputs.push_back(5);
    return p;
}

// k sections linked in lanes, skewed, each a one-pole y = c·y[n − 1] + u, u = chain(x, V): section j
// takes section j − 1's V at this sample, section 0 x, read at lane 0 (the fill); the last section's V is stored (the
// slice, at lane k − 1). V is y, or (`own`) a value of its own, v = y + u, which the link alone reads (TPT's, opened by
// none). `closes` and `inside` break a rule. Buffers: x, y (its past alone), the constants c, the output, v.
struct Sections {
    std::int32_t k{3};
    std::int32_t lanes{4};
    std::int32_t bodies{1};
    bool own{false};
    bool closes{true};  // y kept in its registers
    bool inside{false}; // v's State Registers inside the time loop
};

constexpr void linked_sections(Program& p, const Sections& s) {
    p.buffers.push_back(feed(Shape{Dim{0}}));
    Buffer y = made(Shape{s.k, Dim{0}}, 1);
    y.prefix = 1;
    y.only_past = s.closes;
    p.buffers.push_back(y);
    for (std::int32_t j = 0; j < s.k; ++j) {
        p.constants.push_back(std::bit_cast<std::uint64_t>(0.5 - 0.0625 * j));
    }
    p.buffers.push_back(Buffer{.type = ElemType::F32, .shape = Shape{s.k, 1}, .home = Home::Constant, .value = 2, .first = 0});
    p.buffers.push_back(made(Shape{Dim{0}}, 3));
    if (s.own) {
        Buffer v = made(Shape{s.k, Dim{0}}, 4);
        v.only_past = true;
        p.buffers.push_back(v);
    }
    Access below = read(s.own ? 4 : 1, {0, -1});
    below.offset[0] = -1;
    p.stmts.push_back(load(read(0, {-1, 0})));                             // s0 the fill
    p.stmts.push_back(load(read(1, {0, -1}, 1)));                          // s1 y[n − 1]
    p.stmts.push_back(map(op::Chain, local(0), below));                    // s2 u, the link
    p.stmts.push_back(load(read(2, {0, -1})));                             // s3 c
    p.stmts.push_back(map(op::Mul, local(3), local(1)));                   // s4
    p.stmts.push_back(map(op::Add, local(4), local(2), read(1, {0, -1}))); // s5 y
    if (s.own) {
        p.stmts.push_back(map(op::Add, local(5), local(2), read(4, {0, -1}))); // s6 v
    }
    p.stmts.push_back(load(local(s.own ? 6 : 5), read(3, {-1, 0}))); // the slice
}

// The lanes' chain under `parent`: an Unroll by the lanes where there are bodies, then the Vector.
constexpr std::int32_t lanes_of(Program& p, const Sections& s, std::int32_t parent) {
    const std::int32_t bodies = s.bodies > 1 ? add(p, walker(NodeKind::Unroll, 0, s.lanes), parent) : parent;
    return add(p, walker(NodeKind::Vector, 0, s.lanes), bodies);
}

// The voice layout (`loops a0 vector 4 x2 | a1`): the sections' strip, the States and y's Open, the time loop and a
// step in it, y's Close.
constexpr Program sections_in_lanes(Sections s) {
    Program p{};
    linked_sections(p, s);
    const std::int32_t linked = s.own ? 6 : 5; // V's statement
    const std::int32_t width = s.lanes * s.bodies;
    const std::int32_t root = stage(p, {s.k, Dim{0}}, linked + 2);
    const std::int32_t strip = add(p, loop(0, width, width > s.k ? Tail::Masked : Tail::Ones, 0), root);
    const std::int32_t state = add(p, registers(Role::State, 5, -1), strip);
    const std::int32_t outer = s.own && !s.inside ? add(p, registers(Role::State, 6, -1), state) : state;
    add(p, Node{.kind = NodeKind::Open, .stmt = 1, .keeps = 5}, lanes_of(p, s, outer));
    const std::int32_t time = add(p, loop(1, 1), outer);
    const std::int32_t inner = s.own && s.inside ? add(p, registers(Role::State, 6, -1), time) : time;
    const auto head = static_cast<std::int32_t>(p.nodes.size());
    const std::int32_t step = lanes_of(p, s, inner);
    add(p, Node{.kind = NodeKind::Compute, .stmt = 0, .lane = 0}, step);
    add(p, Node{.kind = NodeKind::Compute, .stmt = 1, .reads = 5}, step);
    add(p, Node{.kind = NodeKind::Compute, .stmt = 2, .reads = linked, .shifted = true}, step);
    add(p, leaf(NodeKind::Compute, 3), step);
    add(p, leaf(NodeKind::Compute, 4), step);
    add(p, Node{.kind = NodeKind::Compute, .stmt = 5, .keeps = 5, .closes = s.closes}, step);
    if (s.own) {
        add(p, Node{.kind = NodeKind::Compute, .stmt = 6, .keeps = 6, .closes = true}, step);
    }
    add(p, Node{.kind = NodeKind::Compute, .stmt = linked + 1, .lane = s.k - 1}, step);
    if (s.closes) {
        add(p, Node{.kind = NodeKind::Close, .stmt = 1, .keeps = 5}, lanes_of(p, s, outer));
    }
    for (Node& n : p.nodes) {
        n.link = n.kind == NodeKind::Registers ? head : n.link;
    }
    p.stages[0].time_loop = time;
    p.stages[0].skewed = true;
    p.outputs.push_back(3);
    return p;
}
constexpr Program three_sections() { return sections_in_lanes({}); }
constexpr Program eight_sections() { return sections_in_lanes({.k = 8, .bodies = 2}); }
constexpr Program a_value_of_its_own() { return sections_in_lanes({.k = 2, .lanes = 2, .own = true}); }
constexpr Program eight_in_one_body() { return sections_in_lanes({.k = 8}); }
constexpr Program not_closed() { return sections_in_lanes({.closes = false}); }
constexpr Program restarted_each_step() { return sections_in_lanes({.k = 2, .lanes = 2, .own = true, .inside = true}); }

// y = c·x over three lanes, skewed, no link: x read, scaled by the last lane's c and stored at the last lane, so
// y[n] = 0.75 x[n]. `TimeOutside`: the time loop outside the strip; `OneAtATime`: the lanes walked one at a time, no
// Vector.
enum class Gain : std::uint8_t { Lanes, TimeOutside, OneAtATime };

constexpr Program skewed_gain(Gain g) {
    Program p{};
    p.buffers.push_back(feed(Shape{Dim{0}}));
    for (std::int32_t j = 0; j < 3; ++j) {
        p.constants.push_back(std::bit_cast<std::uint64_t>(0.25 * (j + 1)));
    }
    p.buffers.push_back(Buffer{.type = ElemType::F32, .shape = Shape{3, 1}, .home = Home::Constant, .value = 1, .first = 0});
    p.buffers.push_back(made(Shape{Dim{0}}, 2));
    p.stmts.push_back(load(read(0, {-1, 0})));
    p.stmts.push_back(load(read(1, {0, -1})));
    p.stmts.push_back(map(op::Mul, local(1), local(0)));
    p.stmts.push_back(load(local(2), read(2, {-1, 0})));
    const std::int32_t root = stage(p, {3, Dim{0}}, 4);
    const std::int32_t outer = g == Gain::Lanes ? add(p, loop(0, 4, Tail::Masked, 0), root) : g == Gain::OneAtATime ? add(p, loop(0, 1), root) : root;
    const std::int32_t time = add(p, loop(1, 1), outer);
    const std::int32_t strip = g == Gain::TimeOutside ? add(p, loop(0, 4, Tail::Masked, 0), time) : time;
    const std::int32_t step = g == Gain::OneAtATime ? time : add(p, walker(NodeKind::Vector, 0, 4), strip);
    add(p, Node{.kind = NodeKind::Compute, .stmt = 0, .lane = 2}, step);
    add(p, leaf(NodeKind::Compute, 1), step);
    add(p, Node{.kind = NodeKind::Compute, .stmt = 2, .lane = 2}, step);
    add(p, Node{.kind = NodeKind::Compute, .stmt = 3, .lane = 2}, step);
    p.stages[0].time_loop = time;
    p.stages[0].skewed = true;
    p.outputs.push_back(2);
    return p;
}
constexpr Program gain_in_lanes() { return skewed_gain(Gain::Lanes); }
constexpr Program gain_time_outside() { return skewed_gain(Gain::TimeOutside); }
constexpr Program gain_one_at_a_time() { return skewed_gain(Gain::OneAtATime); }

// The first node of `kind` that names statement `stmt` (of the first stage).
Node& node_of(Program& p, NodeKind kind, std::int32_t stmt) {
    std::size_t n = 0;
    while (p.nodes[n].kind != kind || p.nodes[n].stmt != stmt) {
        ++n;
    }
    return p.nodes[n];
}

// own lag (`fold_in_feedback.hpp`) skewed, its store along time at lane 0, x's Load reading a number instead: its fold
// sums the sections every sample, though each lane holds another sample.
void own_lag_skewed(Program& p) {
    for (std::size_t n = 0; n < p.nodes.size(); ++n) {
        p.stages[0].time_loop = p.nodes[n].kind == NodeKind::Loop && p.nodes[n].axis == 1 ? static_cast<std::int32_t>(n) : p.stages[0].time_loop;
    }
    p.stages[0].skewed = true;
    node_of(p, NodeKind::Compute, 9).lane = 0;
    p.stmts[2].in[0] = loops::Access{.source = Source::Immediate, .immediate = std::bit_cast<std::uint64_t>(1.0)};
}

// ...its fold folding x: a fold reading along time at every lane's own sample.
void folding_x_skewed(Program& p) {
    own_lag_skewed(p);
    p.stmts[8].in[0] = read(0, {-1, 0});
}

static_assert(loops::well_formed(kept_knobs()).has_value(), "two kept stages, the second reading the first's value and a constant, are well formed");
static_assert(loops::well_formed(delay_line()).has_value() && loops::well_formed(relaid_in_whole_blocks()).has_value() && loops::well_formed(lag_of_a_feed()).has_value(),
              "a delay line with its silence, a relaid buffer walked in whole blocks, and a feed's past split along its time loop, are well formed");
static_assert([] {
    Program p = band_of_two();
    p.bands[0].parallel = true;
    return loops::well_formed(p).has_value();
}(), "and so is a split band");
static_assert(loops::well_formed(three_sections()).has_value() && loops::well_formed(eight_sections()).has_value() &&
                  loops::well_formed(a_value_of_its_own()).has_value() && loops::well_formed(gain_in_lanes()).has_value(),
              "sections linked in lanes, skewed — three with a padding lane, eight in two bodies, two linked by a value only "
              "the link reads — and a skewed gain with no link, are well formed");

// Bands, lanes, relaid blocks, every point made, masked strips, loops within loops.
const Refused kWalks[] = {
    {"a band of no rows", band_of_two, [](Program& p) { p.bands[0].step = 0; }, "heinzel::loops: a band walks chunks of one row or more"},
    {"a band of no stages", band_of_two, [](Program& p) { p.bands[0].count = 0; }, "heinzel::loops: a band holds one stage or more"},
    {"two bands sharing a stage", band_of_two, [](Program& p) { p.bands.push_back(loops::BandInfo{.first = 1, .count = 1, .step = 4}); },
     "heinzel::loops: bands are in the stages' order, and no two share a stage"},
    {"a band listed twice", band_of_two, [](Program& p) { p.bands.push_back(loops::BandInfo{p.bands[0]}); },
     "heinzel::loops: bands are in the stages' order, and no two share a stage"},
    {"a band past the last stage", band_of_two, [](Program& p) { p.bands[0].count = 3; }, "heinzel::loops: a band's stages are stages of the program"},
    {"a band stage with no band axis", band_of_two, [](Program& p) { p.stages[1].band_axis = -1; },
     "heinzel::loops: a band walks chunks of rows along a band axis of each of its stages, which make and read its buffers"},
    {"lanes loading every other element", map_in_lanes, [](Program& p) { p.stmts[0].in[0].step[0] = 2; },
     "heinzel::loops: an access walking the lane axis steps one element at a time, side by side"},
    {"lanes along a buffer axis whose elements are not side by side", fold_across_outputs,
     [](Program& p) { p.stmts[0].in[0].buffer_axis[0] = 1, p.stmts[0].in[0].buffer_axis[1] = 0; },
     "heinzel::loops: an access walking the lane axis steps one element at a time, side by side"},
    {"a leaf in lanes storing where the lanes stand still", map_in_lanes, [](Program& p) { p.stmts[1].store.buffer_axis[0] = -1; },
     "heinzel::loops: a leaf in lanes stores along them"},
    {"a band buffer made outside the band", band_of_two, [](Program& p) { p.buffers[1].maker = -1; },
     "heinzel::loops: a band walks chunks of rows along a band axis of each of its stages, which make and read its buffers"},
    {"head samples in a band", band_of_two, [](Program& p) { p.stages[0].head = 1, p.stages[0].time_loop = 1; },
     "heinzel::loops: a stage in a band runs no head samples"},
    {"a chunk of lanes run once, at width one", map_in_lanes, [](Program& p) { p.nodes[2].kind = NodeKind::Unroll; },
     "heinzel::loops: every point is made: a leaf walks each axis of its accesses one at a time or in lanes"},
    {"a masked strip's chunk run at width one", half_in_lanes, [](Program&) {},
     "heinzel::loops: below a Masked strip a leaf runs its axis in lanes"},
    {"a relaid buffer's lanes crossing its blocks", map_in_lanes, [](Program& p) { p.buffers[0].blocked_axis = 0, p.buffers[0].block = 16; },
     "heinzel::loops: a relaid buffer's lanes are walked by whole blocks, or within one"},
    {"a relaid buffer's blocked axis walked off the lanes", state_in_registers, [](Program& p) { p.buffers[0].blocked_axis = 0, p.buffers[0].block = 4; },
     "heinzel::loops: a relaid buffer's blocked axis is walked along the lane axis alone"},
    {"a feed's past with no time loop", map_in_lanes, [](Program& p) { p.stmts[0].in[0].back = 1; },
     "heinzel::loops: a feed's past is read by a lag along the stage's time loop"},
    {"a lag deeper than its buffer's silence", state_in_registers, [](Program& p) { p.buffers[1].prefix = 0; },
     "heinzel::loops: a lag or a delay line reads no further back than its buffer keeps, a feed's by head samples"},
    {"a split inside the state it hands on", state_in_registers, [](Program& p) { p.stages[0].split = 4; },
     "heinzel::loops: a split writes nothing two workers share"},
    {"a split along the axis a fold collapses", fold_across_outputs, [](Program& p) { p.stages[0].split = 7; },
     "heinzel::loops: a split writes nothing two workers share"},
    {"lanes a relaid buffer's whole blocks walk with a rest one at a time", map_in_lanes, [](Program& p) { p.buffers[0].blocked_axis = 0, p.buffers[0].block = 8; },
     "heinzel::loops: a relaid buffer's lanes are walked by whole blocks, or within one"},
    {"a relaid buffer of no blocks", map_in_lanes, [](Program& p) { p.buffers[0].blocked_axis = 0, p.buffers[0].block = 0; },
     "heinzel::loops: a relaid buffer is cut into blocks of one or more"},
    {"a relaid buffer's lanes split within a block", relaid_in_whole_blocks, [](Program& p) { p.stages[0].split = 2; },
     "heinzel::loops: a split, a band or head samples cut a relaid buffer's lanes at whole blocks"},
    {"a loop re-walking the axis of the strip it stands in", map_in_lanes, [](Program& p) { p.nodes[2].kind = NodeKind::Loop, p.nodes[2].step = 1; },
     "heinzel::loops: a loop inside another over its axis walks the chunk of a Clip loop of more than one"},
    {"a masked strip over an axis off the lanes", fold_across_outputs, [](Program& p) { p.nodes[7].tail = Tail::Masked, p.nodes[7].strip = 1; },
     "heinzel::loops: a Masked strip walks the lane axis, and a fold's own lanes only where the fold restarts every sample"},
    {"a masked strip over a fold's own lanes", sibling_folds_in_lanes, [](Program& p) { p.nodes[6].tail = Tail::Masked, p.nodes[6].rest = 1; },
     "heinzel::loops: a Masked strip walks the lane axis, and a fold's own lanes only where the fold restarts every sample"},
    {"an axis nothing walks, folded", sum_over_nothing, [](Program&) {},
     "heinzel::loops: every point is made: a leaf walks each axis of its accesses one at a time or in lanes"},
    {"a band buffer kept along an axis it does not have", band_of_two, [](Program& p) { p.buffers[1].kept_axis = 7; },
     "heinzel::loops: a band walks chunks of rows along a band axis of each of its stages, which make and read its buffers"},
    {"a split band's buffer keeping silence", band_of_two, [](Program& p) { p.bands[0].parallel = true, p.buffers[1].prefix = 1; },
     "heinzel::loops: a split writes nothing two workers share"},
    {"a stage running ahead of a split band writing outside it", band_of_two, [](Program& p) { p.bands[0].parallel = true, p.stages[1].lead = 1; },
     "heinzel::loops: a split writes nothing two workers share"},
};

// The past an access reaches, and splits.
const Refused kPastAndSplits[] = {
    {"a delay line reaching past its buffer's silence", delay_line, [](Program& p) { p.buffers[2].prefix = 5; },
     "heinzel::loops: a lag or a delay line reads no further back than its buffer keeps, a feed's by head samples"},
    {"a delay line over a feed", delay_line, [](Program& p) { p.stmts[2].in[0].buffer = 0; },
     "heinzel::loops: a feed's past is read by a lag along the stage's time loop"},
    {"a feed's past deeper than the head samples", lag_of_a_feed, [](Program& p) { p.stmts[0].in[0].back = 2; },
     "heinzel::loops: a lag or a delay line reads no further back than its buffer keeps, a feed's by head samples"},
    {"a store into a feed", map_in_lanes, [](Program& p) { p.stmts[1].store.buffer = 0; },
     "heinzel::loops: a store writes the workspace, no further back than its buffer keeps"},
    {"a store deeper than its buffer's silence", map_in_lanes, [](Program& p) { p.stmts[1].store.back = 1; },
     "heinzel::loops: a store writes the workspace, no further back than its buffer keeps"},
    {"an access before its buffer", map_in_lanes, [](Program& p) { p.stmts[0].in[0].offset[0] = -1; },
     "heinzel::loops: an access starts inside its buffer"},
    {"a delay line of the stage's own output, split along time", delay_line, [](Program& p) { p.stages[0].split = 1; },
     "heinzel::loops: a split writes nothing two workers share"},
    {"a split along time off the time loop", lag_of_a_feed, [](Program& p) { p.stages[0].split = 1; },
     "heinzel::loops: a split writes nothing two workers share"},
};

// Kept stages (`well_formed_kept.hpp`).
const Refused kKept[] = {
    {"a kept stage with axes", kept_knobs, [](Program& p) { p.stages[2].kept = true, p.stages[2].watched[0] = 0b11; },
     "heinzel::loops: a kept stage has no axes and stands in no band"},
    {"a kept stage in a band", band_of_two, [](Program& p) { p.stages[0].kept = true; }, "heinzel::loops: a kept stage has no axes and stands in no band"},
    {"a kept stage reading a signal", kept_knobs, [](Program& p) { p.stmts[1].in[0].buffer = 2; },
     "heinzel::loops: a kept stage reads only constants, Params, and what earlier stages that could be kept store"},
    {"a kept stage reading what a stage that reads a signal stores", kept_knobs,
     [](Program& p) { p.stmts[1].in[0].buffer = 2, p.stages[0].kept = false, p.stages[0].watched[0] = 0; },
     "heinzel::loops: a kept stage reads only constants, Params, and what earlier stages that could be kept store"},
    {"a kept stage reading what a later stage stores", kept_knobs, [](Program& p) { p.stmts[1].in[0].buffer = 4; },
     "heinzel::loops: a kept stage reads only constants, Params, and what earlier stages that could be kept store"},
    {"a kept stage storing an output", kept_knobs, [](Program& p) { p.outputs.push_back(4); },
     "heinzel::loops: a kept stage stores only workspace buffers that are no output and no other stage stores"},
    {"a kept stage storing what another stage stores too", kept_knobs, [](Program& p) { p.stmts[2].store.buffer = 4; },
     "heinzel::loops: a kept stage stores only workspace buffers that are no output and no other stage stores"},
    {"a kept stage not watching its maker's Param", kept_knobs, [](Program& p) { p.stages[1].watched[0] = 0b01; },
     "heinzel::loops: a kept stage watches exactly the Params it and its makers read, a stage not kept none"},
    {"a kept stage watching a Param nothing reads", kept_knobs, [](Program& p) { p.stages[0].watched[0] = 0b111; },
     "heinzel::loops: a kept stage watches exactly the Params it and its makers read, a stage not kept none"},
    {"a stage not kept watching a Param", kept_knobs, [](Program& p) { p.stages[2].watched[0] = 0b01; },
     "heinzel::loops: a kept stage watches exactly the Params it and its makers read, a stage not kept none"},
};

// Skewed stages (`well_formed_skew.hpp`).
const Refused kSkewed[] = {
    {"a skewed stage running head samples", three_sections, [](Program& p) { p.stages[0].head = 1; },
     "heinzel::loops: a skewed stage runs no head samples, stands in no band and is never split"},
    {"a skewed stage in a band", three_sections,
     [](Program& p) {
         p.bands.push_back(loops::BandInfo{.count = 1, .step = 4});
         p.stages[0].band_axis = 1;
     },
     "heinzel::loops: a skewed stage runs no head samples, stands in no band and is never split"},
    {"a skewed stage split", three_sections, [](Program& p) { p.stages[0].split = 1; },
     "heinzel::loops: a skewed stage runs no head samples, stands in no band and is never split"},
    {"a skewed stage's time loop a strip", three_sections,
     [](Program& p) {
         Node& time = p.nodes[static_cast<std::size_t>(p.stages[0].time_loop)];
         time.tail = Tail::Ones;
         time.strip = 1;
     },
     "heinzel::loops: a skewed stage's time loop is serial, of step 1, innermost, under every State"},
    {"a skewed stage's time loop outside its strip", gain_time_outside, [](Program&) {},
     "heinzel::loops: a skewed stage's time loop is serial, of step 1, innermost, under every State"},
    {"a skewed stage's State restarted every step", restarted_each_step, [](Program&) {},
     "heinzel::loops: a skewed stage's time loop is serial, of step 1, innermost, under every State"},
    {"a skewed stage's lanes one at a time", gain_one_at_a_time, [](Program&) {},
     "heinzel::loops: a skewed stage's lanes are a Vector over a literal k, in one chunk of its strip"},
    {"a skewed stage's lanes over a symbol", three_sections, [](Program& p) { p.stages[0].axes[0] = Dim{1}; },
     "heinzel::loops: a skewed stage's lanes are a Vector over a literal k, in one chunk of its strip"},
    {"a skewed stage's lanes in two chunks", eight_in_one_body, [](Program&) {},
     "heinzel::loops: a skewed stage's lanes are a Vector over a literal k, in one chunk of its strip"},
    {"a skewed stage's State not closed", not_closed, [](Program&) {}, "heinzel::loops: every State of a skewed stage closes"},
    {"a lane in a stage not skewed", map_in_lanes, [](Program& p) { p.nodes[3].lane = 0; }, "heinzel::loops: shifted and lane stand only in a skewed stage"},
    {"linked sections in a stage not skewed", three_sections, [](Program& p) { p.stages[0].skewed = false; },
     "heinzel::loops: shifted and lane stand only in a skewed stage"},
    {"shifted on a Compute reading no State", three_sections, [](Program& p) { node_of(p, NodeKind::Compute, 4).shifted = true; },
     "heinzel::loops: shifted: a Compute reading slot 0 before its keeper, operand 1 one element back"},
    {"shifted on an Open", three_sections, [](Program& p) { node_of(p, NodeKind::Open, 1).shifted = true; },
     "heinzel::loops: shifted: a Compute reading slot 0 before its keeper, operand 1 one element back"},
    {"shifted reading its State after the keeper", three_sections,
     [](Program& p) {
         Node& slice = node_of(p, NodeKind::Compute, 6);
         slice.reads = 5;
         slice.shifted = true;
     },
     "heinzel::loops: shifted: a Compute reading slot 0 before its keeper, operand 1 one element back"},
    {"shifted reading where the keeper stores", three_sections, [](Program& p) { p.stmts[2].in[1].offset[0] = 0; },
     "heinzel::loops: shifted: a Compute reading slot 0 before its keeper, operand 1 one element back"},
    {"a lane past the sections", three_sections, [](Program& p) { node_of(p, NodeKind::Compute, 6).lane = 3; },
     "heinzel::loops: a lane stands on a Compute, within [0, k)"},
    {"a lane on an Open", three_sections, [](Program& p) { node_of(p, NodeKind::Open, 1).lane = 0; }, "heinzel::loops: a lane stands on a Compute, within [0, k)"},
    {"the fill read at no lane", three_sections, [](Program& p) { node_of(p, NodeKind::Compute, 0).lane = -1; },
     "heinzel::loops: a Compute of a skewed stage that reads or stores along time stands at a lane"},
    {"a fold reading along time in a skewed stage", own_lag_of_three, folding_x_skewed,
     "heinzel::loops: no other leaf of a skewed stage reads or stores a buffer along time"},
    {"a fold over the sections in a skewed stage", own_lag_of_three, own_lag_skewed,
     "heinzel::loops: no fold of a skewed stage runs along its lanes, whose sections stand on different samples"},
    {"a value made at one lane read at every lane", gain_in_lanes, [](Program& p) { node_of(p, NodeKind::Compute, 2).lane = -1; },
     "heinzel::loops: a value made at one lane is read at that lane, or as the link's fill from lane 0"},
    {"a value made at one lane read at another", gain_in_lanes, [](Program& p) { node_of(p, NodeKind::Compute, 0).lane = 0; },
     "heinzel::loops: a value made at one lane is read at that lane, or as the link's fill from lane 0"},
    {"the link's fill made at a lane but 0", three_sections, [](Program& p) { node_of(p, NodeKind::Compute, 0).lane = 1; },
     "heinzel::loops: a value made at one lane is read at that lane, or as the link's fill from lane 0"},
    {"a keeper at the slice's lane", three_sections, [](Program& p) { node_of(p, NodeKind::Compute, 5).lane = 2; },
     "heinzel::loops: a Compute at a lane keeps no State and reads none, shifted or not"},
    {"a reader of state at a lane", three_sections, [](Program& p) { node_of(p, NodeKind::Compute, 1).lane = 0; },
     "heinzel::loops: a Compute at a lane keeps no State and reads none, shifted or not"},
    {"the link at a lane", three_sections, [](Program& p) { node_of(p, NodeKind::Compute, 2).lane = 0; },
     "heinzel::loops: a Compute at a lane keeps no State and reads none, shifted or not"},
    {"the slice stored along the lanes", three_sections,
     [](Program& p) {
         p.buffers[3].shape = Shape{3, Dim{0}};
         p.stmts[6].store = read(3, {0, 1});
     },
     "heinzel::loops: a Compute at a lane stores its lane's number, not along the lanes"},
    {"a timeless store at no lane", three_sections,
     [](Program& p) {
         p.buffers.push_back(made(Shape{3, 1}, 4));
         p.stmts[4].store = read(4, {0, -1});
     },
     "heinzel::loops: a leaf of a skewed stage at no lane stores only a State it keeps and closes"},
};

// The listing names what a skewed stage holds: its stage line ends `skewed`, the link reads shifted, the fill and the
// slice stand at a lane.
bool printed_skewed() {
    const std::string got = loops::print(*made_by(eight_sections));
    bool all = true;
    for (const std::string_view want : {"time n6 skewed\n", "compute s2 reads s5 shifted\n", "compute s0 lane 0\n", "compute s6 lane 7\n"}) {
        all = all && got.find(want) != std::string::npos;
    }
    if (!all) {
        std::printf("a skewed program printed as:\n%s\n", got.c_str());
    }
    return all;
}

} // namespace

int main() {
    for (const Refused& c : kWalks) {
        g_wrong += refused_as_named(c) ? 0 : 1;
    }
    for (const Refused& c : kPastAndSplits) {
        g_wrong += refused_as_named(c) ? 0 : 1;
    }
    for (const Refused& c : kKept) {
        g_wrong += refused_as_named(c) ? 0 : 1;
    }
    for (const Refused& c : kSkewed) {
        g_wrong += refused_as_named(c) ? 0 : 1;
    }
    g_wrong += printed_skewed() ? 0 : 1;
    if (g_wrong != 0) {
        std::printf("%d checks failed\n", g_wrong);
        return 1;
    }
    return 0;
}
