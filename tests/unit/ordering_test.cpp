// MILESTONE M4.2, the ordering axis: what `Ordering::Reproducible` PROMISES, and what
// `Ordering::Free` promises today — which is a different kind of statement, and the difference is
// the subject of this file.
//
// The condition is written in two halves and only the second has an escape clause:
//
//   Reproducible  an expression that could be contracted into a fused multiply-add is REQUIRED to
//                 equal its unfused reference bit for bit. A promise, and gated here.
//   Free          contraction is PERMITTED, not required — so nothing may demand it be observed.
//                 What is demanded instead is a graph whose two answers differ, measured; or, if no
//                 such graph can be produced because nothing in this tree ever reassociates, THAT
//                 recorded as the finding. It is the second branch, and `docs/decisions.md` carries
//                 the record. This file carries its teeth.
//
// NOT the same subject as `two_worlds/fusion_probe.cpp`, which is a paragraph worth having because
// the two look alike. That one asks whether `-ffp-contract=off` does its work when THIS TREE'S OWN
// SOURCE is compiled — a question about C++. This one asks what a GRAPH at a named ordering answers,
// which is a question about the ordering axis, and it would still need answering on a tree compiled
// with contraction on.
#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/rewrite/rewritten.hpp"
#include "heinzel/runtime/instruction_set.hpp"
#include "heinzel/sema/analyze.hpp"

#include "executed.hpp"
#include "interpret.hpp"

#include <array>
#include <cstddef>
#include <string_view>

using namespace heinzel;

namespace {

// One sample is all this asks. The question is what one multiply-add answers, and a block of eight
// would be the same question answered eight times.
constexpr std::size_t kBlock = 1;

// `a * a + c` with `a = 1 + 2^-27` and `c = -1`, which is the input the whole file turns on.
//
// The exact product is `1 + 2^-26 + 2^-54`. A double cannot hold the last term at that exponent, so
// a ROUNDED product is `1 + 2^-26` and the answer is `2^-26`. A product kept exactly until the
// addition — which is what a fused multiply-add does, and what `_fma` promises in software — keeps
// it, and the answer is `2^-26 + 2^-54`. One instruction selection, one ulp of difference, and it
// is the difference the ordering axis is ABOUT.
constexpr double kOnePlusHalfAnUlp = 0x1.0000002p0;
constexpr double kMinusOne = -1.0;
constexpr double kUnfusedAnswer = 0x1p-26;               // the rounded product, then the sum
constexpr double kFusedAnswer = 0x1.0000001p-26;         // 2^-26 + 2^-54, the product kept exact

// F64, stated rather than defaulted, and finding S12-F32HIDES is the reason to think about it at
// all: `input()` builds an `F32` value unless told otherwise, and this file's whole subject is a
// difference of 2^-54 against an answer of 2^-26 — 2^-28 relative, four orders below a float ulp.
//
// What actually happens at `F32` is worse than the difference being hidden, and it was MEASURED
// rather than assumed: the INPUT is not representable either. `1 + 2^-27` rounds to 1, every answer
// becomes 0, and all three assertions below turn red at once. Either way the file would be
// measuring nothing; recorded this way because "would be green" was the guess, and it was wrong.
constexpr ElemType kType = ElemType::F64;

constexpr numerics::Precision level_of(Ordering ordering) {
    return ordering == Ordering::Reproducible ? numerics::Precision::High : numerics::Precision::Normal;
}

// The multiply-add as two operations, at a named ordering. This is the graph a user writes when they
// write `a * b + c`, and the one a contraction pass would rewrite.
constexpr Graph unfused_at(Ordering ordering) {
    // The ordering a graph runs at is its values' precision: High is Reproducible, Normal is Free.
    Input x{"x", Signal{1}, kType};
    Expr product = apply(op::Mul, x, x);
    Expr root = apply(op::Add, product, constant(kMinusOne));
    Graph g = finish(Output{"y"} = root);
    set_default_precision(g, level_of(ordering));
    if (const Diagnostic d = infer(g); !d.ok) {
        fail("heinzel::ordering_test: the unfused graph did not resolve");
    }
    return g;
}

// The same arithmetic spelled as ONE operation that promises a single rounding. `_fma` is
// `CostClass::Software` and tier-invariant — it is not a coarse anything — and it is exactly what a
// contracting pass would leave behind, which is what makes it the right other side of the
// comparison rather than merely a second graph.
constexpr Graph fused_at(Ordering ordering) {
    Input x{"x", Signal{1}, kType};
    Expr root = apply(op::Fma, x, x, constant(kMinusOne));
    Graph g = finish(Output{"y"} = root);
    set_default_precision(g, level_of(ordering));
    if (const Diagnostic d = infer(g); !d.ok) {
        fail("heinzel::ordering_test: the fused graph did not resolve");
    }
    return g;
}

inline constexpr Graph kUnfusedReproducible = unfused_at(Ordering::Reproducible);
inline constexpr Graph kUnfusedFree = unfused_at(Ordering::Free);
static_assert(kUnfusedReproducible.values[static_cast<std::size_t>(kUnfusedReproducible.outputs[0].value)].precision ==
                      numerics::Precision::High &&
                  kUnfusedReproducible != kUnfusedFree,
              "the two rungs really are different graphs: one resolved at High, one at Normal");
inline constexpr Graph kFusedReproducible = fused_at(Ordering::Reproducible);

// ---- the same graph, run two ways ----------------------------------------------------------------
//
// Walked and on the loop tree (`Executor`), because the promise is about what a graph answers and not
// about which runner asked. A contraction that happened in one of them and not the other would be a
// promise kept in one world and broken in another, which is the failure M3.2 exists to make impossible
// and which a single-runner check here would not see. The walk is checked while compiling, the loop
// tree in `main`.
constexpr std::array<double, kBlock> kInput{kOnePlusHalfAnUlp};

constexpr double walked(const Graph& g) {
    Signals<kBlock> signals{};
    run(g, kInput, signals);
    return sample_at(signals, g.outputs[0].value, 0, 0);
}

constexpr Graph unfused_reproducible() { return kUnfusedReproducible; }
constexpr Graph unfused_free() { return kUnfusedFree; }
constexpr Graph fused_reproducible() { return kFusedReproducible; }

template <auto Build, const Machine& M = default_machine>
double executed() {
    Executor<Build, M> ex;
    testing::held(ex);
    double out = 0.0;
    ex.input("x") = kInput.data();
    ex.output("y") = &out;
    ex.run();
    return out;
}

} // namespace

