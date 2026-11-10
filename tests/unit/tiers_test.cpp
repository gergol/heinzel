// MILESTONE M3b-2: the numeric-precision axis, from a value's .precision() mark to the core that runs.
//
// THE SEAM. A value's compile-time-resolved precision reaches the arithmetic down every code path that
// runs a graph — the walk and the loop tree (`Executor`). `infer`'s data-flow pass
// (M3b-1) resolves each value's precision (select-higher, seeded Normal); the four consumption sites
// (M3b-2) read it through `numeric_setting_of` instead of the retired run-time NumericSetting axis. A
// precision that reaches one runner and not another is a graph that computes different numbers
// depending on how it is run, which is the one thing this tree is built not to do.
//
// TWO FUNCTION BEHAVIOURS, not three — owner decision C (2026-09-15). The three-level precision model
// maps High -> {Exact, Reproducible}, Normal -> {Exact, Free}, Fast -> {Approx, Free}. High and Normal
// name the SAME transcendental cores and differ only in Ordering (reassociation), which nothing reads
// yet, so they are bit-identical today; Fast alone reaches the coarse (~1e-4) cores. So the claim this
// file can make is that a Fast-marked graph differs from a Normal/High/unmarked one, and that the
// runners agree at each precision. The old ~1e-2 `Fast` FnAccuracy core is not reachable through a
// .precision() mark; its fate is M3c's, with the run-time axis it belongs to.
//
// What each precision COSTS in accuracy is oracle O10 and is next door in `tier_bounds_test.cpp`.
//
// The operations covered are M4.2's fifteen: the exponential family's six, the periodic four, the
// inverse circular four, and `_pow`. The loop tree runs nine of them (`_exp`, `_log`, `_tanh`, `_sin`,
// `_sinpi`, `_cos` with a core in the value's own type, `_tan`, `_exp2`, `_pow` on their double cores one number at a
// time; the graphs here are F64, so every one runs its double core), and `main` runs both runners on each but `_tan`,
// whose input here crosses a pole (`cos_tan_pow_exp2_test.cpp` runs it); the other six it refuses for now, and the
// walk is their one runner.
//
// WHY THE PRECISIONS MUST DIFFER, asserted rather than assumed. A coarse precision implemented by
// calling the exact core would pass every accuracy bound in this file — it would be more accurate than
// required everywhere. So each operation is required to give a DIFFERENT answer at Fast than at Normal.
// That is the assertion that would fail if the routing were removed, and it is the reason this file can
// claim the coarse cores exist rather than that they are named.
#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"

#include <array>
#include <cstddef>
#include <string_view>

#include "executed.hpp"
#include "interpret.hpp"
#include "one_operation.hpp"

using namespace heinzel;
using heinzel::testing::apply_at;

namespace {

// Eight samples. The question here is whether a precision reaches the arithmetic, which one sample
// settles and eight settle with room; the sixty-four-sample agreement is M4.1's condition and is
// checked over M4.1's corpus.
constexpr std::size_t kBlock = 8;

// Strictly positive, so the three logarithms are in domain, and small enough that an exponential
// stays finite. Not an impulse: seven of these eight samples would be zero, and `log(0)` is minus
// infinity, which no comparison here could then tell apart from another minus infinity.
constexpr std::array<double, kBlock> kInput{0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0};

// `_asin` and `_acos` answer NaN outside [-1, 1], and a NaN is never equal to itself — so a block
// containing one would fail every comparison below for a reason that has nothing to do with precision.
// The last sample is close enough to one to exercise the branch that is delicate there.
constexpr std::array<double, kBlock> kUnitInput{0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875, 0.9375};

constexpr std::array<double, kBlock> input_for(OpCode which) {
    return which.index == op::Asin.index || which.index == op::Acos.index ? kUnitInput : kInput;
}

// ---- one operation, as a graph, at a named precision ---------------------------------------------
//
// Built through the frontend rather than by hand, deliberately: the precision is pinned with the
// `.precision()` mark a user writes — a local per-value fact, no context, no ambient state — and
// `infer` resolves it onto the value the runners read. A hand-built node with its Value's `precision`
// field poked would test the runners and skip the frontend.
constexpr Graph one_operation_at(OpCode which, numerics::Precision p) {
    // F64, and it is not a detail. Every runner rounds a value to its own element type on the way
    // out, so at `F32` a difference below a float ulp — six e-eight relative — is not merely hard to
    // see, it is GONE. The inverse circular family's approximate cores differ from their exact ones by
    // about 5e-10, which is exactly that case: built at `F32` this file would have reported that
    // `_atan` computes the same thing at every precision, and been right about the graph while wrong
    // about the cores. What is being asked here is a question about arithmetic, so the graph is
    // built in the type the arithmetic runs in.
    // Declared above 0, as both inputs are, so the proof of ranges sees the logarithms in their domain.
    Input x{"x", Signal{static_cast<std::int32_t>(kBlock)}, ElemType::F64, gt(0)};
    // A two-operand operation gets a literal for its second, read off the rule table rather than
    // decided per operation: `_atan2` and `_pow` both arrive that way, and a list of which
    // operations take two would go stale the day a third does. The core is chosen by the precision
    // mark it is made with; the retired run-time NumericSetting axis no longer rides the node at all.
    Expr root = (rules_of(which).arity == 2 ? apply(which, x, constant(0.7))
                                            : apply(which, x)).precision(p);
    Graph g = finish(Output{"y"} = root);
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("heinzel::tiers_test: the one-operation graph did not resolve");
    }
    return g;
}

