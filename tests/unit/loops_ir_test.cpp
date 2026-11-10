// The loop-tree program IR (`include/heinzel/loops/`): small trees built by hand — a map in lanes, a fold with
// lanes across its outputs and a prefetch, an order statistic, state handed on in registers, a fold chunked
// along its own axis, sibling folds with lanes along their own axis, a band, a fold inside a feedback stage's sample
// (Plan/23 10) — their printed form pinned, their
// equality, and `well_formed` refusing one ill-formed tree per rule of `Plan/archive/13-loop-tree/02` §4.2 that
// `well_formed.hpp` holds (the rules of what the walks keep: `loops_walks_test`); a schedule of every choice, printed.
//
// Built with `tests/support/small_trees.hpp`. Nothing but `loops/` and its allowlist is in sight (`loops_alone`).

#include "heinzel/loops/print.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/schedule.hpp"

#include "small_trees.hpp"
#include "well_formed.hpp"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

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
using namespace small_trees;

namespace {

int g_wrong = 0;

constexpr Program kMap = map_in_lanes();
static_assert(loops::well_formed(kMap).has_value(), "the runner accepts a map in lanes");
static_assert(loops::well_formed(sibling_folds_in_lanes()).has_value(), "and sibling folds in lanes along their axis");
static_assert(!loops::well_formed([] {
                   Program p = map_in_lanes();
                   p.stages[0].root = 1;
                   return p;
               }()).has_value(),
              "and refuses a stage out of place, while the compiler evaluates");
static_assert(kMap == map_in_lanes() && !(kMap == fold_across_outputs()), "programs compare as values");

void pinned(const char* what, Program (*build)(), std::string_view want) {
    const std::string got = loops::print(*made_by(build));
    if (got != want) {
        ++g_wrong;
        std::printf("%s printed differently:\n%s\n", what, got.c_str());
    }
    const auto ok = loops::well_formed(*made_by(build));
    if (!ok) {
        ++g_wrong;
        std::printf("%s is not well formed: %.*s\n", what, static_cast<int>(ok.error().size()), ok.error().data());
    }
}

// Immediates compare by their bits: a NaN fill equals itself, and -0.0 is not +0.0.
void immediates_compare_bit_for_bit() {
    const auto with = [](double x) {
        std::unique_ptr<Program> p = made_by(map_in_lanes);
        p->stmts[1].in[1] = Access{.source = Source::Immediate, .immediate = std::bit_cast<std::uint64_t>(x)};
        return p;
    };
    const double nan = std::numeric_limits<double>::quiet_NaN();
    if (!(*with(nan) == *with(nan)) || *with(-0.0) == *with(0.0) || !(*with(0.25) == *with(0.25))) {
        std::printf("immediates do not compare bit for bit\n");
        ++g_wrong;
    }
}

// The listings, checked once by eye against `02` §4.
constexpr std::string_view kMapInLanes = R"(b0 f32 [d0] feed 0 value 0
b1 f32 [d0] value 2
stage 0 [d0]
  s0 load f32 b0(0)
  s1 map f32 _mul normal node 3 s0 s0 -> b1(0)
outputs b1
n0 stage 0
  n1 loop a0 x8 ones strip 0
    n2 vector a0 x8
      n3 compute s0
      n4 compute s1
)";
constexpr std::string_view kFoldAcrossOutputs = R"(b0 f32 [64 32] feed 0 value 0
b1 f32 [64] value 2
stage 0 [64 32]
  s0 load f32 b0(0 1)
  s1 fold f32 normal node 4 reduce-sum s0 -> b1(0 -)
