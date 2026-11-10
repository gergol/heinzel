// The numeric contract's two halves: the rule-table column that says what numeric freedom an operation
// HAS (`NumericContract`), and `numerics::grants`, which says what a value's precision PERMITS. Whether
// a freedom is taken is the schedule's business and is tested there.

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/support/fail.hpp"

using namespace heinzel;

namespace {

// The numeric-freedom column left blank, and only it: the adjoint after it is stated, so the row is refused for the
// numeric reason alone.
constexpr OpTableRow kNumericFreedomUnstated[] = {
    {"_new", {1, AttrKind::None, ShapeRule::Elementwise, TypeRule::FromOperands, TimeRule::SameSample, MeaningRule::PerSample, CostClass::Cheap,
              NumericContract::Unclassified, AdjointClass::Differentiable}},
};

} // namespace

static_assert(!every_row_is_complete(kNumericFreedomUnstated, 1),
              "a row that omits what numeric freedom its operation has is half-filled");

// What the column says, row by row where it is not `Exact`: the four folds, the one selection, the one
// intrinsic, a transcendental standing for the tiered fifteen (`tier_bounds_test` covers the rest), and the one
// multiply-add the machine rounds.
static_assert(rules_of(op::Reduce).numeric == NumericContract::Reducible &&
                  rules_of(op::Contract).numeric == NumericContract::Reducible &&
                  rules_of(op::MatMul).numeric == NumericContract::Reducible &&
                  rules_of(op::OverlapAdd).numeric == NumericContract::Reducible,
              "a fold's order of combination is its freedom");
static_assert(rules_of(op::Order).numeric == NumericContract::Selection, "order statistics select, exactly");
static_assert(rules_of(op::Fft).numeric == NumericContract::Intrinsic, "the transform is defined by its reference");
static_assert(rules_of(op::Tanh).numeric == NumericContract::Approximate, "a transcendental's accuracy is tiered");
static_assert(rules_of(op::Add).numeric == NumericContract::Exact && rules_of(op::Sqrt).numeric == NumericContract::Exact,
              "one correctly rounded step has no freedom, and is the same number in float");
static_assert(rules_of(op::Fma).numeric == NumericContract::ExactInDouble &&
                  rules_of(op::Rsqrt).numeric == NumericContract::ExactInDouble &&
                  rules_of(op::DelayLine).numeric == NumericContract::ExactInDouble &&
                  rules_of(op::TableRead).numeric == NumericContract::ExactInDouble,
              "a fixed sequence of steps has none either, but is defined in double (native_f32_is_exact_test)");
static_assert(rules_of(op::MultiplyAdd).numeric == NumericContract::MachineRounded,
              "a multiply-add rounded as the machine rounds: its rounding is the build's, a freedom only where granted");
static_assert([] {
    int free = 0;
    for (std::size_t i = 0; i < kOpCount; ++i) {
        const NumericContract c = kOpTable[i].rules.numeric;
        free += c != NumericContract::Exact && c != NumericContract::ExactInDouble && !is_retired(OpCode{static_cast<std::uint8_t>(i)}) ? 1 : 0;
    }
    return free == 4 + 1 + 1 + 15 + 1;
}(), "and nothing else in the table has one");

// What a precision grants (`numerics::grants`): High nothing, Normal and Fast every ordering freedom,
// and only Fast the approximate cores.
static_assert(numerics::grants(numerics::Precision::High) ==
                  numerics::Grants{false, FnAccuracy::Exact},
              "High grants nothing: it is the bit-exact promise");
static_assert(numerics::grants(numerics::Precision::Normal) ==
                  numerics::Grants{true, FnAccuracy::Exact},
              "Normal may reorder (a fold's terms in lanes or tiles, a fused multiply-add), at full accuracy");
static_assert(numerics::grants(numerics::Precision::Fast) ==
                  numerics::Grants{true, FnAccuracy::Approx},
              "Fast adds the approximate cores");

// An unresolved precision has no grants: "nothing" would read exactly like High.
int main() {
    try {
        (void)numerics::grants(numerics::Precision::Inherit);
        return 1;
    } catch (const error&) {
        return 0;
    }
}
