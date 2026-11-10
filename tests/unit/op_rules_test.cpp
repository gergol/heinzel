// The rule table.
//
// The table's own coverage checks fire during compilation, so this file passing at all is part of
// the evidence: if a row were half-filled, including the header would already have failed. What is left to test here is that the columns say what they are
// supposed to say, and that the lookups are keyed the way the design requires.
//
// The other half — that REMOVING a row breaks the build — cannot be written here, because a test
// that fails to compile is not a test. It belongs with the build-failure gates.
#include "heinzel/ir/op_rules.hpp"

#include "incomplete_operation.hpp"

using namespace heinzel;

// ---- the columns are not all one value ----------------------------------------------------------
// A table whose shape column reads the same in every row would prove nothing about being a table.
// M1's operation set was chosen partly so that each column discriminates.
static_assert(rules_of(op::Add).shape == ShapeRule::Elementwise);
static_assert(rules_of(op::FixedDelay).shape == ShapeRule::Passthrough);
static_assert(rules_of(op::Reduce).shape == ShapeRule::ReduceAxes);
static_assert(rules_of(op::Reshape).shape == ShapeRule::ByViewKind);

static_assert(rules_of(op::Add).type == TypeRule::FromOperands);
static_assert(rules_of(op::Cast).type == TypeRule::FromAttribute && rules_of(op::Cast).attr == AttrKind::Cast,
              "a conversion states its result type in an attribute — which is why the frontend need not");
static_assert(rules_of(op::Select).type == TypeRule::FromOperandsExceptFirst,
              "the condition does not contribute its type to the answer");
static_assert(rules_of(op::Recurrence).type == TypeRule::SameAsFirst);

// A comparison answers in its operands' own type. There is no boolean element type, and adding one
// would change the numbers this tree is measured against.
static_assert(rules_of(op::Lt).type == TypeRule::FromOperands);

// ---- cost ---------------------------------------------------------------------------------------
static_assert(rules_of(op::Add).cost == CostClass::Cheap);
static_assert(rules_of(op::Sqrt).cost == CostClass::Software,
              "expensive and tier-invariant: a Newton iteration this tree runs itself, in both worlds");
// A coarse core that a setting selects is a numeric freedom (`NumericContract::Approximate`), not a price
// (finding S8-COST): the transcendentals are expensive either way. Which rows are `Approximate` is checked
// against the rows actually MEASURED giving three answers, in `tier_bounds_test.cpp`, and both directions
// of that set equality fail.
static_assert(rules_of(op::Exp).cost == CostClass::Software && rules_of(op::Exp).numeric == NumericContract::Approximate &&
                  rules_of(op::Tanh).numeric == NumericContract::Approximate,
              "the exponential family got its coarse cores at M4.2");
static_assert(rules_of(op::Sin).numeric == NumericContract::Approximate && rules_of(op::SinPi).numeric == NumericContract::Approximate,
              "the periodic family got its coarse cores at M4.2");
static_assert(rules_of(op::Atan).numeric == NumericContract::Approximate && rules_of(op::Atan2).numeric == NumericContract::Approximate,
              "the inverse circular family got its coarse cores at M4.2");
static_assert(rules_of(op::Pow).numeric == NumericContract::Approximate, "`_pow` got its coarse cores at M4.2");

// Fifteen rows are `Approximate` once M4.2 is complete, and the expensive rows below are not among them
// — each for its own reason rather than because nobody got to them. `_sqrt` and `_rsqrt` have no
// coarser method at all, which is what the old tree's `Hw` says about both; `_fma` promises a single
// rounding, and that is not a thing an accuracy setting may take away. So the assertion below is the
// claim that these three are tier-INVARIANT.
static_assert(rules_of(op::Fma).cost == CostClass::Software && rules_of(op::Rsqrt).cost == CostClass::Software &&
                  rules_of(op::Fma).numeric == NumericContract::ExactInDouble && rules_of(op::Rsqrt).numeric == NumericContract::ExactInDouble,
              "the three operations this tree computes rather than the machine are expensive and tier-invariant");

// ---- `_contr`: the column where this tree's row differs from the old tree's ----------------------
// The difference is a defect corrected rather than a deviation chosen, and it is asserted here because a
// row that is confidently wrong is exactly what the coverage check cannot see (M4.4).
static_assert(rules_of(op::Contract).arity == 2,
              "the old row says arity 1; its only builder passes two operands and its units arm reads b");
static_assert(rules_of(op::Contract).meaning == MeaningRule::AxisChanging &&
                  rules_of(op::Contract).shape == ShapeRule::ContractAxes &&
                  rules_of(op::Contract).attr == AttrKind::Contract,
              "it collapses axes, so it has no sample-at-a-time meaning and its axes live in an attribute");
