#pragma once

// Schedules the corpus takes back one decision of, or schedules for a machine with caches small enough that a small
// graph shows what a large one would — shared by the tests that plan the same programs (`nest_contract`,
// `nest_odd_shapes`, `nest_v6_review`, `loops_workspace`). Each edits the planner's schedule (`planned::schedule_of`),
// which `planned::program_of(g, schedule)` then lowers.

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/timing.hpp"
#include "heinzel/loops/schedule.hpp"
#include "heinzel/schedule/machine.hpp"

namespace edited_schedules {

using namespace heinzel;

// A machine whose caches are tiny: a 50 x 21 contraction is tiled like a large one.
inline constexpr Machine kTinyCaches{.vector_bytes = 32, .registers = 16, .l1_bytes = 1024, .l2_bytes = 2048, .page_bytes = 256, .relay_pays_after = 1, .fma = true};

// The schedule with no operand relaid: the chunks are seen over the operand as it was fed.
constexpr loops::Schedule fed_as_it_is(loops::Schedule d) {
    for (loops::Schedule::Stage& stage : d.stages) {
        stage.relaid = -1;
    }
    return d;
}

// The first lag of `g` (a node reading a value's past), or the lag that reads the past of value `of`.
constexpr std::int32_t lag_reading(const Graph& g, std::int32_t of) {
    for (std::size_t n = 0; n < g.nodes.size(); ++n) {
        if (samples_back(g.nodes[n]) > 0 && (of == kNoValue || g.nodes[n].inputs[0] == of)) {
            return static_cast<std::int32_t>(n);
        }
    }
    return -1;
}

// A lag of a lag with its two lags exchanged in stage 0's node list: the lag of the lag then runs first, and the lag
// it reads is itself handed on to the next turn. Reading a value's past does not depend on where in the body it is
// read, and the lowering accepts it (a past read asks which STAGE made the value, not which statement).
constexpr loops::Schedule lags_swapped(const Graph& g, loops::Schedule d) {
    const std::int32_t one = lag_reading(g, kNoValue);
    const std::int32_t two = lag_reading(g, g.nodes[static_cast<std::size_t>(one)].output);
    decltype(loops::Schedule::Stage::nodes) order{};
    for (const std::int32_t n : d.stages[0].nodes) {
        order.push_back(n == one ? two : n == two ? one : n);
    }
    d.stages[0].nodes = order;
    return d;
}

} // namespace edited_schedules
