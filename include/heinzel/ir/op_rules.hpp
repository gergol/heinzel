#pragma once

// The rule table: what each operation does, written as data rather than as code.
//
// One row per code (a retired code keeps an empty row); each column is a fact a pass needs (shape, element
// type, timing, meaning, cost, ...). Passes read columns instead of branching on the operation, so adding an
// operation is adding a row. Three checks at the bottom make a forgotten row a build error: the row count, the
// lockstep of codes and rows, and the coverage check that refuses a half-filled row.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/rule_columns.hpp"
#include "heinzel/ir/vocab.hpp"

namespace heinzel {

// ---- a row ---------------------------------------------------------------------------------------
struct OpRules {
    std::uint8_t arity{0};
    AttrKind attr{AttrKind::None};
    ShapeRule shape{ShapeRule::Unclassified};
    TypeRule type{TypeRule::Unclassified};
    TimeRule time{TimeRule::Unclassified};
    MeaningRule meaning{MeaningRule::Unclassified};
    CostClass cost{CostClass::Unclassified};
    NumericContract numeric{NumericContract::Unclassified};
    AdjointClass adjoint{AdjointClass::Unclassified};
    Algebra algebra{Algebra::Unclassified};
    Latency latency{Latency::Unclassified};
    RangeRule range{RangeRule::Unclassified};
    Domain domain{Domain::Unclassified};
    Sharing sharing{Sharing::Unclassified};
    RunStart start{RunStart::Unclassified};

