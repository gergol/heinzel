// When a node reads its operands — the column, and the two questions it answers.
//
// Its own group since milestone M4 added the delay line. Until `_dline` the answer was one number
// per step and lived beside the rule table; the moment a distance could be a SIGNAL, "which sample"
// stopped being a fact about a step and became a fact about an operand, and the reasoning moved to
// `ir/timing.hpp`. This is that file's test.
//
// What is worth testing here is that the answers are the TABLE's rather than each runner's, that the
// one piece of arithmetic in the file, resolving a position against a ceiling, is right at its edges —
// and, at the end, that a run reads the lags as the column says.
#include "heinzel/ir/timing.hpp"

#include <array>
#include <limits>

#include "heinzel/executor.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/sema/analyze.hpp"

#include "executed.hpp"
#include "filters.hpp"
#include "hand_built_graph.hpp"
#include "interpret.hpp"

using namespace heinzel;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

// A tap delay line with the given ceiling. Nothing here runs it; what is asked is where it reads.
constexpr Node tap(std::int32_t ceiling) {
    Node n{};
    n.op = op::DelayLine;
    n.attr.kind = AttrKind::SignalledDelay;
    n.attr.signalled_delay.max_delay = ceiling;
    n.inputs[0] = 0;
    n.inputs[1] = 1;
    return n;
}

constexpr Node fixed_tap(std::int32_t samples) {
    Node n{};
    n.op = op::FixedDelay;
    n.attr.kind = AttrKind::Delay;
    n.attr.delay.samples = samples;
    n.inputs[0] = 0;
    return n;
}

// A link, `u = _chain(x, V)`.
constexpr Node link() {
    Node n{};
    n.op = op::Chain;
    n.inputs[0] = 0;
    n.inputs[1] = 1;
    return n;
}

} // namespace

// ---- which operand comes from where, asked per operand ---------------------------------------------
// The widening milestone M4 made. A tap delay line reads its SIGNAL from the past and the POSITION
// that says how far back from the present — so a planner that asked the node rather than the operand
// would drop the second, and could then schedule the delay line before the position exists.
static_assert(operand_comes_from_the_past(tap(8), 0) && !operand_comes_from_the_past(tap(8), 1),
              "the signal is in the past; the position saying how far back is read now");
static_assert(operand_comes_from_the_past(fixed_tap(3), 0), "a constant lag reads all of its operands back");
static_assert(!operand_comes_from_the_past(fixed_tap(0), 0),
              "and a lag of zero samples reads the present, which is not reading the past");

// A link reads `V` at this sample, one element back along axis 0: section j reads what section j - 1 settled in this
// sample. A cycle closes through that operand as through a lag, so it constrains no order within the sample; `x` does.
static_assert(operand_comes_from_the_past(link(), 1) && !operand_comes_from_the_past(link(), 0),
              "the section before is settled before this one needs it; the section's own input is read now");
static_assert(samples_back(link()) == 0 && reads_the_past(link()),
              "no sample back, yet its operand 1 waits on nothing within the sample");
static_assert(loops::graph::is_a_link(link()) && !loops::graph::is_a_link(fixed_tap(1)) && !loops::graph::is_a_link(tap(8)),
              "a link is asked of the timing column");

// A signalled lag reaches into the past whatever its ceiling, INCLUDING a ceiling of zero. That is
// not pedantry: `samples_back()` answers zero for it, so a planner that had gone on asking that
// question would have given its signal a momentary place and handed the history away.
static_assert(samples_back(tap(0)) == 0 && reads_the_past(tap(0)),
              "the distance being unknown is not the distance being zero");

// ---- the position, resolved against the ceiling ------------------------------------------------------
static_assert(signalled_tap(tap(8), 0.0) == SignalledTap{0, 0.0}, "a position of zero is this sample");
static_assert(signalled_tap(tap(8), 3.0) == SignalledTap{3, 0.0},
              "a whole-sample position is one tap and no blend");
static_assert(signalled_tap(tap(8), 3.25) == SignalledTap{3, 0.25},
              "and a fractional one is the nearer tap plus how far toward the further");

// The clamp, at both ends and past them.
static_assert(signalled_tap(tap(8), 8.0) == SignalledTap{8, 0.0});
static_assert(signalled_tap(tap(8), 9.5) == SignalledTap{8, 0.0}, "clamped to the ceiling, exactly");
static_assert(signalled_tap(tap(8), -4.0) == SignalledTap{0, 0.0}, "a run cannot read forward in time");

