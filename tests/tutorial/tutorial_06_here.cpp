// Tutorial chapter 06 — "debugging and numerics" (tutorial/06-debugging-and-numerics.md), built in
// THIS tree's language, run on `Executor`, and its results printed (M8.3, S5). Grown one part at a time.
//
// WHAT THIS CHAPTER IS, AND WHY IT IS RUN-AND-CHECK RATHER THAN CROSS-TREE-COMPARED. The old chapter
// has three parts: (1) `run_pipeline(g, t)` — the compiler pass run as a plain-runtime value you can
// print and step through, showing stages/arena; (2) a broken graph failing with a NAMED diagnostic
// rather than a template-error wall; (3) the numerics precisions — sin(x) at the default (exact) and
// at a coarse precision, each core staying inside its declared error bound. Only part (3) produces
// numbers, and they are NOT cross-tree comparable: the coarse core is this tree's OWN minimax/series
// core (in runtime/periodic_cores.hpp with its own M4.2 term count and measured bound; the old tree's
// is a different polynomial), so a coarse sin here and there are two different approximations by
// design. What IS checkable, and is the chapter's actual subject, is that EACH precision stays inside
// the bound THIS tree declares for it — a self-contained run-and-assert, the ch08 discipline (internal
// checks) rather than the ch04 discipline (cross-tree numeric spine). Part (1)'s RMS-normalize graph
// is the same row-RMS-normalize ch08 already built and cross-compared to the old tree, so it is not
// re-compared here; part (1)'s inspectable-compiler subject re-expresses as this tree's compile-time
// program facts. This step (c1) is part (3).
//
// TWO PRECISIONS, not three — owner decision C (2026-09-15), wired at M3b-2. The core an operation
// runs is now chosen by its result's compile-time-resolved `.precision()`, and the three-level model
// reaches only two function behaviours: `Normal`/`High` name the EXACT core, and `Fast` names the
// coarse (~1e-4) Approx core. The old ~1e-2 three-term `Fast` FnAccuracy core is not reachable through
// a `.precision()` mark; it belongs to the run-time NumericSetting axis this rework retires in M3c,
// which is where the `sin(x, Accuracy)` spelling and the third tier go.
//
// The declared per-precision bounds for sine, from runtime/periodic_cores.hpp (its own words): the
// Approx core is 4 terms, worst 3.1e-7 over the reduced argument; the exact default runs the EXACT
// core, which the module measures at <= 2 ulp over [-pi, pi]. The check below asserts each precision
// inside its bound AND asserts the two genuinely DIFFER, so a precision silently not taking effect
// (which would make the bound pass vacuously) is caught.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "heinzel/compile.hpp"
#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

constexpr std::size_t N = 64; // the chapter's signal length

// sin(x) at a chosen precision. The default (unmarked, which `infer` resolves to `Normal`) runs the
// exact core and is spelled with the plain `sin(x)`; a coarse precision carries a per-value
// `.precision(Fast)` mark, the M3b-2 spelling of the old chapter's `sin(x, Numerics::Fast)`.
constexpr Graph build_sin(numerics::Precision p) {
    // F64 storage, deliberately: the precisions differ by their transcendental CORE's accuracy (the
    // declared bound 3.1e-7 is the Approx core's math error, measured in double). At the default F32
    // storage, f32 rounding (~6e-8) would floor the exact precision and eat the Approx core's headroom,
    // hiding what the chapter is about. In F64 each precision shows at its declared accuracy.
    Input x{"x", Signal{N}, ElemType::F64};
    Expr y = (p == numerics::Precision::Inherit) ? sin(x) : sin(x).precision(p);
    Graph g = finish(Output{"y"} = y);
    if (!infer(g).ok) {
        fail("the tutorial-06 sin graph did not resolve");
    }
    return g;
}

template <numerics::Precision P>
constexpr Graph sin_at() {
    return build_sin(P);
}

// Part (1): the compiler's plan is an inspectable VALUE. The old chapter calls run_pipeline(g, t) and
// reads back the schedule's stages and the arena size — the compiler pass run as plain data you can
// print. Its SUBJECT — the compiler's decisions are inspectable values, not a hidden consteval-only
// step — re-expresses two ways this tree has: infer() returns a Diagnostic value (part 2), and
// compile() produces the loop-tree program, whose stages, statements and buffers are compile-time
// CONSTANTS. Shown here on the sin graph; ch08 shows the same on the RMS-normalize graph the old
// part (1) inspects (that graph overlaps ch08's already-cross-compared norm, so it is not rebuilt).
constexpr Compiled kSinCompiled = compile(sin_at<numerics::Precision::Inherit>());
static_assert(kSinCompiled.program.stages.size() > 0,
              "the sin graph's compiled program has no stages — the inspectable-plan claim is vacuous");

