#pragma once

// The graph: the whole meaning of a computation, as one value.
//
// A graph says what is computed and nothing about how: no buffers, loops or memory plan. Those are
// decisions later passes make by reading it, so the same graph can be planned different ways without
// being edited. Every type here is plain data of fixed size with a generated `==`.

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "heinzel/ir/range.hpp"
#include "heinzel/ir/vocab.hpp"
#include "heinzel/support/fail.hpp"
#include "heinzel/support/fixed_vector.hpp"

namespace heinzel {

// ---- how large a graph may be --------------------------------------------------------------------
// Chosen numbers, not constraints: a limit hit here may be raised. They are modest because every
// constant evaluation that makes a graph pays for its whole capacity, element by element. Raising one
// to fit a bigger graph is ordinary; raising one to rescue a pass whose cost grows faster than the
// graph is the mistake, because a bigger budget only moves that cliff. Each is raised with its
// `-DHEINZEL_MAX_…=N`, the same in every translation unit (`docs/limitations.md`).
#ifndef HEINZEL_MAX_VALUES
#define HEINZEL_MAX_VALUES 128
#endif
#ifndef HEINZEL_MAX_NODES
#define HEINZEL_MAX_NODES 128
#endif
// Inputs hold a plugin's knobs: an 8-band EQ with three a band, and its signal (no measurable cost).
#ifndef HEINZEL_MAX_IO
#define HEINZEL_MAX_IO 32
#endif
#ifndef HEINZEL_MAX_DIM_DECLS
#define HEINZEL_MAX_DIM_DECLS 8
#endif
// The numbers of a graph's constant tensors, all of them. Only constant evaluation pays for it: a binary holds just
// the numbers each constant reads (`loops::kConstantOf`).
#ifndef HEINZEL_MAX_CONSTANTS
#define HEINZEL_MAX_CONSTANTS 1024
#endif
// The ranges a graph states: one for each ranged port, each promised value and each fact (`RangeMark`).
#ifndef HEINZEL_MAX_RANGES
#define HEINZEL_MAX_RANGES 64
#endif
inline constexpr std::size_t kMaxValues = HEINZEL_MAX_VALUES;
inline constexpr std::size_t kMaxNodes = HEINZEL_MAX_NODES;
inline constexpr std::size_t kMaxNodeInputs = 3; // the widest operation takes three
inline constexpr std::size_t kMaxIO = HEINZEL_MAX_IO; // named inputs, or named outputs, in one graph
// How many runtime-resolved dimensions one graph may declare. Modest, because a graph usually
// resolves a single streamed length. A Dim's id is a solver symbol, so at most `kMaxSymbols` (32).
inline constexpr std::size_t kMaxDimDecls = HEINZEL_MAX_DIM_DECLS;
inline constexpr std::size_t kMaxConstants = HEINZEL_MAX_CONSTANTS;
inline constexpr std::size_t kMaxRanges = HEINZEL_MAX_RANGES;
static_assert(kMaxValues >= 1, "HEINZEL_MAX_VALUES must be at least 1");
static_assert(kMaxNodes >= 1, "HEINZEL_MAX_NODES must be at least 1");
static_assert(kMaxIO >= 1, "HEINZEL_MAX_IO must be at least 1");
static_assert(kMaxDimDecls >= 1, "HEINZEL_MAX_DIM_DECLS must be at least 1");
static_assert(kMaxRanges >= 1, "HEINZEL_MAX_RANGES must be at least 1");

inline constexpr std::int32_t kNoValue = -1;     // an operand slot that is not used
inline constexpr std::int32_t kNoDimension = -1; // a declaration slot that names no dimension

// ---- values ---------------------------------------------------------------------------------------
// One value flowing through the graph. `Internal` is the ordinary case — something an operation
// produced. `Variable` is the one that needs explaining: a value defined in terms of its own past,
// which therefore has to EXIST before the expression defining it does. Nothing produces it directly;
// a recurrence names it. Whether a value is a result is not a category: the graph's outputs say so
// (`is_a_result`).
enum class ValueCategory : std::uint8_t { Internal, Input, Const, Variable };

// ---- numeric precision (compile-time resolved) ------------------------------------------------------
// The per-value accuracy and reassociation setting. `Inherit` is the unpinned default: a value carries
// it until data-flow propagation or an explicit `.precision()` mark resolves it, and it is never the
// result of a join.
namespace numerics {
enum class Precision : std::uint8_t { Inherit = 0, Fast = 1, Normal = 2, High = 3 };

// SELECT-HIGHER at a multi-input op: the more accurate/deterministic of the two (toward High). The
// resolved lattice is Fast < Normal < High; Inherit (0) is the unpinned sentinel and never a result.
constexpr Precision join(Precision a, Precision b) {
    return static_cast<std::uint8_t>(a) >= static_cast<std::uint8_t>(b) ? a : b;
}

// The three precisions mapped onto the two numeric axes:
//   High   {Exact,  Reproducible}  -- accurate and bit-reproducible
//   Normal {Exact,  Free}          -- the default
//   Fast   {Approx, Free}          -- ~1e-4 transcendentals, reassociation allowed
constexpr NumericSetting numeric_setting_of(Precision p) {
    switch (p) {
    case Precision::High:    return {FnAccuracy::Exact,  Ordering::Reproducible};
    case Precision::Normal:  return {FnAccuracy::Exact,  Ordering::Free};
    case Precision::Fast:    return {FnAccuracy::Approx, Ordering::Free};
    case Precision::Inherit: return {FnAccuracy::Inherit, Ordering::Inherit};
    }
    fail("heinzel::numerics::numeric_setting_of: precision outside the three-level model");
}

// What a value's precision PERMITS a schedule to do where the operation has the freedom at all
// (`NumericContract`). A grant is a permission, not an instruction: a schedule that takes none of them
// computes the same bits at every precision. `High` grants nothing — that is its promise.
struct Grants {
    bool reorder{false}; // evaluate in another order than written: a fold's terms in lanes or tiles, a*b+c fused
    FnAccuracy fn{FnAccuracy::Inherit};

