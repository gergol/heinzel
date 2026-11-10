#pragma once

// The loop-tree program: a graph once it is decided how it runs. Buffers say what storage exists, statements what is
// computed (with affine accesses), stages the loop nest over each domain, and bands which stages run chunk by chunk
// under one loop; each stage's tree of nodes says in which loops, in which order, at which width and with which
// registers each of its statements runs. Stages run in order, a band's together. It is plain `constexpr` data of fixed
// capacity, passed as `template <const Program& P>`, never by value. The tests check every program they make against
// what the runner accepts (`tests/support/well_formed.hpp`).

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "heinzel/ir/ir.hpp"
#include "heinzel/support/fail.hpp"
#include "heinzel/support/fixed_string.hpp"
#include "heinzel/support/fixed_vector.hpp"

namespace heinzel::loops {

// Why a pass refuses, by name: `heinzel::compile:` for the planner and the lowering, `heinzel::loops:` for the
// well-formedness check and the runner. Most are literals, kept as such, so that where `compile` fails with one under
// constant evaluation clang's note shows its words (a capacity's: `tests/gates/refusals/capacity.cpp`); a node's
// refusal is written where the planner has the node (`planner/stages.hpp`: its operation, the case, its operands'
// shapes) and held as a text of its own. Either reads as at most a message's length, cut where `fail` and
// `MessageWriter` cut, and a text compares equal to the part of it a refusal holds.
struct Refusal {
    std::string_view literal{}; // a literal, or a view of one; never of a buffer, which would not outlive it
    fixed_string<kMaxMessageLength> text{}; // a text written on the spot

    constexpr Refusal() = default;
    constexpr Refusal(std::string_view why) : literal(why) {}
    constexpr Refusal(const char* why) : literal(why) {} // (`ok ? "" : "…"` is one)
    constexpr explicit Refusal(const fixed_string<kMaxMessageLength>& written) : text(written) {}