    constexpr bool operator==(const OpRules&) const = default;
};

struct OpTableRow {
    OpName name;
    OpRules rules;
    // How a user writes the operation, where the DSL has exactly one spelling for it (`+`, `sqrt`, `einsum`;
    // a composed helper such as `frac` reuses the spellings it is written in). A refusal names this
    // instead of `name`. Empty where the DSL has several spellings (`_reduce`: `x.sum(1)`, `einsum`,
    // `reduce`), none, one whose operands come in another order (`a > b` is `_lt(b, a)`), or one that is
    // not a name (`x[n - k]`).
    OpName spelled{};
};

// ---- adding an operation --------------------------------------------------------------------------
//
// Its code goes in `ir/vocab.hpp` and its row here; a per-sample operation also gets its arithmetic in
// `runtime/compute.hpp`, and the frontend spells it if users write it. An axis-changing one reuses a
// shape rule or adds one: the analysis pass names no operation (`gate_no_per_op_branches`).
// `gate_one_writing_per_operation` lets only `runtime/compute.hpp` spell `op::Something` in `include/`,
// besides the files exempt from it: this one (its lockstep assertion names every code), the frontend
// files that record what the user wrote, and `autodiff/`.

// One row per code, in the order of the `op::` codes, a retired code's row empty — the two are held in
// step by an assertion below rather than by remembering to.
inline constexpr OpTableRow kOpTable[] = {
    //  name, then the columns in order: arity, attribute, shape, element type, timing, meaning, cost,
    //  numeric freedom, adjoint, algebra, latency, range, domain, sharing, run start; then the DSL's spelling, where it has one
    {"_add", {2, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable, Algebra::Add, Latency::Add, RangeRule::Sum, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}, "+"},
    {"_sub", {2, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable, Algebra::Subtract, Latency::Add, RangeRule::Difference, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}, "-"},
    {"_mul", {2, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable, Algebra::Product, Latency::Add, RangeRule::Product, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}, "*"},
    {"_div", {2, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable, Algebra::None, Latency::Divide, RangeRule::Quotient, Domain::NonZeroSecond, Sharing::OneValue, RunStart::Blind}, "/"},
    {"_neg", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable, Algebra::Negate, Latency::Add, RangeRule::Negation, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}, "-"},
    // A square root is a single machine instruction: expensive, but there is no cheaper approximate
    // version to trade accuracy for, so its price is the same at every accuracy setting.
    {"_sqrt", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Software, NumericContract::Exact, AdjointClass::Differentiable, Algebra::None, Latency::Root, RangeRule::StrictlyIncreasing, Domain::NonNegative, Sharing::OneValue, RunStart::Blind}, "sqrt"},
    // A conversion is the one operation whose result type is stated rather than derived, and it
    // states it in an attribute the node carries — so the node says what it does without anyone
    // having to look at the value it wrote, and the frontend decides no types.
    {"_cast", {1, AttrKind::Cast, ShapeRule::Elementwise, TypeRule::FromAttribute, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable, Algebra::None, Latency::Add, RangeRule::Same, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}},
    // A comparison answers in the operands' own type, holding one or zero. There is no separate
    // boolean type, and adding one would change the numbers this tree is measured against.
    {"_lt", {2, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::ZeroDerivative, Algebra::None, Latency::Add, RangeRule::Truth, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}},
    {"_select",
     {3, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperandsExceptFirst, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable, Algebra::None, Latency::Add, RangeRule::Choice, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}, "where"},
    {"_fdelay",
     {1, AttrKind::Delay, ShapeRule::Passthrough, TypeRule::SameAsFirst, TimeRule::ReadsEarlierSamples, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::Excluded, Algebra::None, Latency::None, RangeRule::Same, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}},
    {"_recur",
     {1, AttrKind::Recurrence, ShapeRule::Passthrough, TypeRule::SameAsFirst, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::Excluded, Algebra::None, Latency::None, RangeRule::Same, Domain::Anywhere, Sharing::Own, RunStart::Blind}},
    {"_reduce", {1, AttrKind::Reduce, ShapeRule::ReduceAxes, TypeRule::SameAsFirst, TimeRule::SameSample, MeaningRule::AxisChanging, CostClass::Cheap, NumericContract::Reducible, AdjointClass::Differentiable, Algebra::None, Latency::Add, RangeRule::Summed, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}},
    {"_reshape", {1, AttrKind::View, ShapeRule::ByViewKind, TypeRule::SameAsFirst, TimeRule::SameSample, MeaningRule::AxisChanging, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable, Algebra::None, Latency::None, RangeRule::Same, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}},
    // ---- appended in code order: a code is never renumbered (`ir/vocab.hpp`) ----------------------
    {"_min", {2, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable, Algebra::None, Latency::Add, RangeRule::EachIncreasing, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}, "min"},
    {"_max", {2, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable, Algebra::None, Latency::Add, RangeRule::EachIncreasing, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}, "max"},
    {"_le", {2, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::ZeroDerivative, Algebra::None, Latency::Add, RangeRule::Truth, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}},
    {"_eq", {2, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::ZeroDerivative, Algebra::None, Latency::Add, RangeRule::Truth, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}},
    {"_sign", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::ZeroDerivative, Algebra::None, Latency::Add, RangeRule::Increasing, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}},
    {"_floor", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::ZeroDerivative, Algebra::None, Latency::Add, RangeRule::Increasing, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}, "floor"},
    {"_ceil", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::ZeroDerivative, Algebra::None, Latency::Add, RangeRule::Increasing, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}},
    {"_round", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::ZeroDerivative, Algebra::None, Latency::Add, RangeRule::Increasing, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}},
    {"_trunc", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::ZeroDerivative, Algebra::None, Latency::Add, RangeRule::Increasing, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}},
    {"_fma", {3, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Software, NumericContract::ExactInDouble, AdjointClass::Differentiable, Algebra::None, Latency::MultiplyAdd, RangeRule::ProductSum, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}},
    {"_rsqrt", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Software, NumericContract::ExactInDouble, AdjointClass::Differentiable, Algebra::None, Latency::Root, RangeRule::StrictlyDecreasing, Domain::Positive, Sharing::OneValue, RunStart::Blind}, "rsqrt"},
    {"_exp", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Software, NumericContract::Approximate, AdjointClass::Differentiable, Algebra::None, Latency::Core, RangeRule::StrictlyIncreasing, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}, "exp"},
    {"_log", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Software, NumericContract::Approximate, AdjointClass::Differentiable, Algebra::None, Latency::Core, RangeRule::StrictlyIncreasing, Domain::Positive, Sharing::OneValue, RunStart::Blind}, "log"},
    {"_exp2", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Software, NumericContract::Approximate, AdjointClass::Differentiable, Algebra::None, Latency::Core, RangeRule::Positive, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}, "exp2"},
    {"_log2", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Software, NumericContract::Approximate, AdjointClass::Differentiable, Algebra::None, Latency::Core, RangeRule::Whole, Domain::Positive, Sharing::OneValue, RunStart::Blind}},
    {"_log10", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Software, NumericContract::Approximate, AdjointClass::Differentiable, Algebra::None, Latency::Core, RangeRule::Whole, Domain::Positive, Sharing::OneValue, RunStart::Blind}},
    {"_sin", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Software, NumericContract::Approximate, AdjointClass::Differentiable, Algebra::None, Latency::Core, RangeRule::Unit, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}, "sin"},
    {"_cos", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Software, NumericContract::Approximate, AdjointClass::Differentiable, Algebra::None, Latency::Core, RangeRule::Unit, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}, "cos"},
    {"_tan", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Software, NumericContract::Approximate, AdjointClass::Differentiable, Algebra::None, Latency::Core, RangeRule::Whole, Domain::OneBranch, Sharing::OneValue, RunStart::Blind}, "tan"},
    {"_sinpi", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Software, NumericContract::Approximate, AdjointClass::Differentiable, Algebra::None, Latency::Core, RangeRule::Unit, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}, "sinpi"},
    {"_atan", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Software, NumericContract::Approximate, AdjointClass::Differentiable, Algebra::None, Latency::Core, RangeRule::Angle, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}},
    {"_asin", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Software, NumericContract::Approximate, AdjointClass::Differentiable, Algebra::None, Latency::Core, RangeRule::Angle, Domain::Unit, Sharing::OneValue, RunStart::Blind}},
    {"_acos", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Software, NumericContract::Approximate, AdjointClass::Differentiable, Algebra::None, Latency::Core, RangeRule::Angle, Domain::Unit, Sharing::OneValue, RunStart::Blind}},
    {"_atan2", {2, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Software, NumericContract::Approximate, AdjointClass::Differentiable, Algebra::None, Latency::Core, RangeRule::Angle, Domain::NotBothZero, Sharing::OneValue, RunStart::Blind}},
    // `_tanh` is a hyperbolic function and every other row above it is circular or exponential, but
    // no column can tell them apart and none should: what the table records is what a PASS needs to
    // know, and to every pass here `_tanh` is a one-operand approximation like `_exp`.
    {"_tanh", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Software, NumericContract::Approximate, AdjointClass::Differentiable, Algebra::None, Latency::Core, RangeRule::StrictlyIncreasing, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}, "tanh"},
    {"_pow", {2, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Software, NumericContract::Approximate, AdjointClass::Differentiable, Algebra::None, Latency::Core, RangeRule::NonNegative, Domain::PowerBase, Sharing::OneValue, RunStart::Blind}, "pow"},
    // `_contr` — an elementwise contraction: the operands broadcast together, then the named axes are
    // summed. No spelling builds it: an aligned `einsum` is a product and a sum (Decision 78).
    {"_contr", {2, AttrKind::Contract, ShapeRule::ContractAxes, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::AxisChanging, CostClass::Cheap, NumericContract::Reducible, AdjointClass::Differentiable, Algebra::None, Latency::MultiplyAdd, RangeRule::Summed, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}},
    {}, // 40, retired: `_delay`, folded into `_fdelay`
    // `_dline` — the second lag, and the only operation whose operands are not all read from one
    // sample: the distance is operand 1's value at this sample (`ReadsAtSignalledDepth`).
    {"_dline", {2, AttrKind::SignalledDelay, ShapeRule::Passthrough, TypeRule::SameAsFirst, TimeRule::ReadsAtSignalledDepth, MeaningRule::PerSample, CostClass::Cheap, NumericContract::ExactInDouble, AdjointClass::Excluded, Algebra::None, Latency::Load, RangeRule::Covering, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}},

    // `_xpose` — a transpose: a permutation of the axes.
    {"_xpose", {1, AttrKind::View, ShapeRule::ByViewKind, TypeRule::SameAsFirst, TimeRule::SameSample, MeaningRule::AxisChanging, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable, Algebra::None, Latency::None, RangeRule::Same, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}},
    // `_slice` — a strided sub-array of one axis.
    {"_slice", {1, AttrKind::View, ShapeRule::ByViewKind, TypeRule::SameAsFirst, TimeRule::SameSample, MeaningRule::AxisChanging, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable, Algebra::None, Latency::None, RangeRule::Same, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}, "slice"},
    // `_unfold` — a sliding window: extent L becomes N_out=(L-K)/H+1 frames, a window axis of K beside it.
    {"_unfold", {1, AttrKind::View, ShapeRule::ByViewKind, TypeRule::SameAsFirst, TimeRule::SameSample, MeaningRule::AxisChanging, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable, Algebra::None, Latency::None, RangeRule::Same, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}, "unfold"},
    // `_unfld2d` — a 2-D sliding window: two outer axes each become their frame count, a window axis beside each.
    {"_unfld2d", {1, AttrKind::View, ShapeRule::ByViewKind, TypeRule::SameAsFirst, TimeRule::SameSample, MeaningRule::AxisChanging, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable, Algebra::None, Latency::None, RangeRule::Same, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}, "unfold2d"},
    // `_flip` — a reversal of one outer axis: shape-preserving, the workaround the negative-stride slice names.
    {"_flip", {1, AttrKind::View, ShapeRule::ByViewKind, TypeRule::SameAsFirst, TimeRule::SameSample, MeaningRule::AxisChanging, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable, Algebra::None, Latency::None, RangeRule::Same, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}},
    // `_padc` — a constant pad of one outer axis: extent L grows to L+before+after, the margins a constant.
    {"_padc", {1, AttrKind::View, ShapeRule::ByViewKind, TypeRule::SameAsFirst, TimeRule::SameSample, MeaningRule::AxisChanging, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable, Algebra::None, Latency::None, RangeRule::Union, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}},
    // `_concat` — a join of two operands along one outer axis: extents summed.
    {"_concat", {2, AttrKind::View, ShapeRule::ByViewKind, TypeRule::SameAsFirst, TimeRule::SameSample, MeaningRule::AxisChanging, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable, Algebra::None, Latency::None, RangeRule::Union, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}},
    // `_fft` — an intrinsic boundary. Its shape rule transforms the innermost axis to its half-spectrum
    // length, and every extent must be a literal (`sema/spectral.hpp`). Its arithmetic is left to
    // a spectral backend that does not exist yet, so no runner computes it: the reference walk's element
    // kind for it is `Boundary` (`tests/reference/compute_reference.hpp`), which it refuses by name.
    {"_fft", {1, AttrKind::Fft, ShapeRule::SpectralAxis, TypeRule::SameAsFirst, TimeRule::SameSample, MeaningRule::AxisChanging, CostClass::Cheap, NumericContract::Intrinsic, AdjointClass::Differentiable, Algebra::None, Latency::Core, RangeRule::Whole, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}},
    {}, // 50, retired: `_pack`, folded into `_concat`
    // `_ovadd` — the scatter-add that undoes a framing. Its two innermost element axes collapse into
    // one, which no view expresses, so it is an operation of its own. Its shape rule reads the window and
    // hop from the `OverlapAdd` attribute; a symbolic frame count resolves through the solver. Element kind
    // `Scatter` in the reference walk. Cost `Cheap`: one add per element read.
    {"_ovadd", {1, AttrKind::OverlapAdd, ShapeRule::OverlapAddAxes, TypeRule::SameAsFirst, TimeRule::SameSample, MeaningRule::AxisChanging, CostClass::Cheap, NumericContract::Reducible, AdjointClass::Differentiable, Algebra::None, Latency::Add, RangeRule::Summed, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}},
    // `_order` — order statistics along one axis: sort, top-k or a quantile. The ordered extent sizes
    // the compare-exchange network, so every element extent must be known now; the stream's length may
    // come at run time (`sema/order.hpp`).
    {"_order", {1, AttrKind::Order, ShapeRule::OrderAxis, TypeRule::SameAsFirst, TimeRule::SameSample, MeaningRule::AxisChanging, CostClass::Software, NumericContract::Selection, AdjointClass::Excluded, Algebra::None, Latency::Core, RangeRule::Covering, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}},
    // `_realize` — a materialisation barrier that is value for value the identity. It changes no axes,
    // so it is `PerSample`: the reference walk hands its operand straight back. No attribute: it copies
    // whatever shape it is given.
    {"_realize", {1, AttrKind::None, ShapeRule::Passthrough, TypeRule::SameAsFirst, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable, Algebra::None, Latency::None, RangeRule::Same, Domain::Anywhere, Sharing::Own, RunStart::Blind}},
    // `_tblread` — a linearly interpolated wavetable read, `out[e] = table[frac(phase[e]) * N]`. The
    // result has the phase's shape and type, and both operands are read at this sample. Which table
    // element is read is data, so it has no per-sample meaning: `AxisChanging` with element kind
    // `Gathered` (`runtime/table_gather_element.hpp`), despite its `Passthrough` shape. The phase may be
    // symbolic; the table holds a literal `N >= 1` samples (the attribute check), `N` in `AttrKind::Table`.
    // Spelled `wavetable`, one character more than an `OpName` holds: a refusal names it `_tblread`.
    {"_tblread", {2, AttrKind::Table, ShapeRule::Passthrough, TypeRule::SameAsFirst, TimeRule::SameSample, MeaningRule::AxisChanging, CostClass::Cheap, NumericContract::ExactInDouble, AdjointClass::Excluded, Algebra::None, Latency::Load, RangeRule::CoveringTable, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}},
    // `_iota` — an index ramp, `out[e] = e`, a generator: an operation with no operand. Its shape and type
    // are declared in `GeneratorAttr` (`ShapeRule::Declared`, `TypeRule::Declared`): nothing flows in to derive
    // them from. `AxisChanging` with element kind `Indexed`, because a per-sample computation is handed
    // operand values, not an index. Its "extents known now" refusal is an attribute check on the declared
    // shape (`sema/attribute_checks.hpp`).
    {"_iota", {0, AttrKind::Iota, ShapeRule::Declared, TypeRule::Declared, TimeRule::SameSample, MeaningRule::AxisChanging, CostClass::Cheap, NumericContract::Exact, AdjointClass::ZeroDerivative, Algebra::None, Latency::Add, RangeRule::Generated, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}, "iota"},
    // `_matmul` — the general contraction (transposing, strided, batched) that `_contr` refuses. Its
    // shape rule reads the `MatMul` attribute's binding (which axis of each operand is summed, which are
    // batched) and builds `[batch] ++ [a free] ++ [b free] ++ [streamed]`.
    // `AxisChanging` with element kind `Contracted`: its two operands are read at different index maps.
    // A symbolic contracted or streamed extent resolves through the solver.
    // `Excluded`: its derivative is a pair of contractions, `dA = dOut·Bᵀ` and `dB = Aᵀ·dOut`, but the rule
    // is not written yet; a named failure rather than a false claim.
    {"_matmul", {2, AttrKind::MatMul, ShapeRule::ContractGeneral, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::AxisChanging, CostClass::Cheap, NumericContract::Reducible, AdjointClass::Excluded, Algebra::None, Latency::MultiplyAdd, RangeRule::Summed, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}, "einsum"},
    // `_const` — a constant tensor, the other generator: `out[e]` is the graph's pool at `first + e`, numbers the
    // frontend rounded to the declared type. `_iota`'s row but for its attribute kind, which says where the numbers
    // come from; `AxisChanging` with element kind `Pooled`.
    {"_const", {0, AttrKind::Constant, ShapeRule::Declared, TypeRule::Declared, TimeRule::SameSample, MeaningRule::AxisChanging, CostClass::Cheap, NumericContract::Exact, AdjointClass::ZeroDerivative, Algebra::None, Latency::None, RangeRule::Generated, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}, "constant"},
    // `_madd` — `a * b + c` rounded as the machine rounds: one multiply-add's worth in every build, unlike `_fma`,
    // whose single rounding is a software core where the build lacks the instruction. No spelling: the rewrite emits it.
    {"_madd", {3, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::MachineRounded, AdjointClass::Differentiable, Algebra::ProductAdd, Latency::MultiplyAdd, RangeRule::ProductSum, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}},
    // `_abs` — |x|, the sign bit cleared: exact, and one instruction (`fabs`, `andpd`) on every machine.
    {"_abs", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable, Algebra::None, Latency::Add, RangeRule::Magnitude, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}, "abs"},
    // `_chain` — a link: element 0 of axis 0 is operand 0 (one section's worth, broadcast), element j operand 1's
    // element j - 1, both at this sample (`ReadsTheElementBefore`). It moves numbers between elements and computes
    // none, so it has no per-sample meaning: element kind `Linked`. A shuffle's latency. No spelling: the rewrite
    // emits it.
    {"_chain", {2, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::ReadsTheElementBefore, MeaningRule::AxisChanging, CostClass::Cheap, NumericContract::Exact, AdjointClass::Excluded, Algebra::None, Latency::Add, RangeRule::Union, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}},
    // `_first` — 1 at a run's first sample, 0 at every other (`RunStart::Marks`): where the run stands, not anything it
    // reads, so no operand, one number with no axis (`OneNumber`), typed where it is read as a literal is (`Adapts`).
    // Element kind `Started` in the walk; stored nowhere, each reader computes it. No spelling: `dsp::smoothed` reads it.
    {"_first", {0, AttrKind::None, ShapeRule::OneNumber, TypeRule::Adapts, TimeRule::SameSample, MeaningRule::AxisChanging, CostClass::Cheap, NumericContract::Exact, AdjointClass::ZeroDerivative, Algebra::None, Latency::None, RangeRule::Truth, Domain::Anywhere, Sharing::OneValue, RunStart::Marks}},
};