outputs b1 line-bytes 64
n0 stage 0
  n1 loop a0 x16 masked strip 0
    n2 registers acc s1 link n8
      n3 prefetch link n1
      n4 unroll a0 x8
        n5 vector a0 x8
          n6 init s1
      n7 loop a1 x1
        n8 unroll a0 x8
          n9 vector a0 x8
            n10 compute s0
            n11 accumulate s1
      n12 unroll a0 x8
        n13 vector a0 x8
          n14 finish s1
)";
constexpr std::string_view kMedianOfThree = R"(b0 f32 [d0] feed 0 value 0
b1 f32 [d0] value 2
stage 0 [d0 3]
  s0 load f32 b0(0 -)
  s1 select f32 node 4 gathered 3 rank 1 s0 -> b1(0 -)
outputs b1
n0 stage 0
  n1 loop a0 x1
    n2 registers gathered s1
      n3 turns a1 x1
        n4 compute s0
        n5 collect s1
      n6 rank s1
)";
constexpr std::string_view kStateInRegisters = R"(b0 f32 [4 d0] feed 0 value 0
b1 f32 [4 d0] value 2 prefix 1
stage 0 [4 d0]
  s0 load f32 b1(0 1) back 1
  s1 map f32 _add normal node 3 s0 b0(0 1) -> b1(0 1)
outputs b1
n0 stage 0
  n1 loop a0 x1
    n2 registers state s1
      n3 open s0 keeps s1
      n4 loop a1 x1
        n5 compute s0 reads s1
        n6 compute s1 keeps s1
)";
constexpr std::string_view kStateClosed = R"(b0 f32 [4 d0] feed 0 value 0
b1 f32 [4 d0] value 2 prefix 1 only-past
b2 f32 [4 d0] value 3
stage 0 [4 d0]
  s0 load f32 b1(0 -) back 1
  s1 map f32 _add normal node 3 s0 b0(0 1) -> b1(0 -)
  s2 load f32 s1 -> b2(0 1)
outputs b2
n0 stage 0
  n1 loop a0 x1
    n2 registers state s1
      n3 open s0 keeps s1
      n4 loop a1 x1
        n5 compute s0 reads s1
        n6 compute s1 keeps s1 closes
        n7 compute s2
      n8 close s0 keeps s1
)";
constexpr std::string_view kChunkedFold = R"(b0 f32 [8 256] feed 0 value 0
b1 f32 [8] value 2
stage 0 [8 256]
  s0 load f32 b0(0 1)
  s1 fold f32 normal node 4 reduce-sum s0 -> b1(0 -)
outputs b1
n0 stage 0
  n1 loop a0 x1
    n2 loop a1 x64 chunks
      n3 registers acc s1
        n4 init s1
        n5 loop a1 x1
          n6 compute s0
          n7 accumulate s1
        n8 when-last
          n9 finish s1
        n10 when-not-last
          n11 partial s1
)";
constexpr std::string_view kBandOfTwo = R"(b0 f32 [16 d0] feed 0 value 0
b1 f32 [16 4] value 1 band 0 kept a1 maker stage 0 reader stage 1
b2 f32 [16 d0] value 2
stage 0 [16 d0] band-axis a1
  s0 load f32 b0(0 1) -> b1(0 1)
stage 1 [16 d0] band-axis a1
  s0 load f32 b1(0 1) -> b2(0 1)
band 0 stages 0..1 step 4 b1
outputs b2
n0 stage 0
  n1 loop a1 x1
    n2 loop a0 x1
      n3 compute s0
n4 stage 1
  n5 loop a1 x1
    n6 loop a0 x1
      n7 compute s0
)";
constexpr std::string_view kSiblingFoldsInLanes = R"(b0 f32 [64 d0] feed 0 value 0
b1 f32 [1 d0] value 2
b2 f32 [1 d0] value 3
stage 0 [d0 64]
  s0 load f32 b0(1 0)
  s1 fold f32 normal node 4 reduce-sum s0 -> b1(1 -)
  s2 fold f32 normal node 4 reduce-max s0 -> b2(1 -)
