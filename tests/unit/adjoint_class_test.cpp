// Every operation is classified for differentiation, exactly once, and into the class the milestone
// decided.
//
// The completeness itself is the rule table's coverage check (`ir/op_rules.hpp`: a row without its
// adjoint column does not build), so a missing classification never reaches this file. What is left to
// check here is that the decisions are the ones M7 records, and that the three classes partition the
// vocabulary rather than merely covering it: the counts below add back to `kOpCount`, so a row silently
// changed from one class to another moves a count and fails.
#include <cstdint>

#include "heinzel/ir/op_rules.hpp"
#include "heinzel/ir/vocab.hpp"

using namespace heinzel;

namespace {

constexpr AdjointClass adjoint_class(OpCode c) { return rules_of(c).adjoint; }

// How many operations fall in each class, counted over the whole table. A partition, not just a
// cover: the three add back to every operation there is.
struct ClassCounts {
    std::int32_t differentiable{0};
    std::int32_t zero{0};
    std::int32_t excluded{0};
    std::int32_t retired{0}; // a retired code's empty row (`ir/vocab.hpp`)
};

constexpr ClassCounts count_classes() {
    ClassCounts c{};
    for (std::uint8_t code = 0; code < kOpCount; ++code) {
        switch (adjoint_class(OpCode{code})) {
        case AdjointClass::Differentiable:
            ++c.differentiable;
            break;
        case AdjointClass::ZeroDerivative:
            ++c.zero;
            break;
        case AdjointClass::Excluded:
            ++c.excluded;
            break;
        case AdjointClass::Unclassified:
            c.retired += is_retired(OpCode{code}) ? 1 : 0;
            break;
        }
    }
    return c;
}

constexpr ClassCounts kCounts = count_classes();

// The zero-derivative operations are the comparisons, the sign, the four roundings, the two
// generators (the index ramp and the constant tensor) and a run's start — eleven of them, and naming the number here
// means a twelfth cannot be added to that class without this line being revisited.
static_assert(kCounts.zero == 11, "the zero-derivative operations are the six comparisons/sign, the four "
                                  "roundings, the two generators, and a run's start");

// The excluded operations are the recurrence, the two lags, the order statistic, the table read, the
// general contraction and the link — the seven a cotangent cannot pass through here. `_matmul`'s adjoint
// exists (a pair of contractions) but is not carried yet — a reserved M7 extension (MB), so it is
// excluded until that rule is written rather than falsely claimed differentiable. The link (`_chain`),
// which only the rewrite emits, closes a feedback cycle as a lag does.
static_assert(kCounts.excluded == 7, "the excluded operations are the recurrence, the two lags, "
                                     "the order statistic, the table read, the general contraction and the link");

// Everything else differentiates, and the three add back to the whole vocabulary — the check that
// makes this a partition.
static_assert(kCounts.differentiable == kOpCount - kCounts.retired - kCounts.zero - kCounts.excluded);
static_assert(kCounts.differentiable + kCounts.zero + kCounts.excluded + kCounts.retired == kOpCount);

// A spot check that the boundary the file is most careful about is where it says: `_iota` is a ramp
// of positions, so its derivative is genuinely zero, while `_order` is a data-dependent scatter whose
// derivative exists but is not carried — the two are on opposite sides of the zero/excluded line, not
// the same side.
static_assert(adjoint_class(op::Iota) == AdjointClass::ZeroDerivative && adjoint_class(op::Constant) == AdjointClass::ZeroDerivative);
static_assert(adjoint_class(op::Order) == AdjointClass::Excluded);

// Representatives of each class, by name, so a row's class is read rather than assumed.
static_assert(adjoint_class(op::Add) == AdjointClass::Differentiable && adjoint_class(op::Mul) == AdjointClass::Differentiable &&
                  adjoint_class(op::Div) == AdjointClass::Differentiable && adjoint_class(op::Exp) == AdjointClass::Differentiable &&
                  adjoint_class(op::Reduce) == AdjointClass::Differentiable && adjoint_class(op::Contract) == AdjointClass::Differentiable &&
                  adjoint_class(op::Transpose) == AdjointClass::Differentiable && adjoint_class(op::Concat) == AdjointClass::Differentiable &&
                  adjoint_class(op::Fft) == AdjointClass::Differentiable &&
                  adjoint_class(op::Realize) == AdjointClass::Differentiable,
              "a differentiable operation is misclassified");
static_assert(adjoint_class(op::Lt) == AdjointClass::ZeroDerivative && adjoint_class(op::Le) == AdjointClass::ZeroDerivative &&
                  adjoint_class(op::Eq) == AdjointClass::ZeroDerivative && adjoint_class(op::Sign) == AdjointClass::ZeroDerivative &&
                  adjoint_class(op::Floor) == AdjointClass::ZeroDerivative && adjoint_class(op::Ceil) == AdjointClass::ZeroDerivative &&
                  adjoint_class(op::Round) == AdjointClass::ZeroDerivative && adjoint_class(op::Trunc) == AdjointClass::ZeroDerivative,
              "a zero-derivative operation is misclassified");
static_assert(adjoint_class(op::Recurrence) == AdjointClass::Excluded && adjoint_class(op::FixedDelay) == AdjointClass::Excluded &&
                  adjoint_class(op::DelayLine) == AdjointClass::Excluded &&
                  adjoint_class(op::TableRead) == AdjointClass::Excluded && adjoint_class(op::MatMul) == AdjointClass::Excluded &&
                  adjoint_class(op::Chain) == AdjointClass::Excluded,
              "an excluded operation is misclassified");

} // namespace

int main() { return 0; }