inline constexpr std::uint8_t kOpCount = static_cast<std::uint8_t>(sizeof(kOpTable) / sizeof(kOpTable[0]));

constexpr OpRules rules_of(OpCode c) { return kOpTable[c.index].rules; }
constexpr OpName name_of(OpCode c) { return kOpTable[c.index].name; }

// A retired code (`ir/vocab.hpp`) keeps an empty row, so that the codes after it keep theirs; no node
// carries one.
constexpr bool is_retired(OpCode c) { return name_of(c) == OpName{}; }

// Which `Approximate` rows have a writing over the value type, and which of those a core in the value's own
// type, a float core for a float. Facts about `runtime/compute.hpp`, stated here because the planner may name no
// operation and may not include `runtime/`; a static_assert there refuses a build in which they disagree. Lists,
// because no column separates these rows: which ones have a writing is a fact about what has been written so far.
//
// A writing without a float core runs the double core one number at a time, a float widened: slower and more
// accurate than the code a float is compared against, and run rather than refused (Gerald: none of these
// capabilities may go, `Plan/16-simplify/08-Later.md`); a float core is written where a benchmark asks for one.
// The loop tree refuses a graph holding a row with no writing.
constexpr bool has_a_core_in_the_values_own_type(OpCode c) {
    return c == op::Exp || c == op::Tanh || c == op::SinPi || c == op::Sin || c == op::Cos || c == op::Log;
}