// ---- the promise: at `Reproducible`, the unfused reference, bit for bit --------------------------
static_assert(walked(kUnfusedReproducible) == kUnfusedAnswer,
              "at `Ordering::Reproducible` a multiply-add answered something other than its unfused "
              "reference — the one thing that rung promises");

// ---- and the teeth, without which the assertion above is green on a tree that contracts ----------
//
// If the reference and the graph were BOTH contracted, the comparison above would hold and mean
// nothing. So the fused spelling is required to answer something else at this very input: that is
// what makes "the unfused reference" a claim about the ANSWER rather than about the spelling, and
// it is simultaneously the evidence that this input separates the two forms at all.
static_assert(walked(kFusedReproducible) == kFusedAnswer,
              "`_fma` did not keep the product exact at the input this file turns on");
static_assert(kUnfusedAnswer != kFusedAnswer,
              "the chosen input does not distinguish a fused multiply-add from an unfused one, so "
              "nothing in this file is measuring anything");

// ---- at `Free`, a freedom taken only by a rule written down ---------------------------------------
//
// `Ordering::Free` PERMITS contraction and reassociation (`numerics::grants`). A grant is a permission,
// and taking one up is a planning decision that has to be written down where it can be read. The walk runs
// the graph as written and takes none: the permissive rung answers there exactly what the reproducible one
// does. (Recorded first as a freedom nothing in the tree ever took: `docs/decisions.md`, "Decision 11".)
//
// It is not trivially true, and the two assertions above are why: they establish that this graph, at
// this input, in these bits, is one where contraction WOULD be visible — and that the value it would
// then take is `kFusedAnswer`. If THIS assertion goes red, the walk started fusing on its own account,
// and that is a defect.
static_assert(walked(kUnfusedFree) == kUnfusedAnswer,
              "the walk took a numeric freedom on its own account: at `Ordering::Free` it no longer "
              "answers what `Ordering::Reproducible` does");

// The loop tree runs the rewritten graph, and one rule there takes the grant (Decision 73,
// `rewrite/product_adds.hpp`): on a machine with a fused multiply-add, an add of a product becomes `_madd`,
// rounded as the machine rounds. Only there: not at `Reproducible`, not on a machine without the instruction.
constexpr bool multiply_added(const Graph& g) {
    for (const Node& node : g.nodes) {
        if (node.op == op::MultiplyAdd) {
            return true;
        }
    }
    return false;
}
static_assert(multiply_added(rewrite::rewritten(kUnfusedFree, zen2)), "at `Free` on a machine with fma, the rule fuses");
static_assert(!multiply_added(rewrite::rewritten(kUnfusedReproducible, zen2)), "nothing fuses at `Reproducible`");
static_assert(!multiply_added(rewrite::rewritten(kUnfusedFree, default_machine)), "nor on a machine without fma");

// What the fused graph answers is the build's: rounded once where it has the instruction (an arm64 build, an x86
// one with `-mfma`), twice where not (the x86 test builds, no `-march`), which is the unfused answer.
constexpr double kMachineRoundedAnswer = kMultiplyAddIsOneInstruction ? kFusedAnswer : kUnfusedAnswer;

// ---- the machine's multiply-add: a freedom only where granted ------------------------------------
//
// `_madd` rounds as the machine rounds (`NumericContract::MachineRounded`): once where the build has the
// instruction, twice where it does not. The rewrite layer emits one only where the precision grants reordering;
// built by hand at `Reproducible`, its bits would follow the build, so the analysis refuses it there.
constexpr Diagnostic machine_rounded_at(Ordering ordering) {
    Input x{"x", Signal{1}, kType};
    Graph g = finish(Output{"y"} = apply(op::MultiplyAdd, x, x, constant(kMinusOne)));
    set_default_precision(g, level_of(ordering));
    return infer(g);
}
static_assert(!machine_rounded_at(Ordering::Reproducible).ok &&
                  machine_rounded_at(Ordering::Reproducible).message.view().find("rounds as the machine rounds") != std::string_view::npos,
              "a multiply-add rounded as the machine rounds was accepted where nothing grants that freedom");
static_assert(machine_rounded_at(Ordering::Free).ok, "at `Free` the machine's rounding is granted");

int main() {
    if (executed<unfused_reproducible>() != kUnfusedAnswer) return 1;
    if (executed<fused_reproducible>() != kFusedAnswer) return 2;
    // The loop tree took a freedom no rule wrote down: on a machine without a fused multiply-add, the unfused answer;
    // on one with it, the machine's.
    if (executed<unfused_free>() != kUnfusedAnswer) return 3;
    if (executed<unfused_free, zen2>() != kMachineRoundedAnswer) return 4;
    return 0;
}
