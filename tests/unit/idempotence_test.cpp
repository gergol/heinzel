// M6.5 — analysis is idempotent: running the pass twice over one graph gives an equal result.
//
// This is the constraint old Phase 8 called V4: a pass that rewrites a graph and re-runs analysis is
// only well-defined if analysis has reached a fixpoint the second run agrees with. It retires risk R-S5
// (the re-analysis after a rewrite disagreeing with the first).
//
// Idempotence is `f(f(x)) == f(x)`. It is shown two ways, because the two catch different mistakes:
//
//   * on a PRE-inference graph, `f(f(x))` is compared against `f(x)` directly — the strong form, and
//     the one that would catch a second sweep learning something the first did not. The symbolic
//     case (a block length bound to 64) exercises `resolve_through_solver`, which is where a residue
//     left in a value's shape would make the second run differ — the precise failure R-S5 names.
//   * on an already-resolved graph — every corpus entry is one — `f` must be the IDENTITY: it may
//     change nothing and must still come back `ok`. That is the same property seen on `f`'s image.
//
// And the idempotence CHECK is shown OBJECTING: pointed at a pass that drifts on every run it must
// answer no, or its agreement with `infer` would be evidence of nothing.
#include "heinzel/sema/analyze.hpp"

#include "corpus.hpp"  // the M4/M5-family graphs, each already resolved
#include "filters.hpp" // biquad / comb, for the frontend-built expression the fresh graph reuses

using namespace heinzel;
constexpr Index n{};

namespace {

// `f(f(x)) == f(x)`, for an arbitrary pass over a PRE-inference graph. Generic in the pass so the
// same mechanism can be pointed at `infer` and at a deliberately non-idempotent stub — a check that
// only ever agreed would say nothing.
template <class Pass>
constexpr bool applying_twice_equals_once(Pass apply, Graph fresh) {
    Graph once = fresh;
    apply(once);
    Graph twice = fresh;
    apply(twice);
    apply(twice);
    return once == twice;
}

// A biquad built through the frontend but NOT resolved, so the property under test is the strong
// `f(f(x)) == f(x)` and not the weaker `f(y) == y` on an already-resolved `y`. It is `filters.hpp`'s
// biquad with the one `infer` call left off — the same expression, so the two cannot drift.
constexpr Graph fresh_biquad(Extent block) {
    Input x{"x", Signal{block}};
    Variable y;
    y[n] = x + 1.27279221 * y[n - 1] + -0.81 * y[n - 2];
    return finish(Output{"y"} = y);
}

// The whole result — the resolved graph AND the diagnostic — is stable under a second run. Written
// out rather than folded into `applying_twice_equals_once` because it also pins that every run comes
// back `ok`: a pass that failed on the second run would be non-idempotent in a way a graph
// comparison alone could miss.
constexpr bool infer_result_is_stable(Graph fresh, const Bindings& sizes = Bindings{}) {
    Graph once = fresh;
    const Diagnostic d_once = infer(once, sizes);
    Graph twice = fresh;
    const Diagnostic d_first = infer(twice, sizes);
    const Diagnostic d_second = infer(twice, sizes);
    return d_once.ok && d_first.ok && d_second.ok && once == twice;
}

// `infer` is the identity on a graph it has already resolved: it changes nothing and stays `ok`.
// This is idempotence seen on `f`'s image, and it is what lets a later pass re-run analysis on an
// already-analysed graph without moving anything.
constexpr bool infer_is_identity_on(Graph resolved) {
    const Graph before = resolved;
    const Diagnostic d = infer(resolved);
    return d.ok && resolved == before;
}

// A stand-in analysis pass that never settles: it moves the graph on every run. R-S5's failure has
// this shape — a second analysis disagreeing with the first — and the check must answer no to it.
constexpr auto drifting_analysis = [](Graph& g) { g.values[0].constant += 1.0; };
constexpr auto run_infer = [](Graph& g) { (void)infer(g); };

} // namespace

// ---- the strong form: f(f(x)) == f(x) on a pre-inference graph -----------------------------------
static_assert(applying_twice_equals_once(run_infer, fresh_biquad(literal_extent(64))),
              "analysis run twice over a literal-length biquad must equal analysis run once");
static_assert(infer_result_is_stable(fresh_biquad(literal_extent(64))),
              "and the whole result — the resolved graph and the diagnostic — is stable");

// The symbolic case, which is the one R-S5 is about: a block length that arrives as a symbol bound
// to 64 flows through `resolve_through_solver`, and a residue left in a value's shape after the
// first run is exactly what would make the second run differ. It does not.
static_assert(infer_result_is_stable(fresh_biquad(Extent{Dim{0}}), Bindings{}.bind(Dim{0}, 64)),
              "analysis of a bound-symbol biquad is stable across a second run — no solver residue");

// ---- idempotence on f's image: every resolved corpus graph is left untouched ---------------------
//
// Breadth over the operation set the numeric and structural milestones built, so a family whose
// analysis did not settle would be caught here rather than only on whichever graph happened to reach
// it. Each corpus entry is already resolved, so `infer` must be the identity on it.
static_assert(infer_is_identity_on(corpus_entry(0).graph), "biquad");
static_assert(infer_is_identity_on(corpus_entry(2).graph), "comb");
static_assert(infer_is_identity_on(corpus_entry(3).graph), "biquad, block bound to 64");
static_assert(infer_is_identity_on(corpus_entry(4).graph), "an input read twice");
static_assert(infer_is_identity_on(corpus_entry(5).graph), "a reduction over one axis");
static_assert(infer_is_identity_on(corpus_entry(7).graph), "the elementwise set at M4");
static_assert(infer_is_identity_on(corpus_entry(8).graph), "the transcendental set at M4");
static_assert(infer_is_identity_on(corpus_entry(9).graph), "a contraction of two operands");
static_assert(infer_is_identity_on(corpus_entry(10).graph), "the two lags that arrived at M4");

// ---- the check shown OBJECTING -------------------------------------------------------------------
//
// A pass that drifts on every run is not idempotent, and the same mechanism that accepts `infer`
// must reject it — otherwise its agreement with `infer` would be evidence of nothing.
static_assert(!applying_twice_equals_once(drifting_analysis, fresh_biquad(literal_extent(64))),
              "the idempotence check must reject a pass that changes the graph every run");
static_assert(applying_twice_equals_once(run_infer, fresh_biquad(literal_extent(64))),
              "and it accepts infer through that same check, so the rejection above is not vacuous");

int main() {
    // The same properties while the program runs, which is the half a static_assert cannot show.
    if (!infer_result_is_stable(fresh_biquad(literal_extent(64)))) {
        return 1;
    }
    if (!infer_result_is_stable(fresh_biquad(Extent{Dim{0}}), Bindings{}.bind(Dim{0}, 64))) {
        return 2;
    }
    for (std::size_t i = 0; i < kCorpusSize; ++i) {
        if (!infer_is_identity_on(corpus_entry(i).graph)) {
            return static_cast<int>(i) + 3;
        }
    }
    if (applying_twice_equals_once(drifting_analysis, fresh_biquad(literal_extent(64)))) {
        return 100;
    }
    return 0;
}
