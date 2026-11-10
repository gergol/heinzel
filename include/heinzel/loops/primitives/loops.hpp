#pragma once

// A stage's skeleton: the chain of its walking nodes (`Loop`, `Unroll`, `Vector`), outermost first, which normalization
// turns into a tree. It is built from the stage's planned loops (`loops/schedule.hpp`) once the schedule is held to
// what a schedule may plan: every axis walked once, and the inner loops it names the ones its loops imply.

#include <array>
#include <bit>
#include <cstdint>
#include <expected>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/timing.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/graph/regions.hpp"
#include "heinzel/loops/graph/views.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/schedule.hpp"
#include "heinzel/support/fixed_vector.hpp"

namespace heinzel::loops {

struct Skeleton {
    fixed_vector<Node, kMaxStageLoops> walk{}; // Loop, Unroll and Vector nodes, outermost first

    constexpr bool operator==(const Skeleton&) const = default;
};

// The inner loops a skeleton implies, counted as how many `Loop`s stand outside them (-1: it implies none). A fold's
// inner loops start at the outermost loop over an axis it collapses that is not a chunk of it; a network's is the loop
// over its ordered axis; for state handed on, it is the time loop, innermost — also where the stage holds a fold, which
// then folds every sample (Plan/23 10). Also whether a chunk of a fold's own axis stands outside them, in which case
// the fold resumes from its partial results.
struct InnerLoops {
    std::int32_t outer{-1};
    bool resumes{false};
};
constexpr InnerLoops implied_inner(const Graph& g, const graph::Facts& facts, const Schedule::Stage& stage, const Skeleton& s) {
    const graph::GroupShape shape = graph::shape_of_group(g, stage);
    InnerLoops in{.outer = 0};
    const bool sample = shape.fold >= 0 && graph::is_a_fold(g.nodes[static_cast<std::size_t>(shape.fold)]) && graph::hands_state_on(g, facts, stage);
    for (std::size_t k = 0; k < s.walk.size(); ++k) {
        const Node& n = s.walk[k];
        if (n.kind != NodeKind::Loop) {
            continue;
        }
        const bool chunk = n.tail == Tail::Clip && n.step > 1;
        const bool time = k + 1 == s.walk.size() && n.tail == Tail::Clip && n.step == 1 && n.axis == shape.domain.rank - 1;
        if (sample) {
            if (time) {
                return in;
            }
        } else if (shape.fold >= 0) {
            const bool ordered = graph::is_an_ordering(g.nodes[static_cast<std::size_t>(shape.fold)]);
            const bool own = (shape.collapsed >> n.axis & 1u) != 0;
            if (own && (ordered || !chunk)) {
                return in;
            }
            in.resumes = in.resumes || (own && chunk);
        } else if (time && graph::hands_state_on(g, facts, stage)) {
            return in;
        }
        ++in.outer;
    }
    return InnerLoops{};
}

// Does a node of the stage read the past of a value the stage makes (feedback)?
constexpr bool reads_its_own_past(const Graph& g, const graph::Facts& facts, const Schedule::Stage& stage) {
    for (const std::int32_t ni : stage.nodes) {
        const heinzel::Node& n = g.nodes[static_cast<std::size_t>(ni)];
        const bool lag = graph::hands_its_operand_on(n) && samples_back(n) > 0;
        const std::int32_t maker = lag ? facts.producer[static_cast<std::size_t>(facts.read_as[static_cast<std::size_t>(n.inputs[0])])] : -1;
        for (const std::int32_t other : stage.nodes) {
            if (maker >= 0 && other == maker) {
                return true;
            }
        }
    }
    return false;
}

// What a schedule may plan: every axis of the stage's `axes` walked by exactly one loop, cut into chunks by at most one
// Tile outside it, of two or more (a Tile of one becomes a Loop of step one, which hands the walk inside it the whole
// axis again). A stage without inner loops runs an Unroll as a plain Loop, one body at a time, so time unrolled in
// feedback is refused here, where the unroll would be lost.
constexpr std::expected<void, Refusal> check_loops(const Graph& g, const graph::Facts& facts, const Schedule::Stage& ps, std::size_t axes, bool has_inner) {
    std::array<std::int32_t, kMaxAxes + 1> walks{};
    std::array<std::int32_t, kMaxAxes + 1> tiles{};
    for (std::size_t j = 0; j < ps.loops.size(); ++j) {
        const Schedule::Loop& l = ps.loops[j];
        if (l.axis < 0 || static_cast<std::size_t>(l.axis) >= axes || (l.kind == LoopKind::Tile ? tiles : walks)[static_cast<std::size_t>(l.axis)]++ > 0 ||
            (l.kind == LoopKind::Tile && walks[static_cast<std::size_t>(l.axis)] > 0)) {
            return std::unexpected("heinzel::compile: a schedule's stage walks each axis once, in at most one Tile outside the walk");
        }
        if (l.kind == LoopKind::Vector && (l.step < 2 || !std::has_single_bit(static_cast<std::uint32_t>(l.step)))) {
            return std::unexpected("heinzel::compile: a schedule's lanes are a power of two, two or more"); // (a vector of 24 bytes: six)
        }
        if (l.kind == LoopKind::Tile && l.step == 1) { // (below one: `skeleton_of`'s refusal)
            return std::unexpected("heinzel::compile: a Tile walks chunks of two or more: a chunk of one is a Serial loop");
        }
        if (!has_inner && l.kind == LoopKind::Unroll && static_cast<std::size_t>(l.axis) + 1 == axes && loops::reads_its_own_past(g, facts, ps)) {
            return std::unexpected("heinzel::compile: a stage that reads its own past plans time unrolled");
        }
    }
    for (std::size_t a = 0; a < axes; ++a) {
        if (walks[a] != 1) {
            return std::unexpected("heinzel::compile: a schedule's stage walks each axis once, in at most one Tile outside the walk");
        }
    }
    return {};
}

// Builds a checked schedule's skeleton: its planned loops in order, each as the nodes it walks in. A Tile becomes a
// Loop over its chunks. A serial loop becomes a Loop, and so does an Unroll in a stage without inner loops, which runs
// it one body at a time; elsewhere an Unroll becomes a strip of its bodies with an Unroll below it. Lanes become a
// strip (Masked outside the inner loops; Ones or, with blocks, LanesThenOnes inside them), with an Unroll over its
// blocks where there is more than one, and then the Vector. A chunk below one is refused, and so is a skeleton longer
// than kMaxStageLoops: one Loop per axis, and one more for each cut into chunks, counted in planned order.
constexpr std::expected<Skeleton, Refusal> skeleton_of(const Schedule::Stage& ps, bool has_inner, std::size_t axes) {
    Skeleton s{};
    std::size_t grown = axes;
    const std::int32_t outer = has_inner ? ps.inner_from : 0;
    for (std::size_t j = 0; j < ps.loops.size(); ++j) {
        const Schedule::Loop& l = ps.loops[j];
        if (l.kind == LoopKind::Serial || (l.kind == LoopKind::Unroll && !has_inner)) {
            s.walk.push_back(Node{.kind = NodeKind::Loop, .axis = l.axis, .step = 1});
            continue;
        }
        const bool blocks = l.kind == LoopKind::Vector && l.unroll > 1;
        const bool outside = static_cast<std::int32_t>(j) < outer;
        const std::int32_t chunk = l.kind == LoopKind::Vector ? l.step * l.unroll : l.step;
        if (chunk < 1) {
            return std::unexpected("heinzel::compile: a Split cuts into chunks of one or more, and an exact one a known chunk it divides");
        }
        if (grown + (blocks ? 1 : 0) >= kMaxStageLoops) {
            return std::unexpected("heinzel::compile: a stage needs more loops than kMaxStageLoops");
        }
        grown += blocks ? 2 : 1;
        if (l.kind == LoopKind::Tile) {
            s.walk.push_back(Node{.kind = NodeKind::Loop, .axis = l.axis, .step = chunk});
        } else if (l.kind == LoopKind::Unroll) {
            s.walk.push_back(Node{.kind = NodeKind::Loop, .axis = l.axis, .step = chunk, .tail = Tail::Ones});
            s.walk.push_back(Node{.kind = NodeKind::Unroll, .axis = l.axis, .step = 1});
        } else {
            const Tail tail = outside ? Tail::Masked : blocks ? Tail::LanesThenOnes : Tail::Ones;
            s.walk.push_back(Node{.kind = NodeKind::Loop, .axis = l.axis, .step = chunk, .tail = tail, .rest = !outside && blocks ? l.step : 1});
            if (blocks) {
                s.walk.push_back(Node{.kind = NodeKind::Unroll, .axis = l.axis, .step = l.step});
            }
            s.walk.push_back(Node{.kind = NodeKind::Vector, .axis = l.axis, .step = l.step});
        }
    }
    return s;
}

// The `Loop` planned loop `j` walks in: the `m`-th over its axis, `m` the planned loops over it before `j`.
constexpr std::int32_t loop_of(const Schedule::Stage& ps, const Skeleton& s, std::size_t j) {
    std::int32_t m = 0;
    for (std::size_t i = 0; i < j; ++i) {
        m += ps.loops[i].axis == ps.loops[j].axis ? 1 : 0;
    }
    for (std::size_t k = 0; k < s.walk.size(); ++k) {
        if (s.walk[k].kind == NodeKind::Loop && s.walk[k].axis == ps.loops[j].axis && m-- == 0) {
            return static_cast<std::int32_t>(k);
        }
    }
    return -1;
}

} // namespace heinzel::loops
