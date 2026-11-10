#pragma once

// What one operation computes for the reference walk: one element at one sample, in double.
// The arithmetic shared with the loop tree is `runtime/compute.hpp`, called here rather than copied.
// What only the walk needs is here: a dispatch on `node.op` read at run time, every transcendental
// through its double core, conversions, lags, the tap blend, and which kind of element meaning an
// operation that changes axes has.

#include <cstdint>

#include "heinzel/ir/ir.hpp"
#include "heinzel/runtime/compute.hpp"
#include "heinzel/runtime/math_cores.hpp"
#include "heinzel/runtime/table_gather_element.hpp"
#include "heinzel/runtime/value_type.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel {

using Computed = ComputedAs<double>;

// The exponential family. Each is a core of its own (`math_cores.hpp`) because the standard library's
// version cannot be called in a constant expression. `fn` is the tier this node runs at, already
// resolved against its graph; each core spends it on how many terms of its series it evaluates, and the
// error bound for each tier is argued beside the series it bounds.
constexpr Computed exponential_of(OpCode op, FnAccuracy fn, double a) {
    if (op == op::Exp) {
        return Computed{exponential(a, fn), true};
    }
    if (op == op::Exp2) {
        return Computed{two_to_the_power(a, fn), true};
    }
    if (op == op::Log) {
        return Computed{natural_logarithm(a, fn), true};
    }
    if (op == op::Log2) {
        return Computed{logarithm_base_two(a, fn), true};
    }
    if (op == op::Log10) {
        return Computed{logarithm_base_ten(a, fn), true};
    }
    // `_tanh` belongs to this family: its core computes `e^2x - 1` directly rather than by
    // subtraction, so its accuracy is that of the exponential series at every tier.
    if (op == op::Tanh) {
        return Computed{hyperbolic_tangent(a, fn), true};
    }
    return Computed{};
}

// The periodic family. `_sinpi` is a separate operation rather than `_sin` of a scaled argument
// because its argument reduction is exact and the other three's is not (`periodic_cores.hpp`). The
// tier reaches four polynomials, a sine and a cosine for each of the two reductions, and they take
// different term counts at the same tier because their intervals differ.
constexpr Computed periodic_of(OpCode op, FnAccuracy fn, double a) {
    if (op == op::Sin) {
        return Computed{sine(a, fn), true};
    }
    if (op == op::Cos) {
        return Computed{cosine(a, fn), true};
    }
    if (op == op::Tan) {
        return Computed{tangent(a, fn), true};
    }
    if (op == op::SinPi) {
        return Computed{sine_of_half_turns(a, fn), true};
    }
    return Computed{};
}

// The inverse circular family: all four recover an angle from a ratio, so `_atan2` belongs here
// despite its two operands. One tier reaches all four because each forms a ratio and hands it to the
// same arc-tangent polynomial, which is also why the tier buys less here than elsewhere
// (`inverse_circular_cores.hpp`).
constexpr Computed inverse_circular_of(OpCode op, FnAccuracy fn, double a, double b) {
    if (op == op::Atan) {
        return Computed{arc_tangent(a, fn), true};
    }
    if (op == op::Asin) {
        return Computed{arc_sine(a, fn), true};
    }
    if (op == op::Acos) {
        return Computed{arc_cosine(a, fn), true};
    }
    if (op == op::Atan2) {
        return Computed{arc_tangent_of_ratio(a, b, fn), true};
    }
    return Computed{};
}

// `_pow`: a logarithm multiplied by an operand before an exponential undoes it, so the logarithm has to
// be carried to more bits than a double holds or the multiplication scatters them. `power_cores.hpp`
// works that argument through.
constexpr Computed power_of(OpCode op, FnAccuracy fn, double a, double b) {
    if (op == op::Pow) {
        return Computed{power(a, b, fn), true};
    }
    return Computed{};
}

// Conversion, and the operations that hand their operand back unchanged.
constexpr Computed conversion_or_timing_of(const Node& node, double a) {
    if (node.op == op::Cast) {
        // Narrowing really does lose bits, and a check for agreement with a compiled artifact would
        // be worthless if this quietly kept them.
        return Computed{as_type(node.attr.cast.to, a), true};
    }
    if (node.op == op::Realize) {
        // A materialisation barrier is the identity, value for value: whether it copies is a question
        // of storage, not of arithmetic.
        return Computed{a, true};
    }
    if (node.op == op::FixedDelay || node.op == op::Recurrence) {
        // Both hand back what they were given. A delay's operand was fetched from an earlier sample and a
        // recurrence's from this one, and that difference is entirely the caller's.
        return Computed{a, true};
    }
    return Computed{};
}