// The three values a position can take that are not numbers on the line. Each is pinned rather than
// left to whatever a cast would do, because a position is DATA and any of them can arrive.
static_assert(signalled_tap(tap(8), kNaN) == SignalledTap{0, 0.0},
              "not a number lands at this sample rather than surviving into a cast");
static_assert(signalled_tap(tap(8), kInf) == SignalledTap{8, 0.0}, "and an infinite position clamps");
static_assert(signalled_tap(tap(8), -kInf) == SignalledTap{0, 0.0});

// A ceiling of zero pins the tap at this sample however far the position asks to reach.
static_assert(signalled_tap(tap(0), 5.0) == SignalledTap{0, 0.0});

// ---- the fetch schedule the walk reads a node's operands by --------------------------------------------
// `operands_of` is handed a reader and returns what the meaning is computed from. Here the reader
// simply reports what it was asked for, so what the assertions pin is the SCHEDULE of reads rather
// than any samples.
namespace {

// Encodes (operand, distance) as one number so a test can say which reads happened.
constexpr double asked(std::size_t k, std::int32_t back) {
    return static_cast<double>(k) * 100.0 + static_cast<double>(back);
}

constexpr auto reader = [](std::size_t k, std::int32_t back) { return asked(k, back); };

} // namespace

// An ordinary operation: three operands, all at this sample.
static_assert([] {
    Node add{};
    add.op = op::Add;
    return operands_of(add, reader) == Operands{asked(0, 0), asked(1, 0), asked(2, 0)};
}());

// A constant lag: every operand from the same earlier sample.
static_assert(operands_of(fixed_tap(3), reader) == Operands{asked(0, 3), asked(1, 3), asked(2, 3)});

// And the signalled one, which is the shape no earlier operation had: operand 1 is read FIRST and at
// this sample, and operand 0 is then read twice, at the two samples the position falls between.
static_assert([] {
    // The reader answers 2.5 for the position — operand 1 at distance 0 encodes as 100.0, so the
    // ceiling is set high enough that 100.0 is not clamped, and the taps land at 100 and 101.
    return operands_of(tap(200), reader) == Operands{asked(0, 100), asked(0, 101), 0.0};
}(), "the position decides the two taps, and the fraction goes with them");

// ---- the tap delay line, run ---------------------------------------------------------------------------
//
// Its distance arrives as a SIGNAL, so the position has to be computed BEFORE the delay line reads in
// the same sample, while the signal it taps is read from the past. What is checked here is that a run
// actually does both — the walk and the loop tree (`Executor`) — against a reference worked out by hand
// rather than against another run of the same code.
//
// The graph, on an impulse:
//
//   s = x + 1          a step: 2 at sample 0, then 1
//   p = 3.5 - 3.5*x    a position: 0 at sample 0, then 3.5 samples in the past
//   y = dline(s, p)    the tap, clamped at 8
//
// which makes the blend visible: at sample 3 the tap straddles the step's edge and answers 1, half
// way between the 2 that is three samples back and the 0 before the block began.
namespace {

constexpr std::size_t kBlock = 16;
constexpr std::int32_t kBlockSize = static_cast<std::int32_t>(kBlock);

constexpr std::int32_t literal(Graph& g, double value) {
    const auto v = add_value(g, ElemType::F64, Shape{kBlockSize}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(v)].constant = value;
    return v;
}

constexpr Graph tapped() {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{kBlockSize}, ValueCategory::Input);
    const auto one = literal(g, 1.0);
    const auto reach = literal(g, 3.5);
    const auto s = add_node(g, op::Add, x, one);
    const auto scaled = add_node(g, op::Mul, x, reach);
    const auto position = add_node(g, op::Sub, reach, scaled);
    OpAttr k{};
    k.kind = AttrKind::SignalledDelay;
    k.signalled_delay.max_delay = 8;
    const auto y = add_node(g, op::DelayLine, s, position, kNoValue, k);
    g.inputs.push_back(NamedValue{Name{"x"}, x});
    g.outputs.push_back(NamedValue{Name{"y"}, y});
    if (!infer(g).ok) {
        fail("the tap delay line graph did not resolve");
    }
    return g;
}