constexpr bool has_a_writing_over_the_value_type(OpCode c) {
    return has_a_core_in_the_values_own_type(c) || c == op::Tan || c == op::Exp2 || c == op::Pow;
}

// The operation as a refusal names it: as the user wrote it, where the DSL has one spelling for it, else by its
// own name.
constexpr OpName shown_name(OpCode c) {
    const OpName spelled = kOpTable[c.index].spelled;
    return spelled == OpName{} ? name_of(c) : spelled;
}

// The operation written into a message, so a complaint says which operation it is about.
constexpr void write_op(MessageWriter& out, OpCode c) { write_op_name(out, shown_name(c)); }

// A node's operation as the user wrote it: `write_op`'s words, and an order statistic by its member's, which its
// attribute says (one code holds `x.sort(0)`, `x.topk(0, 3)`, `x.median(0)` and `x.quantile(0, 0.25)`): "sort",
// "topk 3", "median", "quantile 0.25".
constexpr void write_node(MessageWriter& out, const Node& n) {
    const OrderAttr& order = n.attr.order;
    if (rules_of(n.op).attr != AttrKind::Order) {
        write_op(out, n.op);
    } else if (order.op == OrderOp::Sort) {
        out << std::string_view{"sort"};
    } else if (order.op == OrderOp::TopK) {
        out << std::string_view{"topk "} << order.k;
    } else if (order.q == 0.5) {
        out << std::string_view{"median"};
    } else {
        out << std::string_view{"quantile "} << order.q;
    }
}