outputs b1 b2
n0 stage 0
  n1 loop a0 x1
    n2 registers acc s1 link n7
      n3 registers acc s2 link n7
        n4 init s1
        n5 init s2
        n6 loop a1 x16 lanes-then-ones rest 8 strip 0
          n7 unroll a1 x8
            n8 vector a1 x8
              n9 compute s0
              n10 accumulate s1
              n11 accumulate s2
        n12 finish s1
        n13 finish s2
)";

constexpr std::string_view kFoldInASample = R"(b0 f64 [d0] feed 0 value 0
b1 f64 [d0] value 1 prefix 1
b2 f64 [8 1] value 100 constant first 0
b3 f64 [8 1] value 108 constant first 8
b4 f64 [8 1] value 116 constant first 16
b5 f64 [d0] value 5 prefix 1 only-past
b6 f64 [8 d0] value 6 prefix 1 only-past
stage 0 [8 d0]
  s0 load f64 b1(- 0) back 1
  s1 load f64 b5(- -) back 1
  s2 map f64 _mul high node 3 #0.125 s0
  s3 load f64 b0(- 0)
  s4 map f64 _sub high node 3 s3 s2 -> b5(- -)
  s5 load f64 b2(0 -)
  s6 map f64 _mul high node 3 s5 s4
  s7 load f64 b3(0 -)
  s8 map f64 _mul high node 3 s7 s1
  s9 map f64 _add high node 3 s6 s8
  s10 load f64 b6(0 -) back 1
  s11 load f64 b4(0 -)
  s12 map f64 _mul high node 3 s11 s10
  s13 map f64 _add high node 3 s9 s12 -> b6(0 -)
  s14 fold f64 high node 4 reduce-sum strict s13
  s15 map f64 _mul high node 3 #0.5 s4
  s16 map f64 _add high node 3 s14 s15 -> b1(- 0)
outputs b1
n0 stage 0
  n1 loop a0 x8 masked strip 0
    n2 registers state s4
      n3 registers state s13 link n18
        n4 registers state s16
          n5 unroll a0 x4
            n6 vector a0 x4
              n7 open s10 keeps s13
          n8 open s0 keeps s16
          n9 open s1 keeps s4
          n10 loop a1 x1
            n11 compute s0 reads s16
            n12 compute s1 reads s4
            n13 compute s2
            n14 compute s3
            n15 compute s4 keeps s4 closes
            n16 registers acc s14 first link n18
              n17 init s14
              n18 unroll a0 x4
                n19 vector a0 x4
                  n20 compute s5
                  n21 compute s6
                  n22 compute s7
                  n23 compute s8
                  n24 compute s9
                  n25 compute s10 reads s13
                  n26 compute s11
                  n27 compute s12
                  n28 compute s13 keeps s13 closes
                  n29 accumulate s14
              n30 finish s14
              n31 compute s15
              n32 compute s16 keeps s16
          n33 unroll a0 x4
            n34 vector a0 x4
              n35 close s10 keeps s13
          n36 close s1 keeps s4
)";

// One ill-formed tree per rule: each a well-formed one with one thing changed. (Node numbers: see the listings.)
// Two stages numbered breadth first: each parent before its kids and the kids in order, but not depth first.
constexpr Program numbered_breadth_first() {
    Program p{};
    p.buffers.push_back(feed(Shape{4}));
    p.buffers.push_back(made(Shape{4}, 1));
    p.buffers.push_back(made(Shape{4}, 2));
    p.stmts.push_back(load(read(0, {0}), read(1, {0})));
    const std::int32_t first = stage(p, {4}, 1);
    p.stmts.push_back(load(read(1, {0}), read(2, {0})));
    const std::int32_t second = stage(p, {4}, 1);
    const std::int32_t a = add(p, loop(0, 1), first);
    const std::int32_t b = add(p, loop(0, 1), second);
    add(p, leaf(NodeKind::Compute, 0), a);
    add(p, leaf(NodeKind::Compute, 0), b);
    p.outputs.push_back(2);
    return p;
}