    constexpr std::string_view view() const { return literal.empty() ? text.view() : literal.substr(0, kMaxMessageLength - 1); }
    constexpr bool empty() const { return literal.empty() && text.data_[0] == '\0'; } // (asked in loops: kept cheap)
    constexpr bool operator==(std::string_view other) const { return view() == other.substr(0, kMaxMessageLength - 1); }
    // (what a computed `static_assert` message and `%.*s` read)
    constexpr const char* data() const { return view().data(); }
    constexpr std::size_t size() const { return view().size(); }
};

// A program's capacities are paid for by every constant evaluation that makes one, element by element (an empty
// program cost ~90 ms and ~30 MB at 2048 statements). So they are what programs need, with room to spare (the tree's
// largest has 66 statements, 244 tree nodes and 28 buffers), not the product of the per-stage ones. Those a graph can
// reach by a refusal are raised with their `-DHEINZEL_MAX_…=N`, the same in every translation unit
// (`docs/limitations.md`); the small ones below them are reached only by declining, or by a hand-written schedule.
#ifndef HEINZEL_MAX_STAGES
#define HEINZEL_MAX_STAGES 16
#endif
// Buffers hold a plugin's program: an 8-band EQ with three knobs a band takes 75 (no measurable cost).
#ifndef HEINZEL_MAX_BUFFERS
#define HEINZEL_MAX_BUFFERS 96
#endif
#ifndef HEINZEL_MAX_STAGE_STMTS
#define HEINZEL_MAX_STAGE_STMTS 128
#endif
#ifndef HEINZEL_MAX_STMTS
#define HEINZEL_MAX_STMTS 128
#endif
// Tree nodes and buffers leave room for a contraction in every stage: sixteen need 480 nodes and 49 buffers.
#ifndef HEINZEL_MAX_TREE_NODES
#define HEINZEL_MAX_TREE_NODES 512
#endif
inline constexpr std::size_t kMaxStages = HEINZEL_MAX_STAGES;
inline constexpr std::size_t kMaxBuffers = HEINZEL_MAX_BUFFERS;
inline constexpr std::size_t kMaxStageStmts = HEINZEL_MAX_STAGE_STMTS; // a stage's nodes and a Load per operand buffer
inline constexpr std::size_t kMaxStmts = HEINZEL_MAX_STMTS;
inline constexpr std::size_t kMaxTreeNodes = HEINZEL_MAX_TREE_NODES;
static_assert(kMaxStages >= 1, "HEINZEL_MAX_STAGES must be at least 1");
static_assert(kMaxBuffers >= 1, "HEINZEL_MAX_BUFFERS must be at least 1");
static_assert(kMaxStageStmts >= 2, "HEINZEL_MAX_STAGE_STMTS must be at least 2"); // a quarter of it holds the Loads
static_assert(kMaxStmts >= 1, "HEINZEL_MAX_STMTS must be at least 1");
static_assert(kMaxTreeNodes >= 1, "HEINZEL_MAX_TREE_NODES must be at least 1");
inline constexpr std::size_t kMaxBands = 4;
inline constexpr std::size_t kMaxBandBuffers = 8;
inline constexpr std::size_t kMaxAxes = 8;           // a contraction's domain has one axis more than its result
inline constexpr std::size_t kMaxStrips = 16;        // two mode bits each in the runner's mode word
inline constexpr std::size_t kMaxStageLoops = 24;    // walking nodes of one stage's skeleton
inline constexpr std::size_t kWatchWords = (kMaxIO + 63) / 64; // a bit per named input: the Params a kept stage watches

// ---- storage ---------------------------------------------------------------------------------------------

// The axis a value of `rank` axes is contiguous in: memory is sample-major, so its last element axis, or the one axis
// it has.
constexpr std::int32_t contiguous_axis(std::int32_t rank) { return rank >= 2 ? rank - 2 : 0; }

// Where a buffer lies: a named input's, the caller's; a workspace buffer, in the workspace; a constant (a constant
// tensor, or a ramp whose past nothing reads), in read-only data, one sample of it, every read broadcast along its last
// axis.
enum class Home : std::uint8_t { Feed, Workspace, Constant };

struct Buffer {
    ElemType type{ElemType::Unknown};
    Shape shape{};                  // extents, innermost last; storage is sample-major (the last axis outermost)
    Home home{Home::Workspace};
    std::int32_t feed{-1};          // which named input, when home is Feed
    std::int32_t value{kNoValue};   // the graph value it holds
    std::int32_t blocked_axis{-1};  // a relaid copy: this axis cut into blocks of `block`, the block index
    std::int32_t block{0};          //   outermost among the element axes, the position within a block innermost
    std::int32_t turned{-1};        // a turned copy: its value with this axis and its contiguous one swapped
    std::int32_t prefix{0};         // zeroed samples kept before the block (the past a lag reads)
    bool only_past{false};          // a state handed on in registers: its past alone, opened from and closed into in
                                    //   place (`NodeKind::Open`, `Close`); no block, no access walks time, no carry
    bool ramp{false};               // element e holds e (within a sample): a constant, or filled when the workspace is made
    std::int32_t first{-1};         // a constant tensor: its numbers, from here in the program's pool (one sample)
    std::int32_t band{-1};          // a band buffer: the band keeping it per chunk
    std::int32_t kept_axis{-1};     //   the axis along which it keeps rows (its shape says how many)
    std::int32_t maker{-1};         //   the stage that writes it
    std::int32_t reader{-1};        //   the stage reading it from furthest behind: row 0 is where it starts a chunk

    constexpr bool operator==(const Buffer&) const = default;
};

// ---- statements ------------------------------------------------------------------------------------------

// `First`: a run's start (`_first`), 1 at the run's first sample and 0 at every other, computed where it is read, in a
// stage whose time loop peels that sample (`Node::peels_start`).
enum class Source : std::uint8_t { None, Buffer, Local, Immediate, First };

struct Access {
    Source source{Source::None};
    std::int32_t buffer{-1};
    std::int32_t local{-1};                      // a statement of the same stage (stage-local index)
    std::uint64_t immediate{0};                  // the double's bits: compared bit for bit (a NaN fill, a -0.0)
    std::int32_t buffer_axis[kMaxAxes]{-1, -1, -1, -1, -1, -1, -1, -1}; // per axis of the stage's domain: the
    std::int32_t step[kMaxAxes]{1, 1, 1, 1, 1, 1, 1, 1};                //   buffer axis it walks (-1: broadcast),
                                                                        //   by how many elements (a window's hop)
    std::int32_t offset[kMaxRank]{};             // per buffer axis: where the domain's first point stands
    std::int32_t back{0};                        // samples earlier along the buffer's streamed axis (a lag)
    std::int32_t indexed_axis{-1};               // a gather's source: the buffer axis its position indexes