// ---- coverage ---------------------------------------------------------------------------------------
// Each check turns a mistake that would be a wrong answer at run time into a build error.

// Every operation's code indexes its own row: every code is named here with its row's name, so reordering
// one list without the other, or adding a code without naming it here, fires.
static_assert(name_of(op::Add) == OpName{"_add"} && name_of(op::Sub) == OpName{"_sub"} &&
                  name_of(op::Mul) == OpName{"_mul"} && name_of(op::Div) == OpName{"_div"} &&
                  name_of(op::Neg) == OpName{"_neg"} && name_of(op::Sqrt) == OpName{"_sqrt"} &&
                  name_of(op::Cast) == OpName{"_cast"} && name_of(op::Lt) == OpName{"_lt"} &&
                  name_of(op::Select) == OpName{"_select"} && name_of(op::FixedDelay) == OpName{"_fdelay"} &&
                  name_of(op::Recurrence) == OpName{"_recur"} && name_of(op::Reduce) == OpName{"_reduce"} &&
                  name_of(op::Reshape) == OpName{"_reshape"} && name_of(op::Min) == OpName{"_min"} &&
                  name_of(op::Max) == OpName{"_max"} && name_of(op::Le) == OpName{"_le"} &&
                  name_of(op::Eq) == OpName{"_eq"} && name_of(op::Sign) == OpName{"_sign"} &&
                  name_of(op::Floor) == OpName{"_floor"} && name_of(op::Ceil) == OpName{"_ceil"} &&
                  name_of(op::Round) == OpName{"_round"} && name_of(op::Trunc) == OpName{"_trunc"} &&
                  name_of(op::Fma) == OpName{"_fma"} && name_of(op::Rsqrt) == OpName{"_rsqrt"} &&
                  name_of(op::Exp) == OpName{"_exp"} && name_of(op::Log) == OpName{"_log"} &&
                  name_of(op::Exp2) == OpName{"_exp2"} && name_of(op::Log2) == OpName{"_log2"} &&
                  name_of(op::Log10) == OpName{"_log10"} && name_of(op::Sin) == OpName{"_sin"} &&
                  name_of(op::Cos) == OpName{"_cos"} && name_of(op::Tan) == OpName{"_tan"} &&
                  name_of(op::SinPi) == OpName{"_sinpi"} && name_of(op::Atan) == OpName{"_atan"} &&
                  name_of(op::Asin) == OpName{"_asin"} && name_of(op::Acos) == OpName{"_acos"} &&
                  name_of(op::Atan2) == OpName{"_atan2"} && name_of(op::Tanh) == OpName{"_tanh"} &&
                  name_of(op::Pow) == OpName{"_pow"} && name_of(op::Contract) == OpName{"_contr"} &&
                  is_retired(OpCode{40}) && name_of(op::DelayLine) == OpName{"_dline"} &&
                  name_of(op::Transpose) == OpName{"_xpose"} && name_of(op::Slice) == OpName{"_slice"} &&
                  name_of(op::Unfold) == OpName{"_unfold"} && name_of(op::Unfold2d) == OpName{"_unfld2d"} &&
                  name_of(op::Flip) == OpName{"_flip"} && name_of(op::Pad) == OpName{"_padc"} &&
                  name_of(op::Concat) == OpName{"_concat"} && name_of(op::Fft) == OpName{"_fft"} &&
                  is_retired(OpCode{50}) && name_of(op::OverlapAdd) == OpName{"_ovadd"} &&
                  name_of(op::Order) == OpName{"_order"} && name_of(op::Realize) == OpName{"_realize"} &&
                  name_of(op::TableRead) == OpName{"_tblread"} && name_of(op::Iota) == OpName{"_iota"} &&
                  name_of(op::MatMul) == OpName{"_matmul"} && name_of(op::Constant) == OpName{"_const"} &&
                  name_of(op::MultiplyAdd) == OpName{"_madd"} && name_of(op::Abs) == OpName{"_abs"} &&
                  name_of(op::Chain) == OpName{"_chain"} && name_of(op::First) == OpName{"_first"},
              "the op:: codes must index their own rows in kOpTable");

