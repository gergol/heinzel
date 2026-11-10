// Views on the loop-nest path, against the reference walk, bit for bit (a view moves no number).
//
// A transpose, a slice, a reversal and new extents over the same elements are an ADDRESS: affine in the
// reader's axes, composed once per program (`loops/graph/views.hpp`). One that something other than a view
// reads is a statement — a Load at that address, in a stage over its own shape, so whatever reads it
// there reads a register; one that only views read is nothing at all, and they read its source. A view
// reads STORAGE, so the stage that makes its source closes before it; and lanes run only where they
// would still walk memory.
//
//   turned       transpose(x) * 2                     one stage; the lane axis would stride, so no lanes
//   part         slice(x, axis 0, from 1, 2 of them, every 2nd) + 1      an offset and a step
//   backwards    flip(x, axis 1) * 2                  the far end first, stepping back
//   flat         reshape(x, {4*6, N}) * 2             one axis walks two, in memory order: lanes
//   folded up    reshape(u, {4, 6, N}) * 2            two axes walk one
//   a chain      transpose(slice(x)) as a RESULT      the slice is nothing; the transpose a Load and a store
//   of a map     flip(x * 2) + 1                      same shape, two stages: a view reads storage
//   into a fold  sum over axis 0 of transpose(x)      the view inside the fold's loops
//   product      einsum of transpose(x) with w        a contraction reads the view's stored copy
//   a window     sum of unfold(slice(x)) * h          the window reads through the slice
//   converted    F32 -> F64 -> * 0.5 -> F32, realized conversions and a barrier are Loads of a register
//   a ramp       x * iota({6, 1})                     a generator is storage filled once, and no statement
//   padded       pad(x, axis 1, 2 before, 3 after, of 0.5) * 2   a value made IN PARTS: a margin, the
//                                                     operand, a margin — a copy stage each, stored into
//                                                     its stretch of the result
//   nothing      pad(x, axis 1, nothing before or after) * 2, x of no elements along it: every part is empty, so
//                no stage copies one; in lanes, and with the doubling split among workers, it runs and writes nothing
//   joined       concat(x, w, axis 1) + 1             two parts, one per operand
//   packed       concat(x, flip(x), axis 0)           two equal planes, as a RESULT, its second part a stored view
//   seen through sum of unfold(slice(x * 2, from row 1)) * h, and of unfold(flip(x * 2)): the doubling is
//                computed INSIDE the window's reader, at the address the window AND the view under it give
//   two windows  sum over both taps of unfold2d(x) * k   a window over two axes at once: a 2 x 3 kernel
//   after a fold slice(slice(x) * 2, first row) between x + 1 and sum(x + 1, axis 0, kept), and max(x + 1, that
//                sum): a view of a longer value read in the fold's stage after the fold, where its kept axis is one
//                row — it stands where the fold's first point does (review of loop-tree M3)
//
// Refused by name: new extents over a sliding window.

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/notation/einsum.hpp"
#include "heinzel/sema/analyze.hpp"

#include "canaried.hpp"
#include "interpret.hpp"
#include "planned.hpp"
#include "split_runs.hpp"
#include "tree_facts.hpp"

#include <bit>
#include <cstdio>
#include <memory>
#include <string_view>
#include <vector>

using namespace heinzel;