static_assert(rules_of(op::Contract).cost == CostClass::Cheap,
              "the old tree's price, kept: a contraction is multiply-adds and nothing else");

// The lags answer the timing question, which is the point of asking it by attribute kind rather
// than by listing the kinds that happen to carry a distance today.
static_assert(carries_a_lag(AttrKind::Delay));
static_assert(!carries_a_lag(AttrKind::Recurrence) && !carries_a_lag(AttrKind::None),
              "a recurrence reads this sample; a lag is the only thing that reads an earlier one");

// ---- `_dline`: the second lag, whose distance is a signal ------------------------------------------
// Every column matches the old tree's row (`ir/vocab.hpp:185`: arity 2, `Cheap`). What it adds is a
// timing answer no earlier row could state.
static_assert(rules_of(op::DelayLine).arity == 2 && rules_of(op::DelayLine).cost == CostClass::Cheap,
              "the old tree's row, unchanged");
static_assert(rules_of(op::DelayLine).time == TimeRule::ReadsAtSignalledDepth &&
                  rules_of(op::DelayLine).attr == AttrKind::SignalledDelay,
              "its distance is operand 1's VALUE, so it is neither this sample nor a number in the row");

// The two lags, and the two questions the timing column answers about each.
static_assert(reaches_into_the_past(TimeRule::ReadsEarlierSamples) &&
                  reaches_into_the_past(TimeRule::ReadsAtSignalledDepth) &&
                  !reaches_into_the_past(TimeRule::SameSample));
static_assert(carries_a_lag(AttrKind::SignalledDelay));

// ---- the name a refusal uses ----------------------------------------------------------------------
// The user's spelling where the DSL has exactly one, the operation's own name where it has several or none.
static_assert(shown_name(op::MatMul) == OpName{"einsum"} && shown_name(op::Add) == OpName{"+"} &&
                  shown_name(op::Unfold2d) == OpName{"unfold2d"},
              "one spelling: a refusal names it");
static_assert(shown_name(op::Reduce) == OpName{"_reduce"} && shown_name(op::Lt) == OpName{"_lt"} &&
                  shown_name(op::Flip) == OpName{"_flip"},
              "several spellings, swapped operands, none: a refusal keeps the operation's own name");

// ---- arity --------------------------------------------------------------------------------------
static_assert(rules_of(op::Neg).arity == 1);
static_assert(rules_of(op::Add).arity == 2);
static_assert(rules_of(op::Select).arity == 3);

// ---- the key is the pair, not the operation -----------------------------------------------------
// One operation, several rearrangements: a rearrangement's shape follows its `ViewKind`
// (`sema/view_shape.hpp`), never the operation alone.
static_assert(rules_of(op::Reshape).attr == AttrKind::View);

// The two directions of the shape/attribute agreement, which the coverage check enforces for every
// row: only a rearrangement asks for its view's kind, and every rearrangement does.
static_assert(rules_of(op::Add).attr == AttrKind::None && rules_of(op::Add).shape != ShapeRule::ByViewKind);

// The timing column discriminates, and it discriminates in the one place it matters: a delay reads
// the past, ordinary arithmetic does not, and a recurrence — which is what makes a loop — does NOT
// read the past itself. That last one is the easy mistake: the loop is legal because of the delay
// inside it, not because of the recurrence naming it.
static_assert(rules_of(op::FixedDelay).time == TimeRule::ReadsEarlierSamples);
static_assert(rules_of(op::Add).time == TimeRule::SameSample);
static_assert(rules_of(op::Recurrence).time == TimeRule::SameSample);

// `_fft`'s row: the spectral shape rule paired with the FFT attribute, and axis-changing like the other
// structural rows. Its meaning kind is checked in `meaning_test.cpp`, where the writing is.
static_assert(rules_of(op::Fft).shape == ShapeRule::SpectralAxis &&
              rules_of(op::Fft).attr == AttrKind::Fft &&
              rules_of(op::Fft).meaning == MeaningRule::AxisChanging && rules_of(op::Fft).arity == 1);

// `_ovadd`'s row: the overlap-add shape rule paired with its own attribute — a full operation of its own,
// NOT a view (its two innermost axes collapse into one, which no view expresses) — arity 1,
// axis-changing; a symbolic frame count resolves through the solver. Its element meaning is `Scatter`,
// checked in `meaning_test.cpp`.
static_assert(rules_of(op::OverlapAdd).shape == ShapeRule::OverlapAddAxes &&
              rules_of(op::OverlapAdd).attr == AttrKind::OverlapAdd &&
              rules_of(op::OverlapAdd).meaning == MeaningRule::AxisChanging &&
              rules_of(op::OverlapAdd).arity == 1);