// The table has a row for every operation. This count is updated by hand when an operation is added, with
// its code named in the assertion above; the one below forces its row to be filled in.
static_assert(kOpCount == 62, "add an operation, add its row, and update this count");

// A rule that reads an attribute and the attribute it reads must AGREE, in both directions. An operation
// asking for a rule that lives in an attribute it does not carry would read something that is not there;
// one carrying an attribute no rule ever reads has a field nobody fills correctly.
constexpr bool row_attribute_pairings_agree(const OpRules& r) {
    if ((r.shape == ShapeRule::ByViewKind) != (r.attr == AttrKind::View)) {
        return false;
    }
    if ((r.type == TypeRule::FromAttribute) != (r.attr == AttrKind::Cast)) {
        return false;
    }
    if ((r.shape == ShapeRule::ContractAxes) != (r.attr == AttrKind::Contract)) {
        return false;
    }
    // The general contraction's shape rule reads its per-operand binding — which axis of each operand is
    // summed and which are batched — from the `MatMul` attribute; a row asking for one must carry the
    // other, in both directions like the pairings above.
    if ((r.shape == ShapeRule::ContractGeneral) != (r.attr == AttrKind::MatMul)) {
        return false;
    }
    // The spectral shape rule reads the transform length and direction from the `Fft` attribute, and the
    // overlap-add rule its window and hop from the `OverlapAdd` attribute — each a row asking for one that
    // must carry the other, in both directions like the pairings above.
    if ((r.shape == ShapeRule::SpectralAxis) != (r.attr == AttrKind::Fft)) {
        return false;
    }
    if ((r.shape == ShapeRule::OverlapAddAxes) != (r.attr == AttrKind::OverlapAdd)) {
        return false;
    }
    // The order shape rule reads its statistic and axis from the `Order` attribute — both must agree.
    if ((r.shape == ShapeRule::OrderAxis) != (r.attr == AttrKind::Order)) {
        return false;
    }
    // A generator states its own result: the declared-shape rule reads `GeneratorAttr::shape` and the
    // declared-type rule reads `GeneratorAttr::type`, so BOTH must agree with a generator's attribute (`Iota`,
    // `Constant`), in both directions like the pairings above. Two clauses because the two columns read the same
    // attribute for two facts, and either alone paired with a generator would leave the other free to be forgotten.
    const bool generates = r.attr == AttrKind::Iota || r.attr == AttrKind::Constant;
    if ((r.shape == ShapeRule::Declared) != generates) {
        return false;
    }
    if ((r.type == TypeRule::Declared) != generates) {
        return false;
    }
    // Reading the past and carrying a delay attribute are the same fact stated twice, so they must agree.
    // How far back is in the attribute: an operation that reads the past without one has nowhere to say how
    // far, and one carrying a delay it never reads back has a field nobody fills. Two attribute kinds satisfy
    // it, because there are two lags — one at a constant distance and one whose distance is a signal — and
    // each says, in its own way, how far back the read may reach.
    return reaches_into_the_past(r.time) == carries_a_lag(r.attr);
}