    constexpr bool operator==(const Grants&) const = default;
};

constexpr Grants grants(Precision p) {
    if (p == Precision::Inherit) {
        // "Nothing granted" would read exactly like High; an unresolved value has no answer yet.
        fail("heinzel::numerics::grants: the value's precision is not resolved; analyse the graph first");
    }
    const NumericSetting s = numeric_setting_of(p);
    return {s.ordering == Ordering::Free, s.fn};
}
} // namespace numerics

struct Value {
    ElemType type{ElemType::Unknown};
    Shape shape{};
    ValueCategory category{ValueCategory::Internal};
    double constant{0.0}; // meaningful only when category is Const

    // Compile-time numeric precision: `Inherit` until resolved; an explicit `.precision()` mark pins it.
    ::heinzel::numerics::Precision precision{::heinzel::numerics::Precision::Inherit};

    // Declared a tensor: the same numbers at every sample, no time of its own (`Input w{"w", {64, 64}}`,
    // a `Param`, `iota({…})`). Recorded by the frontend, never inferred. Its shape still ends in a
    // literal length-1 stream axis. Last, so the positional aggregate initialisations stay valid.
    bool timeless{false};

    constexpr bool operator==(const Value&) const = default;
};

// ---- what an operation carries besides its operands ------------------------------------------------
// Each of these holds one fact that is not a value flowing in, and one field never means two things.

struct CastAttr {
    ElemType to{ElemType::Unknown};
    constexpr bool operator==(const CastAttr&) const = default;
};

struct DelayAttr {
    std::int32_t samples{0}; // how far back this reads
    constexpr bool operator==(const DelayAttr&) const = default;
};

// A lag whose distance is a SIGNAL rather than a number: the operation's second operand carries how
// far back to read, in samples, and it may be different at every sample and fractional in between.
// What lives here is the only part of it that is fixed — the ceiling the distance is clamped to.
//
// It is a real ceiling and not a hint. The read position is data, so nothing bounds it in advance;
// clamping is what keeps a delay line from reading off the front of a block because a signal went
// somewhere unexpected. A default of zero makes an attribute nobody filled a tap at the present
// sample rather than a wild read.
//
// It has no axis: a tap at a data-dependent position only makes sense along the streamed axis.
struct SignalledDelayAttr {
    std::int32_t max_delay{0}; // the furthest back the read position may reach, in samples
    constexpr bool operator==(const SignalledDelayAttr&) const = default;
};

struct RecurrenceAttr {
    std::int32_t defines{kNoValue}; // the variable this expression is the definition of
    constexpr bool operator==(const RecurrenceAttr&) const = default;
};

struct ReduceAttr {
    ReduceOp op{ReduceOp::Sum};
    Axes axes{};          // the axes that collapse
    bool keep_axes{false}; // keep them at extent 1 instead of dropping them, so the result broadcasts back
    constexpr bool operator==(const ReduceAttr&) const = default;
};

// One bit per label of a regroup's spec, which names at most `kMaxRank` (`notation/rearrange.hpp`). A byte at the
// default rank, so the two masks of `ViewAttr` sit beside `elements_only` and a node does not grow.
using LabelMask = std::conditional_t<(kMaxRank <= 8), std::uint8_t, std::uint32_t>;

struct ViewAttr {
    ViewKind kind{ViewKind::Reshape};
    // The view names the ELEMENT axes only, as a member writes it: a reshape's `shape` without the operand's
    // last extent (`x.reshape({…})`), a transpose's `order` without the last axis (`x.transpose({1, 0})`). The
    // analysis reads it completed while shapes settle and then writes the full target or order back and clears
    // this, once (`sema/time_checks.hpp`); an analysed graph has none set.
    bool elements_only{false};
    // A reshape is a regroup as `rearrange` wrote it (`"(c f) -> c f"`), its target still to be worked out, while
    // these are set: bit k marks where label k starts a group, on the operand's side and on the result's, and
    // `shape` holds each label's size where a split names it, `no_extent()` where the operand decides it. The
    // analysis works the target out once the operand has its shape, writes it into `shape` and clears both, as it
    // clears `elements_only` (`sema/view_shape.hpp`); an analysed graph has none set.
    LabelMask operand_groups{0};
    LabelMask result_groups{0};
    Shape shape{};             // Reshape: the extents to read the same elements with
    std::int32_t axis{0};      // Squeeze / Unsqueeze: which axis disappears or appears
    // Transpose: output axis k reads source axis order[k], over the full rank, streamed axis
    // included. The analysis pass checks that it is a permutation of `[0, rank)`; that the streamed
    // axis maps to itself is a separate refusal with its own message, because it is a different mistake.
    Axes order{};
    // Slice: a strided sub-array of one axis — `axis` above is the one it shortens.
    // The output extent along that axis is `length`, a literal the slice is given rather than one
    // derived by arithmetic — which is why `_slice` needs no derived-extent algebra, unlike the
    // framing operations whose frame count is `(L-K)/H+1`. `step >= 1`: a negative stride is a
    // reversal, excluded here with `_flip` as the workaround (`docs/limitations.md`).
    std::int32_t offset{0};
    std::int32_t length{0};
    std::int32_t step{1};
    // Unfold: a sliding window over `axis` (an OUTER axis — the streamed axis is time and stays whole,
    // the same restriction `_slice` makes). That axis of extent L becomes the frame count
    // `N_out = (L - window)/hop + 1`, and a window axis of `window` elements appears at `axis + 1`.
    // `window >= 1`, `hop >= 1`.
    std::int32_t window{0};
    std::int32_t hop{1};
    // Unfold over a symbolic axis: a source axis whose extent L is bound later has no literal frame
    // count to write into the shape, so the author declares `N_out` as a symbol and names its id here;
    // the analysis pass records `N_out = (1*L + (hop - window))/hop` with the solver's `relate()`. `-1`
    // is "none": a literal source frames to a literal count. Not hashed, like `window` and `hop`.
    std::int32_t frames{-1};
    // Unfold2d windows a second outer axis at the same time. `axis`/`window`/`hop` above carry the
    // first windowed axis; these carry the second, which must be further out (`axis2 > axis`). Each is
    // framed exactly as `Unfold` frames its one. A single `Unfold` leaves all four at their defaults.
    // Not hashed, like `window`/`hop`/`frames`.
    std::int32_t axis2{0};
    std::int32_t window2{0};
    std::int32_t hop2{1};
    // The second axis's derived frame count, the twin of `frames`: when `axis2` is bound later, the
    // author declares its frame count here and the pass records it from `hop2` and `window2` the same way.
    // `-1` is "none". The two are independent: a 2-D unfold may mix a literal axis with a symbolic one.
    std::int32_t frames2{-1};
    // Pad (`_padc`): a constant pad of one outer axis. `axis` above is the axis it grows — its extent L
    // becomes `L + pad_before + pad_after` — and the margins read `pad_bits` as a `double`. A multi-axis
    // pad is two nodes. The fill is stored as its IEEE-754 bit pattern so that it compares bit for bit
    // (a NaN fill, a -0.0); the default 0 is +0.0, the common zero pad. Not hashed: the graph hash
    // (`tests/conservation/graph_hash.hpp`) mixes only a view's kind, shape and axis, and a regroup's groups while set.
    std::int32_t pad_before{0};
    std::int32_t pad_after{0};
    std::uint64_t pad_bits{0};
    constexpr bool operator==(const ViewAttr&) const = default;
};

// Which axes of the two operands' joined shape a contraction sums over: those an index appears in on
// both operands and not in the output. Naming them directly covers the elementwise case; a batched or
// transposing contraction is `_matmul`'s (`MatMulAttr` below).
struct ContractAttr {
    Axes axes{}; // the axes of the operands' joined shape that collapse into the sum
    constexpr bool operator==(const ContractAttr&) const = default;
};

// What a general contraction (`_matmul`) carries. `_contr` names axes of one joined shape — its two
// operands already line up elementwise, so a summed axis is the same axis in both. A matmul binds a
// different axis of each operand: `out[b,i,j] = Σ_k a[b,i,k]·b[b,k,j]` reads `a` at `(b,i,k)` and `b`
// at `(b,k,j)`, and there are batch axes that are iterated (present in a, b and the output), not summed.
//
// One contracted axis plus shared batch axes, not a general n-ary einsum. The operands' free axes are
// derived (every element axis that is neither the contracted one, a batch axis, nor the streamed
// innermost one), so they are not carried. The shape rule refuses a contraction that names the streamed axis.
struct MatMulAttr {
    std::int32_t a_contract{0};  // the axis of operand a that is summed
    std::int32_t b_contract{0};  // the axis of operand b that is summed; its extent must equal a's
    Axes batch_a{};              // operand a's batch axes, paired position-for-position with batch_b
    Axes batch_b{};              // operand b's batch axes; each shares its extent with batch_a's twin
    constexpr bool operator==(const MatMulAttr&) const = default;
};

// Which way a real FFT runs. `Forward` is the R2C transform (real `[.., N]` -> planar half-spectrum
// `[.., 2*(N/2+1)]`); `Inverse` is the C2R transform, the reverse. Only real transforms exist, so the
// direction is the only choice.
enum class FftDir : std::uint8_t { Forward, Inverse };

// What an `_fft` carries besides its one operand: the REAL transform length `n` and the direction.
// `n` is the length of the real signal — the innermost extent is `n` for a forward transform and
// `2*(n/2+1)` for an inverse, and `fft_shape` (`sema/spectral.hpp`) holds the operand
// to it. No runner computes the transform yet: these two facts give the shape it imposes.
struct FftAttr {
    std::int32_t n{0}; // the real transform length; a power of two >= 2, checked at the pass
    FftDir dir{FftDir::Forward};
    constexpr bool operator==(const FftAttr&) const = default;
};

// What an `_ovadd` carries besides its one operand: the window and the hop of the framing it reverses.
// Where an unfold turns one axis of extent L into `(L-K)/H+1` frames of K taps, an overlap-add takes
// `[.., F, K]` back to `[.., out_len]` by placing frame f at offset `f*hop` and summing where windows
// overlap. The frame count F is the input's own axis, and `out_len = (F-1)*hop + window` is derived
// from it, so neither is carried here.
struct OverlapAddAttr {
    std::int32_t window{0}; // the tap count of each frame — the innermost element axis's extent
    std::int32_t hop{1};    // how far apart consecutive frames are placed along the output
    constexpr bool operator==(const OverlapAddAttr&) const = default;
};

// What an `_order` carries besides its one operand: which order statistic it is, the axis it runs along,
// and the selection its kind needs. Only the field its kind reads is meaningful — `k` for `TopK`, `q` for
// `Quantile` — and the tag makes two nodes carrying the same statistic compare equal without a hand-written
// comparison. The ordered EXTENT is not here: it is the axis's own size, read off the shape (and held to a
// literal by `order_shape`), so nothing can disagree with it. The axis is named
// against the FULL shape and must be an ELEMENT axis — never the streamed one, which is time.
struct OrderAttr {
    OrderOp op{OrderOp::Sort};
    std::int32_t axis{0}; // the ordered axis, against the full shape; an element axis (0 .. rank-2)
    std::int32_t k{1};    // TopK: how many of the largest to keep
    double q{0.0};        // Quantile: the quantile in [0, 1]
    constexpr bool operator==(const OrderAttr&) const = default;
};

// What a `_tblread` carries besides its two operands: the table RESOLUTION `N`. The table operand is
// an `[N]` buffer read as a cycle, the sample after the last the first, and the read is
// `table[frac(phase)*N]` linearly interpolated between the two bracketing samples. `N` lives here and
// not in the shape because the read INDEXES the table by a data-dependent position — the table's own
// axes never enter the iteration domain, so nothing derives `N` from a shape and the shape derives
// nothing from `N` (`wavetable` writes it from the table it is given). `N >= 1` and the table's
// innermost extent `== N` are held by the attribute check, not by this default. Whether the phase lies in one
// turn is the proof of ranges' to say, while compiling (`compile.hpp` `take_ranges`), never the graph's.
struct TableAttr {
    std::int32_t size{0}; // N: the table resolution, the samples the buffer holds
    bool phase_in_one_turn{false}; // in [0, 1] or a NaN at every precision, so its wrap is the identity
    constexpr bool operator==(const TableAttr&) const = default;
};

// What a generator (`_iota`, `_const`) carries INSTEAD of an operand: the whole shape and element type of
// the value it generates, and for a constant where its numbers start in the graph's pool. Nothing flows in, so nothing derives its shape (`ShapeRule::Declared` reads
// `shape` here) or its type (`TypeRule::Declared` reads `type`) — this is where they are stated, the
// same role `CastAttr::to` plays for the one other operation whose type is not derived. The declared
// extents must be known now (a generator has no source that could bind a symbol into them), which the
// attribute check refuses by name rather than this default implying it.
struct GeneratorAttr {
    Shape shape{};                        // the declared output shape; its outer axes are the generated elements
    ElemType type{ElemType::Unknown};     // the declared element type — not derived, there is no operand to derive it from
    std::int32_t first{-1};               // a constant's first number in `Graph::constants`; -1 for a ramp
    constexpr bool operator==(const GeneratorAttr&) const = default;
};

// The tag says which member is meaningful; every other one is at its default, so two nodes carrying
// the same attribute compare equal without anyone writing a comparison.
struct OpAttr {
    AttrKind kind{AttrKind::None};
    // The ranks of operands 0 and 1 a public spec named (`einsum`, `rearrange`, `reduce`), 0 where it named none:
    // the analysis checks them once shapes settle and then clears them (`sema/time_checks.hpp`), so an analysed
    // graph has none set and equals the one the lowering builds without them.
    std::int8_t named_ranks[2]{0, 0};
    CastAttr cast{};
    DelayAttr delay{};
    RecurrenceAttr recurrence{};
    ReduceAttr reduce{};
    ViewAttr view{};
    ContractAttr contract{};
    MatMulAttr matmul{};
    SignalledDelayAttr signalled_delay{};
    FftAttr fft{};
    OverlapAddAttr overlap{};
    OrderAttr order{};
    TableAttr table{};
    GeneratorAttr generator{};

