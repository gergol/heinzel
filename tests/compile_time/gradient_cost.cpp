// M7.6 re-take of M2.8: the extent solver's cost at its SECOND call site — the `infer` that
// `gradients()` runs — measured at growing rung counts so its SHAPE, not only its size, is on record
// (standing rule S-6, amendment 03). At M2 the solver had ONE call site (`infer` on a hand-written
// graph); `gradients()` gives it another, over the graph it has just AUGMENTED with adjoint nodes.
// P1-4 asked for both call sites instrumented; this is the second.
//
// Driven by `tools/measure_gradient_cost.sh`, which compiles this once per (rung, regime) with
// -fsyntax-only and clang invoked directly — no cache, no code generation in the number.
//
//   -DHEINZEL_MEASURE_SECTIONS=N   how many differentiable sections in the chain
//   -DHEINZEL_MEASURE_REGIME=0     literal extents: gradients() infers a fully-literal augmented graph
//                              =1  symbolic extents, unbound: the R4-2 branch runs and the solver
//                                  canonicalises the appended adjoint nodes' symbolic shapes
//
// THE CASCADE is differentiable by construction (no recurrence, no delay): a running signal x of
// symbolic extent, and N sections each `y = tanh(y * cₛ)` with cₛ a coefficient of ITS OWN symbolic
// extent — so every section makes exactly one pair of axes meet, which is what makes "the marginal
// cost per section" a question about the solver rather than the pass around it (solver_cascade's own
// reasoning). The loss `mean(y², axis 0)` reduces a symbolic axis, so the R4-2 run-time reciprocal is
// on the differentiated path. `gradients()` then appends ~4N adjoint nodes and infers them: that infer
// is the call site being measured.
#include "heinzel/autodiff/gradients.hpp"
#include "heinzel/sema/analyze.hpp"

#include "hand_built_graph.hpp"

using namespace heinzel;

#ifndef HEINZEL_MEASURE_SECTIONS
#define HEINZEL_MEASURE_SECTIONS 6
#endif
#ifndef HEINZEL_MEASURE_REGIME
#define HEINZEL_MEASURE_REGIME 0
#endif

namespace {

enum class Extents { Literal, SymbolicUnbound };

constexpr Extents kRegime = HEINZEL_MEASURE_REGIME == 0 ? Extents::Literal : Extents::SymbolicUnbound;
constexpr std::int32_t kBlock = 32; // the literal regime's extent; the symbolic one binds nothing

constexpr Extent section_extent(Extents how, int section) {
    return how == Extents::Literal ? literal_extent(kBlock) : Extent{Dim{section}};
}

// Build the N-section differentiable chain and differentiate it wrt the input. Returns the AUGMENTED
// graph — the one gradients() has appended to and inferred, which is the artifact whose solver cost is
// the subject. gradients() runs `infer` twice (once to read shapes, once to resolve the appended
// nodes); the second is the call site M2.8 did not have.
template <int Sections, Extents How>
constexpr Graph gradient_cascade() {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{section_extent(How, 0), 1}, ValueCategory::Input);
    g.inputs.push_back(NamedValue{Name{"x"}, x});
    std::int32_t y = x;
    for (int s = 0; s < Sections; ++s) {
        const auto c = add_value(g, ElemType::F64, Shape{section_extent(How, s + 1), 1}, ValueCategory::Input);
        g.inputs.push_back(NamedValue{Name{"c"}, c});
        y = add_node(g, op::Tanh, add_node(g, op::Mul, y, c)); // one axis-pair meets per section
    }
    OpAttr mean{};
    mean.kind = AttrKind::Reduce;
    mean.reduce.op = ReduceOp::Mean;
    mean.reduce.axes = Axes{0};
    const auto loss = add_node(g, op::Reduce, add_node(g, op::Mul, y, y), kNoValue, kNoValue, mean);
    g.outputs.push_back(NamedValue{Name{"loss"}, loss});
    autodiff::gradients(g, loss, {x}); // appends the adjoints and infers them — the measured site
    return g;
}

constexpr Graph kRung = gradient_cascade<HEINZEL_MEASURE_SECTIONS, kRegime>();

// The work must not be optimisable away, and it must actually have FINISHED: an unresolved value would
// make the pass do less work, so a rung that quietly failed to resolve would report a cheerful number
// for a graph it never analysed.
static_assert(kRung.nodes.size() > 0);
static_assert([] {
    for (const Value& v : kRung.values) {
        if (v.type == ElemType::Unknown) {
            return false;
        }
    }
    return true;
}(), "the differentiated rung resolved completely");

// The two regimes really differ, so this is not two measurements of the same thing: the literal one
// has no symbol left after gradients(); the unbound one still carries its symbols.
static_assert(is_static(kRung) == (kRegime == Extents::Literal),
              "the literal augmented graph has no symbol; the unbound one does");

} // namespace

int main() { return kRung.nodes.size() > 0 ? 0 : 1; }