namespace {

namespace loops = heinzel::loops;

constexpr std::int32_t kA = 4;
constexpr std::int32_t kB = 6;
constexpr std::int32_t kBlock = 19; // nineteen samples: two lanes of eight and three more

constexpr Graph analysed(Graph g) {
    if (!infer(g).ok) {
        fail("nest_views_test: a graph did not resolve");
    }
    return g;
}

constexpr Expr flip(Expr x, std::int32_t axis) {
    OpAttr attr{};
    attr.kind = AttrKind::View;
    attr.view.kind = ViewKind::Flip;
    attr.view.axis = axis;
    return detail::append_unary(x, op::Flip, attr);
}

constexpr Expr cast(Expr x, ElemType to) {
    OpAttr attr{};
    attr.kind = AttrKind::Cast;
    attr.cast.to = to;
    return detail::append_unary(x, op::Cast, attr);
}

constexpr Expr padded(Expr x, std::int32_t axis, std::int32_t before, std::int32_t after, double fill) {
    OpAttr attr{};
    attr.kind = AttrKind::View;
    attr.view.kind = ViewKind::Pad;
    attr.view.axis = axis;
    attr.view.pad_before = before;
    attr.view.pad_after = after;
    attr.view.pad_bits = std::bit_cast<std::uint64_t>(fill);
    return detail::append_unary(x, op::Pad, attr);
}

constexpr Expr joined(Expr a, Expr b, OpCode code, ViewKind kind, std::int32_t axis) {
    Arena* arena = bring_together(a, b);
    BuildNode n{};
    n.op = code;
    n.inputs[0] = a.position();
    n.inputs[1] = b.position();
    n.attr.kind = AttrKind::View;
    n.attr.view.kind = kind;
    n.attr.view.axis = axis;
    arena->nodes.push_back(n);
    return Expr{arena, static_cast<std::int32_t>(arena->nodes.size() - 1)};
}

enum class Case { Turned, Part, Backwards, Flat, FoldedUp, Chain, OfAMap, IntoAFold, Product, Window, Converted, Ramp, Padded, Joined, Packed, TwoWindows, ThroughASlice, ThroughAFlip, AfterAFold };

// A map over a slice, the map a fold then reads, a slice of the first map, the fold, keeping its axes: in this order
// the slice is offered to the fold's stage once the fold is in it, and joins it after the fold (its shape is the
// fold's result).
constexpr Graph after_a_fold(Expr x) {
    Expr s = x.slice(0, 1, 2);
    Expr v = x + 1.0;
    Expr m = s * 2.0;
    Expr z = m.slice(0, 0, 1);
    Expr f = v.sum(0, keepdims);
    return analysed(finish(Output{"z"} = z, Output{"y"} = max(v, f)));
}

constexpr Graph graph_of(Case which) {
    Input x{"x", Signal{kA, kB, kBlock}};
    Input u{"u", Signal{kA * kB, kBlock}};
    Input w{"w", Signal{kA, 3, kBlock}};
    Input h{"h", Signal{3, kBlock}};
    Input k{"k", Signal{2, 1, 3, kBlock}};
    switch (which) {
    case Case::Turned: return analysed(finish(Output{"y"} = x.transpose({1, 0}) * 2.0));
    case Case::Part: return analysed(finish(Output{"y"} = x.slice(0, 1, 2, 2) + 1.0));
    case Case::Backwards: return analysed(finish(Output{"y"} = flip(x, 1) * 2.0));
    case Case::Flat: return analysed(finish(Output{"y"} = x.reshape({kA * kB}) * 2.0));
    case Case::FoldedUp: return analysed(finish(Output{"y"} = u.reshape({kA, kB}) * 2.0));
    case Case::Chain: return analysed(finish(Output{"y"} = x.slice(1, 2, 3).transpose({1, 0})));
    case Case::OfAMap: return analysed(finish(Output{"y"} = flip(x * 2.0, 0) + 1.0));
    case Case::IntoAFold: return analysed(finish(Output{"y"} = x.transpose({1, 0}).sum(0)));
    case Case::Product: return analysed(finish(Output{"y"} = einsum("ba,ak->bk", x.transpose({1, 0}), w)));
    case Case::Window: return analysed(finish(Output{"y"} = (x.slice(0, 1, 3).unfold(1, 3) * h).sum(2)));
    case Case::Converted: return analysed(finish(Output{"y"} = cast(cast(x, ElemType::F64) * 0.5, ElemType::F32).realize()));
    case Case::Ramp: return analysed(finish(Output{"y"} = x * iota({kB})));
    case Case::Padded: return analysed(finish(Output{"y"} = padded(x, 1, 2, 3, 0.5) * 2.0));
    case Case::Joined: return analysed(finish(Output{"y"} = joined(x, w, op::Concat, ViewKind::Concat, 1) + 1.0));
    case Case::Packed: return analysed(finish(Output{"y"} = joined(x, flip(x, 1), op::Concat, ViewKind::Concat, 0)));
    case Case::ThroughASlice: return analysed(finish(Output{"y"} = ((x * 2.0).slice(0, 1, 3).unfold(1, 3) * h).sum(2)));
    case Case::ThroughAFlip: return analysed(finish(Output{"y"} = (flip(x * 2.0, 1).unfold(1, 3) * h).sum(2)));
    case Case::TwoWindows: return analysed(finish(Output{"y"} = (x.unfold2d(0, 2, 1, 3) * k).sum({1, 3})));
    case Case::AfterAFold: return after_a_fold(x);
    }
    fail("nest_views_test: no such case");
}

template <Case Which>
constexpr planned::Held kNestHeld{planned::program_of(graph_of(Which), zen2)};
template <Case Which>
constexpr const loops::Program& kNest = kNestHeld<Which>.program;

// Statement `k` of stage `s` runs once per point of the outer loops, after the fold's own loops close: the nearest
// walking node above it stands outside the fold's Registers node.
constexpr bool after_the_fold_loops(const loops::Program& p, std::size_t s, std::int32_t k) {
    const auto walking = [](const loops::Node& a) {
        return a.kind == loops::NodeKind::Loop || a.kind == loops::NodeKind::Unroll || a.kind == loops::NodeKind::Vector;
    };
    const auto registers = [](const loops::Node& a) { return a.kind == loops::NodeKind::Registers && a.role == loops::Role::Accumulator; };
    bool after = false;
    for (std::int32_t n = p.stages[s].root + 1; n < tree_facts::subtree_end(p, s); ++n) {
        const loops::Node& node = loops::node_at(p, n);
        if (node.kind == loops::NodeKind::Compute && node.stmt == k) {
            const std::int32_t w = loops::ancestor(p, n, walking);
            after = w < 0 || loops::ancestor(p, w, registers) < 0;
        }
    }
    return after;
}

static_assert(kNest<Case::Turned>.stages.size() == 1 && tree_facts::stmt(kNest<Case::Turned>, 0, 0).kind == loops::StmtKind::Load &&
                  tree_facts::stmt(kNest<Case::Turned>, 0, 0).in[0].buffer_axis[0] == 1 && tree_facts::stmt(kNest<Case::Turned>, 0, 0).in[0].buffer_axis[1] == 0 &&
                  tree_facts::serial(tree_facts::walk(kNest<Case::Turned>, 0, 2)),
              "one stage: the transpose is a Load with its axes exchanged, and no lane strides a row per step");
static_assert(tree_facts::stmt(kNest<Case::Part>, 0, 0).in[0].offset[0] == 1 && tree_facts::stmt(kNest<Case::Part>, 0, 0).in[0].step[0] == 2,
              "a slice is where it starts and how far it steps");
static_assert(tree_facts::stmt(kNest<Case::Backwards>, 0, 0).in[0].offset[1] == kB - 1 && tree_facts::stmt(kNest<Case::Backwards>, 0, 0).in[0].step[1] == -1,
              "a reversal starts at the far end and steps back");
// (the walk over time, then a strip over the flat axis with a Vector inside it)
static_assert(tree_facts::stmt(kNest<Case::Flat>, 0, 0).in[0].buffer_axis[0] == 1 && loops::is_strip(tree_facts::walk(kNest<Case::Flat>, 0, 1)) &&
                  tree_facts::walk(kNest<Case::Flat>, 0, 2).kind == loops::NodeKind::Vector &&
                  tree_facts::walk(kNest<Case::Flat>, 0, 2).axis == tree_facts::walk(kNest<Case::Flat>, 0, 1).axis,
              "the flat axis walks the source's innermost element axis on past its end, in memory order — so in lanes");
static_assert(tree_facts::stmt(kNest<Case::FoldedUp>, 0, 0).in[0].buffer_axis[0] == 0 && tree_facts::stmt(kNest<Case::FoldedUp>, 0, 0).in[0].step[0] == kB &&
                  tree_facts::stmt(kNest<Case::FoldedUp>, 0, 0).in[0].buffer_axis[1] == 0,
              "two axes walk one: a row is six elements of it");
static_assert(kNest<Case::Chain>.stages.size() == 1 && kNest<Case::Chain>.stages[0].count == 1 && kNest<Case::Chain>.buffers.size() == 2,
              "the slice is no statement and no buffer; the transpose reads the input through it and is stored as the result");
static_assert(kNest<Case::OfAMap>.stages.size() == 2, "a view reads storage, so the stage that makes what it reads closes first");
static_assert(kNest<Case::Ramp>.stages.size() == 1 && kNest<Case::Ramp>.buffers.size() == 3 && kNest<Case::Ramp>.buffers[1].ramp &&
                  kNest<Case::Ramp>.stages[0].count == 3,
              "the ramp is a buffer and no statement: two Loads and the product");
static_assert(kNest<Case::Padded>.stages.size() == 4 && tree_facts::stmt(kNest<Case::Padded>, 0, 0).in[0].source == loops::Source::Immediate &&
                  tree_facts::stmt(kNest<Case::Padded>, 1, 0).store.offset[1] == 2 && tree_facts::stmt(kNest<Case::Padded>, 2, 0).store.offset[1] == 2 + kB,
              "a margin of the fill, the operand two elements in, the other margin after it — and the doubling");
static_assert(kNest<Case::Joined>.stages.size() == 3 && tree_facts::stmt(kNest<Case::Joined>, 1, 0).store.offset[1] == kB, "one copy per operand");
static_assert(kNest<Case::ThroughASlice>.stages.size() == 1 && kNest<Case::ThroughASlice>.buffers.size() == 3 &&
                  kNest<Case::ThroughAFlip>.stages.size() == 1 && tree_facts::stmt(kNest<Case::ThroughASlice>, 0, 0).in[0].offset[0] == 1,
              "the doubling is no stage and no buffer: it reads the input one row in, where the slice under the window starts");
static_assert(kNest<Case::TwoWindows>.stages.size() == 1 && kNest<Case::TwoWindows>.buffers.size() == 3, "no buffer holds the windows");
static_assert(kNest<Case::IntoAFold>.stages.size() == 1 && kNest<Case::Converted>.stages.size() == 1, "fused with what reads them");
static_assert([] {
    const std::int32_t last = kNest<Case::AfterAFold>.stages[1].count - 1;
    const loops::Stmt& read = tree_facts::stmt(kNest<Case::AfterAFold>, 1, last);
    return kNest<Case::AfterAFold>.stages.size() == 3 && read.kind == loops::StmtKind::Load && after_the_fold_loops(kNest<Case::AfterAFold>, 1, last) &&
           read.in[0].buffer_axis[0] == -1;
}(), "the slice is read in the fold's stage after the fold — outside its loops, so walking no row of the fold's axis");

// A value made in parts whose every part is empty forms no stage: the doubling after it is the program's one, in lanes,
// and split among workers along its rows.
constexpr Graph kNothingPadded = analysed(finish(Output{"y"} = padded(detail::input("x", Shape{4, 0, 8}), 1, 0, 0, 0.0) * 2.0));
constexpr Machine kLanes{.vector_bytes = 32};
constexpr planned::Held kNothingInLanesHeld{planned::program_of(kNothingPadded, kLanes)};
constexpr const loops::Program& kNothingInLanes = kNothingInLanesHeld.program;
constexpr planned::Held kNothingSplitHeld{planned::program_of(kNothingPadded, split_runs::split(planned::schedule_of(kNothingPadded, kLanes), 1, 0))};
constexpr const loops::Program& kNothingSplit = kNothingSplitHeld.program;
static_assert(kNothingInLanes.stages.size() == 1 && tree_facts::count(kNothingInLanes, 0, loops::NodeKind::Vector) == 1 &&
                  kNothingSplit.stages.size() == 1 && kNothingSplit.stages[0].split >= 0,
              "no stage for the parts, none of which has an element; the doubling in lanes, and split");

int g_wrong = 0;

std::vector<float> noise(std::size_t n, std::uint32_t seed) {
    std::vector<float> v(n);
    std::uint32_t state = seed;
    for (float& x : v) {
        state = state * 1664525u + 1013904223u;
        x = static_cast<float>(static_cast<std::int32_t>(state >> 8) % 2001 - 1000) * 0.0625f;
    }
    return v;
}

// Every element of the one result, against the walk. (The feeds hold sixteenths, so every product and
// sum here is exact in float and in double alike — no order of a fold's terms can show.)
template <Case Which>
void check(const char* what) {
    static constexpr Graph g = graph_of(Which);
    const std::vector<float> x = noise(kA * kB * kBlock, 1);
    const std::vector<float> u = noise(kA * kB * kBlock, 2);
    const std::vector<float> w = noise(kA * 3 * kBlock, 3);
    const std::vector<float> h = noise(3 * kBlock, 4);
    const std::vector<float> k = noise(2 * 3 * kBlock, 5);
    // A graph holds the inputs it uses, in the order its arenas merged: found by name.
    const auto named = [&](const Name& name) -> const std::vector<float>& {
        return name.data_[0] == 'x' ? x : name.data_[0] == 'u' ? u : name.data_[0] == 'w' ? w : name.data_[0] == 'k' ? k : h;
    };
    std::vector<std::vector<double>> widened;
    Inputs feeds{};
    loops::Feeds nest_feeds{};
    for (std::size_t i = 0; i < g.inputs.size(); ++i) {
        const std::vector<float>& from = named(g.inputs[i].name);
        widened.emplace_back(from.begin(), from.end());
        nest_feeds[i] = from.data();
    }
    for (std::size_t i = 0; i < g.inputs.size(); ++i) {
        const Shape& shape = g.values[static_cast<std::size_t>(g.inputs[i].value)].shape;
        feeds.push_back(InputFeed{widened[i].data(), elements_of(shape).count});
    }
    const auto out = std::make_unique<Signals<kBlock>>();
    run(g, feeds, *out);
    canaried::Workspace<kNest<Which>> work(loops::Sizes{});
    canaried::run<kNest<Which>>(nest_feeds, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(kNest<Which>.outputs[0]));
    const std::int32_t result = g.outputs[0].value;
    const Shape& shape = g.values[static_cast<std::size_t>(result)].shape;
    const std::size_t width = static_cast<std::size_t>(elements_of(shape).count);
    const std::size_t samples = extent_at(shape, shape.rank - 1).encoded == 1 ? 1 : kBlock;
    for (std::size_t t = 0; t < samples; ++t) {
        for (std::size_t e = 0; e < width; ++e) {
            const float want = static_cast<float>(sample_at(*out, result, static_cast<std::int32_t>(e), t));
            if (!(y[t * width + e] == want) && ++g_wrong <= 10) {
                std::printf("  %s [sample %zu, element %zu]: got %a, want %a\n", what, t, e, static_cast<double>(y[t * width + e]), static_cast<double>(want));
            }
        }
    }
}

bool refuses(const Graph& g) {
    const loops::Refusal held = planned::refusal_of(g, zen2);
    const std::string_view why = held.view();
    if (!why.empty()) {
        std::printf("  (refused: \"%.*s\")\n", static_cast<int>(why.size()), why.data());
    }
    return why.starts_with("heinzel::compile: ");
}

void check_refusals() {
    Input x{"x", Signal{kA, kB, kBlock}};
    const Graph reshaped_window = analysed(finish(Output{"y"} = x.unfold(0, 2).reshape({3 * 2 * kB}) * 2.0));
    if (!refuses(reshaped_window) || loops::lowers(reshaped_window, Machine{})) {
        ++g_wrong;
        std::printf("  new extents over a sliding window must be refused by name\n");
    }
}

} // namespace