// The tap delay line, whose three arguments are not its three operands. Its distance is a signal that
// lands between two samples, so a read at that distance is two reads: the caller fetches the nearer tap,
// the further one and the fraction between them (`TimeRule::ReadsAtSignalledDepth`; `signalled_tap()` in
// `ir/timing.hpp` turns the position into them).
//
// The blend is linear, between `x[n-i0]` and `x[n-i0-1]` at `i0 = floor(clamp(pos))`. Written as
// `near + f*(far - near)` rather than `(1-f)*near + f*far`: the two differ in their last bits, and this
// one is exactly `near` when `f` is zero, so a whole-sample position agrees with a fixed delay of the
// same distance to the bit.
constexpr Computed tapped_of(OpCode op, double near_tap, double far_tap, double fraction) {
    if (op == op::DelayLine) {
        // `lerp2` is the blend `_tblread`'s gather shares (`runtime/table_gather_element.hpp`).
        return Computed{lerp2(near_tap, far_tap, fraction), true};
    }
    return Computed{};
}

constexpr Computed compute_if_written(const Node& node, FnAccuracy fn, double a, double b, double c) {
    if (const Computed r = arithmetic_of(node.op, a, b, c); r.written) {
        return r;
    }
    if (const Computed r = root_of(node.op, a); r.written) {
        return r;
    }
    if (const Computed r = rounding_of(node.op, a); r.written) {
        return r;
    }
    if (const Computed r = exponential_of(node.op, fn, a); r.written) {
        return r;
    }
    if (const Computed r = periodic_of(node.op, fn, a); r.written) {
        return r;
    }
    if (const Computed r = inverse_circular_of(node.op, fn, a, b); r.written) {
        return r;
    }
    if (const Computed r = power_of(node.op, fn, a, b); r.written) {
        return r;
    }
    if (const Computed r = comparison_of(node.op, a, b, c); r.written) {
        return r;
    }
    if (const Computed r = conversion_or_timing_of(node, a); r.written) {
        return r;
    }
    if (const Computed r = tapped_of(node.op, a, b, c); r.written) {
        return r;
    }
    // No meaning was written. Whether that is a mistake or the truth about this operation is not a
    // question for the arithmetic to answer — the rule table's meaning column answers it, and the
    // coverage check in `meaning_coverage.hpp` joins the two.
    return Computed{};
}

// The name the walk calls. An operation the table classifies `AxisChanging` has no sample-at-a-time
// meaning, so asking for one is the caller's mistake and stops here.
//
// `fn` is the resolved tier, not the node's request: `Inherit` means the graph's default, which this
// function does not have. Reading the tier off the node would give an unmarked node the exact cores
// while its graph asked for coarse ones, so the caller resolves it and `Inherit` is refused rather than
// treated as `Exact`. The refusal is here and not in `compute_if_written`, whose question (is a meaning
// written at all?) is the same at every tier and is asked by the coverage check with a tier of its own.
constexpr double compute(const Node& node, FnAccuracy fn, double a, double b, double c) {
    if (fn == FnAccuracy::Inherit) {
        // 87 of the 96 bytes a message may carry, so it arrives whole.
        fail("heinzel::compute: FnAccuracy::Inherit is not a tier; resolve the node against its graph");
    }
    const Computed result = compute_if_written(node, fn, a, b, c);
    if (!result.written) {
        fail("heinzel::compute: this operation rearranges or collapses axes, which a sample-at-a-time "
             "walk does not do");
    }
    return result.value;
}

// ---- what an operation that COLLAPSES axes means ------------------------------------------------
//
// `compute()` above answers for one element out of three numbers. A fold has no such answer: one
// element of its result is assembled out of many of its operand's. Which elements fold into one is
// `elements.hpp`; what a fold does with them (`fold_step`, `fold_finish`) is
// `runtime/fold_arithmetic.hpp`; which axes a node collapses (`collapse_of`) is `collapse_element.hpp`.

// What one contributor to a fold is worth. A reduction's contributor is one operand's element; a
// contraction's is the product of the two operands' elements, which is why its units rule is `Product`.
// Kept apart from `fold_step`, which says what is done with a run of contributors.
constexpr double contribution(const Node& n, double a, double b) {
    if (n.attr.kind == AttrKind::Reduce) {
        return a;
    }
    if (n.attr.kind == AttrKind::Contract) {
        return a * b;
    }
    fail("heinzel::contribution: this collapses axes and carries no attribute saying what one is");
}

