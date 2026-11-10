#pragma once

// The vocabulary: the words every layer of this library spells its meaning in.
//
// Element types, shapes, the names of the operations, and the small tags that say what extra
// information an operation carries. Nothing here knows how anything is computed, scheduled or
// lowered — those are decisions made later by passes that READ this vocabulary and never redefine
// it. Keeping the alphabet in one place is what stops two layers from disagreeing about what an
// operation means.
//
// Everything here is a plain value with public members and a fixed size, so a graph can be built in
// a constant expression and kept: nothing allocated may escape constant evaluation.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>

#include "heinzel/ir/numeric_setting.hpp"
#include "heinzel/support/fail.hpp"
#include "heinzel/support/fixed_string.hpp"

namespace heinzel {

// ---- limits ------------------------------------------------------------------------------------

// How many axes a value may have. Six is what the operation set needs — batched image patches reach
// [N, C, H, W, Kh, Kw] — and raising it from four cost about 6% of compile time on the heaviest
// graphs, paid in the compiler and not at run time. Overridable for a source build rather than edited.
#ifndef HEINZEL_MAX_RANK
#define HEINZEL_MAX_RANK 6
#endif
inline constexpr std::size_t kMaxRank = HEINZEL_MAX_RANK;

// The longest name a declared input or output may have, its enclosing subgraphs included:
// "mixer.eq.filter.cutoff" is a realistic one.
inline constexpr std::size_t kMaxNameLength = 64;

// One name type for the whole library: the graph's named values and the frontend's declarations both
// use it.
using Name = fixed_string<kMaxNameLength>;

// ---- element types -----------------------------------------------------------------------------

// What kind of number a value holds. `Unknown` means "not worked out yet": a value the user did not
// annotate starts here and the analysis pass resolves it. No value that reaches the planner is still
// `Unknown` — that is checked, not hoped for.
enum class ElemType : std::uint8_t { Unknown, F32, F64 };

constexpr std::size_t elem_size(ElemType t) {
    switch (t) {
    case ElemType::F32:
        return 4;
    case ElemType::F64:
        return 8;
    case ElemType::Unknown:
        fail("heinzel: elem_size(Unknown) — an unresolved element type reached code generation");
    }
    return 4;
}

// ---- shapes ------------------------------------------------------------------------------------
//
// An axis extent is one number, and it says one of two things: a literal size, or "this size is a
// symbol whose value is supplied later". Both live in the same integer slot, with symbols encoded
// as negative values, so a shape stays a small fixed array of ints.
//
//   e >= 0  a literal extent
//   e == -1 reserved, never a stored extent: it is the marker meaning "work this axis out for me"
//   e <= -2 a symbol, whose id is -e - 2
//

constexpr bool is_symbol(std::int32_t e) { return e <= -2; }
constexpr std::int32_t symbol_of(std::int32_t e) { return -e - 2; }
constexpr std::int32_t encode_symbol(std::int32_t id) { return -(id + 2); }

// A handle to a declared symbolic dimension. Deliberately not convertible to int and offering no
// arithmetic, so `{N * 2, 64}` does not compile — it would otherwise quietly do arithmetic on the
// ENCODING. A derived extent is something the library computes, never something a user multiplies.
struct Dim {
    std::int32_t id;
};

// A symbol's id, checked where it is encoded: a negative one would encode to a literal extent (`Dim{-3}` would be a
// size of 1), and one at the top of the range would overflow.
constexpr std::int32_t symbol_id(Dim d) {
    if (d.id < 0 || d.id > INT32_MAX - 2) {
        fail("heinzel: a Dim's id is out of range");
    }
    return d.id;
}

// One entry in a shape literal. Constructible from both an int and a Dim, so `{256, 64}` and
// `{N, 64}` both simply work, and nothing else does.
struct Extent {
    std::int32_t encoded;