// `_order`'s row: the order shape rule paired with its own attribute — a full operation of its own, NOT a
// view (its ordered axis is kept, resized or dropped, which no view expresses) — arity 1,
// axis-changing (the network is sized from a static element extent, `sema/order.hpp`). Its cost is `CostClass::Software`: expensive and
// tier-invariant, the network computed by this tree with no coarser core — NOT `Cheap` (finding S8-COST).
// Its element meaning is `Ordered`, checked in `meaning_test.cpp`.
static_assert(rules_of(op::Order).shape == ShapeRule::OrderAxis &&
              rules_of(op::Order).attr == AttrKind::Order &&
              rules_of(op::Order).meaning == MeaningRule::AxisChanging &&
              rules_of(op::Order).cost == CostClass::Software && rules_of(op::Order).arity == 1);

// `_realize`'s row: the FIRST of M5's set that is NOT axis-changing. A materialisation barrier that is
// value-for-value the identity, so its shape is its operand's (`Passthrough`), its type its operand's
// (`SameAsFirst`), and its meaning `PerSample` —
// its writing is in `tests/reference/compute_reference.hpp` and checked in `meaning_test.cpp`. It
// carries no attribute (`None`): it copies any shape, symbolic or not.
// Cost `Cheap`: one copy per element, unlike `_order`'s `Software`.
static_assert(rules_of(op::Realize).shape == ShapeRule::Passthrough &&
              rules_of(op::Realize).attr == AttrKind::None &&
              rules_of(op::Realize).type == TypeRule::SameAsFirst &&
              rules_of(op::Realize).meaning == MeaningRule::PerSample &&
              rules_of(op::Realize).cost == CostClass::Cheap && rules_of(op::Realize).arity == 1);

// `_tblread`'s row (S6): a wavetable read whose result has the PHASE's shape (`Passthrough` of operand 0)
// and type (`SameAsFirst`). It carries `AttrKind::Table` (the resolution N), which no shape rule reads —
// the shape is the phase's — so it is an UNPAIRED attribute, `Reduce`'s company. `SameSample` and no lag:
// both operands are read at this sample, the data-dependence is in the table ELEMENT, not the sample. Its
// meaning is `AxisChanging` (a data-dependent element read `compute()` cannot answer) with element kind
// `Gathered`, checked in `meaning_test.cpp`. Cost `Cheap`: a floor, two loads, a lerp.
static_assert(name_of(op::TableRead) == OpName{"_tblread"});
static_assert(rules_of(op::TableRead).shape == ShapeRule::Passthrough &&
              rules_of(op::TableRead).attr == AttrKind::Table &&
              rules_of(op::TableRead).type == TypeRule::SameAsFirst &&
              rules_of(op::TableRead).time == TimeRule::SameSample &&
              rules_of(op::TableRead).meaning == MeaningRule::AxisChanging &&
              rules_of(op::TableRead).cost == CostClass::Cheap && rules_of(op::TableRead).arity == 2);

// `_iota`'s row (S6): the table's FIRST ARITY-0 operation. Its shape and type are DECLARED in `GeneratorAttr`
// (`ShapeRule::Declared` reads `iota.shape`, `TypeRule::Declared` reads `iota.type`) — the pair only a
// generator needs, because there is nothing flowing in to derive either from. Its extents-known-now refusal is
// `sema/attribute_checks.hpp`'s, against the declared shape; `SameSample`, `AxisChanging` with element
// meaning `Indexed` (checked in `meaning_test.cpp`), cost `Cheap`. The arity is genuinely ZERO — the check
// below proves that no longer disqualifies a row, and that a forgotten one is still caught by its enum sentinels.
static_assert(name_of(op::Iota) == OpName{"_iota"});
static_assert(rules_of(op::Iota).arity == 0 &&
              rules_of(op::Iota).attr == AttrKind::Iota &&
              rules_of(op::Iota).shape == ShapeRule::Declared &&
              rules_of(op::Iota).type == TypeRule::Declared &&
              rules_of(op::Iota).time == TimeRule::SameSample &&
              rules_of(op::Iota).meaning == MeaningRule::AxisChanging &&
              rules_of(op::Iota).cost == CostClass::Cheap);