// The chapter's own probe signal, computed through float so its input bits are well-defined; the
// range [-3.2, 3.1] crosses zero and both quadrant boundaries, so the argument reduction is exercised.
std::array<double, N> probe() {
    std::array<double, N> x{};
    for (std::size_t i = 0; i < N; ++i) {
        x[i] = static_cast<double>(0.1f * static_cast<float>(i) - 3.2f);
    }
    return x;
}

// Run a sin graph on `Executor` over `x` and return its N outputs.
template <auto Build>
std::array<double, N> run_tier(const std::array<double, N>& x) {
    std::array<double, N> y{};
    Executor<Build> ex;
    testing::held(ex);
    ex.input("x") = x.data();
    ex.output("y") = y.data();
    ex.run();
    return y;
}

double max_error_vs_true_sin(const std::array<double, N>& y, const std::array<double, N>& x) {
    double worst = 0.0;
    for (std::size_t n = 0; n < N; ++n) {
        worst = std::fmax(worst, std::fabs(y[n] - std::sin(x[n])));
    }
    return worst;
}

// Part (2): a broken graph is refused with a NAMED diagnostic, not a template-error wall. `infer()`
// is ordinary code that returns a Diagnostic VALUE — the chapter's dual-mode point: the SAME pass
// that runs under consteval for the graphs above runs here at plain runtime, so a deliberately
// mismatched graph can be inspected (printed, stepped) instead of aborting the build. Two inputs of
// disagreeing shapes are added; the pass refuses and its message names the operation and both shapes.
// finish() only lays out nodes, so building this graph does not fail — the refusal is infer()'s, and
// it is a returned value, checked below at runtime.
constexpr Graph build_broken() {
    Input x{"x", Signal{4}};
    Input y{"y", Signal{8}};
    return finish(Output{"z"} = x + y);
}

int g_fail = 0;
void check(const char* name, bool ok) {
    if (!ok) {
        ++g_fail;
    }
    std::printf("  %-24s %s\n", name, ok ? "ok" : "FAIL");
}

} // namespace

int main() {
    std::printf("06 debugging & numerics:\n");

    // Part (1): the compiler's plan, read back as compile-time facts.
    std::printf("  plan: stages=%zu statements=%zu buffers=%zu\n", kSinCompiled.program.stages.size(),
                kSinCompiled.program.stmts.size(), kSinCompiled.program.buffers.size());
    check("plan is inspectable", kSinCompiled.program.stages.size() > 0);

    const std::array<double, N> x = probe();
    const double ep = max_error_vs_true_sin(run_tier<sin_at<numerics::Precision::Inherit>>(x), x);
    const double ea = max_error_vs_true_sin(run_tier<sin_at<numerics::Precision::Fast>>(x), x);
    std::printf("  sin error: precise=%.2e approx=%.2e\n", ep, ea);

    // Each precision inside the exact bound THIS tree declares for it (periodic_cores.hpp:40-51): the
    // Approx sine core is worst 3.1e-7. The default precision runs the exact core, asserted at machine
    // precision. These are the module's own declared numbers, not fitted margins; measured
    // (1.11e-16 / 1.86e-07) they sit comfortably under.
    check("precise ~ machine (<=1e-12)", ep <= 1e-12);
    check("approx <= 3.1e-7 (declared)", ea <= 3.1e-7);

    // Non-vacuity: the two precisions genuinely differ, so the coarse one is not silently running the
    // exact core. A precision that did not take effect would make its bound pass for the wrong reason.
    check("precisions are distinct", ep < ea);

    // Part (2): the broken graph, inspected as a runtime value.
    Graph broken = build_broken();
    const Diagnostic d = infer(broken);
    const std::string_view msg = d.message.view();
    std::printf("  diagnostic: ok=%d \"%.*s\"\n", d.ok ? 1 : 0, static_cast<int>(msg.size()), msg.data());
    check("broken graph refused", !d.ok);
    check("diagnostic names the op", msg.find("at +:") != std::string_view::npos);

    std::printf("%s\n", g_fail == 0 ? "PASS" : "FAIL");
    return g_fail == 0 ? 0 : 1;
}
