// The planner and lowering agree: over a corpus of graphs shaped like the ones the surfaces built before
// they left the tree (Plan/archive/14-dsl DS-20), `try_compile` either hands back a program or a refusal by name — lowering
// never refuses what the planner accepted (both say `heinzel::compile:`, so the planner is asked alone, of the graph
// prepared as `try_compile` prepares it). Each graph is built twice, its stream a run-time symbol and a literal 64,
// and each verdict is pinned: which graphs lower, and which are refused with what — and how many of each, so the corpus
// is the one pinned here.
//
// The corpus is `tests/support/agreement_graphs.hpp` (from `Plan/probes/p5_surface.cpp`, P5, `Plan/archive/make-default/02`
// §4). `Plan/probes/p5_python_corpus/` reran this check over the graphs of the Python suite, which left the tree with
// the binding.

#include "heinzel/compile.hpp"
#include "heinzel/loops/planner/plan.hpp"
#include "heinzel/sema/analyze.hpp"

#include <cstdio>
#include <expected>
#include <memory>
#include <string_view>

#include "agreement_graphs.hpp"
#include "well_formed.hpp"

using namespace heinzel;
using namespace agreement_graphs;

namespace {

// Does the planner alone make a schedule of `g`, prepared as `try_compile` prepares it?
bool planner_accepts(Graph g) {
    if (!infer(g, Bindings{}).ok) {
        return false;
    }
    const std::unique_ptr<const std::expected<loops::Schedule, loops::Refusal>> d(new auto(loops::planner::schedule_of(g, zen2)));
    return d->has_value();
}

} // namespace

int main() {
    int wrong = 0;
    int lowered = 0;
    int refused = 0;
    int unanalysed = 0;
    for (const bool symbolic : {true, false}) {
        sym = symbolic;
        for (const Expected& one : kCorpus) {
            const Graph g = one.build(); // outside the try: a graph that fails while being built is a test bug
            const std::string_view want = symbolic ? one.symbolic : one.literal;
            try {
                // (on the heap: a result holds a program of about half a megabyte)
                const std::unique_ptr<const CompileResult> r(new auto(try_compile(g, zen2)));
                const std::string_view got = r->refusal.view();
                if (got.empty()) {
                    if (const std::expected<void, loops::Refusal> w = loops::well_formed(r->compiled.program); !w) {
                        ++wrong;
                        std::printf("  %s (%s): the runner does not accept its program: %.*s\n", one.name, symbolic ? "symbolic" : "literal",
                                    static_cast<int>(w.error().size()), w.error().data());
                    }
                }
                ++(got.empty() ? lowered : got.starts_with("heinzel::compile: ") ? refused : unanalysed);
                if (want.empty() ? !got.empty() : !got.starts_with(want)) {
                    ++wrong;
                    std::printf("  %s (%s): got \"%.*s\"\n", one.name, symbolic ? "symbolic" : "literal",
                                static_cast<int>(got.size()), got.data());
                }
                if (!got.empty() && planner_accepts(g)) {
                    ++wrong;
                    std::printf("  %s (%s): the lowering refuses what the planner accepted\n", one.name, symbolic ? "symbolic" : "literal");
                }
            } catch (const error& e) {
                ++wrong; // try_compile hands every refusal back: a throw is a stage failing outright
                std::printf("  %s (%s): try_compile threw: %.*s\n", one.name, symbolic ? "symbolic" : "literal",
                            static_cast<int>(e.message().size()), e.message().data());
            }
        }
    }
    // (both variants: the graphs that lower, those the planner or the lowering refuses, those the analysis refuses)
    if (lowered != 78 || refused != 27 || unanalysed != 1) {
        ++wrong;
        std::printf("  %d lowered, %d refused, %d not analysed: the corpus is not the one pinned here\n", lowered, refused, unanalysed);
    }
    if (wrong != 0) {
        std::printf("nest_agreement_test: %d wrong\n", wrong);
        return 1;
    }
    std::printf("nest_agreement_test: the planner and lowering agree on the surface corpus\n");
    return 0;
}
