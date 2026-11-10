#pragma once

// Plans a stage's loops from its membership alone. The domain is walked in memory order, the axes that stay first and
// then the axes a fold collapses, and the innermost loop that memory allows runs in lanes, as many as a share of the
// machine's vector registers holds of the stage's widest element type (`put_the_lanes`). A cycle's loops and a
// contraction's have files of their own. The predicates mirror the lowering's checks, so the planner does not propose
// what the lowering would refuse. It names no operation.

#include <cstdint>
#include <expected>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/graph/regions.hpp"
#include "heinzel/loops/graph/views.hpp"
#include "heinzel/loops/planner/contraction.hpp"
#include "heinzel/loops/planner/feedback.hpp"
#include "heinzel/loops/planner/stages.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/schedule.hpp"
#include "heinzel/schedule/machine.hpp"
#include "heinzel/support/fixed_vector.hpp"

namespace heinzel::loops::planner {
namespace detail {

constexpr std::int32_t widest_element(const Graph& g, const Schedule::Stage& stage) {
    std::int32_t widest = 4;
    for (const std::int32_t n : stage.nodes) {
        const std::int32_t b = loops::bytes_of(g.values[static_cast<std::size_t>(g.nodes[static_cast<std::size_t>(n)].output)].type);
        widest = b > widest ? b : widest;
    }
    return widest;
}

// Would a lane along domain axis `along` walk memory contiguously? Whatever the stage reads from outside it (as it is,
// through a view, or as a view), and whatever of its own is read from outside it and so is stored, has to move
// contiguously along that axis or stand still. The lowering checks the accesses it writes against the same rule; this
// keeps the planner from proposing what the lowering would refuse. A region with no answer answers no, and the
// lowering refuses it by name.
constexpr bool a_lane_walks_memory(const Graph& g, const graph::Facts& facts, const OpenGroup& open, std::int32_t along) {
    const Schedule::Stage& stage = open.stage;
    std::vector<char> here(g.nodes.size(), false);
    std::vector<char> made_here(g.values.size(), false);
    std::vector<char> stored(g.values.size(), false);
    for (const std::int32_t ni : stage.nodes) {
        here[static_cast<std::size_t>(ni)] = true;
        made_here[static_cast<std::size_t>(g.nodes[static_cast<std::size_t>(ni)].output)] = true;
    }
    for (std::size_t ni = 0; ni < g.nodes.size(); ++ni) {
        for (std::size_t k = 0; !here[ni] && k < rules_of(g.nodes[ni].op).arity; ++k) {
            stored[static_cast<std::size_t>(facts.read_as[static_cast<std::size_t>(g.nodes[ni].inputs[k])])] = true; // read by another stage, or through a view
        }
    }
    const graph::GroupShape& shape = open.shape;
    const graph::Region inside = graph::whole_domain(shape.domain);
    graph::Region after = inside;
    if (shape.fold >= 0 && !graph::is_a_contraction(g.nodes[static_cast<std::size_t>(shape.fold)])) {
        const std::expected<graph::Region, Refusal> folded = graph::after_the_fold(shape.domain, g.nodes[static_cast<std::size_t>(shape.fold)], shape.folded);
        if (!folded) {
            return false;
        }
        after = *folded;
    }
    for (std::size_t at = 0; at < stage.nodes.size(); ++at) {
        const std::int32_t ni = stage.nodes[at];
        const heinzel::Node& n = g.nodes[static_cast<std::size_t>(ni)];
        const Value& made = g.values[static_cast<std::size_t>(n.output)];
        // Its operands are read on its side of the fold; a fold's result, and a map over it, is stored after. (A
        // contraction reads its operands through its own axes: `contraction_axes` asks those.)
        const bool is_after = graph::side_of(g, stage, shape, at) == graph::Side::After;
        const graph::Region& region = is_after ? after : inside;
        for (std::size_t k = 0; k < rules_of(n.op).arity && !graph::is_an_affine_view(n) && !graph::is_a_contraction(n); ++k) {
            const std::int32_t v = n.inputs[k];
            if (k == 1 && graph::gathers(n) == Gather::Table) { // a table moves with time only, a whole row of cells per sample
                if (along == shape.domain.rank - 1 && extent_at(g.values[static_cast<std::size_t>(v)].shape, 1) != literal_extent(1)) {
                    return false;
                }
                continue;
            }
            const Value& read = g.values[static_cast<std::size_t>(v)];
            // (a part of a padded or joined value is walked over the operand's own extents)
            if (read.category != ValueCategory::Const && !made_here[static_cast<std::size_t>(v)] &&
                !graph::a_lane_could_read(g, read.shape, graph::is_made_in_parts(n) ? graph::whole_domain(read.shape) : region,
                                          facts.through[static_cast<std::size_t>(v)], along)) {
                return false;
            }
        }
        if (graph::is_an_affine_view(n) &&
            !graph::a_lane_could_read(g, made.shape, region, graph::through_view(g, n, facts.through[static_cast<std::size_t>(n.inputs[0])]), along)) {
            return false;
        }
        const graph::Region& stored_over = is_after || ni == shape.fold || (shape.fold >= 0 && planner::collapses_an_axis(n)) ? after : inside;
        if ((stored[static_cast<std::size_t>(n.output)] || is_a_result(g, n.output)) &&
            !graph::a_lane_could_read(g, made.shape, stored_over, graph::read_as_itself(n.output), along)) {
            return false;
        }
    }
    return true;
}

// May a fold stage's lanes run across its outputs, along domain axis `along`? They may where a lane there walks memory
// and the axis is not known to be shorter than a lane.
constexpr bool lanes_may_cross(const Graph& g, const graph::Facts& facts, const OpenGroup& open, std::int32_t along, std::int32_t lanes) {
    const Extent extent = extent_at(open.shape.domain, along);
    return (!is_literal(extent) || extent.encoded >= lanes) && detail::a_lane_walks_memory(g, facts, open, along);
}

// Lanes along a fold's own axis reorder its sum: every fold of the stage has to be granted that.
constexpr bool every_fold_may_reassociate(const Graph& g, const Schedule::Stage& stage) {
    for (const std::int32_t ni : stage.nodes) {
        const heinzel::Node& n = g.nodes[static_cast<std::size_t>(ni)];
        if (graph::is_a_fold(n) && !numerics::grants(g.values[static_cast<std::size_t>(n.output)].precision).reorder) {
            return false;
        }
    }
    return true;
}

// Where memory allows, a fold's lanes run across its outputs, along the innermost loop outside its own: that reorders
// no sum, so any precision allows it, and a window's few taps are not a lane's worth of work anyway. Otherwise, and for
// a stage without a fold, lanes run along the innermost loop when its axis is the one memory is contiguous in
// (`contiguous`) and, for a fold, when its precision grants the reordering.
constexpr void put_the_lanes(OpenGroup& open, const Graph& g, const graph::Facts& facts, const heinzel::Node* fold, std::int32_t contiguous, std::int32_t lanes,
                             std::int32_t registers) {
    // Four accumulators side by side, since one accumulator is one dependency chain. Eight were tried for lanes across
    // outputs: 7 % faster for a window of 32 taps, 5-8 % slower for 16 taps, so not taken.
    constexpr std::int32_t bodies = 4;
    fixed_vector<Schedule::Loop, kMaxStageLoops>& loops = open.stage.loops;
    const std::size_t outside = static_cast<std::size_t>(open.stage.inner_from) - 1;
    Schedule::Loop& innermost = loops[loops.size() - 1];
    // An order statistic holds all the values along its axis at once (one body of them fills the registers, and there
    // is no chain between outputs to interleave), and that axis can carry no lane.
    const bool ordering = fold != nullptr && graph::is_an_ordering(*fold);
    // An axis known to be shorter than a lane never fills one: a Vector loop there would vectorize nothing and still
    // cost the fold its walk order (it would open at the identity, not at its first element — a sum of -0.0 comes out
    // +0.0, a max loses a leading NaN).
    const Extent along = extent_at(open.shape.domain, innermost.axis);
    const bool holds_a_lane = !is_literal(along) || along.encoded >= lanes;
    // How many lanes a vector runs: a map's one vector an eighth of the registers — a sixteenth where it runs a core,
    // whose dozen or so values live take the rest — and each of a fold's blocks a sixteenth (a quarter in all), as far
    // as `fits` lets it widen. The compiler lays the registers' operations side by side, so a long chain runs several at
    // once and a cheap map spends less on its loop. On the M1 Pro (32 registers) the channel strip 1.3x faster (two),
    // a multiply-add map 1.25x (four), conv's window at 32 taps 1.3x (blocks of two); on zen2 (16) a multiply-add map
    // level, while two registers spilled the oscillator's `sinpi` (1.1x slower). Blocks of one register more (`unroll`)
    // ran the strip 1.2x and conv at 16 taps 0.75x, the chains one after the other (`Plan/probes/map-unroll/maps.cpp`,
    // `folds.cpp`).
    bool runs_a_core = false;
    for (const std::int32_t ni : open.stage.nodes) {
        runs_a_core = runs_a_core || rules_of(g.nodes[static_cast<std::size_t>(ni)].op).latency == Latency::Core;
    }
    const auto widened = [&](std::int32_t share, const auto& fits) {
        std::int32_t step = lanes;
        while (step * share < lanes * registers && fits(2 * step)) {
            step *= 2;
        }
        return step;
    };
    if (fold != nullptr && open.stage.inner_from > 0 && detail::lanes_may_cross(g, facts, open, loops[outside].axis, lanes)) {
        const std::int32_t step =
            ordering ? lanes : widened(16, [&](std::int32_t s) { return detail::lanes_may_cross(g, facts, open, loops[outside].axis, s); });
        loops[outside] = Schedule::Loop{.axis = loops[outside].axis, .step = step, .kind = LoopKind::Vector, .unroll = ordering ? 1 : bodies};
    } else if (innermost.axis == contiguous && !ordering && (fold == nullptr || holds_a_lane) && detail::a_lane_walks_memory(g, facts, open, innermost.axis) &&
               detail::every_fold_may_reassociate(g, open.stage)) {
        const std::int32_t share = fold != nullptr || runs_a_core ? 16 : 8;
        const std::int32_t step = widened(share, [&](std::int32_t s) { return !is_literal(along) || along.encoded >= s; });
        innermost = Schedule::Loop{.axis = innermost.axis, .step = step, .kind = LoopKind::Vector, .unroll = fold == nullptr ? 1 : bodies};
    }
}

// Does the stage's fold sum (or average) a product of the stage that nothing else reads, its operands of the product's
// type, at a precision that grants reordering? Then where the machine has the multiply-add, and issues four adds a cycle
// — pipes that each multiply or add, the M1 Pro's — each point is folded in one (`Schedule::Stage::contract`): conv's
// window 1.6x faster there. zen2's two adds have pipes of their own beside its two multiply-adds, so fused the add only
// lengthened the chain: conv at 32 taps 1.1x slower, at 16 level (`Plan/probes/map-unroll/conv_fused.cpp`).
constexpr bool sums_a_product(const Graph& g, const graph::Facts& facts, const Schedule::Stage& stage, const heinzel::Node& fold) {
    const std::int32_t v = fold.inputs[0];
    const std::int32_t p = facts.producer[static_cast<std::size_t>(v)];
    if ((fold.attr.reduce.op != ReduceOp::Sum && fold.attr.reduce.op != ReduceOp::Mean) || p < 0 ||
        !numerics::grants(g.values[static_cast<std::size_t>(fold.output)].precision).reorder) {
        return false;
    }
    const heinzel::Node& product = g.nodes[static_cast<std::size_t>(p)];
    bool here = false;
    for (const std::int32_t ni : stage.nodes) {
        here = here || ni == p;
    }
    std::int32_t readers = 0;
    for (const heinzel::Node& n : g.nodes) {
        for (std::size_t k = 0; k < rules_of(n.op).arity; ++k) {
            readers += n.inputs[k] == v ? 1 : 0;
        }
    }
    for (const NamedValue& out : g.outputs) {
        readers += out.value == v ? 1 : 0;
    }
    const ElemType type = g.values[static_cast<std::size_t>(v)].type;
    return here && readers == 1 && rules_of(product.op).algebra == Algebra::Product &&
           g.values[static_cast<std::size_t>(product.inputs[0])].type == type && g.values[static_cast<std::size_t>(product.inputs[1])].type == type;
}

} // namespace detail

// One stage's loops, from its membership alone; or why it has none (a feedback stage whose sample folds its voices,
// `plan_feedback_loops`).
constexpr Refusal plan_loops(OpenGroup& open, const Graph& g, const graph::Facts& facts, const Machine& m) {
    const heinzel::Node* fold = open.shape.fold < 0 ? nullptr : &g.nodes[static_cast<std::size_t>(open.shape.fold)];
    const std::int32_t lanes = m.vector_bytes / detail::widest_element(g, open.stage);
    if (open.cycle >= 0 || (fold != nullptr && graph::is_a_contraction(*fold))) {
        if (open.cycle >= 0) {
            return planner::plan_feedback_loops(open, g, facts, lanes);
        } else {
            // A contraction's lanes run across its outputs only where every other read and store of its stage walks
            // memory along them.
            const std::int32_t rank = open.shape.domain.rank;
            const bool across = rank >= 2 && detail::a_lane_walks_memory(g, facts, open, loops::contiguous_axis(rank));
            planner::plan_contraction_loops(open.stage, g, facts, *fold, m, across);
        }
        return {};
    }
    const auto order = graph::memory_order(open.shape.domain.rank);
    // The axes that stay, then the axes that collapse, each in memory order.
    for (const bool collapsing : {false, true}) {
        for (const std::int32_t axis : order) {
            if (((open.shape.collapsed >> axis & 1u) != 0) == collapsing) {
                open.stage.loops.push_back(Schedule::Loop{.axis = axis});
            }
        }
        if (!collapsing) {
            open.stage.inner_from = static_cast<std::int32_t>(open.stage.loops.size());
        }
    }
    if (fold == nullptr) {
        open.stage.inner_from = static_cast<std::int32_t>(open.stage.loops.size());
    }
    if (lanes > 1 && !order.empty()) {
        detail::put_the_lanes(open, g, facts, fold, order[order.size() - 1], lanes, m.registers);
    }
    open.stage.contract = m.fma && m.fp_per_cycle >= 4 && fold != nullptr && graph::is_a_fold(*fold) && detail::sums_a_product(g, facts, open.stage, *fold);
    return {};
}

} // namespace heinzel::loops::planner