    constexpr bool operator==(const Access&) const = default;
};

enum class StmtKind : std::uint8_t { Map, Load, Gather, Fold, Select };
enum class Gather : std::uint8_t { None, Back, Table };

struct Stmt {
    StmtKind kind{StmtKind::Map};
    OpCode op{};
    std::int32_t node{-1};                   // the graph node it came from (-1: a Load lowering added, a relay copy)
    ElemType type{ElemType::Unknown};        // the result's
    ElemType computed_in{ElemType::Unknown}; // F64 where an operand is F64, else its own type
    numerics::Precision precision{numerics::Precision::Inherit};
    Access in[kMaxNodeInputs]{};
    Access store{};                          // where the value is ALSO written (Source::None: nowhere)
    // Fold — a reduction (one operand) or a contraction (the product of two):
    AttrKind collapse{AttrKind::Reduce};
    ReduceOp fold{ReduceOp::Sum};
    bool strict{false};                      // accumulates in double, in walk order, from its first element (High)
    bool fused{false};                       // a contraction's multiply-add as one operation, or a sum's and the
                                             //   product it folds (its two operands the product's)
    // Select — one rank of the values gathered along an axis (an order statistic):
    std::int32_t gathered{0};
    std::int32_t rank{0};
    // Gather — a read whose address is a signal:
    Gather gather{Gather::None};
    bool phase_in_one_turn{false};           // a table read's phase lies in [0, 1] (`TableAttr`): read with no wrap
    std::int32_t reach{0};

    constexpr bool operator==(const Stmt&) const = default;
};

// ---- the tree --------------------------------------------------------------------------------------------

enum class NodeKind : std::uint8_t {
    Stage,                             // a stage's root
    Loop, Unroll, Vector, Turns,       // walking
    Registers, WhenLast, WhenNotLast,  // scopes
    Compute, Init, Accumulate, Finish, Partial, Open, Close, Collect, Rank, Prefetch, // leaves
};
enum class Tail : std::uint8_t { Clip, Ones, Masked, LanesThenOnes };
enum class Role : std::uint8_t { Accumulator, State, Gathered };

struct Node {
    NodeKind kind{NodeKind::Stage};
    std::int32_t parent{-1};  // a Stage node has no parent
    std::int32_t first_kid{-1};
    std::int32_t next_sibling{-1};  // a Stage node has no sibling
    std::int32_t axis{-1};        // Loop, Unroll, Vector, Turns: an axis of the stage's domain
    std::int32_t step{1};         // Loop: chunk; Unroll: sub-chunk; Vector: lanes; Turns: one turn
    Tail tail{Tail::Clip};        // Loop
    bool peel_first{false};       // Loop: iteration 0 keeps the inherited "first", later ones clear it
    bool peels_start{false};      // Loop over time: iteration 0 keeps the run's start where the walk is at it, later
                                  //   ones are past it (`AtStart`), so a `Source::First` reads a constant 0 there
    bool chunks_fold{false};      // Loop (Clip, step > 1) outside the inner loops, over an axis an inner loop walks
    std::int32_t rest{1};         // Loop: the sub-chunk of a LanesThenOnes rest
    std::int32_t strip{-1};       // Loop with tail != Clip: its ordinal among its stage's strips (pre-order)
    std::int32_t stmt{-1};        // Registers and the leaves: the statement (stage-local)
    Role role{Role::Accumulator}; // Registers
    bool opens_first{false};      // Registers(Accumulator): the fold opens at its first element, not its identity
    std::int32_t link{-1};        // Registers: the first node of the chain that sizes it (-1: one body, width 1);
                                  // Prefetch: the Loop whose next iteration's stores it asks for
    std::int32_t reads{-1};       // Compute: its value is the State register of statement `reads`
    std::int32_t keeps{-1};       // Compute, Open: the value is written into the State register of `keeps`; Close:
                                  //   that register's slot written back where the Open of `stmt` read it
    bool closes{false};           // Compute that keeps state: kept in its registers only, closed after the loop
    bool shifted{false};          // Compute reading state in a skewed stage (the link): its operand 1 is that register
                                  //   moved one lane up across bodies, lane 0 taking operand 0 (the fill)
    std::int32_t slot{0};         // Compute that reads state, Open, Close: the register's slot, its lag less one
    std::int32_t slots{1};        // Registers(State): how many samples of state it holds, the longest lag read
    std::int32_t lane{-1};        // Compute in a skewed stage standing at one lane (the fill's Load, the slice's): its
                                  //   value that lane's, broadcast, its reads and store at that lane's sample

