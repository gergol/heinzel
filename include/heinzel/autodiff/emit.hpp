#pragma once

// Appending adjoint nodes to a graph, as the ordinary operations a hand-built graph uses (`_mul`, `_add`,
// `_reduce`, `_reshape`, ...). Each helper pins the precision it is given, the primal node's resolved one,
// so a derivative runs on its primal's core: left to the resolve pass, a `Fast` `_exp`'s derivative would
// be pulled up to `Normal`. No helper reads a shape off a node it just made; `infer` runs once over the
// augmented graph at the end, and the rules decide from the primal shapes.

#include <bit>
#include <cstdint>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/vocab.hpp"

namespace heinzel::autodiff {

// One appended operation. The result value is created shapeless and typeless (infer resolves both);
// its precision is PINNED to the primal's resolved precision so its arithmetic runs on the primal's core.
constexpr std::int32_t emit(Graph& g, OpCode op, ::heinzel::numerics::Precision precision, std::int32_t a,
                            std::int32_t b = kNoValue, std::int32_t c = kNoValue, OpAttr attr = OpAttr{}) {
    const std::int32_t out = push_value(g, Value{ElemType::Unknown, Shape{}, ValueCategory::Internal, 0.0});
    Node n{};
    n.op = op;
    n.inputs[0] = a;
    n.inputs[1] = b;
    n.inputs[2] = c;
    n.output = out;
    n.attr = attr;
    push_node(g, n);
    g.values[static_cast<std::size_t>(out)].precision = precision;
    return out;
}

// A scalar constant the adjoint arithmetic needs — the ½ in `d(√x) = ½/√x`, the `ln 2` in `d(2^x)`.
// One number standing for every element of its value, so it broadcasts against anything. Its element
// type is stated, not left to infer: a `Const` is a leaf, so nothing flows into it to work its type
// out (`sema/analyze.hpp` refuses a value whose type is still Unknown). The caller passes the primal
// type the constant is combined with.
constexpr std::int32_t emit_const(Graph& g, double value, ElemType type) {
    return push_value(g, Value{type, Shape{}, ValueCategory::Const, value});
}

// A constant filled with ones, of a given shape and type. There is no broadcast view, so an expand to
// a wider shape is a `_mul` by a ones buffer of that shape. Its type is stated for the reason
// `emit_const`'s is.
constexpr std::int32_t emit_ones(Graph& g, const Shape& shape, ElemType type) {
    return push_value(g, Value{type, shape, ValueCategory::Const, 1.0});
}

// The arithmetic an adjoint rule composes, each one appended node, named so a rule reads as its formula.
constexpr std::int32_t emit_add(Graph& g, ::heinzel::numerics::Precision precision, std::int32_t a, std::int32_t b) {
    return emit(g, op::Add, precision, a, b);
}
constexpr std::int32_t emit_sub(Graph& g, ::heinzel::numerics::Precision precision, std::int32_t a, std::int32_t b) {
    return emit(g, op::Sub, precision, a, b);
}
constexpr std::int32_t emit_mul(Graph& g, ::heinzel::numerics::Precision precision, std::int32_t a, std::int32_t b) {
    return emit(g, op::Mul, precision, a, b);
}
constexpr std::int32_t emit_div(Graph& g, ::heinzel::numerics::Precision precision, std::int32_t a, std::int32_t b) {
    return emit(g, op::Div, precision, a, b);
}
constexpr std::int32_t emit_neg(Graph& g, ::heinzel::numerics::Precision precision, std::int32_t a) {
    return emit(g, op::Neg, precision, a);
}

// The mask alphabet the routing rules need: a comparison answering one-or-zero (`_lt` / `_eq`, in the
// operands' own type — this tree has no boolean type) and the choice it drives (`_select`, picking its
// second operand where the first is non-zero, its third where it is zero). A `Min`/`Max` adjoint sends
// each element's cotangent to whichever operand won; a reduce `Max`/`Min` sends it to every operand
// element equal to the result. These build the mask and the routing as ordinary nodes.
constexpr std::int32_t emit_lt(Graph& g, ::heinzel::numerics::Precision precision, std::int32_t a, std::int32_t b) {
    return emit(g, op::Lt, precision, a, b);
}
constexpr std::int32_t emit_eq(Graph& g, ::heinzel::numerics::Precision precision, std::int32_t a, std::int32_t b) {
    return emit(g, op::Eq, precision, a, b);
}
constexpr std::int32_t emit_select(Graph& g, ::heinzel::numerics::Precision precision, std::int32_t cond, std::int32_t a,
                                   std::int32_t b) {
    return emit(g, op::Select, precision, cond, a, b);
}

// numpy unbroadcast: reduce a contribution whose shape is `from` down to the operand's shape `to`.
// An elementwise result broadcasts its operands up, so a raw contribution (the primal OUTPUT's shape)
// has to be summed back over the axes the operand did not have — the leading axes it gained, and the
// axes it held at extent 1 while the result was wider. Two `_reduce(Sum)` at most: one dropping the
// leading axes, one keeping the extent-1 axes at one. The result then equals `to`, the invariant every
// accumulation relies on. When the shapes already match, nothing is emitted.
constexpr std::int32_t unbroadcast(Graph& g, ::heinzel::numerics::Precision precision, std::int32_t contrib, const Shape& from,
                                   const Shape& to) {
    if (from == to) {
        return contrib;
    }
    std::int32_t t = contrib;
    const std::int32_t lead = from.rank - to.rank;
    if (lead > 0) {
        Axes drop{};
        drop.rank = lead;
        for (std::int32_t k = 0; k < lead; ++k) {
            drop.axes[k] = k;
        }
        OpAttr a{};
        a.kind = AttrKind::Reduce;
        a.reduce = ReduceAttr{ReduceOp::Sum, drop, false};
        t = emit(g, op::Reduce, precision, t, kNoValue, kNoValue, a);
    }
    Axes keep{};
    for (std::int32_t k = 0; k < to.rank; ++k) {
        if (same_extent(extent_at(to, k), literal_extent(1)) &&
            !same_extent(extent_at(from, lead + k), literal_extent(1))) {
            keep.axes[keep.rank++] = k;
        }
    }
    if (keep.rank > 0) {
        OpAttr a{};
        a.kind = AttrKind::Reduce;
        a.reduce = ReduceAttr{ReduceOp::Sum, keep, true};
        t = emit(g, op::Reduce, precision, t, kNoValue, kNoValue, a);
    }
    return t;
}

// One reduced axis reinserted at extent 1 — the inverse of a `_reduce` that dropped it. A `_reshape`
// carrying `ViewKind::Unsqueeze` names the axis; `infer` derives the grown shape.
constexpr std::int32_t emit_unsqueeze(Graph& g, ::heinzel::numerics::Precision precision, std::int32_t v, std::int32_t axis) {
    OpAttr a{};
    a.kind = AttrKind::View;
    a.view.kind = ViewKind::Unsqueeze;
    a.view.axis = axis;
    return emit(g, op::Reshape, precision, v, kNoValue, kNoValue, a);
}

// The structural view constructors an adjoint composes. Each rearrangement's adjoint is another
// rearrangement (a transpose's is a transpose, a slice's a pad, a concat's two slices), so the
// adjoint pass needs the same views a hand-built graph uses. Each builds one view node: an operation
// code with its `ViewKind`. `infer` fills the result shape from the primal shapes these are given.

// One axis dropped — the inverse of an unsqueeze. `ViewKind::Squeeze` names the extent-1 axis.
constexpr std::int32_t emit_squeeze(Graph& g, ::heinzel::numerics::Precision precision, std::int32_t v, std::int32_t axis) {
    OpAttr a{};
    a.kind = AttrKind::View;
    a.view.kind = ViewKind::Squeeze;
    a.view.axis = axis;
    return emit(g, op::Reshape, precision, v, kNoValue, kNoValue, a);
}

// The same elements read with the axes in `order` — a transpose. The adjoint of a transpose is the
// transpose by the INVERSE permutation, which the caller computes; this only carries it.
constexpr std::int32_t emit_transpose(Graph& g, ::heinzel::numerics::Precision precision, std::int32_t v, const Axes& order) {
    OpAttr a{};
    a.kind = AttrKind::View;
    a.view.kind = ViewKind::Transpose;
    a.view.order = order;
    return emit(g, op::Transpose, precision, v, kNoValue, kNoValue, a);
}

// The same elements re-cut into `shape` — a reshape. A reshape's adjoint reshapes the cotangent back
// to the operand's full shape (element count and streamed axis preserved, which the inverse keeps).
constexpr std::int32_t emit_reshape_to(Graph& g, ::heinzel::numerics::Precision precision, std::int32_t v, const Shape& shape) {
    OpAttr a{};
    a.kind = AttrKind::View;
    a.view.kind = ViewKind::Reshape;
    a.view.shape = shape;
    return emit(g, op::Reshape, precision, v, kNoValue, kNoValue, a);
}

// A strided sub-array of ONE axis — the adjoint of a pad (slice the body out) and the two halves of a
// concat/pack adjoint. `step` is 1 for every adjoint use (a slice's own adjoint is a pad, not another
// slice), so no adjoint constructs a strided slice.
constexpr std::int32_t emit_slice(Graph& g, ::heinzel::numerics::Precision precision, std::int32_t v, std::int32_t axis,
                                  std::int32_t offset, std::int32_t length, std::int32_t step) {
    OpAttr a{};
    a.kind = AttrKind::View;
    a.view.kind = ViewKind::Slice;
    a.view.axis = axis;
    a.view.offset = offset;
    a.view.length = length;
    a.view.step = step;
    return emit(g, op::Slice, precision, v, kNoValue, kNoValue, a);
}

// A constant pad of ONE axis — the adjoint of a slice (place the cotangent back into a zero buffer,
// the margins the sliced-away elements whose gradient is zero). The fill is stored as the constant's
// IEEE-754 bit pattern, the same encoding the forward `_padc` carries (`ir/ir.hpp`, `ViewAttr`).
constexpr std::int32_t emit_pad(Graph& g, ::heinzel::numerics::Precision precision, std::int32_t v, std::int32_t axis,
                                std::int32_t before, std::int32_t after, double fill) {
    OpAttr a{};
    a.kind = AttrKind::View;
    a.view.kind = ViewKind::Pad;
    a.view.axis = axis;
    a.view.pad_before = before;
    a.view.pad_after = after;
    a.view.pad_bits = std::bit_cast<std::uint64_t>(fill);
    return emit(g, op::Pad, precision, v, kNoValue, kNoValue, a);
}

// One axis read back-to-front — a flip. A flip is its own adjoint: reversing a reversed cotangent puts
// each element's contribution back where the forward read it from.
constexpr std::int32_t emit_flip(Graph& g, ::heinzel::numerics::Precision precision, std::int32_t v, std::int32_t axis) {
    OpAttr a{};
    a.kind = AttrKind::View;
    a.view.kind = ViewKind::Flip;
    a.view.axis = axis;
    return emit(g, op::Flip, precision, v, kNoValue, kNoValue, a);
}

// The framing pair, each the other's adjoint. An unfold slides a `window`-tap window along `axis` at
// `hop`, turning extent L into frames; an overlap-add scatters frames back, summing where windows
// overlap. `emit_unfold` re-frames a reconstructed axis (an overlap-add's adjoint); `emit_ovadd`
// reassembles frames (an unfold's adjoint). `infer` derives the frame count / reconstructed length from
// the primal shape these are given, so neither writes a size here.
constexpr std::int32_t emit_unfold(Graph& g, ::heinzel::numerics::Precision precision, std::int32_t v, std::int32_t axis,
                                   std::int32_t window, std::int32_t hop) {
    OpAttr a{};
    a.kind = AttrKind::View;
    a.view.kind = ViewKind::Unfold;
    a.view.axis = axis;
    a.view.window = window;
    a.view.hop = hop;
    return emit(g, op::Unfold, precision, v, kNoValue, kNoValue, a);
}
constexpr std::int32_t emit_ovadd(Graph& g, ::heinzel::numerics::Precision precision, std::int32_t v, std::int32_t window,
                                  std::int32_t hop) {
    OpAttr a{};
    a.kind = AttrKind::OverlapAdd;
    a.overlap.window = window;
    a.overlap.hop = hop;
    return emit(g, op::OverlapAdd, precision, v, kNoValue, kNoValue, a);
}

// Broadcast a reduction's cotangent back to the reduced operand's shape. A `_reduce` collapsed some
// axes; its adjoint must put a copy of the cotangent at every position those axes ranged over. First
// re-insert each reduced axis at extent 1 (unless `keep_axes` already held it there), lifting the
// cotangent to the operand's rank; then a `_mul` by a ones buffer of the operand's shape expands each
// extent-1 axis to its full extent, since there is no broadcast view. The reduced
// axes are named against the operand's full rank and reinserted ascending so each insertion's index
// still refers to the operand's axis.
constexpr std::int32_t bcast_back(Graph& g, ::heinzel::numerics::Precision precision, std::int32_t cotangent,
                                  const ReduceAttr& ra, const Shape& operand_shape, ElemType operand_type) {
    std::int32_t t = cotangent;
    if (!ra.keep_axes) {
        std::int32_t axs[kMaxRank]{};
        for (std::int32_t k = 0; k < ra.axes.rank; ++k) {
            const std::int32_t a = ra.axes.axes[k];
            axs[k] = a < 0 ? a + operand_shape.rank : a;
        }
        for (std::int32_t i = 0; i < ra.axes.rank; ++i) {
            for (std::int32_t j = i + 1; j < ra.axes.rank; ++j) {
                if (axs[j] < axs[i]) {
                    const std::int32_t tmp = axs[i];
                    axs[i] = axs[j];
                    axs[j] = tmp;
                }
            }
        }
        for (std::int32_t k = 0; k < ra.axes.rank; ++k) {
            t = emit_unsqueeze(g, precision, t, axs[k]);
        }
    }
    return emit(g, op::Mul, precision, t, emit_ones(g, operand_shape, operand_type));
}

} // namespace heinzel::autodiff