    // A default-constructed extent is "nothing said about this axis", spelled with the encoding's
    // own reserved -1. It is deliberately NOT a literal zero: the same reason `ElemType{}` is
    // `Unknown` rather than `F32`, so an absence reads as an absence. See `no_extent()` below.
    constexpr Extent() : encoded(-1) {}
    constexpr Extent(std::int32_t literal) : encoded(literal) {}
    constexpr Extent(Dim d) : encoded(encode_symbol(symbol_id(d))) {}

    constexpr bool operator==(const Extent&) const = default;
};

// A shape, innermost axis last. Rank 0 is a scalar; the rank is deduced from the brace list, so
// `Shape{}` is a scalar and `Shape{H, W}` has two axes.
struct Shape {
    std::int32_t dims[kMaxRank]{};
    std::int32_t rank{0};

    constexpr Shape() = default;

    constexpr Shape(std::initializer_list<Extent> ds) {
        if (ds.size() > kMaxRank) {
            fail("heinzel::Shape: more axes than kMaxRank; rebuild with -DHEINZEL_MAX_RANK=N");
        }
        rank = static_cast<std::int32_t>(ds.size());
        std::int32_t k = 0;
        for (Extent e : ds) {
            dims[k++] = e.encoded;
        }
    }

    constexpr bool operator==(const Shape&) const = default;
};

// ---- reading an extent, and the only place it is a plain integer -------------------------------
//
// Below this line an extent is an `Extent`, never an `std::int32_t`. The encoding — negative means
// symbol, non-negative means literal — is a fact of THIS header and of nowhere else, and every
// other file in the tree asks these helpers instead of knowing it.
//
// A raw read is invisible until the first symbolic value reaches it: `e == 1` is correct only because
// a symbol happens to encode negative, and `e * 2` is silently wrong the moment `e` is one. Neither is
// a compile error, so a gate (`gate_extents_are_not_plain_integers`) checks this property.

// The one place a stored extent becomes an `Extent`. Named rather than done with the constructor
// because `Extent`'s constructor takes a LITERAL size — handing it an encoded value would
// reinterpret a symbol as an enormous negative extent, quietly.
constexpr Extent extent_from_encoded(std::int32_t encoded) {
    Extent e{0};
    e.encoded = encoded;
    return e;
}

// An extent that is a known size. The spelling to use when writing one, so `1` never appears as a
// bare integer standing for an extent.
constexpr Extent literal_extent(std::int32_t size) { return Extent{size}; }

// "Nothing said about this axis." The encoding reserves -1 for exactly this — it is never a stored
// extent — so an absence can be marked without colliding with a size or with a symbol.
constexpr Extent no_extent() { return extent_from_encoded(-1); }
constexpr bool is_no_extent(Extent e) { return e.encoded == -1; }

constexpr bool is_symbol(Extent e) { return is_symbol(e.encoded); }
constexpr bool is_literal(Extent e) { return !is_symbol(e.encoded); }
constexpr std::int32_t symbol_of(Extent e) { return symbol_of(e.encoded); }

// The size, for an extent that has one. Asking a symbol for its size is a mistake in the caller
// rather than a case to handle, so it stops here instead of returning something plausible.
constexpr std::int32_t literal_of(Extent e) {
    if (is_symbol(e)) {
        fail("heinzel::literal_of: this extent is a symbol resolved later, so it has no size yet");
    }
    return e.encoded;
}

// Two extents that are the same extent — the same size, or the same symbol. NOT "the same number":
// two different symbols may later turn out to name one size, and deciding that is the solver's job.
constexpr bool same_extent(Extent a, Extent b) { return a.encoded == b.encoded; }

constexpr Extent extent_at(const Shape& s, std::int32_t axis) { return extent_from_encoded(s.dims[axis]); }

constexpr void set_extent(Shape& s, std::int32_t axis, Extent e) { s.dims[axis] = e.encoded; }

// A shape with no symbolic axis in it — everything about it is known now.
constexpr bool is_static(const Shape& s) {
    for (std::int32_t k = 0; k < s.rank; ++k) {
        if (is_symbol(s.dims[k])) {
            return false;
        }
    }
    return true;
}

// ---- how many numbers one sample of a value is ---------------------------------------------------
//
// The innermost axis (written last) is the streamed one, and every other axis is width: a run computes
// one number per value per sample along it. So a value of `Shape{64}` is one number per sample over a
// block of 64, not 64 numbers, and `Shape{3, 64}` is three. Multiplying every extent together would be
// the plausible wrong answer. A rank-0 value is one number.
//
// `kWidthNotKnown` when an axis outside the streamed one has no size yet: a symbol resolved later, or
// an extent nobody stated. That is a real state, not an error; a check that needs the width refuses it
// by name.
inline constexpr std::int32_t kWidthNotKnown = -1;

constexpr std::int32_t numbers_per_sample(const Shape& s) {
    std::int64_t width = 1;
    for (std::int32_t k = 0; k + 1 < s.rank; ++k) {
        const Extent e = extent_at(s, k);
        if (is_symbol(e) || is_no_extent(e)) {
            return kWidthNotKnown;
        }
        width *= literal_of(e);
        // Not a chosen ceiling but the edge of the result type. Five axes multiply, so leaving the
        // range is reachable, and an overflow would be a plausible small width someone sizes storage from.
        if (width > 2147483647) {
            fail("heinzel::numbers_per_sample: more numbers per sample than an int32 holds");
        }
    }
    return static_cast<std::int32_t>(width);
}

// A set of axes, for the operations that name some of a value's axes rather than all of them.
struct Axes {
    std::int32_t axes[kMaxRank]{};
    std::int32_t rank{0};