// B with its Finish right below the Registers, no chain between: the lanes run across the outputs, so it would
// fold sixteen of them into one number.
constexpr Program finish_without_chain() {
    Program p = fold_across_outputs();
    p.nodes.clear();
    add(p, Node{.kind = NodeKind::Stage}, -1);
    const std::int32_t regs = add(p, registers(Role::Accumulator, 1, 3), add(p, loop(0, 16, Tail::Masked, 0), 0));
    add(p, leaf(NodeKind::Init, 1), add(p, walker(NodeKind::Vector, 0, 8), add(p, walker(NodeKind::Unroll, 0, 8), regs)));
    const std::int32_t update = add(p, walker(NodeKind::Vector, 0, 8), add(p, walker(NodeKind::Unroll, 0, 8), add(p, loop(1, 1), regs)));
    add(p, leaf(NodeKind::Compute, 0), update);
    add(p, leaf(NodeKind::Accumulate, 1), update);
    add(p, leaf(NodeKind::Finish, 1), regs);
    return p;
}

// A schedule with a choice of every kind: a loop of each kind, the inner loops, a fused multiply-add, a relaid operand,
// a prefetch, a split, a kept stage and the Params it watches, a band split among workers, what a split is given.
constexpr Schedule every_choice() {
    using Loop = Schedule::Loop;
    Schedule d{.parallel_work = 262144, .line_bytes = 64};
    d.stages.push_back(Schedule::Stage{.nodes = {{2, 3}, 2}, .loops = {{Loop{.axis = 0}}, 1}, .inner_from = 1});
    d.stages.push_back(Schedule::Stage{.nodes = {{4, 5, 7, 8}, 4},
                                       .inlined = 2,
                                       .through = 6,
                                       .loops = {{Loop{.axis = 2, .step = 8, .kind = LoopKind::Tile}, Loop{.axis = 2}, Loop{.axis = 0, .step = 4, .kind = LoopKind::Unroll},
                                                  Loop{.axis = 1, .step = 8, .kind = LoopKind::Vector, .unroll = 2}, Loop{.axis = 3}},
                                                 5},
                                       .inner_from = 4,
                                       .contract = true,
                                       .relaid = 1,
                                       .relaid_block = 16,
                                       .fetches = true,
                                       .parallel = 1});
    d.stages.push_back(Schedule::Stage{.nodes = {{9}, 1}, .inner_from = 0, .kept = true, .watched = {0b101}});
    d.bands.push_back(Schedule::Band{.first = 0, .count = 2, .axis = 0, .step = 30, .parallel = true});
    return d;
}

// The graph `every_choice` names inputs of: three Params, the kept stage watching the first and the third.
std::unique_ptr<heinzel::Graph> three_params() {
    auto g = std::make_unique<heinzel::Graph>();
    g->inputs.push_back(heinzel::NamedValue{"freq", 0});
    g->inputs.push_back(heinzel::NamedValue{"gain", 1});
    g->inputs.push_back(heinzel::NamedValue{"q", 2});
    return g;
}
static_assert(every_choice() == every_choice() && !(every_choice() == Schedule{}), "schedules compare as values");

constexpr std::string_view kEveryChoice = R"(s0 = n2 n3
  loops a0 |
s1 = n7 n8 inline(n4 n5 through n6)
  loops a2 tile 8, a2, a0 unroll 4, a1 vector 8 x2 | a3
  contract relaid 1 block 16 fetch parallel 1
s2 = n9
  loops |
  kept: freq q
band s0+2 a0 30 parallel
parallel-work 262144 line-bytes 64
)";