// Worked out by hand from the graph above, not from a run of it.
constexpr std::array<double, kBlock> by_hand() {
    std::array<double, kBlock> out{};
    out[0] = 2.0; // the position is zero, so the tap is this sample's step value
    out[1] = 0.0; // three and a half samples back is before the block, where there is silence
    out[2] = 0.0;
    out[3] = 1.0; // half way between s[0] = 2 and the silence before it
    out[4] = 1.5; // half way between s[1] = 1 and s[0] = 2
    for (std::size_t n = 5; n < kBlock; ++n) {
        out[n] = 1.0; // the step is flat by now, so the blend has nothing to blend
    }
    return out;
}

std::array<double, kBlock> tapped_on_the_loop_tree() {
    const std::array<double, kBlock> in = impulse<kBlock>();
    std::array<double, kBlock> out{};
    Executor<tapped> ex;
    testing::held(ex);
    ex.input("x") = in.data();
    ex.output("y") = out.data();
    ex.run();
    return out;
}

} // namespace

static_assert(walk<kBlock>(tapped()) == by_hand(), "the walk does not compute the tap this graph describes");

// ---- a linked graph, analysed -----------------------------------------------------------------------
//
// Three one-pole sections in lanes, their states `V` over `[3, time]`: `u = _chain(x, V)`, `V = u / 2 + V[n - 1] / 4`,
// and the last section read out. The analysis reads the link's row as any other's: `u` is `[3, time]` (operand 0, one
// section's worth, broadcast), in the wider of its operands' types, and a signal. Nothing runs it yet.
namespace {

constexpr std::int32_t kSections = 3;

struct Linked {
    Graph g{};
    std::int32_t u{kNoValue};
};

constexpr Linked linked() {
    Linked l{};
    Graph& g = l.g;
    const auto x = add_value(g, ElemType::F32, Shape{kBlockSize}, ValueCategory::Input);
    const auto states = add_value(g, ElemType::F64, Shape{kSections, kBlockSize}, ValueCategory::Variable);
    l.u = add_node(g, op::Chain, x, states);
    OpAttr previous{};
    previous.kind = AttrKind::Delay;
    previous.delay.samples = 1;
    const auto back = add_node(g, op::FixedDelay, states, kNoValue, kNoValue, previous);
    const auto fed = add_node(g, op::Mul, l.u, literal(g, 0.5));
    const auto kept = add_node(g, op::Mul, back, literal(g, 0.25));
    const auto next = add_node(g, op::Add, fed, kept);
    OpAttr defines{};
    defines.kind = AttrKind::Recurrence;
    defines.recurrence.defines = states;
    const auto settled = add_node(g, op::Recurrence, next, kNoValue, kNoValue, defines);
    OpAttr last{};
    last.kind = AttrKind::View;
    last.view.kind = ViewKind::Slice;
    last.view.axis = 0;
    last.view.offset = kSections - 1;
    last.view.length = 1;
    last.view.step = 1;
    const auto section = add_node(g, op::Slice, settled, kNoValue, kNoValue, last);
    OpAttr flatten{};
    flatten.kind = AttrKind::View;
    flatten.view.kind = ViewKind::Reshape;
    flatten.view.shape = Shape{kBlockSize};
    const auto y = add_node(g, op::Reshape, section, kNoValue, kNoValue, flatten);
    g.inputs.push_back(NamedValue{Name{"x"}, x});
    g.outputs.push_back(NamedValue{Name{"y"}, y});
    return l;
}

constexpr bool link_is_analysed() {
    Linked l = linked();
    if (!infer(l.g).ok) {
        return false;
    }
    const Value& u = l.g.values[static_cast<std::size_t>(l.u)];
    detail::TimeKinds kind{};
    detail::time_kinds(l.g, detail::declared_variables(l.g), kind);
    const bool signal = kind[static_cast<std::size_t>(l.u)] == detail::TimeKind::Signal;
    return u.shape == Shape{kSections, kBlockSize} && u.type == ElemType::F64 && signal;
}

} // namespace

static_assert(link_is_analysed(), "a link is [k, time], in its operands' wider type, and a signal");

int main() {
    // Repeated while the program runs: the same answers when the compiler is not the one working
    // them out. `signalled_tap` does arithmetic on data, so this is not a formality.
    const bool clamped = signalled_tap(tap(8), 9.5) == SignalledTap{8, 0.0} &&
                         signalled_tap(tap(8), kNaN) == SignalledTap{0, 0.0} &&
                         signalled_tap(tap(8), 3.25) == SignalledTap{3, 0.25};
    return clamped && tapped_on_the_loop_tree() == by_hand() ? 0 : 1;
}