// What the fold does, read off the same attribute so the walk names neither an operation nor an
// attribute case. A contraction's fold is a sum: what differs is its contributor (`contribution`), so
// the walk keeps one loop for both.
constexpr ReduceOp fold_of(const Node& n) {
    if (n.attr.kind == AttrKind::Reduce) {
        return n.attr.reduce.op;
    }
    if (n.attr.kind == AttrKind::Contract) {
        return ReduceOp::Sum;
    }
    fail("heinzel::fold_of: this collapses axes and carries no attribute saying what the fold is");
}

// The table resolution `N` of a `_tblread`. `N >= 1` is checked because a hand-built node can reach the
// walk without the analysis pass, and the table must hold a sample to read. That the operand holds `N`
// samples is the analysis pass's check.
constexpr std::int32_t table_size(const Node& n) {
    const std::int32_t size = n.attr.table.size;
    if (size < 1) {
        fail("heinzel: a table read's resolution N must be at least 1");
    }
    return size;
}

// ---- which KIND of element meaning an operation has ---------------------------------------------
//
// The walk needs this before it can loop: a fold assembles one result element out of many operand
// elements, a rearrangement out of exactly one. A walk that assumed every axis-changing operation folded
// would run a reshape as a fold of one contributor: a wrong number, not a refusal.
//
// The rule table's `MeaningRule` column says an operation has no sample-at-a-time meaning; this says
// which kind of meaning the walk has written. `None` is a row whose writing is missing, which
// `meaning_coverage.hpp` checks for. `Boundary` is a decision, not a gap: the element belongs to an
// intrinsic backend that does not exist yet (`_fft`), and the walk refuses it.
enum class ElementKind : std::uint8_t {
    None,      // the row says it changes axes and nothing here answers for it yet
    Fold,      // one element of the result is a fold over many contributors
    Rearrange, // one element of the result is one element of its operand, found somewhere else
    Guarded,   // one element of the result is one element of its operand OR a constant fill (`_padc`)
    Joined,    // one element of the result is one element of ONE of two operands (`_concat`)
    Boundary,  // the element is a spectral transform, left to an intrinsic backend (`_fft`)
    Scatter,   // one element of the result is a SUM of the operand's elements a window places onto it (`_ovadd`)
    Ordered,   // one element of the result is an ordered rank of the operand's elements along one axis (`_order`)
    Gathered,  // one element of the result is a blend of two table elements at a data-dependent index (`_tblread`)
    Indexed,   // one element of the result IS its own index — an arity-0 generator with no operand (`_iota`)
    Pooled,    // one element of the result IS a number of the graph's pool — the other generator (`_const`)
    Contracted, // one element of the result is a sum over a contracted extent, its two operands read at DIFFERENT index maps (`_matmul`)
    Linked,    // element 0 of the result is operand 0's, element j operand 1's element j - 1 (`_chain`)
    Started,   // the result IS whether this sample starts a run, 1 or 0 — the other arity-0 node, of no axis (`_first`)
};

constexpr ElementKind element_kind_of(OpCode op) {
    if (op == op::Reduce || op == op::Contract) {
        return ElementKind::Fold;
    }
    if (op == op::Reshape || op == op::Transpose || op == op::Slice || op == op::Unfold ||
        op == op::Unfold2d || op == op::Flip) {
        return ElementKind::Rearrange;
    }
    if (op == op::Pad) {
        return ElementKind::Guarded;
    }
    if (op == op::Concat) {
        return ElementKind::Joined;
    }
    if (op == op::Fft) {
        return ElementKind::Boundary;
    }
    if (op == op::OverlapAdd) {
        return ElementKind::Scatter;
    }
    if (op == op::Order) {
        return ElementKind::Ordered;
    }
    if (op == op::TableRead) {
        return ElementKind::Gathered;
    }
    if (op == op::Iota) {
        return ElementKind::Indexed;
    }
    if (op == op::MatMul) {
        return ElementKind::Contracted;
    }
    if (op == op::Constant) {
        return ElementKind::Pooled;
    }
    if (op == op::Chain) {
        return ElementKind::Linked;
    }
    if (op == op::First) {
        return ElementKind::Started;
    }
    return ElementKind::None;
}

// What each element kind means element by element lives in the element files beside this one
// (`rearrange_element.hpp`, `pad_element.hpp`, …).

} // namespace heinzel