    constexpr bool operator==(const OpAttr&) const = default;
};

// ---- nodes ------------------------------------------------------------------------------------------
struct Node {
    OpCode op{};
    std::int32_t inputs[kMaxNodeInputs]{kNoValue, kNoValue, kNoValue};
    std::int32_t output{kNoValue};
    OpAttr attr{};

    constexpr bool operator==(const Node&) const = default;
};

// ---- the graph ----------------------------------------------------------------------------------------
// A name the outside world uses for one of the graph's values.
struct NamedValue {
    Name name{};
    std::int32_t value{kNoValue};

    constexpr bool operator==(const NamedValue&) const = default;
};

// ---- runtime-resolved dimensions ------------------------------------------------------------------
// What the frontend declared about a streamed extent that is a symbol resolved not at compile time
// but at prepare() — the audio-plugin `prepareToPlay()` block length. The frontend records what the user
// stated, not a decision it made (the frontend-decides-nothing gate holds it to that). The analysis pass
// does not read them; only the prepare()-time resolver does, where it validates the size the symbol is
// bound to.

// The constraints a runtime-resolved dimension's size must satisfy when it is resolved.
struct DimConstraints {
    std::int32_t multiple_of{1}; // the resolved size must be a whole multiple of this (1 = no constraint)
    std::int32_t min{0};         // the resolved size must be at least this (0 = no floor)
    constexpr bool operator==(const DimConstraints&) const = default;
};

// One runtime-resolved dimension a graph declares: the symbol id an `Extent` encodes, and the
// constraints its size must satisfy. Deduplicated by id when a graph is finished; two declarations
// of one id that disagree on their constraints are a contradiction the frontend refuses.
struct DimDecl {
    std::int32_t id{kNoDimension};
    DimConstraints constraints{};
    constexpr bool operator==(const DimDecl&) const = default;
};

// ---- stated ranges ----------------------------------------------------------------------------------
// A range the graph states for one of its values (`ir/range.hpp`): a port's, from its declaration, a computed value's
// `promise` (`frontend/ranged.hpp`), or a fact, the range a composition's result always has (`abs`, `frac`).
// Recorded as written, like a `DimDecl`, and read by the proof of ranges (`ranges/proven.hpp`, Plan/26-value-ranges).
// A value may have a fact and a promise, in that order.
struct RangeMark {
    std::int32_t value{kNoValue};
    Range range{};
    bool promised{false}; // a promise, checked in debug where not proven; otherwise a declared range or a fact, trusted
    constexpr bool operator==(const RangeMark&) const = default;
};

// A port's range is its declaration's, which a debug build checks as the number comes in; a promise on one, which no
// check would read, is refused in these words by the frontend and, in a graph built by hand, by the proof.
inline constexpr std::string_view kPromiseOnAPort = "heinzel: a port's range goes in its declaration: Param p{\"p\", ge(0), le(1)}";

// The capacities the frontend counts as it finishes a graph (`capacity_passed`, `frontend/graph_builder.hpp`).
enum class Capacity : std::uint8_t { None, Values, Nodes, Inputs, Constants, Ranges };

struct Graph {
    fixed_vector<Value, kMaxValues> values{};
    fixed_vector<Node, kMaxNodes> nodes{};
    fixed_vector<NamedValue, kMaxIO> inputs{};
    fixed_vector<NamedValue, kMaxIO> outputs{};
    // The runtime-resolved dimensions this graph declares — one per symbolic streamed extent the
    // frontend recorded (a `Length`). Empty for a fully static graph; the graph hash
    // (`tests/conservation/graph_hash.hpp`) folds it in only when it is non-empty.
    fixed_vector<DimDecl, kMaxDimDecls> dims{};
    // The numbers of its constant tensors (`_const`), each the bits of a double rounded to its constant's element type,
    // so that a NaN's payload and -0 compare. A constant's are `numbers_per_sample` of them from its `first`.
    fixed_vector<std::uint64_t, kMaxConstants> constants{};
    // The ranges it states, in the order their values were built. Empty for a graph that states none; the graph hash
    // folds it in only when it is not, as it does `dims`.
    fixed_vector<RangeMark, kMaxRanges> ranges{};