// `_const`'s row (Plan/19-constants): the other generator, `_iota`'s row but for its attribute kind, which says
// its numbers come from the graph's pool.
static_assert(name_of(op::Constant) == OpName{"_const"});
static_assert(rules_of(op::Constant).arity == 0 && rules_of(op::Constant).attr == AttrKind::Constant &&
              rules_of(op::Constant).shape == ShapeRule::Declared && rules_of(op::Constant).type == TypeRule::Declared &&
              rules_of(op::Constant).time == TimeRule::SameSample && rules_of(op::Constant).meaning == MeaningRule::AxisChanging &&
              rules_of(op::Constant).cost == CostClass::Cheap && rules_of(op::Constant).adjoint == AdjointClass::ZeroDerivative);

// `_chain`'s row (`docs/sections-in-lanes.md`): a link, `u = _chain(x, V)`, element 0 of axis 0 `x`, element j
// `V`'s element j - 1, both at this sample. Operand 0 is one section's worth and broadcasts, in the operands' type; the
// timing column says operand 1 is read one element back, now; it moves numbers between elements, so it is
// `AxisChanging`, element kind `Linked` (`meaning_test.cpp`). No spelling: only the rewrite emits it.
static_assert(rules_of(op::Chain) == OpRules{2, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands,
                                             TimeRule::ReadsTheElementBefore, MeaningRule::AxisChanging, CostClass::Cheap,
                                             NumericContract::Exact, AdjointClass::Excluded, Algebra::None, Latency::Add,
                                             RangeRule::Union, Domain::Anywhere, Sharing::OneValue, RunStart::Blind},
              "the link's row, whole");
static_assert(name_of(op::Chain) == OpName{"_chain"} && shown_name(op::Chain) == OpName{"_chain"},
              "no spelling: a refusal names it `_chain`");
static_assert(op::Chain.index == 60, "appended after `_abs`'s 59: every code before it keeps its number, so no hash moves");

// `_first`'s row (Plan/33 S3): 1 at a run's first sample, else 0. No operand and no attribute: one number of no axis,
// typed where it is read as a literal is; `AxisChanging`, element kind `Started` (`meaning_test.cpp`), as `_iota` has
// no per-sample meaning either; [0, 1]. The one row the run-start column marks. No spelling: `dsp::smoothed` reads it.
static_assert(rules_of(op::First) == OpRules{0, AttrKind::None, ShapeRule::OneNumber, TypeRule::Adapts, TimeRule::SameSample,
                                             MeaningRule::AxisChanging, CostClass::Cheap, NumericContract::Exact,
                                             AdjointClass::ZeroDerivative, Algebra::None, Latency::None, RangeRule::Truth,
                                             Domain::Anywhere, Sharing::OneValue, RunStart::Marks},
              "the run start's row, whole");
static_assert(op::First.index == 61 && kOpCount == op::First.index + 1 && shown_name(op::First) == OpName{"_first"},
              "appended after `_chain`'s 60, no spelling");
static_assert([] {
    std::size_t marks = 0;
    for (std::size_t i = 0; i < kOpCount; ++i) {
        marks += kOpTable[i].rules.start == RunStart::Marks ? 1 : 0;
    }
    OpTableRow row = kOpTable[op::Add.index];
    row.rules.start = RunStart::Unclassified;
    return marks == 1 && !every_row_is_complete(&row, 1);
}(), "the run-start column: only `_first` marks a run's start, and a row that omits it is half-filled");
static_assert(!reaches_into_the_past(TimeRule::ReadsTheElementBefore) && !carries_a_lag(rules_of(op::Chain).attr),
              "read at this sample, one element back: it reads no earlier sample, and has no lag to say how far");

// ---- the coverage checks actually reject something ----------------------------------------------
//
// The table's own assertions fire while this file is compiled, so they are known to PASS. That is
// not the same as knowing they would fail, and a check that has only ever been seen to pass is not
// evidence. So the checks are named predicates over a table, and here they are handed deliberately
// broken tables.
//
// This is stronger evidence than a translation unit that must fail to compile, and it is worth
// saying why: a build-failure gate shows the compiler rejecting something, but not WHICH check did
// the rejecting or whether it rejected for the reason intended. These assertions name the defect.

namespace {

// A row with its shape rule left out — what "you added an operation and forgot its shape rule"
// actually looks like in the source.
constexpr OpTableRow kShapeRuleForgotten[] = {
    {"_add", {2, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable}},
    {"_new", {2, AttrKind::None, ShapeRule::Unclassified, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable}},
};

constexpr OpTableRow kTypeRuleForgotten[] = {
    {"_new", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::Unclassified, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable}},
};

// A conversion whose target type has nowhere to live: the rule says "read the attribute" and the
// operation carries none.
constexpr OpTableRow kStatesATypeItCannotStore[] = {
    {"_new", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromAttribute, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable}},
};

