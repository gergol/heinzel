#pragma once

// What kind of node the rule table says a node is. What may share a loop nest, and what may run in a lane, is asked of
// the rule table; nothing here names an operation.

#include <cstdint>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/support/fixed_vector.hpp"
#include "heinzel/support/order_net.hpp"

namespace heinzel::loops::graph {

// The planner's limit on one stage's graph nodes: a cycle, a merge and a join stay within it, so that a stage's
// statements — its nodes and a Load per operand buffer — fit `kMaxStageStmts`. Raised with it
// (`-DHEINZEL_MAX_STAGE_STMTS`): reaching it closes a stage or declines a merge, and refuses only a longer cycle. A
// capacity, never a cost: how much work a feedback stage merges is `Machine::merge_work`'s, which a raise leaves alone.
inline constexpr std::size_t kMaxGroupStmts = kMaxStageStmts * 3 / 4;
static_assert(kMaxGroupStmts <= kMaxStageStmts, "a stage the planner gathers or merges fits a schedule's stage");

// A plain map makes one value per point from its operands at the same point, by an operation written over any value
// type (one the build rounds, too), or by a transcendental the table says has a writing over the value type
// (`Approximate`): a lane of floats spends that accuracy freedom where it has a float core, as the code it is
// benchmarked against does, and runs the double core one number at a time where it has none yet.
constexpr bool is_a_plain_map(const heinzel::Node& n) {
    const OpRules& r = rules_of(n.op);
    const bool computed_over_a_value = r.numeric == NumericContract::Exact || r.numeric == NumericContract::ExactInDouble ||
                                       r.numeric == NumericContract::MachineRounded ||
                                       (r.numeric == NumericContract::Approximate && has_a_writing_over_the_value_type(n.op));
    return r.shape == ShapeRule::Elementwise && r.attr == AttrKind::None && r.time == TimeRule::SameSample && computed_over_a_value;
}

// Does the node convert its operand, at this sample, into the element type its attribute states?
constexpr bool converts(const heinzel::Node& n) {
    const OpRules& r = rules_of(n.op);
    return r.time == TimeRule::SameSample && r.type == TypeRule::FromAttribute && r.shape == ShapeRule::Elementwise;
}

// Does the node hand its one operand on unchanged in value? A lag hands it on from an earlier sample, a recurrence's
// definition as the variable it defines, a conversion into another element type, and a plain hand-on as it is.
constexpr bool hands_its_operand_on(const heinzel::Node& n) {
    const OpRules& r = rules_of(n.op);
    const bool whenever = r.time == TimeRule::ReadsEarlierSamples || r.time == TimeRule::SameSample;
    return r.arity == 1 && r.meaning == MeaningRule::PerSample && ((whenever && r.shape == ShapeRule::Passthrough) || graph::converts(n));
}

// A link (`u = _chain(x, V)`): element 0 of `u` is `x`, element j is `V`'s element j - 1, both at this sample, so k
// sections laid along axis 0 run as one cycle. Asked of the timing column: the one question every pass meeting it asks.
constexpr bool is_a_link(const heinzel::Node& n) { return rules_of(n.op).time == TimeRule::ReadsTheElementBefore; }

// A generator has no operand, so no stage runs it: a ramp (each element its own index) or a constant tensor, numbers
// known now, declares what it makes and is a buffer with no statement; a run's start (`marks_a_run_start`) is no buffer.
constexpr bool is_generated(const heinzel::Node& n) { return rules_of(n.op).arity == 0; }

// Is the node a run's start (`_first`: 1 at a run's first sample, else 0)? Stored nowhere: each reader computes it
// (`Source::First`), in a stage that runs that sample apart (`Schedule::Stage::peeled`).
constexpr bool marks_a_run_start(const heinzel::Node& n) { return rules_of(n.op).start == RunStart::Marks; }

// A read whose address is a signal: a delay line, read as far back as a position says, or a table, read where a phase
// says. Each blends the two samples the position falls between.
constexpr Gather gathers(const heinzel::Node& n) {
    const OpRules& r = rules_of(n.op);
    if (r.arity != 2 || r.shape != ShapeRule::Passthrough) {
        return Gather::None;
    }
    return r.time == TimeRule::ReadsAtSignalledDepth                        ? Gather::Back
           : r.time == TimeRule::SameSample && r.attr == AttrKind::Table ? Gather::Table
                                                                         : Gather::None;
}
// How far such a read reaches: samples back (and one more, for the far tap), or cells of the table.
constexpr std::int32_t reach_of(const heinzel::Node& n) {
    return graph::gathers(n) == Gather::Back ? n.attr.signalled_delay.max_delay : n.attr.table.size;
}

// A value made in parts along one axis: a constant pad (a margin, its operand, a margin) or a join (its first operand,
// then its second). Each part is a copy into its stretch of the result, one stage per part.
constexpr bool is_made_in_parts(const heinzel::Node& n) {
    const ViewKind kind = n.attr.view.kind;
    return rules_of(n.op).attr == AttrKind::View && (kind == ViewKind::Pad || kind == ViewKind::Concat);
}

struct Part {
    std::int32_t from{0};
    std::int32_t length{0};
    std::int32_t operand{-1}; // which operand it copies; -1: the fill
};

// The parts of such a value along its axis, in order (a part may be empty); none where an extent along it is symbolic.
constexpr fixed_vector<Part, 3> parts_of(const Graph& g, const heinzel::Node& n) {
    const ViewAttr& how = n.attr.view;
    const Extent first = extent_at(g.values[static_cast<std::size_t>(n.inputs[0])].shape, how.axis);
    fixed_vector<Part, 3> parts{};
    if (!is_literal(first)) {
        return parts;
    }
    if (how.kind == ViewKind::Pad) {
        parts.push_back(Part{0, how.pad_before, -1});
        parts.push_back(Part{how.pad_before, first.encoded, 0});
        parts.push_back(Part{how.pad_before + first.encoded, how.pad_after, -1});
        return parts;
    }
    const Extent second = extent_at(g.values[static_cast<std::size_t>(n.inputs[1])].shape, how.axis);
    if (is_literal(second)) {
        parts.push_back(Part{0, first.encoded, 0});
        parts.push_back(Part{first.encoded, second.encoded, 1});
    }
    return parts;
}

// Memory order of a domain, outermost first: sample-major, as a feed is — the streamed axis outermost, the others in
// shape order, so the innermost is the last element axis (or time, for one number per sample).
constexpr fixed_vector<std::int32_t, kMaxAxes> memory_order(std::int32_t rank) {
    fixed_vector<std::int32_t, kMaxAxes> order{};
    if (rank > 0) {
        order.push_back(rank - 1);
    }
    for (std::int32_t axis = 0; axis + 1 < rank; ++axis) {
        order.push_back(axis);
    }
    return order;
}

// A fold over some axes of its operand; a contraction in its general spelling; an order statistic along one axis.
constexpr bool is_a_fold(const heinzel::Node& n) {
    const OpRules& r = rules_of(n.op);
    return r.attr == AttrKind::Reduce && r.shape == ShapeRule::ReduceAxes && r.time == TimeRule::SameSample;
}
constexpr bool is_a_contraction(const heinzel::Node& n) {
    const OpRules& r = rules_of(n.op);
    return r.attr == AttrKind::MatMul && r.shape == ShapeRule::ContractGeneral && r.time == TimeRule::SameSample;
}
constexpr bool is_an_ordering(const heinzel::Node& n) {
    const OpRules& r = rules_of(n.op);
    return r.attr == AttrKind::Order && r.shape == ShapeRule::OrderAxis && r.time == TimeRule::SameSample;
}
// ...of which the lowering takes those that keep one exact rank (a median over an odd window, a minimum, a maximum): a
// selection by a fixed network of compare-exchanges, which gives the same bits at every precision.
constexpr bool selects_one_rank(const Graph& g, const heinzel::Node& n) {
    const Shape& operand = g.values[static_cast<std::size_t>(n.inputs[0])].shape;
    if (n.attr.order.op != OrderOp::Quantile || n.attr.order.axis < 0 || n.attr.order.axis + 1 >= operand.rank) {
        return false;
    }
    const Extent along = extent_at(operand, n.attr.order.axis);
    return is_literal(along) && along.encoded >= 1 && along.encoded <= order::kMaxOrderedLength &&
           order::quantile_rank(along.encoded, n.attr.order.q).w == 0.0;
}
constexpr bool folds(const heinzel::Node& n) { return graph::is_a_fold(n) || graph::is_an_ordering(n) || graph::is_a_contraction(n); }

// Does `fold` (a fold over some axes, or an order statistic along one) make this axis of its operand go away, or, for
// a fold that keeps its axes, shrink it to one?
constexpr bool collapses(const heinzel::Node& fold, std::int32_t axis) {
    if (fold.attr.kind == AttrKind::Order) {
        return axis == fold.attr.order.axis;
    }
    for (std::int32_t k = 0; k < fold.attr.reduce.axes.rank; ++k) {
        if (fold.attr.reduce.axes.axes[k] == axis) {
            return true;
        }
    }
    return false;
}

// The axes of a domain of `rank` axes that `fold` collapses, one bit each: those `collapses` names, or, for a
// contraction, the contracted axis, which its stage numbers after the others (bit `rank`).
constexpr std::uint32_t collapsed_axes(const heinzel::Node& fold, std::int32_t rank) {
    if (graph::is_a_contraction(fold)) {
        return std::uint32_t{1} << rank;
    }
    std::uint32_t axes = 0;
    for (std::int32_t a = 0; a < rank; ++a) {
        axes |= graph::collapses(fold, a) ? std::uint32_t{1} << a : 0u;
    }
    return axes;
}

// A contraction's stage walks its output's axes and one more, the contracted one, numbered after them. This says which
// of those each axis of operand `which` (0 or 1) is, with the output laid out as
// [batch] ++ [a's free] ++ [b's free] ++ [streamed].
constexpr fixed_vector<std::int32_t, kMaxAxes> contraction_axes(const Graph& g, const heinzel::Node& n, std::int32_t which) {
    const MatMulAttr& m = n.attr.matmul;
    const Shape& a = g.values[static_cast<std::size_t>(n.inputs[0])].shape;
    const Shape& b = g.values[static_cast<std::size_t>(n.inputs[1])].shape;
    const std::int32_t produced = g.values[static_cast<std::size_t>(n.output)].shape.rank;
    const auto batch_position = [](const Axes& batch, std::int32_t axis) {
        for (std::int32_t k = 0; k < batch.rank; ++k) {
            if (batch.axes[k] == axis) {
                return k;
            }
        }
        return -1;
    };
    const std::int32_t a_free = a.rank - 2 - m.batch_a.rank; // all but the streamed, the contracted and the batch axes
    const Shape& held = which == 0 ? a : b;
    const Axes& batch = which == 0 ? m.batch_a : m.batch_b;
    const std::int32_t contracted = which == 0 ? m.a_contract : m.b_contract;
    fixed_vector<std::int32_t, kMaxAxes> axes{};
    std::int32_t next_free = m.batch_a.rank + (which == 0 ? 0 : a_free);
    for (std::int32_t axis = 0; axis < held.rank; ++axis) {
        if (axis == held.rank - 1) {
            axes.push_back(produced - 1);
        } else if (axis == contracted) {
            axes.push_back(produced);
        } else if (batch_position(batch, axis) >= 0) {
            axes.push_back(batch_position(batch, axis));
        } else {
            axes.push_back(next_free++);
        }
    }
    return axes;
}

// A fold over the streamed axis folds over time, over operands that do not all exist yet, and one naming an axis its
// operand does not have would lower to a copy. Both are declined, as the reference walk declines them.
constexpr bool folds_over_time(const Graph& g, const heinzel::Node& fold) {
    const std::int32_t streamed = g.values[static_cast<std::size_t>(fold.inputs[0])].shape.rank - 1;
    return graph::collapses(fold, streamed) || graph::collapses(fold, kInnermostAxis);
}
constexpr bool folds_a_missing_axis(const Graph& g, const heinzel::Node& fold) {
    const std::int32_t rank = g.values[static_cast<std::size_t>(fold.inputs[0])].shape.rank;
    for (std::int32_t k = 0; k < fold.attr.reduce.axes.rank; ++k) {
        const std::int32_t axis = fold.attr.reduce.axes.axes[k];
        if (axis != kInnermostAxis && (axis < 0 || axis >= rank)) {
            return true;
        }
    }
    return false;
}

} // namespace heinzel::loops::graph
