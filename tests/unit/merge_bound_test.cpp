// How much work a feedback stage merges is the machine's (`Machine::merge_work`, Plan/23 04 4.3c), not a stage's
// capacity. Two targets build this file, at the default `HEINZEL_MAX_STAGE_STMTS` and raised to the comparison targets'
// 256, and the planner cuts the same feedback stages in both, as `compile` hands it the graphs (rewritten for the
// machine it plans for: a feedback sum's order is priced on the machine, Plan 25 0.5b, so the bank rewritten for zen2,
// where its stages hold two sections, need not be the bank one stage of four wants):
//
//   bank             32 voices of four low-passes (`Plan/probes/audio-iir/cost_corpus18.cpp`): two stages of two
//                    sections, 0.89x the one stage a raised capacity used to merge them into
//   eight sections   one voice: seven sections in one stage, the eighth with what follows in the next (one sample's
//                    work past 96); a raised capacity used to merge all eight. Each section reads its input as `b0 * x`
//                    and the next as `x * b0`, so no two neighbours are alike and sections in lanes leaves the cascade
//                    serial (`rewrite/like_sections.hpp`; eight like ones run in lanes)
//   merging to 192   a machine that merges further: the bank in one stage at either capacity; the eight sections (97
//                    nodes) in one only where the capacity holds them, else as zen2: the capacity only caps
//
// Plans only: `nest_feedback_chain` runs such stages against loops by hand.

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/planner/plan.hpp"
#include "heinzel/rewrite/rewritten.hpp"
#include "heinzel/sema/analyze.hpp"

#include <array>
#include <cstdio>

using namespace heinzel;

namespace {

namespace loops = heinzel::loops;

constexpr Index n{};
constexpr bool kRaised = loops::kMaxStageStmts >= 256;
constexpr double f(float v) { return v; }

constexpr Graph analysed(Graph g) {
    if (!infer(g).ok) {
        fail("merge_bound_test: a graph did not resolve");
    }
    return g;
}

// `sections` RBJ low-passes in series over `voices` voices (Linkwitz-Riley's, so no parallel form), rewritten for `m`;
// every other one reads its input in the other order, so that no two neighbours are alike.
constexpr Graph cascade(std::int32_t voices, int sections, const Machine& m) {
    Expr s = voices == 1 ? Input{"x", Signal{256}} : Input{"x", Signal{voices, 256}};
    for (int k = 0; k < sections; ++k) {
        const Expr input = k % 2 == 0 ? f(0.003916f) * s : s * f(0.003916f);
        Variable y;
        y[n] = input + f(0.007832f) * s[n - 1] + f(0.003916f) * s[n - 2] - f(-1.815341f) * y[n - 1] - f(0.831006f) * y[n - 2];
        s = y;
    }
    return rewrite::rewritten(analysed(finish(Output{"y"} = s)), m);
}

// The recurrences in each stage the planner makes of `g` for `m`, in order; -1 past the last stage.
constexpr std::array<int, 6> cycles_per_stage(const Graph& g, const Machine& m) {
    const std::expected<loops::Schedule, loops::Refusal> s = loops::planner::schedule_of(g, m);
    if (!s) {
        fail(s.error().view());
    }
    std::array<int, 6> out{-1, -1, -1, -1, -1, -1};
    for (std::size_t k = 0; k < s->stages.size() && k < out.size(); ++k) {
        out[k] = 0;
        for (const std::int32_t ni : s->stages[k].nodes) {
            out[k] += g.nodes[static_cast<std::size_t>(ni)].op == op::Recurrence ? 1 : 0;
        }
    }
    return out;
}

constexpr Machine kFurther = [] {
    Machine m = zen2;
    m.merge_work = 192;
    return m;
}();
constexpr Graph kBank = cascade(32, 4, zen2);
constexpr Graph kEight = cascade(1, 8, zen2);
constexpr Graph kBankFurther = cascade(32, 4, kFurther);
constexpr Graph kEightFurther = cascade(1, 8, kFurther);

static_assert(cycles_per_stage(kBank, zen2) == std::array<int, 6>{2, 2, -1, -1, -1, -1},
              "32 voices of four sections: two stages of two, at the default capacity and raised");
static_assert(cycles_per_stage(kEight, zen2) == std::array<int, 6>{0, 7, 1, -1, -1, -1},
              "eight sections: seven in one stage, the eighth in the next, at the default capacity and raised");
static_assert(cycles_per_stage(kBankFurther, kFurther) == std::array<int, 6>{4, -1, -1, -1, -1, -1},
              "a machine merging further: the bank in one stage, at the default capacity and raised");
static_assert(cycles_per_stage(kEightFurther, kFurther) == (kRaised ? std::array<int, 6>{0, 8, 0, -1, -1, -1} : cycles_per_stage(kEight, zen2)),
              "a machine merging further: the eight sections in one stage where the capacity allows it");

} // namespace

int main() {
    std::printf("merge_bound_test: at a stage capacity of %zu, the planner merges as far as the machine says\n", loops::kMaxStageStmts);
    return 0;
}
