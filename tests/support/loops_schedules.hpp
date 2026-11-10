#pragma once

// Schedules written by hand for the lowering's tests (`loops_lower`, `loops_primitives`): a stage's nodes, its loops,
// a stage walked as it stands, and what the lowering answers — the program's refusal, the runner's rule its program
// breaks (`well_formed.hpp`), or "lowered". It includes only `ir/`, `loops/`, `support/` and `well_formed.hpp`, so
// `loops_alone` admits it.

#include <algorithm>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <string_view>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/timing.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/graph/regions.hpp"
#include "heinzel/loops/lower.hpp"
#include "heinzel/loops/primitives/loops.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/schedule.hpp"
#include "heinzel/support/fixed_vector.hpp"

#include "well_formed.hpp"

namespace loops_schedules {

using heinzel::loops::LoopKind;
using heinzel::loops::Schedule;

constexpr Schedule::Stage stage(std::initializer_list<std::int32_t> nodes, std::int32_t inlined = 0, std::int32_t through = -1) {
    decltype(Schedule::Stage::nodes) members{};
    for (const std::int32_t n : nodes) {
        members.push_back(n);
    }
    return Schedule::Stage{.nodes = members, .inlined = inlined, .through = through};
}

constexpr Schedule::Loop serial(std::int32_t axis) { return Schedule::Loop{.axis = axis}; }
constexpr Schedule::Loop tile(std::int32_t axis, std::int32_t chunk) { return Schedule::Loop{.axis = axis, .step = chunk, .kind = LoopKind::Tile}; }
constexpr Schedule::Loop unrolled(std::int32_t axis, std::int32_t bodies) { return Schedule::Loop{.axis = axis, .step = bodies, .kind = LoopKind::Unroll}; }
constexpr Schedule::Loop lanes(std::int32_t axis, std::int32_t width, std::int32_t blocks = 1) {
    return Schedule::Loop{.axis = axis, .step = width, .kind = LoopKind::Vector, .unroll = blocks};
}

// A planned stage: its nodes, its loops outermost first, and where its inner loops start.
constexpr Schedule::Stage planned(const Schedule::Stage& members, std::initializer_list<Schedule::Loop> loops, std::int32_t inner_from) {
    Schedule::Stage ps{.nodes = members.nodes, .inlined = members.inlined, .through = members.through, .inner_from = inner_from};
    for (const Schedule::Loop& l : loops) {
        ps.loops.push_back(l);
    }
    return ps;
}

// A stage walked as it stands: one serial loop an axis, in memory order — the axes a fold keeps, then those it
// collapses, a contraction's contracted axis last; a value made in parts, its own shape's — its inner loops where those
// loops imply them (none: after them all). `facts` are `g`'s, built once for all its stages.
constexpr Schedule::Stage walked(const heinzel::Graph& g, const heinzel::loops::graph::Facts& facts, const Schedule::Stage& members) {
    namespace graph = heinzel::loops::graph;
    Schedule::Stage ps{.nodes = members.nodes, .inlined = members.inlined, .through = members.through};
    if (ps.nodes.empty()) {
        return ps;
    }
    const heinzel::Node& first = g.nodes[static_cast<std::size_t>(ps.nodes[0])];
    const bool parts = ps.nodes.size() == 1 && graph::is_made_in_parts(first);
    const graph::GroupShape shape = graph::shape_of_group(g, ps);
    const bool contraction = !parts && shape.fold >= 0 && graph::is_a_contraction(g.nodes[static_cast<std::size_t>(shape.fold)]);
    heinzel::loops::Skeleton s{};
    for (const bool collapsed : {false, true}) {
        for (const std::int32_t axis : graph::memory_order(parts ? g.values[static_cast<std::size_t>(first.output)].shape.rank : shape.domain.rank)) {
            if (contraction ? !collapsed : ((shape.collapsed >> axis & 1u) != 0) == collapsed) {
                ps.loops.push_back(serial(axis));
            }
        }
    }
    if (contraction) {
        ps.loops.push_back(serial(shape.domain.rank));
    }
    for (const Schedule::Loop& l : ps.loops) {
        s.walk.push_back(heinzel::loops::Node{.kind = heinzel::loops::NodeKind::Loop, .axis = l.axis});
    }
    const std::int32_t outer = heinzel::loops::implied_inner(g, facts, members, s).outer;
    ps.inner_from = outer >= 0 ? outer : static_cast<std::int32_t>(ps.loops.size());
    return ps;
}
// ...and one stage alone, the facts built for it.
constexpr Schedule::Stage walked(const heinzel::Graph& g, const Schedule::Stage& members) {
    return loops_schedules::walked(g, heinzel::loops::graph::facts_of(g), members);
}

constexpr Schedule schedule(std::initializer_list<Schedule::Stage> stages, std::initializer_list<Schedule::Band> bands = {}) {
    Schedule d{};
    for (const Schedule::Stage& ps : stages) {
        d.stages.push_back(ps);
    }
    for (const Schedule::Band& b : bands) {
        d.bands.push_back(b);
    }
    return d;
}

// A schedule of these stages, each walked as it stands.
constexpr Schedule stages(const heinzel::Graph& g, std::initializer_list<Schedule::Stage> members) {
    const heinzel::loops::graph::Facts facts = heinzel::loops::graph::facts_of(g);
    Schedule d{};
    for (const Schedule::Stage& s : members) {
        d.stages.push_back(loops_schedules::walked(g, facts, s));
    }
    return d;
}

constexpr Schedule::Band band(std::int32_t first, std::int32_t count, std::int32_t axis, std::int32_t rows, bool parallel = false) {
    return Schedule::Band{.first = first, .count = count, .axis = axis, .step = rows, .parallel = parallel};
}

// Every node of `g` with a statement in one feedback stage whose sample holds `g`'s fold (Plan/23 10), laid out as the
// planner lays one out: a lag of what the stage made earlier just before its maker, then the nodes over the fold's
// result that come before the fold, the lanes over its operand (the fold among them), the rest over its result; the
// voices in lanes of `width`, as many blocks as they fill, time innermost, the inner loop.
constexpr Schedule in_one_sample(const heinzel::Graph& g, std::int32_t width) {
    namespace graph = heinzel::loops::graph;
    const graph::Facts facts = graph::facts_of(g);
    const std::size_t count = g.nodes.size();
    std::vector<std::int32_t> lag_before(count, -1); // (one lag a maker is all these graphs need)
    std::vector<std::int32_t> order{};
    std::int32_t fold = -1;
    for (std::size_t i = 0; i < count; ++i) {
        const heinzel::Node& n = g.nodes[i];
        if (graph::hands_its_operand_on(n) && heinzel::samples_back(n) > 0) {
            const std::int32_t maker = facts.producer[static_cast<std::size_t>(facts.read_as[static_cast<std::size_t>(n.inputs[0])])];
            if (maker >= 0 && maker < static_cast<std::int32_t>(i) && facts.statement[static_cast<std::size_t>(maker)]) {
                lag_before[static_cast<std::size_t>(maker)] = static_cast<std::int32_t>(i);
            }
        }
        fold = fold < 0 && graph::is_a_fold(n) ? static_cast<std::int32_t>(i) : fold;
    }
    for (std::size_t i = 0; i < count; ++i) {
        const bool lag = std::find(lag_before.begin(), lag_before.end(), static_cast<std::int32_t>(i)) != lag_before.end();
        if (lag_before[i] >= 0) {
            order.push_back(lag_before[i]);
        }
        if (facts.statement[i] && !graph::is_generated(g.nodes[i]) && !lag) {
            order.push_back(static_cast<std::int32_t>(i));
        }
    }
    const heinzel::Shape& folded = g.values[static_cast<std::size_t>(g.nodes[static_cast<std::size_t>(fold)].output)].shape;
    const auto part = [&](std::int32_t i, bool past_fold) {
        const bool over_result = i != fold && g.values[static_cast<std::size_t>(g.nodes[static_cast<std::size_t>(i)].output)].shape == folded;
        return !over_result ? 1 : past_fold ? 2 : 0;
    };
    decltype(Schedule::Stage::nodes) members{};
    for (const int which : {0, 1, 2}) {
        bool past_fold = false;
        for (const std::int32_t i : order) {
            if (part(i, past_fold) == which) {
                members.push_back(i);
            }
            past_fold = past_fold || i == fold;
        }
    }
    const heinzel::Extent voices = heinzel::extent_at(g.values[static_cast<std::size_t>(g.nodes[static_cast<std::size_t>(fold)].inputs[0])].shape, 0);
    const std::int32_t blocks = (static_cast<std::int32_t>(heinzel::literal_of(voices)) + width - 1) / width;
    return schedule({planned(Schedule::Stage{.nodes = members}, {lanes(0, width, blocks), serial(1)}, 1)});
}

// The program, or the refusal or the rule it breaks.
constexpr std::expected<heinzel::loops::Program, heinzel::loops::Refusal> lowered(const heinzel::Graph& g, const Schedule& d) {
    std::expected<heinzel::loops::Program, heinzel::loops::Refusal> result{std::in_place};
    const std::expected<void, heinzel::loops::Refusal> r = heinzel::loops::lower_into(g, d, *result);
    if (const std::expected<void, heinzel::loops::Refusal> w = r ? heinzel::loops::well_formed(*result) : r; !w) {
        result = std::unexpected(w.error());
    }
    return result;
}

// What the lowering answers: its refusal, the rule its program breaks, or "lowered".
constexpr heinzel::loops::Refusal refusal(const heinzel::Graph& g, const Schedule& d) {
    heinzel::loops::Program p{};
    const std::expected<void, heinzel::loops::Refusal> r = heinzel::loops::lower_into(g, d, p);
    const std::expected<void, heinzel::loops::Refusal> w = r ? heinzel::loops::well_formed(p) : r;
    return w ? heinzel::loops::Refusal{"lowered"} : w.error();
}

} // namespace loops_schedules