template <OpCode Which, numerics::Precision P>
inline constexpr Graph kGraph = one_operation_at(Which, P);

template <OpCode Which, numerics::Precision P>
constexpr Graph graph_of() {
    return one_operation_at(Which, P);
}

// ---- the same graph, run two ways ----------------------------------------------------------------

constexpr std::array<double, kBlock> walked(const Graph& g, const std::array<double, kBlock>& in) {
    Signals<kBlock> signals{};
    run(g, in, signals);
    std::array<double, kBlock> out{};
    for (std::size_t n = 0; n < kBlock; ++n) {
        out[n] = sample_at(signals, g.outputs[0].value, 0, n);
    }
    return out;
}

template <OpCode Which, numerics::Precision P>
std::array<double, kBlock> executed(const std::array<double, kBlock>& in) {
    Executor<graph_of<Which, P>> ex;
    testing::held(ex);
    std::array<double, kBlock> out{};
    ex.input("x") = in.data();
    ex.output("y") = out.data();
    ex.run();
    return out;
}

// Both runners, at one precision, required to agree BIT FOR BIT. A tolerance here would defeat the
// point: the two are the same arithmetic reached two ways, and the precision is the same precision.
template <OpCode Which, numerics::Precision P>
bool runners_agree() {
    const std::array<double, kBlock> in = input_for(Which);
    return walked(kGraph<Which, P>, in) == executed<Which, P>(in);
}

constexpr bool differs_somewhere(const std::array<double, kBlock>& a, const std::array<double, kBlock>& b) {
    for (std::size_t n = 0; n < kBlock; ++n) {
        if (a[n] != b[n]) {
            return true;
        }
    }
    return false;
}

// The precision reaches the cores. High, Normal and an unmarked value all name the SAME (Exact) cores
// and are bit-identical — decision C makes High and Normal differ only in the still-vacuous Ordering
// axis, and an unmarked value resolves to Normal — while Fast reaches the coarse cores and so differs
// somewhere.
//
// Asked of the WALK alone, and that is not a narrower claim than it looks. `main` requires the runners
// to agree at each precision, so a precision that differs under the walk differs under both. Repeating
// the comparison per runner would be repeating a composition the two checks already make.
template <OpCode Which>
constexpr bool precision_selects_the_core() {
    const std::array<double, kBlock> in = input_for(Which);
    const std::array<double, kBlock> normal   = walked(kGraph<Which, numerics::Precision::Normal>, in);
    const std::array<double, kBlock> high     = walked(kGraph<Which, numerics::Precision::High>, in);
    const std::array<double, kBlock> unmarked = walked(kGraph<Which, numerics::Precision::Inherit>, in);
    const std::array<double, kBlock> fast     = walked(kGraph<Which, numerics::Precision::Fast>, in);
    return normal == high && normal == unmarked && differs_somewhere(normal, fast);
}

} // namespace