    constexpr Axes() = default;

    constexpr Axes(std::initializer_list<std::int32_t> a) {
        if (a.size() > kMaxRank) {
            fail("heinzel::Axes: more axes than kMaxRank");
        }
        rank = static_cast<std::int32_t>(a.size());
        std::int32_t k = 0;
        for (std::int32_t v : a) {
            axes[k++] = v;
        }
    }

    constexpr bool operator==(const Axes&) const = default;
};

// ---- the operations ----------------------------------------------------------------------------
//
// An operation is identified two ways, and the split is not redundant. Inside a node it is a single
// byte, because a graph is a template argument and every byte of one shows up in a mangled symbol
// name. At the surface where humans read dumps it is a short spelled-out name. The byte indexes the
// rule table, and the two are kept in step by an assertion rather than by care.

// A short name packed into one integer, so it can live inside a compile-time value. Character 0 is
// the low byte.
struct OpName {
    std::uint64_t bits{};
    constexpr OpName() = default;

    template <std::size_t N>
    constexpr OpName(const char (&s)[N]) { // N counts the trailing '\0'
        static_assert(N <= 9, "heinzel::OpName: an operation name is at most 8 characters");
        for (std::size_t i = 0; i + 1 < N; ++i) {
            bits |= std::uint64_t(static_cast<unsigned char>(s[i])) << (8 * i);
        }
    }

    constexpr char operator[](std::size_t i) const { return static_cast<char>((bits >> (8 * i)) & 0xFF); }