// An operation whose shape rule sends the pass to its view's kind, but which carries no rearrangement
// for it to look at. The pass would read a rearrangement that is not there.
constexpr OpTableRow kAsksForAViewItDoesNotHave[] = {
    {"_new", {1, AttrKind::None, ShapeRule::ByViewKind, TypeRule::SameAsFirst, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable}},
};

// The mirror image: it carries a rearrangement, but its shape rule never consults one.
constexpr OpTableRow kCarriesAViewItNeverReads[] = {
    {"_new", {1, AttrKind::View, ShapeRule::Passthrough, TypeRule::SameAsFirst, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable}},
};

// Reaching back into the past with nowhere to say how far. The attribute is where the distance
// lives, so this row describes an operation nobody could ask a question of.
constexpr OpTableRow kReadsThePastWithNoDistance[] = {
    {"_new", {1, AttrKind::None, ShapeRule::Passthrough, TypeRule::SameAsFirst, TimeRule::ReadsEarlierSamples, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable}},
};

// And the other direction: carrying a distance that nothing ever reads back.
constexpr OpTableRow kCarriesADistanceItNeverReads[] = {
    {"_new", {1, AttrKind::Delay, ShapeRule::Passthrough, TypeRule::SameAsFirst, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable}},
};

// The timing column left blank. A planner reading it would have to guess, and guessing "same
// sample" for a delay silently hands its history away to another value.
constexpr OpTableRow kTimingUnstated[] = {
    {"_new", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::Unclassified, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable}},
};

constexpr OpTableRow kImpossibleArity[] = {
    {"_new", {4, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable}},
};

// A forgotten row, the way it actually happens: default-constructed, so its arity is zero. Zero is no
// longer disqualifying on its own (a generator's arity IS zero, `_iota`), so this must still be caught —
// and it is, by the `Unclassified` sentinel every other column carries. The check that arity 0 alone no
// longer rejects is the POSITIVE assertion further down; this is that the hole did not open.
constexpr OpTableRow kEverythingForgotten[] = {
    {"_new", {}},
};

// A well-formed ARITY-0 row (an `_iota`-shaped generator): declared shape and type paired with the Iota
// attribute, every other column stated. It must be ACCEPTED — the whole point of relaxing the lower bound.
constexpr OpTableRow kValidArityZero[] = {
    {"_new", {0, AttrKind::Iota, ShapeRule::Declared, TypeRule::Declared, TimeRule::SameSample, MeaningRule::AxisChanging, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable, Algebra::None, Latency::Add, RangeRule::Generated, Domain::Anywhere, Sharing::OneValue, RunStart::Blind}},
};

// The declared-shape rule with no `Iota` attribute to read the shape from: the pass would read a
// default (empty) shape. A wrong answer rather than a complaint.
constexpr OpTableRow kAsksForDeclaredShapeItDoesNotHave[] = {
    {"_new", {1, AttrKind::None, ShapeRule::Declared, TypeRule::SameAsFirst, TimeRule::SameSample, MeaningRule::AxisChanging, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable}},
};

// The declared-TYPE rule with no `Iota` attribute to read the element type from: it would read a default.
constexpr OpTableRow kAsksForDeclaredTypeItDoesNotHave[] = {
    {"_new", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::Declared, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable}},
};

// And the mirror image: carrying the Iota attribute with rules that never consult it — neither the shape
// nor the type is `Declared`, so both pairings a generator owes are left unfilled.
constexpr OpTableRow kCarriesAnIotaItNeverReads[] = {
    {"_new", {1, AttrKind::Iota, ShapeRule::Passthrough, TypeRule::SameAsFirst, TimeRule::SameSample, MeaningRule::AxisChanging, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable}},
};

// A contraction's shape rule with no contraction attribute to read the summed axes from: the pass
// would collapse whatever the default `Axes` happens to be, which is none of them, and answer the
// broadcast shape. A wrong answer rather than a complaint.
constexpr OpTableRow kAsksForContractedAxesItDoesNotHave[] = {
    {"_new", {2, AttrKind::None, ShapeRule::ContractAxes, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::AxisChanging, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable}},
};

// And the mirror image: carrying the axes with a shape rule that never consults them.
constexpr OpTableRow kCarriesContractedAxesItNeverReads[] = {
    {"_new", {2, AttrKind::Contract, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::AxisChanging, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable}},
};

// The spectral shape rule with no `Fft` attribute to read the transform length from: the pass would
// shape the innermost axis from a default length of zero. A wrong answer rather than a complaint.
constexpr OpTableRow kAsksForSpectralAxisItDoesNotHave[] = {
    {"_new", {1, AttrKind::None, ShapeRule::SpectralAxis, TypeRule::SameAsFirst,
              TimeRule::SameSample, MeaningRule::AxisChanging, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable}},
};

