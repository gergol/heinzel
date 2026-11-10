// The model's staging of feedback (`loops/graph/staging.hpp` `stages_of`, what the rewrites price) against the
// planner's (`loops/planner/chains.hpp` `chains_on`), on graphs rewritten for zen2: per pair of cycles, one stage in
// both or apart in both. One-poles in series at this sample and through a lag (the planner reads what the open stage
// makes, lags included: `reads_the_open`), and two rooms on one input, each handing its values to its own reader (the
// corpus has no graph of two cycles). Not here: a cycle the planner takes ahead of one it refused (`next_unit` scans every ready
// unit), which the model, a scan of the feedback order, does not; a fanned pair of one-poles, each read through a lag
// by one more, stages so.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/graph/staging.hpp"
#include "heinzel/rewrite/rewritten.hpp"
#include "heinzel/schedule/machine.hpp"

#include "executed.hpp"
#include "planned.hpp"

using namespace heinzel;

namespace {

template <bool Lagged>
constexpr Graph series() {
    constexpr Index n{};
    Input x{"x", Signal{Length{}}};
    Variable a;
    Variable b;
    Variable c;
    Variable d;
    a[n] = 0.5 * a[n - 1] + x;
    b[n] = 0.5 * b[n - 1] + (Lagged ? Expr{a[n - 1]} : Expr{a});
    c[n] = 0.5 * c[n - 1] + (Lagged ? Expr{b[n - 1]} : Expr{b});
    d[n] = 0.5 * d[n - 1] + (Lagged ? Expr{c[n - 1]} : Expr{c});
    Output y{"y"};
    y = d;
    return finish(y);
}

constexpr Graph two_rooms() {
    constexpr Index n{};
    Input x{"x", Signal{Length{}}};
    Variable left_comb;
    Variable left_pass;
    Variable right_comb;
    Variable right_pass;
    const Expr in = x * 0.25;
    left_comb[n] = 0.7 * left_comb[n - 5] + in;
    left_pass[n] = 0.5 * left_pass[n - 1] + left_comb;
    right_comb[n] = 0.6 * right_comb[n - 7] + in;
    right_pass[n] = 0.5 * right_pass[n - 1] + right_comb;
    Output y{"y"};
    y = left_pass + right_pass;
    return finish(y);
}

template <auto Build>
bool agrees(const char* name) {
    const Graph g = rewrite::rewritten(testing::analysed<Build>(0), zen2);
    const loops::graph::Facts facts = loops::graph::facts_of(g);
    const loops::graph::Feedback feedback = loops::graph::find_feedback(g, facts);
    const std::vector<std::int32_t> target = loops::graph::detail::leads_into(g, facts, feedback);
    const std::vector<char> shares = loops::graph::detail::makes_the_domain(g, feedback, target);
    const std::vector<cost::Stage> alone = loops::graph::cycle_stages(g, facts, feedback, zen2);
    const std::vector<std::int32_t> one_group(g.nodes.size(), 0);
    const std::vector<std::int32_t> model = loops::graph::detail::stages_of(g, facts, feedback, target, shares, alone, one_group, zen2);
    const loops::Schedule planned = planned::schedule_of(g, zen2);
    std::vector<std::int32_t> stage_of(g.nodes.size(), -1);
    for (std::size_t s = 0; s < planned.stages.size(); ++s) {
        for (const std::int32_t i : planned.stages[s].nodes) {
            stage_of[static_cast<std::size_t>(i)] = static_cast<std::int32_t>(s);
        }
    }
    std::vector<std::size_t> cycles{};
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        if (feedback.cycle[i] == static_cast<std::int32_t>(i)) {
            cycles.push_back(i);
        }
    }
    bool ok = cycles.size() > 1;
    for (const std::size_t a : cycles) {
        for (const std::size_t b : cycles) {
            const bool together = model[a] == model[b];
            const bool planned_together = stage_of[a] == stage_of[b];
            if (together != planned_together) {
                std::printf("%s: n%zu and n%zu %s in the model, %s in the planner\n", name, a, b, together ? "together" : "apart",
                            planned_together ? "together" : "apart");
                ok = false;
            }
        }
    }
    return ok;
}

} // namespace

int main() {
    const bool ok = agrees<series<false>>("series") && agrees<series<true>>("series through lags") && agrees<two_rooms>("two rooms");
    std::printf("staging_mirror: %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
