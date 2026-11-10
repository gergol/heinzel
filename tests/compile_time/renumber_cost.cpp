// What a canonical renumbering pass would cost, and that it would actually work.
//
// MEASUREMENT ONLY. `canonical_renumber` is not part of the library — see the header for why it
// exists. Operator decision 2026-08-25: do not change the mechanism, but record the number so a
// later decision about Q-2 starts from evidence rather than from an estimate.
//
// Built only with -DHEINZEL_BUILD_COMPILE_TIME_GUARD=ON, and even then excluded from the default
// build and never registered with ctest, for the same three reasons the guard is.
//
// Timings are in `docs/measurements/renumber-cost.md`, produced by
// `tools/measure_renumber_cost.sh`.

#include "canonical_renumber.hpp"

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"

#include <cstddef>

using namespace heinzel;
constexpr Index n{};
using heinzel::measurement::canonical_renumber;

namespace {

// ---- the property, demonstrated rather than asserted -------------------------------------------
//
// The same expression, `x*2 + x*3`, with the two products created in the two possible orders. The
// SYNTAX is identical — `a + b` either way — so a correct graph identity should not be able to tell
// them apart. Emission order can, which is the whole of Q-2, reproduced here without needing a
// second compiler: what gcc does to one expression is what writing the temporaries in the other
// order does to any compiler.

constexpr Graph made_left_first() {
    Input x{"x", Signal{64}};
    const Expr a = x * 2.0;
    const Expr b = x * 3.0;
    return finish(Output{"y"} = a + b);
}

constexpr Graph made_right_first() {
    Input x{"x", Signal{64}};
    const Expr b = x * 3.0; // the only difference: this one exists first
    const Expr a = x * 2.0;
    return finish(Output{"y"} = a + b);
}

// The defect, stated as a test: today these are different graphs.
static_assert(!(made_left_first() == made_right_first()),
              "if these ever compare equal, emission order stopped deciding identity and Q-2 is moot");

// And the candidate closes it.
static_assert(canonical_renumber(made_left_first()) == canonical_renumber(made_right_first()),
              "a walk from the outputs gives one identity to one expression");

// Renumbering is a permutation, not an edit: nothing is invented and — for a graph whose nodes are
// all reachable — nothing is lost.
static_assert(canonical_renumber(made_left_first()).values.size() == made_left_first().values.size());
static_assert(canonical_renumber(made_left_first()).nodes.size() == made_left_first().nodes.size());

// Already canonical is a fixed point. A pass that kept shuffling would still pass the test above.
static_assert(canonical_renumber(canonical_renumber(made_left_first())) == canonical_renumber(made_left_first()));

// ---- the same ladder the guard measures, so the two numbers are comparable ---------------------
//
// Identical to `guard.cpp`'s cascade, deliberately: comparing this file's timings against that
// file's only means something if the graph being built is the same graph.

template <int Sections>
constexpr Graph cascade() {
    Expr signal = Input{"x", Signal{64}};

    for (int s = 0; s < Sections; ++s) {
        Variable stage;
        const double feedback1 = 1.2 + 0.001 * s;
        const double feedback2 = -0.81;
        stage[n] = signal - feedback1 * stage[n - 1] - feedback2 * stage[n - 2];
        signal = stage;
    }

    Graph g = finish(Output{"y"} = signal);
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the cascade did not resolve");
    }
    return g;
}

#ifndef HEINZEL_MEASURE_SECTIONS
#define HEINZEL_MEASURE_SECTIONS 24
#endif

// With HEINZEL_MEASURE_RENUMBER defined, the rung pays for the walk as well as the build. Without
// it, this file measures exactly what the guard measures — which is the baseline the difference is
// taken against, measured through the same file so the headers cannot account for the gap.
#ifdef HEINZEL_MEASURE_RENUMBER
constexpr Graph rung = canonical_renumber(cascade<HEINZEL_MEASURE_SECTIONS>());
#else
constexpr Graph rung = cascade<HEINZEL_MEASURE_SECTIONS>();
#endif

// A recurrence still names its variable after the move, and every operand still precedes its use.
// Without this the pass could be fast because it was wrong.
static_assert([] {
    for (const Node& n : rung.nodes) {
        for (std::size_t k = 0; k < kMaxNodeInputs; ++k) {
            if (n.inputs[k] != kNoValue && n.inputs[k] >= n.output) {
                return false;
            }
        }
        if (n.attr.kind == AttrKind::Recurrence && n.attr.recurrence.defines == kNoValue) {
            return false;
        }
    }
    return true;
}(), "operands precede their uses, and no recurrence lost the variable it defines");

} // namespace

int main() { return rung.nodes.size() > 0 ? 0 : 1; }