// And the mirror image: carrying the FFT attribute with a shape rule that never consults it.
constexpr OpTableRow kCarriesAnFftItNeverReads[] = {
    {"_new", {1, AttrKind::Fft, ShapeRule::Passthrough, TypeRule::SameAsFirst,
              TimeRule::SameSample, MeaningRule::AxisChanging, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable}},
};

// The overlap-add shape rule with no `OverlapAdd` attribute to read the window and hop from: the pass
// would derive an output length from a default window and hop of zero. A wrong answer rather than a complaint.
constexpr OpTableRow kAsksForOverlapAddAxesItDoesNotHave[] = {
    {"_new", {1, AttrKind::None, ShapeRule::OverlapAddAxes, TypeRule::SameAsFirst,
              TimeRule::SameSample, MeaningRule::AxisChanging, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable}},
};

// And the mirror image: carrying the overlap-add attribute with a shape rule that never consults it.
constexpr OpTableRow kCarriesAnOverlapAddItNeverReads[] = {
    {"_new", {1, AttrKind::OverlapAdd, ShapeRule::Passthrough, TypeRule::SameAsFirst,
              TimeRule::SameSample, MeaningRule::AxisChanging, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable}},
};

// The order shape rule with no `Order` attribute to read the statistic, axis and selection from: the pass
// would order a default axis by a default statistic. A wrong answer rather than a complaint.
constexpr OpTableRow kAsksForOrderAxisItDoesNotHave[] = {
    {"_new", {1, AttrKind::None, ShapeRule::OrderAxis, TypeRule::SameAsFirst,
              TimeRule::SameSample, MeaningRule::AxisChanging, CostClass::Software, NumericContract::Exact, AdjointClass::Differentiable}},
};

// And the mirror image: carrying the order attribute with a shape rule that never consults it.
constexpr OpTableRow kCarriesAnOrderItNeverReads[] = {
    {"_new", {1, AttrKind::Order, ShapeRule::Passthrough, TypeRule::SameAsFirst,
              TimeRule::SameSample, MeaningRule::AxisChanging, CostClass::Software, NumericContract::Exact, AdjointClass::Differentiable}},
};

// The widened paired check still refuses a row that reads the past under an attribute which is
// neither lag — carrying a rearrangement, say, which has nowhere to say how far back.
constexpr OpTableRow kReadsThePastUnderTheWrongAttribute[] = {
    {"_new", {1, AttrKind::View, ShapeRule::ByViewKind, TypeRule::SameAsFirst, TimeRule::ReadsEarlierSamples, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable}},
};

// A signalled lag whose attribute is one of the constant ones: it would be asked for a ceiling by a
// field that does not hold one.
constexpr OpTableRow kSignalledDepthWithAConstantLag[] = {
    {"_new", {2, AttrKind::None, ShapeRule::Passthrough, TypeRule::SameAsFirst, TimeRule::ReadsAtSignalledDepth, MeaningRule::PerSample, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable}},
};


// A link carrying a distance: one element back is always one, so a lag attribute on it is a field nobody fills.
constexpr OpTableRow kLinkCarryingADistance[] = {
    {"_new", {2, AttrKind::Delay, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::ReadsTheElementBefore, MeaningRule::AxisChanging, CostClass::Cheap, NumericContract::Exact, AdjointClass::Excluded, Algebra::None, Latency::Add, RangeRule::Union, Domain::Anywhere}},
};

// The meaning column left blank. It says whether an operation has a sample-at-a-time meaning at all,
// and a row that does not answer leaves `tests/reference/meaning_coverage.hpp`'s check unable to tell a
// missing meaning from an operation that legitimately has none (milestone M3.4).
constexpr OpTableRow kMeaningUnstated[] = {
    {"_new", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::Unclassified, CostClass::Cheap, NumericContract::Exact, AdjointClass::Differentiable}},
};

// The cost column left blank — and left blank the way it actually happens, by writing a row with one
// fewer initialiser rather than by naming the sentinel. Until milestone M4 this was not rejected at
// all: `Cheap` was value zero and `every_row_is_complete` did not look at this column, so a row that
// forgot its cost read as "one multiply-add's worth" and nothing said otherwise.
constexpr OpTableRow kCostUnstated[] = {
    {"_new", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample}},
};

// The adjoint column left blank: autodiff would have to guess whether a cotangent passes.
constexpr OpTableRow kAdjointUnstated[] = {
    {"_new", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample,
              CostClass::Cheap, NumericContract::Exact}},
};

} // namespace