    constexpr bool operator==(const Node&) const = default;
};

struct Stage {
    fixed_vector<Extent, kMaxAxes> axes{};   // the domain
    std::int32_t first{0};  // its statements: Program::stmts[first, first + count)
    std::int32_t count{0};
    std::int32_t root{-1};                   // its Stage node, the root of its tree
    std::int32_t split{-1};                  // the Loop node whose axis is cut among workers (-1: none)
    std::int32_t head{0};                    // first samples run one at a time, a feed's past read from its tail
    std::int32_t time_loop{-1};              // the Loop node walking time, where head > 0 or skewed
    // Skewed: its lanes are its sections, lane j standing j samples behind lane 0, so its time loop runs k − 1 steps
    // past the block, k the lane axis' literal extent (`docs/sections-in-lanes.md`).
    bool skewed{false};
    // In a band: the axis it walks with the band, rows ahead of the last stage.
    std::int32_t band_axis{-1};
    std::int32_t lead{0};
    // Kept: run only in a run where a Param it watches changed, its results kept in their buffers in between
    // (`docs/knob-math.md`); `watched`, a bit per named input, in the feeds' order.
    bool kept{false};
    std::array<std::uint64_t, kWatchWords> watched{};

    constexpr bool operator==(const Stage&) const = default;
};

struct BandInfo {
    std::int32_t first{0};  // stages first .. first + count - 1
    std::int32_t count{0};
    std::int32_t step{0};                    // rows of the last stage's band axis per chunk
    bool parallel{false};                    // chunks split among workers, band buffers per worker

    constexpr bool operator==(const BandInfo&) const = default;
};

struct Program {
    fixed_vector<Buffer, kMaxBuffers> buffers{};
    fixed_vector<Stmt, kMaxStmts> stmts{};
    fixed_vector<Stage, kMaxStages> stages{};
    fixed_vector<BandInfo, kMaxBands> bands{};
    fixed_vector<Node, kMaxTreeNodes> nodes{};    // pre-order: each stage's nodes in one run, after the stage before
    fixed_vector<std::int32_t, kMaxIO> outputs{}; // the buffer of each named output, in graph order
    fixed_vector<std::uint64_t, kMaxConstants> constants{}; // the graph's pool: a constant tensor's numbers (`first`)
    std::int64_t parallel_work{0};                // statement-points a worker is given at least
    std::int32_t line_bytes{0};                   // a cache line: a split range covers whole lines of what it writes, and
                                                  // a Prefetch asks for lines

    constexpr bool operator==(const Program&) const = default;
};

// ---- helpers the passes and the runner share (called qualified: `loops::bytes_of(t)`) ----------------------

// The size bound to each dimension symbol, by symbol id.
using Sizes = std::array<std::int64_t, kMaxDimDecls>;

constexpr std::int32_t bytes_of(ElemType t) { return t == ElemType::F32 ? 4 : 8; }

template <ElemType E>
using element_t = std::conditional_t<E == ElemType::F32, float, double>;

constexpr std::int64_t resolved(Extent e, const Sizes& sizes) {
    return is_literal(e) ? literal_of(e) : sizes[static_cast<std::size_t>(symbol_of(e))];
}

// A statement's access in slot `slot`: its operands, then its store.
constexpr const Access& access_in(const Stmt& st, std::size_t slot) { return slot < kMaxNodeInputs ? st.in[slot] : st.store; }
constexpr Access& access_in(Stmt& st, std::size_t slot) { return slot < kMaxNodeInputs ? st.in[slot] : st.store; }

} // namespace heinzel::loops