    // The precision a value resolves to when no `.precision()` mark reaches it. Select-higher cannot
    // spread `Fast` (a join never goes below where it starts), so "this whole graph may be approximate"
    // — or "this whole graph is bit-exact" — is said here once rather than on every value.
    numerics::Precision default_precision{numerics::Precision::Normal};

    // The default the precisions were last resolved under; `Inherit` until the graph is analysed.
    // Analysis writes resolved levels where pins are read, so a default set afterwards could reach
    // nothing — this is what lets analysis refuse that instead of ignoring it.
    numerics::Precision precision_resolved_under{numerics::Precision::Inherit};

    // `None`, or the first capacity the frontend's graph would pass, and then the graph holds nothing else. Finishing
    // counts before it emits, so that a graph function over a capacity returns this rather than stopping, and `infer`
    // refuses it first, in that capacity's words (`words_of`): one error (Decision 77). Which capacity, not its words: a
    // `string_view` would make the graph no structural type. The graph hash folds it in only when it is set.
    Capacity over{Capacity::None};

    constexpr bool operator==(const Graph&) const = default;
};

// A graph past a capacity is refused by name, with the override that raises it: where the frontend finishes it
// (`Graph::over`), and where a value or a node is added to it (`push_value`, …: `gradients`, a graph built by hand).
inline constexpr std::string_view kOverValues = "heinzel: a graph has more values than kMaxValues; rebuild with -DHEINZEL_MAX_VALUES=N";
inline constexpr std::string_view kOverNodes = "heinzel: a graph has more nodes than kMaxNodes; rebuild with -DHEINZEL_MAX_NODES=N";
inline constexpr std::string_view kOverInputs = "heinzel: a graph has more inputs than kMaxIO; rebuild with -DHEINZEL_MAX_IO=N";
inline constexpr std::string_view kOverConstants =
    "heinzel: a graph has more constants than kMaxConstants; rebuild with -DHEINZEL_MAX_CONSTANTS=N";
inline constexpr std::string_view kOverRanges = "heinzel: a graph has more ranges than kMaxRanges; rebuild with -DHEINZEL_MAX_RANGES=N";

// A capacity's words: the literal itself, so that a `fail` with them shows them in clang's note.
constexpr std::string_view words_of(Capacity c) {
    const std::string_view words[] = {{}, kOverValues, kOverNodes, kOverInputs, kOverConstants, kOverRanges};
    return words[static_cast<std::size_t>(c)];
}

constexpr std::int32_t push_value(Graph& g, const Value& v) {
    if (g.values.size() == kMaxValues) {
        fail(kOverValues);
    }
    g.values.push_back(v);
    return static_cast<std::int32_t>(g.values.size() - 1);
}
constexpr void push_constant(Graph& g, double number) {
    if (g.constants.size() == kMaxConstants) {
        fail(kOverConstants);
    }
    g.constants.push_back(std::bit_cast<std::uint64_t>(number));
}
constexpr void push_node(Graph& g, const Node& n) {
    if (g.nodes.size() == kMaxNodes) {
        fail(kOverNodes);
    }
    g.nodes.push_back(n);
}
constexpr void push_range(Graph& g, const RangeMark& m) {
    if (g.ranges.size() == kMaxRanges) {
        fail(kOverRanges);
    }
    g.ranges.push_back(m);
}

// A graph that contains no symbolic extent anywhere: everything about its sizes is known now.
constexpr bool is_static(const Graph& g) {
    for (const Value& v : g.values) {
        if (!is_static(v.shape)) {
            return false;
        }
    }
    return true;
}

// Is value `v` a constant: a number known now, held nowhere, read as an immediate?
constexpr bool is_a_constant(const Graph& g, std::int32_t v) {
    return g.values[static_cast<std::size_t>(v)].category == ValueCategory::Const;
}

// Is value `v` a result the caller reads? The graph's list of named outputs says so.
constexpr bool is_a_result(const Graph& g, std::int32_t v) {
    for (const NamedValue& out : g.outputs) {
        if (out.value == v) {
            return true;
        }
    }
    return false;
}

} // namespace heinzel