static_assert(!every_row_is_complete(kShapeRuleForgotten, 2), "a missing shape rule must be rejected");
static_assert(!every_row_is_complete(kAdjointUnstated, 1), "a row that omits how its operation differentiates is half-filled");
static_assert(!every_row_is_complete(kTypeRuleForgotten, 1), "a missing element-type rule must be rejected");
static_assert(!every_row_is_complete(kAsksForAViewItDoesNotHave, 1),
              "an operation cannot ask for a rule that lives in an attribute it does not carry");
static_assert(!every_row_is_complete(kCarriesAViewItNeverReads, 1),
              "and it cannot carry a rearrangement its shape rule never reads");
static_assert(!every_row_is_complete(kImpossibleArity, 1), "an arity above 3 must be rejected");
static_assert(!every_row_is_complete(kEverythingForgotten, 1),
              "a default-constructed row is still caught — by its enum sentinels, not its zero arity");
static_assert(every_row_is_complete(kValidArityZero, 1),
              "a well-formed arity-0 generator row is accepted: the lower bound was the only thing in its way");
static_assert(!every_row_is_complete(kAsksForDeclaredShapeItDoesNotHave, 1),
              "a declared-shape rule needs an Iota attribute to read the shape from");
static_assert(!every_row_is_complete(kAsksForDeclaredTypeItDoesNotHave, 1),
              "a declared-type rule needs an Iota attribute to read the element type from");
static_assert(!every_row_is_complete(kCarriesAnIotaItNeverReads, 1),
              "and an Iota attribute whose declared shape and type rules are absent is fields nobody fills");
static_assert(!every_row_is_complete(kTimingUnstated, 1), "a missing timing rule must be rejected");
static_assert(!every_row_is_complete(kMeaningUnstated, 1),
              "a row must SAY whether the operation has a sample-at-a-time meaning");
static_assert(!every_row_is_complete(kCostUnstated, 1),
              "a row that omits its cost is half-filled, and used to read as Cheap");
// The same row with its trailing columns stated is complete, so the assertion above is about them.
static_assert([] {
    OpTableRow priced = kCostUnstated[0];
    priced.rules.cost = CostClass::Software;
    priced.rules.numeric = NumericContract::Exact;
    priced.rules.adjoint = AdjointClass::Differentiable;
    priced.rules.algebra = Algebra::None;
    priced.rules.latency = Latency::Add;
    priced.rules.range = RangeRule::Same;
    priced.rules.domain = Domain::Anywhere;
    priced.rules.sharing = Sharing::OneValue;
    priced.rules.start = RunStart::Blind;
    return every_row_is_complete(&priced, 1);
}(), "stating the trailing columns is all that row was missing");
// A row that says everything but what it does to a range, or where its operands must lie: the proof of ranges
// (`ranges/`) would say nothing of its result, or check no domain (`gates/refusals/row_with_no_range.cpp` builds it).
static_assert([] {
    OpTableRow no_range = kOpTable[op::Div.index];
    OpTableRow no_domain = kOpTable[op::Div.index];
    no_range.rules.range = RangeRule::Unclassified;
    no_domain.rules.domain = Domain::Unclassified;
    return !every_row_is_complete(&no_range, 1) && !every_row_is_complete(&no_domain, 1) &&
           !every_row_is_complete(kRowWithNoRange.data(), kOpCount);
}(), "a row that omits its range rule or its domain is half-filled");
static_assert(rules_of(op::Div).range == RangeRule::Quotient && rules_of(op::Div).domain == Domain::NonZeroSecond &&
                  rules_of(op::Sqrt).range == RangeRule::StrictlyIncreasing && rules_of(op::Sqrt).domain == Domain::NonNegative &&
                  rules_of(op::Rsqrt).range == RangeRule::StrictlyDecreasing && rules_of(op::Log2).range == RangeRule::Whole &&
                  rules_of(op::DelayLine).range == RangeRule::Covering && rules_of(op::TableRead).range == RangeRule::CoveringTable &&
                  rules_of(op::MatMul).range == RangeRule::Summed && rules_of(op::Add).domain == Domain::Anywhere,
              "the range columns: how a result's range follows, and where an operand must lie");