// The tree's links, its stages' runs of nodes, and where each node stands.
const Refused kStructure[] = {
    {"nodes numbered breadth first", numbered_breadth_first, [](Program&) {}, "heinzel::loops: nodes are numbered depth first"},
    {"a stage's root that is no Stage node", map_in_lanes, [](Program& p) { p.nodes[0].kind = NodeKind::Loop; },
     "heinzel::loops: each stage's Stage node is a root, right after the nodes of the stage before"},
    {"a kid naming another parent", map_in_lanes, [](Program& p) { p.nodes[4].parent = 1; },
     "heinzel::loops: a node's kids follow it, in order, and name it their parent"},
    {"a band's stages out of order", band_of_two, [](Program& p) { std::swap(p.stages[0].root, p.stages[1].root); },
     "heinzel::loops: each stage's Stage node is a root, right after the nodes of the stage before"},
    {"a stage not in the tree", map_in_lanes, [](Program& p) { p.stages.push_back(p.stages[0]); },
     "heinzel::loops: each stage's Stage node is a root, right after the nodes of the stage before"},
    {"a range at INT_MAX", map_in_lanes, [](Program& p) { p.stages[0].first = 2147483647; }, "heinzel::loops: a stage names a range of statements"},
    {"a leaf outside every stage", map_in_lanes, [](Program& p) { add(p, leaf(NodeKind::Compute, 0), -1); },
     "heinzel::loops: every node stands in a stage's tree"},
    {"a stage inside a stage", map_in_lanes, [](Program& p) { add(p, Node{.kind = NodeKind::Stage}, 1); },
     "heinzel::loops: Stage nodes are the roots, everything else stands below one, only leaves below a Vector"},
    {"a loop below a Vector", map_in_lanes, [](Program& p) { add(p, loop(0, 1), 2); },
     "heinzel::loops: Stage nodes are the roots, everything else stands below one, only leaves below a Vector"},
};