// No row was left half-filled. Written as a NAMED PREDICATE over a table rather than inline over
// this one, so a test can hand it a deliberately broken table and show it saying no. A check that
// has only ever been seen to pass is not evidence that it would fail.
constexpr bool every_row_is_complete(const OpTableRow* rows, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        const OpRules& r = rows[i].rules;
        if (rows[i].name == OpName{} && r == OpRules{}) {
            continue; // a retired code's empty row
        }
        if (r.shape == ShapeRule::Unclassified) {
            return false;
        }
        if (r.type == TypeRule::Unclassified) {
            return false;
        }
        if (r.time == TimeRule::Unclassified) {
            return false;
        }
        if (r.meaning == MeaningRule::Unclassified) {
            return false;
        }
        if (r.cost == CostClass::Unclassified) {
            return false;
        }
        if (r.numeric == NumericContract::Unclassified || r.adjoint == AdjointClass::Unclassified || r.algebra == Algebra::Unclassified ||
            r.latency == Latency::Unclassified) {
            return false;
        }
        // Without both, the proof (`ranges/`) could say nothing of the result, or would check no domain.
        if (r.range == RangeRule::Unclassified || r.domain == Domain::Unclassified) {
            return false;
        }
        // Without it, the rewrite that computes equal values once (`rewrite/equal_values.hpp`) could not tell an
        // operation's value from a node of its own.
        if (r.sharing == Sharing::Unclassified) {
            return false;
        }
        // Without it, a run's first sample could not be told from the others' (`loops/`, Plan/33 S3).
        if (r.start == RunStart::Unclassified) {
            return false;
        }
        // Arity is bounded above only: `_iota` has none, and a forgotten row is already refused by the
        // sentinels above.
        if (r.arity > 3) {
            return false;
        }
        // The cross-column pairings — every rule that reads an attribute agreeing with the attribute the
        // row carries, in both directions — are one concern, checked together next door.
        if (!row_attribute_pairings_agree(r)) {
            return false;
        }
    }
    return true;
}

static_assert(every_row_is_complete(kOpTable, kOpCount),
              "every operation needs every rule column filled in, and its shape rule must match its attribute");

} // namespace heinzel