// A row that says everything but what it is to a sum: the rewrite layer would read it as part of none, silently.
static_assert([] {
    OpTableRow row = kOpTable[op::Add.index];
    row.rules.algebra = Algebra::Unclassified;
    return !every_row_is_complete(&row, 1);
}(), "a row that omits what it is to a sum is half-filled");
// A row that says everything but whether two of its nodes over the same values are one: the rewrite that computes
// equal values once (`rewrite/equal_values.hpp`) would have to guess, and a guess of "one" merges two outputs.
static_assert([] {
    OpTableRow row = kOpTable[op::Add.index];
    row.rules.sharing = Sharing::Unclassified;
    return !every_row_is_complete(&row, 1);
}(), "a row that omits whether its nodes are shared is half-filled");
static_assert(rules_of(op::Realize).sharing == Sharing::Own && rules_of(op::Recurrence).sharing == Sharing::Own && [] {
    std::size_t own = 0;
    for (std::size_t i = 0; i < kOpCount; ++i) {
        own += kOpTable[i].rules.sharing == Sharing::Own ? 1 : 0;
    }
    return own == 2;
}(), "the sharing column: every operation computes its value from its operands alone, but an output's materialisation and a variable's definition");
static_assert(kOpTable[op::Add.index].rules.algebra == Algebra::Add && kOpTable[op::Sub.index].rules.algebra == Algebra::Subtract &&
                  kOpTable[op::Neg.index].rules.algebra == Algebra::Negate && kOpTable[op::Mul.index].rules.algebra == Algebra::Product &&
                  kOpTable[op::MultiplyAdd.index].rules.algebra == Algebra::ProductAdd && kOpTable[op::Div.index].rules.algebra == Algebra::None,
              "the algebra column: the sums and products the rewrite layer may reorder and fuse, nothing else");
static_assert(kOpTable[op::Fma.index].rules.algebra == Algebra::None && rules_of(op::MultiplyAdd).cost == CostClass::Cheap,
              "a multiply-add rounded once is no part of a sum (the rule keeps its promise); the one the machine rounds is "
              "one multiply-add's worth in every build");
static_assert(rules_of(op::Add).latency == Latency::Add && rules_of(op::Mul).latency == Latency::Add &&
                  rules_of(op::MultiplyAdd).latency == Latency::MultiplyAdd && rules_of(op::Div).latency == Latency::Divide &&
                  rules_of(op::Sqrt).latency == Latency::Root && rules_of(op::Tanh).latency == Latency::Core &&
                  rules_of(op::FixedDelay).latency == Latency::None && rules_of(op::DelayLine).latency == Latency::Load,
              "the latency column: which of the machine's costs an operation takes (a division is no add, though both are cheap)");
static_assert(!every_row_is_complete(kReadsThePastWithNoDistance, 1),
              "reading the past without a delay attribute leaves nowhere to say how far back");
static_assert(!every_row_is_complete(kCarriesADistanceItNeverReads, 1),
              "carrying a delay attribute that no rule reads back is a field nobody fills correctly");
static_assert(!every_row_is_complete(kStatesATypeItCannotStore, 1),
              "a result type read from an attribute needs an attribute to read");
static_assert(!every_row_is_complete(kAsksForContractedAxesItDoesNotHave, 1),
              "a contraction's summed axes must live somewhere the shape rule can read them");
static_assert(!every_row_is_complete(kCarriesContractedAxesItNeverReads, 1),
              "and axes no shape rule ever collapses are a field nobody fills correctly");
static_assert(!every_row_is_complete(kAsksForSpectralAxisItDoesNotHave, 1),
              "a spectral shape rule needs an FFT attribute to read its transform length from");
static_assert(!every_row_is_complete(kCarriesAnFftItNeverReads, 1),
              "and an FFT attribute no shape rule ever reads is a field nobody fills correctly");
static_assert(!every_row_is_complete(kAsksForOverlapAddAxesItDoesNotHave, 1),
              "an overlap-add shape rule needs an OverlapAdd attribute to read its window and hop from");
static_assert(!every_row_is_complete(kCarriesAnOverlapAddItNeverReads, 1),
              "and an OverlapAdd attribute no shape rule ever reads is a field nobody fills correctly");
static_assert(!every_row_is_complete(kAsksForOrderAxisItDoesNotHave, 1),
              "an order shape rule needs an Order attribute to read its statistic, axis and selection from");
static_assert(!every_row_is_complete(kCarriesAnOrderItNeverReads, 1),
              "and an Order attribute no shape rule ever reads is a field nobody fills correctly");
static_assert(!every_row_is_complete(kReadsThePastUnderTheWrongAttribute, 1),
              "an attribute that is not a lag has nowhere to say how far back");
static_assert(!every_row_is_complete(kSignalledDepthWithAConstantLag, 1),
              "a distance that arrives as a signal still needs an attribute to hold its ceiling");
static_assert(!every_row_is_complete(kLinkCarryingADistance, 1),
              "a link reads no earlier sample, so a lag attribute on it is one no rule reads");

// And the predicates do accept the real tables — the same call the header makes, restated here so
// the positive and the negative sit next to each other.
static_assert(every_row_is_complete(kOpTable, kOpCount));

int main() { return 0; }