// Each node against the rules of its kind, the leaves each statement has, and their order.
const Refused kRefused[] = {
    {"a Finish with no chain, the lanes across the outputs", finish_without_chain, [](Program&) {},
     "heinzel::loops: a fold's or a Select's leaf stands below its Registers, indexed by their chain"},
    {"a Vector with no strip above it", map_in_lanes, [](Program& p) { p.nodes[1].tail = Tail::Clip, p.nodes[1].strip = -1; },
     "heinzel::loops: an Unroll or Vector stands below a strip over its axis, and no other loop over it between"},
    {"a loop over the lane axis between a Vector and its strip", map_with_a_loop_between, [](Program&) {},
     "heinzel::loops: an Unroll or Vector stands below a strip over its axis, and no other loop over it between"},
    {"a strip ordinal past kMaxStrips", map_in_lanes, [](Program& p) { p.nodes[1].strip = 16; },
     "heinzel::loops: a strip has an ordinal below kMaxStrips, and a rest only after its lanes"},
    {"a strip that peels", map_in_lanes, [](Program& p) { p.nodes[1].peel_first = true; }, "heinzel::loops: a loop that peels its first iteration is a Clip loop"},
    {"a Vector not a power of two wide", map_in_lanes, [](Program& p) { p.nodes[1].step = 6, p.nodes[2].step = 6; },
     "heinzel::loops: a Vector is as wide as the chunk it is handed, a power of two, two or more"},
    {"an Unroll that does not divide its chunk", fold_across_outputs, [](Program& p) { p.nodes[1].step = 12; },
     "heinzel::loops: an Unroll's step divides the chunk it is handed"},
    {"an Unroll that does not divide the rest of its strip", sibling_folds_in_lanes, [](Program& p) { p.nodes[6].rest = 4; },
     "heinzel::loops: an Unroll's step divides the chunk it is handed"},
    {"two lane axes in a stage", lanes_over_two_axes, [](Program&) {}, "heinzel::loops: every Vector of a stage walks one axis, the stage's lane axis"},
    {"two strips with one ordinal", lanes_over_two_axes, [](Program& p) { p.nodes[6].strip = 0; }, "heinzel::loops: strip ordinals are unique within a stage"},
    {"a fold's leaves below another fold's Registers", sibling_folds_in_lanes, [](Program& p) { p.nodes[3].stmt = 1; },
     "heinzel::loops: a fold's or a Select's leaf stands below its Registers, indexed by their chain"},
    {"Registers holding a statement in another role", fold_across_outputs, [](Program& p) { p.nodes[2].role = Role::State; },
     "heinzel::loops: Registers hold a statement of their stage in its role"},
    {"a state read with no State registers", state_in_registers, [](Program& p) { p.nodes[2].stmt = 0; },
     "heinzel::loops: a leaf reading or keeping state stands below that State's Registers, indexed by their chain"},
    {"a WhenLast below no loop that chunks a fold", chunked_fold, [](Program& p) { p.nodes[2].chunks_fold = false; },
     "heinzel::loops: WhenLast and WhenNotLast stand below a loop that chunks a fold"},
    {"a loop that chunks a stage without a fold", band_of_two, [](Program& p) { p.nodes[1].step = 2, p.nodes[1].chunks_fold = true; },
     "heinzel::loops: a loop that chunks a fold is a Clip loop of chunks of two or more, in a stage with a fold"},
    {"a Collect over turns other than it gathers", median_of_three, [](Program& p) { p.stages[0].axes[1] = Extent{4}; },
     "heinzel::loops: a Collect stands below the turns of its Select, as many as it gathers"},
    {"a statement with its Compute twice", map_in_lanes, [](Program& p) { p.nodes[4].stmt = 0; }, "heinzel::loops: every statement has exactly its leaves"},
    {"a fold with no Partial where a loop chunks it", chunked_fold, [](Program& p) { p.nodes[11].kind = NodeKind::Finish; },
     "heinzel::loops: every statement has exactly its leaves"},
    {"an Open of another State than its reader reads", state_opened_wrongly, [](Program&) {}, // (its own: it keeps itself)
     "heinzel::loops: a reader of state, and its Open, read the buffer its keeper stores, before the keeper"},
    {"a state read after the statement keeping it", state_read_after_its_keeper, [](Program&) {},
     "heinzel::loops: a reader of state, and its Open, read the buffer its keeper stores, before the keeper"},
    {"a state reader of another buffer than its keeper stores", state_in_registers, [](Program& p) { p.stmts[1].store.buffer = 0; },
     "heinzel::loops: a reader of state, and its Open, read the buffer its keeper stores, before the keeper"},
    {"inits out of statement order", sibling_folds_in_lanes, [](Program& p) { std::swap(p.nodes[4].stmt, p.nodes[5].stmt); },
     "heinzel::loops: leaves come in statement order, a value after every value in reach"},
    {"finishes out of statement order", sibling_folds_in_lanes, [](Program& p) { std::swap(p.nodes[12].stmt, p.nodes[13].stmt); },
     "heinzel::loops: leaves come in statement order, a value after every value in reach"},
    {"a local read before it is made", map_in_lanes, [](Program& p) { std::swap(p.nodes[3].stmt, p.nodes[4].stmt); },
     "heinzel::loops: a local operand's maker is an earlier sibling of its reader or of an ancestor"},
    {"a local read from inside a closed loop", fold_across_outputs,
     [](Program& p) {
         p.stmts.push_back(map(op::Mul, local(0), local(0)));
         ++p.stages[0].count;
         add(p, leaf(NodeKind::Compute, 2), 13);
     },
     "heinzel::loops: a local operand's maker is an earlier sibling of its reader or of an ancestor"},
    {"a split that is not a Loop", map_in_lanes, [](Program& p) { p.stages[0].split = 2; },
     "heinzel::loops: a stage's split and time loop are Loop nodes of that stage"},
    {"head samples with no time loop", map_in_lanes, [](Program& p) { p.stages[0].head = 2; },
     "heinzel::loops: head samples walk a time loop; a band axis is one of the stage's"},
    {"a link to the middle of a chain", fold_across_outputs, [](Program& p) { p.nodes[2].link = 9; },
     "heinzel::loops: a Registers' link is the head of a chain below it"},
    {"a Prefetch along a loop inside its Registers", fold_across_outputs, [](Program& p) { p.nodes[3].link = 7; },
     "heinzel::loops: a Prefetch names a Loop enclosing its Registers, and a line"},
    {"a Prefetch of lines of no size", fold_across_outputs, [](Program& p) { p.line_bytes = 0; },
     "heinzel::loops: a Prefetch names a Loop enclosing its Registers, and a line"},
    {"a leaf indexed by a chain unlike the link's", fold_across_outputs, [](Program& p) { p.nodes[12].step = 4, p.nodes[13].step = 4; },
     "heinzel::loops: a fold's or a Select's leaf stands below its Registers, indexed by their chain"},
    {"a local naming the statement itself", map_in_lanes, [](Program& p) { p.stmts[1].in[0] = local(1); },
     "heinzel::loops: a local operand is an earlier statement of the stage, and no store is one"},
    {"Registers inside an enumeration", registers_in_a_chain, [](Program&) {}, "heinzel::loops: Registers stand outside every enumeration"},
    {"a Gather reading no buffer", map_in_lanes, [](Program& p) { p.stmts[1].kind = StmtKind::Gather; },
     "heinzel::loops: a Gather reads a buffer along the axis its position indexes"},
    {"a store into a constant", map_in_lanes, [](Program& p) { p.buffers[1].home = Home::Constant; }, "heinzel::loops: no store is into a constant"},
    {"a walk along a constant's last axis", map_in_lanes, [](Program& p) { p.buffers[0].home = Home::Constant; },
     "heinzel::loops: no access walks a constant along its last axis: it holds one sample"},
    {"a lag of a constant", map_in_lanes, [](Program& p) { p.buffers[0].home = Home::Constant, p.stmts[0].in[0].buffer_axis[0] = -1, p.stmts[0].in[0].back = 1; },
     "heinzel::loops: no access walks a constant along its last axis: it holds one sample"},
    {"a gather along a constant's last axis", map_in_lanes,
     [](Program& p) { p.buffers[0].home = Home::Constant, p.stmts[0].in[0].buffer_axis[0] = -1, p.stmts[0].in[0].indexed_axis = 0; },
     "heinzel::loops: no access walks a constant along its last axis: it holds one sample"},
    {"a constant's numbers past the pool", map_in_lanes, [](Program& p) { p.buffers[0].home = Home::Constant, p.buffers[0].first = 0; },
     "heinzel::loops: a constant's numbers lie in the program's pool, and it in read-only data"},
};

