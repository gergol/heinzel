// Schedules written by hand that the lowering must refuse by name (`nest_grouping_test.cpp` holds the groupings that
// stand): sibling folds over different axes of one domain, and a contraction sharing its stage with a fold.

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/notation/einsum.hpp"
#include "heinzel/sema/analyze.hpp"
#include "planned.hpp"

#include <cstdint>
#include <cstdio>

using namespace heinzel;

namespace {

namespace loops = heinzel::loops;

constexpr std::int32_t kR = 9;
constexpr std::int32_t kC = 70;

constexpr Graph analysed(Graph g) {
    if (!infer(g).ok) {
        fail("nest_grouping_refusals_test: a graph did not resolve");
    }
    return g;
}

// A value at compile time: `planned::refusal_of`.
constexpr Graph two_ways() {
    Input x{"x", {kR, kC}};
    Output a{"a"};
    Output b{"b"};
    a = x.sum(1);
    b = x.sum(0);
    return analysed(finish(a, b));
}
constexpr Graph contracted_then_folded() {
    Input a{"a", {4, 5}};
    Input b{"b", {5, 6}};
    return analysed(finish(Output{"y"} = einsum("ik,kj->ij", a, b).sum(1)));
}
constexpr Graph kTwoWays = two_ways();
constexpr Graph kContracted = contracted_then_folded();

constexpr loops::Schedule both_folds_by_hand() {
    loops::Schedule::Stage both{.inner_from = 2};
    auto& members = both.nodes;
    members.push_back(0);
    members.push_back(1);
    both.loops.push_back(loops::Schedule::Loop{.axis = 2, .step = 1, .kind = loops::LoopKind::Serial});
    both.loops.push_back(loops::Schedule::Loop{.axis = 0, .step = 1, .kind = loops::LoopKind::Serial});
    both.loops.push_back(loops::Schedule::Loop{.axis = 1, .step = 1, .kind = loops::LoopKind::Serial});
    loops::Schedule d{};
    d.stages.push_back(both);
    return d;
}
static_assert(planned::refusal_of(kTwoWays, both_folds_by_hand()).view().starts_with("heinzel::compile: sibling folds in one group must collapse the same axes"),
              "sibling folds over different axes must be refused by name");

constexpr loops::Schedule contraction_beside_a_fold() {
    loops::Schedule d = planned::schedule_of(kContracted, zen2);
    auto& contracted_members = d.stages[0].nodes;
    contracted_members.push_back(d.stages[1].nodes[0]);
    d.stages.pop_back();
    return d;
}
static_assert(planned::refusal_of(kContracted, contraction_beside_a_fold()).view().starts_with("heinzel::compile: a contraction's group holds no second fold"),
              "a contraction and a fold in one stage must be refused by name");

} // namespace

int main() {
    std::printf("nest_grouping_refusals: every hand-written schedule refused by name\n");
    return 0;
}