    constexpr bool operator==(const OpName&) const = default;
};

// An operation's name, written into a message.
constexpr void write_op_name(MessageWriter& out, OpName n) {
    char text[9]{};
    std::size_t k = 0;
    for (; k < 8 && n[k] != '\0'; ++k) {
        text[k] = n[k];
    }
    out << std::string_view{text, k};
}

// An extent written into a message: its literal size, or `?id` for a symbol.
constexpr void write_extent(MessageWriter& out, Extent e) {
    if (is_symbol(e)) {
        out << std::string_view{"?"} << symbol_of(e);
    } else {
        out << literal_of(e);
    }
}

// A shape written into a message as [4, 8], so a complaint can name what actually disagreed.
constexpr void write_shape(MessageWriter& out, const Shape& s) {
    out << std::string_view{"["};
    for (std::int32_t k = 0; k < s.rank; ++k) {
        if (k != 0) {
            out << std::string_view{", "};
        }
        write_extent(out, extent_at(s, k));
    }
    out << std::string_view{"]"};
}

// The identity a node actually stores.
struct OpCode {
    std::uint8_t index{};
    constexpr bool operator==(const OpCode&) const = default;
};

// The operations. Each new one needs a row in the rule table, and the build refuses to proceed without
// it. A code indexes the rule table and the recorded conservation hash, so new codes are appended, never
// inserted: renumbering would move every hash for no gain. An operation folded into another retires its
// code, which stays unused (its row is empty, `is_retired`).
namespace op {
inline constexpr OpCode Add{0};
inline constexpr OpCode Sub{1};
inline constexpr OpCode Mul{2};
inline constexpr OpCode Div{3};
inline constexpr OpCode Neg{4};
inline constexpr OpCode Sqrt{5};
inline constexpr OpCode Cast{6};
inline constexpr OpCode Lt{7};
inline constexpr OpCode Select{8};
inline constexpr OpCode FixedDelay{9};
inline constexpr OpCode Recurrence{10};
inline constexpr OpCode Reduce{11};
inline constexpr OpCode Reshape{12};
inline constexpr OpCode Min{13};
inline constexpr OpCode Max{14};
inline constexpr OpCode Le{15};
inline constexpr OpCode Eq{16};
inline constexpr OpCode Sign{17};
inline constexpr OpCode Floor{18};
inline constexpr OpCode Ceil{19};
inline constexpr OpCode Round{20};
inline constexpr OpCode Trunc{21};
inline constexpr OpCode Fma{22};
inline constexpr OpCode Rsqrt{23};
inline constexpr OpCode Exp{24};
inline constexpr OpCode Log{25};
inline constexpr OpCode Exp2{26};
inline constexpr OpCode Log2{27};
inline constexpr OpCode Log10{28};
inline constexpr OpCode Sin{29};
inline constexpr OpCode Cos{30};
inline constexpr OpCode Tan{31};
inline constexpr OpCode SinPi{32};
inline constexpr OpCode Atan{33};
inline constexpr OpCode Asin{34};
inline constexpr OpCode Acos{35};
inline constexpr OpCode Atan2{36};
inline constexpr OpCode Tanh{37};
inline constexpr OpCode Pow{38};
inline constexpr OpCode Contract{39};
// 40 is retired: `_delay` named the axis its lag ran along, and only the innermost was allowed, so it
// was `_fdelay` written another way.
inline constexpr OpCode DelayLine{41};
inline constexpr OpCode Transpose{42};
inline constexpr OpCode Slice{43};
inline constexpr OpCode Unfold{44};
inline constexpr OpCode Unfold2d{45};
inline constexpr OpCode Flip{46};
inline constexpr OpCode Pad{47};
inline constexpr OpCode Concat{48};
// `_fft` is an intrinsic boundary: its shape (innermost `N -> 2*(N/2+1)` forward, and the inverse)
// and its literal-length check are modelled, but no runner computes the transform yet.
inline constexpr OpCode Fft{49};
// 50 is retired: `_pack` was `_concat` restricted to identical operands.
// `_ovadd` is the scatter-add that reassembles a windowed signal, the adjoint of `_unfold`. Its two
// innermost element axes `[.., F, K]` (F frames of K taps) collapse into one `[.., out_len]` with
// `out_len = (F-1)*hop + window`, each output the sum of the overlapping frames' taps. A symbolic
// frame count resolves to a literal the same way `_unfold`'s does.
inline constexpr OpCode OverlapAdd{51};
// `_order` is order statistics along one static axis — sort, top-k or a quantile — computed by a
// fixed compare-exchange network (`support/order_net.hpp`), not a fold, so every run selects the same
// values. Its ordered axis is kept, resized or dropped, which no view expresses.
inline constexpr OpCode Order{52};
// `_realize` is a materialisation barrier: it forces its operand into its own contiguous buffer.
// Value for value it is the identity, so it is per-sample and its shape is its operand's.
inline constexpr OpCode Realize{53};
// `_tblread` is a linearly interpolated wavetable read, `out[e] = table[frac(phase[e])*N]`, blended
// between the two bracketing samples of an `[N]` buffer read as a cycle (after the last sample, the
// first). Its result has the phase's shape; the table is indexed by a
// data-dependent position, so it has no per-sample meaning (`runtime/table_gather_element.hpp`).
inline constexpr OpCode TableRead{54};
// `_iota` is an index ramp, a generator (no operand): `out[e] = e`, the element's own index, the same
// at every sample. Its shape and type are declared in `GeneratorAttr`. Time is the streamed axis,
// outside the element loop, so an iota is a ramp over elements, not a sample counter.
inline constexpr OpCode Iota{55};
// `_matmul` is the general contraction — transposing, strided or batched — that `_contr` refuses:
// `"ik,kj->ij"` reads `a` at `(i,k)` and `b` at `(k,j)`, index maps that are not one shared join, with
// an inner sum over an extent that is not an output axis. A new operation rather than a flag on
// `_contr`, so `_contr`'s meaning stays fixed. One contracted axis plus shared batch axes, never a
// general n-ary einsum. The streamed axis passes through unchanged; a contraction may never name it.
inline constexpr OpCode MatMul{56};
// `_const` is a constant tensor, the other generator: `out[e]` is the graph's pool at `first + e`
// (`Graph::constants`, `GeneratorAttr::first`), the same at every sample. Declared as `_iota` is.
inline constexpr OpCode Constant{57};
// `_madd` is `a * b + c` rounded as the machine rounds: once where the build has the instruction, twice where
// it does not. `_fma` promises one rounding and pays for it in software where it must; this one never does.
// The DSL does not spell it: the rewrite layer emits it where a value's precision grants reordering.
inline constexpr OpCode MultiplyAdd{58};
// `_abs` is |x|: the sign bit cleared, so `abs(+0)` and `abs(-0)` are `+0` and a NaN stays a NaN. Spelled `abs`; it
// was `max(x, -x)`, which is no `fabs` (it answers `-0` for `+0`), so the compiler kept the compare and the blend.
inline constexpr OpCode Abs{59};
// `_chain` is a link: `u = _chain(x, V)` is `x` at element 0 of axis 0 and `V`'s element j - 1 at element j, both at
// this sample. With `V` the states of k like sections over `[k, time]`, section j reads section j - 1's present output,
// so a cascade runs as one cycle in lanes. The DSL does not spell it: the rewrite layer emits it where it merges a
// cascade (`docs/sections-in-lanes.md`).
inline constexpr OpCode Chain{60};
// `_first` is 1 at a run's first sample and 0 at every other, a run being one call of a program over its block: where
// the run stands, read by nothing. The DSL does not spell it: `dsp::smoothed` reads it, to restart a ramp where a knob
// may have changed (Plan/33 S3).
inline constexpr OpCode First{61};
} // namespace op

// ---- what extra information an operation carries -----------------------------------------------
//
// Most operations need nothing beyond their operands: an add is an add. Some need one more fact that is
// not a value flowing in — how far back a delay reaches, which axes a reduction collapses. That fact is
// a typed attribute, and this names which kind an operation carries.
//
// There are two lags. `Delay` (`_fdelay`) reads a constant number of samples back along the streamed axis.
// `SignalledDelay` (`_dline`) takes its distance from a signal that changes every sample; the attribute
// holds only the ceiling that distance is clamped to. `Fft` holds the transform's length and
// direction; `OverlapAdd` its window and hop (the frame count is shape, derived); `Order` the statistic,
// the axis and the selection. `Iota` and `Constant` hold a generator's whole declared shape and element
// type, since nothing flows in to derive them from, and a constant also where its numbers start in the
// graph's pool (both in `GeneratorAttr`). `MatMul` holds the general contraction's binding: which axis of
// each operand is summed and which axes are batched.
enum class AttrKind : std::uint8_t {
    // 7 was `AxisDelay`, retired with `_delay`; the kinds after it keep their numbers (the conservation hash), so a
    // new kind goes last.
    None, Cast, Delay, Recurrence, Reduce, View, Contract, SignalledDelay = 8, Fft, OverlapAdd, Order, Table, Iota, MatMul, Constant
};

// The innermost (streamed) axis, named without knowing the rank: an axis a fold may collapse
// (`loops/graph/kinds.hpp`).
inline constexpr std::int32_t kInnermostAxis = -1;

// Rearrangements all share one operation and differ by which rearrangement they are, so a rule cannot
// be looked up by operation alone: each kind has its own shape (`sema/view_shape.hpp` switches on it). A
// `ViewKind` is part of the recorded conservation hash, so new kinds are appended, never inserted.
enum class ViewKind : std::uint8_t {
    Reshape,   // the same elements, read with a different set of axis extents
    Squeeze,   // drop one axis whose extent is 1
    Unsqueeze, // insert one axis of extent 1
    Transpose, // the same elements, read with the axes in another order
    Slice,     // one axis becomes `length` elements read from `offset` at stride `step`
    Unfold,    // axis `ax` of extent L becomes N_out=(L-K)/H+1 frames, a window axis K appears at ax+1
    // Each of the two axes is framed as `Unfold` frames its one, so a window axis sits beside each
    // frame axis (interleaved) and the streamed axis stays innermost.
    Unfold2d,  // axes ax1<ax2 each become their frame count, with a window axis inserted beside each
    // The workaround the refusal of a negative-stride `_slice` names (`docs/limitations.md`).
    Flip,      // axis `ax` is read back-to-front: output element i reads source element (extent-1-i)
    // A margin element has no source element, so a pad's element meaning is a guarded read, not a copy
    // (`ElementKind::Guarded`, `tests/reference/compute_reference.hpp`).
    Pad,       // axis `ax` grows to L+before+after: body reads source (i-before), the margins a constant
    // All other axes equal, no broadcast (numpy's rule). The joined extent is a sum of two, outside the
    // derived-extent algebra, so a symbolic joined axis is refused (`sema/view_shape.hpp`).
    Concat,    // axis `ax` joins two operands: output i below `extent_a` reads a at i, at or above reads b
};

// How a reduction folds the axes it collapses.
enum class ReduceOp : std::uint8_t { Sum, Max, Min, Mean, Prod };

// Which order statistic an `_order` computes along its one static axis. Deliberately NOT a `ReduceOp`: a
// whole-axis permutation is not a monoid fold, and every one is realized by the SAME compare-exchange
// network (`support/order_net.hpp`), not `fold_step`. `median(x)` is frontend sugar for `Quantile{q=0.5}`,
// not a fourth enumerator.
enum class OrderOp : std::uint8_t {
    Sort,     // ascending, shape-preserving (descending = flip(sort(x)), and `_flip` already exists)
    TopK,     // the k LARGEST, sorted descending; the ordered axis extent becomes k (values only)
    Quantile, // a numpy-'linear' quantile at a compile-time q; the ordered axis DROPS (rank falls by one)
};

// Does the op KEEP the ordered axis (Sort keeps L, TopK keeps k) or collapse it (Quantile)? Every `_order`
// site routes its shape and store through this, never a hand `op == Sort || op == TopK` that would misroute.
constexpr bool order_keeps_axis(OrderOp op) { return op == OrderOp::Sort || op == OrderOp::TopK; }

} // namespace heinzel