// A state kept in its registers (`Node::closes`): its Close and the buffer of its past alone.
const Refused kClosed[] = {
    {"a Close of a keeper that stores each sample", state_closed, [](Program& p) { p.nodes[6].closes = false; },
     "heinzel::loops: a Close writes back the State of a keeper that closes"},
    {"a Close inside the loop walking time", state_closed_inside, [](Program&) {}, "heinzel::loops: a Close stands after the loop walking time, not in it"},
    {"a Close before the loop walking time", state_closed_before, [](Program&) {}, "heinzel::loops: a Close stands after the loop walking time, not in it"},
    {"a closed state read by another statement", state_closed, [](Program& p) { p.stmts[2].in[0] = read(1, {0, -1}); },
     "heinzel::loops: a state kept in its registers holds its past alone, read by them and walked along no time, every slot opened closed again"},
    {"a closed state that is an output", state_closed, [](Program& p) { p.outputs[0] = 1; },
     "heinzel::loops: a state kept in its registers holds its past alone, read by them and walked along no time, every slot opened closed again"},
    {"a closed state walked along time", state_closed, [](Program& p) { p.stmts[0].in[0].buffer_axis[1] = 1; },
     "heinzel::loops: a state kept in its registers holds its past alone, read by them and walked along no time, every slot opened closed again"},
    {"a past alone that no keeper closes", state_in_registers, [](Program& p) { p.buffers[1].only_past = true; },
     "heinzel::loops: a buffer of its past alone is a closing keeper's"},
};

