#pragma once

// The rule columns: the values a row of the rule table (`ir/op_rules.hpp`) is spelled in.
//
// Each enum is one column of `kOpTable`: how a result's shape and element type follow from the
// operands, when the operands are read, whether a per-sample meaning exists, cost, numeric freedom,
// how it differentiates, what it does to a range, whether two of its nodes are one value, and whether it marks a run's
// start. Nothing here names an operation.

#include <cstdint>

#include "heinzel/ir/vocab.hpp"

namespace heinzel {

// ---- the shape column ---------------------------------------------------------------------------
// How an operation's result shape follows from its operands'.
enum class ShapeRule : std::uint8_t {
    Unclassified = 0, // the half-filled-row sentinel; rejected by the coverage check below
    Elementwise,      // every operand broadcast together, axes aligned from the innermost outwards
    Passthrough,      // the result has the operand's shape
    ReduceAxes,       // the axes the attribute names collapse, or stay at extent 1
    ByViewKind,       // one operation, several rearrangements: the shape follows the attribute's `ViewKind`
    ContractAxes,     // the operands broadcast together, then the axes the attribute names collapse into a sum
    ContractGeneral,  // a stride-walking / transposing / batched contraction: per-operand index maps, an inner sum over a contracted extent that is not an output axis (_matmul)
    SpectralAxis,     // the innermost axis is transformed to its half-spectrum length (or back): _fft's rule
    OverlapAddAxes,   // the two innermost element axes [.., F, K] collapse into one [.., out_len]: _ovadd's rule
    OrderAxis,        // one element axis is sorted (kept L), topk'd (resized to k) or quantiled (dropped): _order's rule
    Declared,         // the result shape is stated in the attribute, not derived: an arity-0 generator (_iota, _const)
    OneNumber,        // one number with no axis, broadcast where it is read as a literal is: an arity-0 `_first`
};

// ---- the element-type column ---------------------------------------------------------------------
// Where an operation's result element type comes from. The analysis applies it; the frontend only
// records what the user wrote and inserts no conversions.
enum class TypeRule : std::uint8_t {
    Unclassified = 0,        // the half-filled-row sentinel
    // The promoted type of the operands, except that a literal constant adapts to the others and never
    // drives: `0.5 * w[n-1]` must not become double precision because the coefficient was written as a
    // double. A comparison uses this rule too; it answers 1 or 0 in that same type.
    FromOperands,
    FromAttribute,           // the attribute states it outright — this is what a conversion IS
    FromOperandsExceptFirst, // operand 0 is a condition; the result comes from operands 1 and 2
    SameAsFirst,             // the result carries operand 0's type unchanged
    // The attribute states it and there is no operand to derive it from: an arity-0 generator (_iota, _const).
    // Distinct from `FromAttribute` (`CastAttr::to`) because it reads `GeneratorAttr::type`.
    Declared,
    // No operand and no attribute: it takes the type of what reads it, as a literal does (`_first`, a 1 or a 0).
    Adapts,
};

// ---- the timing column ----------------------------------------------------------------------------
//
// Whether an operation reads its operands at the sample it is computing, or reaches back into their
// past. Almost everything is the first; a delay is the second, and that decides two things.
//
// A value whose past some delay reads must keep that past: its storage cannot be handed on once it
// is consumed.
//
// It also decides where feedback is legal. `y = x + c*y[n-1]` is a cycle read as a plain dependency
// graph, and no order satisfies it. It is not a cycle in time: the delay reads a sample that was
// finished before this one began. The planner asks this column which edges step back a sample, so it
// needs no branch about a particular operation.
enum class TimeRule : std::uint8_t {
    Unclassified = 0,    // the half-filled-row sentinel
    SameSample,          // reads its operands at the sample being computed
    ReadsEarlierSamples, // reads its operand's past; how far back is in the node's attribute
    // Reads operand 0's past at a distance operand 1 carries as a signal, read at this sample. The
    // distance changes every sample and lands between two of them, so a run fetches operand 1 now,
    // turns it into a tap with `signalled_tap()` (`ir/timing.hpp`), reads operand 0 twice — `whole`
    // and `whole + 1` samples back — and blends the two by the fraction.
    ReadsAtSignalledDepth,
    // Reads operand 0 at this sample, and operand 1 at this sample one element back along axis 0: element j of the
    // result is operand 1's element j - 1 (a link, `_chain`). A cycle closes through operand 1 as through a lag, since
    // element j - 1 is settled before element j needs it. One element back, always: no attribute says how far, so
    // `reaches_into_the_past` is false for it.
    ReadsTheElementBefore,
};

// ---- the meaning column ----------------------------------------------------------------------------
//
// Whether this operation has a meaning that can be written one sample at a time — one sample of each
// operand in, one sample out — or whether it rearranges or collapses axes, for which a
// sample-at-a-time computation has nothing to say.
//
// A column, so that the table says which rows owe a per-sample meaning and
// `tests/reference/meaning_coverage.hpp` can refuse a row that owes one and has none. It is not
// derived from the shape column because the two are different questions: `_tblread` has its phase's
// shape (`Passthrough`), but which table element it reads is data, so it has no per-sample meaning.
enum class MeaningRule : std::uint8_t {
    Unclassified = 0, // the half-filled-row sentinel
    PerSample,        // one sample of each operand in, one sample out (runtime/compute.hpp)
    AxisChanging,     // rearranges or collapses axes, so no sample-at-a-time meaning exists
};

// ---- the cost column -----------------------------------------------------------------------------
// Roughly what one element costs. The planner recomputes only `Cheap` maps inside their reader
// rather than storing them (`loops/planner/inline.hpp`).
enum class CostClass : std::uint8_t {
    Unclassified = 0, // the half-filled-row sentinel; rejected by the coverage check below
    Cheap,            // one multiply-add's worth
    // Expensive, because this tree computes it itself (a root, a transcendental, a network) and calls no
    // math library. Whether a coarser core exists that the accuracy setting selects is the numeric
    // column's `Approximate`, not a price.
    Software,
};

// What numeric FREEDOM an operation has — what a faster evaluation of it could legally change. The
// operation says which freedom EXISTS; whether it is GRANTED is the precision of the value it produces
// (`numerics::grants`, `ir/ir.hpp`), and whether it is TAKEN is the planner's choice. A column because
// no planning pass may name an operation.
//
// `Exact` is most of the table: the result is defined bit for bit — one correctly rounded step, or a
// fixed sequence of them — so scalar or vector it is the same number. Whether an F32 value may be
// computed in `float` is the difference between the two: it holds for a SINGLE correctly rounded step
// (53 >= 2*24+2) and fails for a sequence, which runs in double and rounds once (`_rsqrt`, `_fma`, the
// interpolating reads) — `tests/unit/native_f32_is_exact_test.cpp` measures both directions.
// Floating-point exceptions are not observed anywhere in this tree, which is what lets a vector form
// compute a lane it then discards.
enum class NumericContract : std::uint8_t {
    Unclassified = 0, // the half-filled-row sentinel; rejected by the coverage check
    Exact,            // no freedom; ONE correctly rounded step, so an F32 value may be computed in float
    ExactInDouble,    // no freedom; a SEQUENCE of steps, defined in double and rounded once
    Reducible,        // a fold over an axis: the ORDER of combination is a freedom
    // The ACCURACY is a freedom, bounded per tier: a coarser core exists that the accuracy setting selects.
    // Not a judgement: `tests/unit/tier_bounds_test.cpp` requires these rows to be exactly the operations
    // whose tiers it measures.
    Approximate,
    Selection,        // order statistics: no freedom to take; any network selects the same values
    Intrinsic,        // defined by a reference evaluation; a backend may substitute within its stated bound
    // The ROUNDING is the build's: one where it has the instruction, two where it does not. A freedom only where the
    // value's precision grants reordering, so only a rule that asks the grant emits such an operation (`rewrite/`).
    MachineRounded,
};

// ---- the adjoint column ----------------------------------------------------------------------------
// What differentiating an operation does to the cotangent that reaches its result (`autodiff/`). The
// arithmetic, transcendental and structural operations, the folds and the contraction carry a rule; the
// comparisons, the sign, the roundings and the index ramp have a derivative that is genuinely zero; the
// recurrence, the lags, the table read and the order statistic are excluded: each is a value defined by
// its past, a data-dependent read or a data-dependent scatter, whose derivative exists, so a silent zero
// would be a wrong answer. `Differentiable` claims that a rule exists, not that a number has been checked
// (`_fft` has no arithmetic yet).
enum class AdjointClass : std::uint8_t {
    Unclassified = 0, // the half-filled-row sentinel; rejected by the coverage check
    Differentiable,   // carries an adjoint rule: a cotangent flows from the result back to its operands
    ZeroDerivative,   // the derivative is genuinely zero, so a cotangent flows on as zero — not dropped
    Excluded,         // the derivative exists but this tree does not carry it: a named fail on an active path
};

// ---- the algebra column ---------------------------------------------------------------------------
// What an operation is to a sum, for the rewrite layer (`rewrite/`), which reorders a sum's terms and fuses its last
// product into a multiply-add where the precision grants it: the rule asks this column, never the operation. A
// multiply-add that promises one rounding (`_fma`) is no part of a sum: the rule keeps a promise it did not make.
enum class Algebra : std::uint8_t {
    Unclassified = 0, // the half-filled-row sentinel; rejected by the coverage check
    None,             // no part of a sum
    Add,              // a + b
    Subtract,         // a - b
    Negate,           // -a
    Product,          // a * b
    ProductAdd,       // a * b + c, rounded as the machine rounds
};

// Which of the machine's latencies and issue costs an operation takes, a class, so that the table holds no machine's
// figures (`Machine`, `schedule/cost.hpp`: P4's cost model prices a stage by them). Measured on zen2
// (`tools/tune/latency.cpp`): the classes are those whose cost differs.
enum class Latency : std::uint8_t {
    Unclassified = 0, // the half-filled-row sentinel; rejected by the coverage check
    None,             // computes nothing a sample: a view, a lag, a constant, a recurrence's marker
    Add,              // one simple step: an add, a multiply, a comparison, a selection, a rounding, a conversion
    MultiplyAdd,      // a * b + c: a multiply-add, a contraction's step
    Divide,
    Root,
    Load,             // a read from memory: a delay line, a table
    Core,             // computed in software: a transcendental, a transform, an order statistic's network
};

// ---- the range columns (Plan/26-value-ranges/03) ---------------------------------------------------------------
// How a result's range follows from its operands' ranges, for the proof that no operation leaves its domain
// (`ranges/`). Each rule is written for one interval of each operand: a range of several is mapped piece by piece and
// joined (`ranges/interval.hpp`). A monotone rule computes its ends with the operation's own writing.
enum class RangeRule : std::uint8_t {
    Unclassified = 0,   // the half-filled-row sentinel; rejected by the coverage check
    Same,               // the operand's range: a view, a conversion, a lag, a recurrence
    Covering,           // one interval covering the operand's: a blend or an interpolation can fill a hole
    CoveringTable,      // one interval covering operand 1's, the table an interpolating read blends
    Sum,
    Difference,
    Negation,
    Product,            // the least and the largest corner product; one value on both sides is a square
    ProductSum,         // a product, then a sum
    Quotient,           // operand 0 times the reciprocal of operand 1
    Increasing,         // [f(lo), f(hi)], both ends closed: a rounding, a sign
    StrictlyIncreasing, // [f(lo), f(hi)], open ends kept
    StrictlyDecreasing, // [f(hi), f(lo)], open ends kept
    EachIncreasing,     // f of the operands' ends, each end reached as the operand that decides it is: min, max
    Magnitude,          // |x|: [0, max(|lo|, |hi|)] where the operand holds 0, else [min(|lo|, |hi|), max(|lo|, |hi|)]
    Unit,               // [-1, 1]
    Truth,              // [0, 1]
    Angle,              // [-π, π]
    NonNegative,        // [0, ∞)
    Positive,           // (0, ∞)
    Choice,             // operand 1's or operand 2's: a selection picks one of them
    Union,              // any operand's, or a pad's fill
    Summed,             // a sum of terms, counted from the attribute: a fold, a contraction, an overlap-add
    Generated,          // a ramp's indices, a constant's numbers
    Whole,              // any number
};

// Where an operand must lie for the operation to have a value: a finding is an operand whose range is not inside.
// About real numbers, so x/0 and log(0) have none.
enum class Domain : std::uint8_t {
    Unclassified = 0, // the half-filled-row sentinel; rejected by the coverage check
    Anywhere,
    NonZeroSecond,    // operand 1 excludes 0
    NonNegative,      // operand 0 >= 0
    Positive,         // operand 0 > 0
    Unit,             // operand 0 within [-1, 1]
    OneBranch,        // each interval of operand 0 inside one open (kπ - π/2, kπ + π/2)
    NotBothZero,      // operands 0 and 1 are not both 0
    PowerBase,        // operand 0 >= 0, and > 0 where operand 1 may be negative
};

// ---- the sharing column ----------------------------------------------------------------------------
// Whether two nodes of an operation over the same values, of one attribute, type, shape and precision, are one value,
// for the rewrite that computes equal values once (`rewrite/equal_values.hpp`): it asks this column, never the
// operation.
enum class Sharing : std::uint8_t {
    Unclassified = 0, // the half-filled-row sentinel; rejected by the coverage check
    OneValue,         // computes nothing but its value from its operands and attribute: two such nodes are one
    Own,              // a value of its own whatever it reads: an output's materialisation, a variable's definition
};

// ---- the run-start column --------------------------------------------------------------------------
// Whether an operation's value is where the run stands rather than anything it reads: a run is one call of a program
// over its block, and `_first` is 1 at its first sample and 0 at every other. Such a value is stored nowhere: each
// reader computes it, and the stage that reads it runs that sample apart, so the samples after it see a 0 the compiler
// knows (`loops/`, Plan/33 S3). Asked of this column, never of the operation.
enum class RunStart : std::uint8_t {
    Unclassified = 0, // the half-filled-row sentinel; rejected by the coverage check
    Blind,            // the same at every sample, whichever starts the run
    Marks,            // 1 at the run's first sample, else 0
};

// Does this timing rule read anything from an earlier sample? Asked by name, so that the paired check
// in `ir/op_rules.hpp` and the time check in `sema/time_checks.hpp` ask the same question rather than
// keep two lists.
constexpr bool reaches_into_the_past(TimeRule t) {
    return t == TimeRule::ReadsEarlierSamples || t == TimeRule::ReadsAtSignalledDepth;
}

// Does this attribute kind say how far back an operation reads? Both lags do: one at a constant
// distance, one at a distance a signal carries.
constexpr bool carries_a_lag(AttrKind k) { return k == AttrKind::Delay || k == AttrKind::SignalledDelay; }

} // namespace heinzel