int main() {
    check<Case::Turned>("turned");
    check<Case::Part>("part");
    check<Case::Backwards>("backwards");
    check<Case::Flat>("flat");
    check<Case::FoldedUp>("folded up");
    check<Case::Chain>("a chain");
    check<Case::OfAMap>("of a map");
    check<Case::IntoAFold>("into a fold");
    check<Case::Product>("product");
    check<Case::Window>("a window");
    check<Case::Converted>("converted");
    check<Case::Ramp>("a ramp");
    check<Case::Padded>("padded");
    check<Case::Joined>("joined");
    check<Case::Packed>("packed");
    check<Case::TwoWindows>("two windows");
    check<Case::ThroughASlice>("seen through a slice");
    check<Case::ThroughAFlip>("seen through a flip");
    check<Case::AfterAFold>("a slice after a fold");
    check_refusals();
    {
        const split_runs::Fed f = split_runs::fed(kNothingInLanes, loops::Sizes{});
        canaried::Workspace<kNothingInLanes> work(loops::Sizes{});
        canaried::run<kNothingInLanes>(f.feeds, work);
    }
    g_wrong += split_runs::check<kNothingSplit>("nothing padded, split", loops::Sizes{}, false);
    if (g_wrong != 0) {
        std::printf("nest_views_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("nest_views_test: views agree with the walk, bit for bit\n");
    return 0;
}