// A fold inside a feedback stage's sample (Plan/23 10, `well_formed_restarts.hpp`): its Registers right below the loop
// handing state on, inside one whole chunk of its lanes' strip, its Init and Finish outside the chain; a one-voice state
// (no link) kept outside every chain.
const Refused kInASample[] = {
    {"a fold's strip inside its Registers, in a loop handing state on", fold_with_its_strip_in_a_sample, [](Program&) {},
     "heinzel::loops: a fold's Registers inside a loop that hands state on stand right below it, inside the strip of their lanes"},
    {"a fold restarted every sample, its strip in two chunks", fold_in_a_sample, [](Program& p) { p.nodes[1].step = 4; },
     "heinzel::loops: a fold restarted every sample runs its lanes in one whole chunk of their strip, its chain along that axis alone"},
    {"a fold restarted every sample, its strip's rest one at a time", own_lag_of_three, [](Program& p) { p.nodes[1].tail = Tail::Ones; },
     "heinzel::loops: a fold restarted every sample runs its lanes in one whole chunk of their strip, its chain along that axis alone"},
    {"a fold restarted every sample, finished in its chain", fold_in_a_sample,
     [](Program& p) { // (the Finish moved into the lanes, its value read by nothing)
         p.nodes[29].next_sibling = 30, p.nodes[30].parent = 19, p.nodes[30].next_sibling = -1, p.nodes[18].next_sibling = 31;
         p.stmts[16].in[0] = local(15);
     },
     "heinzel::loops: a fold restarted every sample opens and finishes outside its chain"},
    {"a one-voice state kept in the lanes", fold_in_a_sample, [](Program& p) { p.nodes[3].link = -1; },
     "heinzel::loops: a leaf reading or keeping state stands below that State's Registers, indexed by their chain"},
    {"a fold's Finish keeping state with no State registers", own_lag_of_three, [](Program& p) { p.nodes[3].stmt = 7; },
     "heinzel::loops: a leaf reading or keeping state stands below that State's Registers, indexed by their chain"},
};

} // namespace

int main() {
    pinned("a map in lanes", map_in_lanes, kMapInLanes);
    pinned("a fold across outputs", fold_across_outputs, kFoldAcrossOutputs);
    pinned("a median of three", median_of_three, kMedianOfThree);
    pinned("state in registers", state_in_registers, kStateInRegisters);
    pinned("a state closed after its loop", state_closed, kStateClosed);
    pinned("a chunked fold", chunked_fold, kChunkedFold);
    pinned("a band of two", band_of_two, kBandOfTwo);
    pinned("sibling folds in lanes", sibling_folds_in_lanes, kSiblingFoldsInLanes);
    pinned("a fold inside a feedback stage's sample", fold_in_a_sample, kFoldInASample);
    if (const std::string got = loops::print(every_choice(), *three_params()); got != kEveryChoice) {
        ++g_wrong;
        std::printf("a schedule of every choice printed differently:\n%s\n", got.c_str());
    }
    for (const Refused& c : kStructure) {
        g_wrong += refused_as_named(c) ? 0 : 1;
    }
    for (const Refused& c : kRefused) {
        g_wrong += refused_as_named(c) ? 0 : 1;
    }
    for (const Refused& c : kClosed) {
        g_wrong += refused_as_named(c) ? 0 : 1;
    }
    for (const Refused& c : kInASample) {
        g_wrong += refused_as_named(c) ? 0 : 1;
    }
    immediates_compare_bit_for_bit();
    if (g_wrong != 0) {
        std::printf("%d checks failed\n", g_wrong);
        return 1;
    }
    return 0;
}