// ---- and the coarse precision is a different answer, while High/Normal/unmarked are one -----------
static_assert(precision_selects_the_core<op::Exp>(),
              "`_exp` computes the same thing at Fast as at Normal");
static_assert(precision_selects_the_core<op::Exp2>(),
              "`_exp2` computes the same thing at Fast as at Normal");
static_assert(precision_selects_the_core<op::Log>(),
              "`_log` computes the same thing at Fast as at Normal");
static_assert(precision_selects_the_core<op::Log2>(),
              "`_log2` computes the same thing at Fast as at Normal");
static_assert(precision_selects_the_core<op::Log10>(),
              "`_log10` computes the same thing at Fast as at Normal");
static_assert(precision_selects_the_core<op::Tanh>(),
              "`_tanh` computes the same thing at Fast as at Normal");
static_assert(precision_selects_the_core<op::Sin>(),
              "`_sin` computes the same thing at Fast as at Normal");
static_assert(precision_selects_the_core<op::Cos>(),
              "`_cos` computes the same thing at Fast as at Normal");
static_assert(precision_selects_the_core<op::Tan>(),
              "`_tan` computes the same thing at Fast as at Normal");
static_assert(precision_selects_the_core<op::SinPi>(),
              "`_sinpi` computes the same thing at Fast as at Normal");
static_assert(precision_selects_the_core<op::Atan>(),
              "`_atan` computes the same thing at Fast as at Normal");
static_assert(precision_selects_the_core<op::Asin>(),
              "`_asin` computes the same thing at Fast as at Normal");
static_assert(precision_selects_the_core<op::Acos>(),
              "`_acos` computes the same thing at Fast as at Normal");
static_assert(precision_selects_the_core<op::Atan2>(),
              "`_atan2` computes the same thing at Fast as at Normal");
static_assert(precision_selects_the_core<op::Pow>(),
              "`_pow` computes the same thing at Fast as at Normal");

// ---- an unresolved tier is refused rather than quietly taken for `Exact` --------------------------
//
// The half that cannot be a `static_assert`: under constant evaluation a `fail()` is a compile
// error by construction, so this is the world where it can be watched happening. `compute` still takes
// a resolved FnAccuracy — the four sites now hand it one derived from precision — and `Inherit` is
// still not a tier. Both the refusal and its message, because the message is what tells the author of
// a fourth runner what they left out — "it threw" would not.
namespace {

bool an_unresolved_tier_is_refused() {
    Node n{};
    n.op = op::Exp;
    try {
        (void)compute(n, FnAccuracy::Inherit, 1.0, 0.0, 0.0);
        return false;
    } catch (const error& e) {
        if (e.message()
            != std::string_view{"heinzel::compute: FnAccuracy::Inherit is not a tier; resolve the "
                                "node against its graph"}) {
            return false;
        }
    }
    // And a core reached directly says the same thing in its own words, because a family that is
    // added later will be reached that way before `compute` ever sees it.
    try {
        (void)terms_at(kExponentialSeries, FnAccuracy::Inherit);
        return false;
    } catch (const error& e) {
        return e.message()
               == std::string_view{"heinzel: a core was asked for FnAccuracy::Inherit, which is not "
                                   "a tier but a request"};
    }
}

} // namespace

// ---- the seam: the precision reaches the arithmetic, in both runners --------------------------------
// At Fast (the coarse cores) and at High (the exact cores under the reproducible ordering), the two
// non-default marks, each required to reach both runners identically.
template <OpCode Which>
bool reaches_both_runners() {
    return runners_agree<Which, numerics::Precision::Fast>() && runners_agree<Which, numerics::Precision::High>();
}

int main() {
    if (!(reaches_both_runners<op::Exp>() && reaches_both_runners<op::Log>() && reaches_both_runners<op::Tanh>() &&
          reaches_both_runners<op::Sin>() && reaches_both_runners<op::SinPi>() && reaches_both_runners<op::Cos>() &&
          reaches_both_runners<op::Exp2>() && reaches_both_runners<op::Pow>())) {
        return 2;
    }
    return an_unresolved_tier_is_refused() ? 0 : 1;
}
