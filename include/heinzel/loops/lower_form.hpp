#pragma once

// Forming the stages of a schedule. Each planned stage becomes a stage: its domain, its statements in its nodes' order
// (each placed inside or after the fold), their stores, and the skeleton its loops spell. A value made in parts (a pad
// or a join) becomes one stage per part. Refusals are returned as values.

#include <cstdint>
#include <expected>
#include <utility>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/graph/regions.hpp"
#include "heinzel/loops/graph/views.hpp"
#include "heinzel/loops/lower_stmts.hpp"
#include "heinzel/loops/primitives/loops.hpp"
#include "heinzel/loops/primitives/memory.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/schedule.hpp"

namespace heinzel::loops::detail {

// Where a stage's inlined producer runs: over its own shape, seen through the window that reads it. That window must
// be a window over this stage's domain.
constexpr std::expected<graph::Region, Refusal> through_the_window(const Graph& g, const graph::Facts& facts, const Schedule::Stage& ps, const Shape& domain) {
    if (ps.through < 0) {
        if (ps.inlined != 0) {
            return std::unexpected("heinzel::compile: a group inlines a producer through no window");
        }
        return graph::whole_domain(domain);
    }
    const heinzel::Node& window = g.nodes[static_cast<std::size_t>(ps.through)];
    if (!graph::folds_into_its_readers(window) || g.values[static_cast<std::size_t>(window.output)].shape != domain) {
        return std::unexpected("heinzel::compile: a group computes a producer through something that is not a window over its own domain");
    }
    return graph::seen_through(g, facts, window);
}

// A value computed inside its reader's stage is never stored, so within the stage it can be read only through the
// window the stage computes it through.
constexpr Done check_window_reads(const Lowering& l, const heinzel::Node& n, const Schedule::Stage& ps, std::int32_t here) {
    for (std::size_t k = 0; k < rules_of(n.op).arity; ++k) {
        const std::int32_t v = n.inputs[k];
        const std::int32_t source = l.facts.through[static_cast<std::size_t>(v)].source;
        if (source != v && l.at.stage[static_cast<std::size_t>(source)] == here && l.facts.producer[static_cast<std::size_t>(v)] != ps.through) {
            return std::unexpected("heinzel::compile: a node reads a value of its own group through a window the group does not compute it through");
        }
    }
    return {};
}

// Checks the region the statements after a stage's fold run over. Every further fold of the stage must leave the same
// result behind (a sibling fold), a contraction shares its stage with no other fold, and every other node of a fold
// stage must run over the fold's domain or its result.
constexpr Done check_region_after(const Graph& g, const Schedule::Stage& ps, const graph::GroupShape& shape) {
    const std::expected<graph::Region, Refusal> after = graph::region_after_the_fold(g, shape);
    if (!after || shape.fold < 0) {
        return after ? Done{} : Done{std::unexpected(after.error())};
    }
    const heinzel::Node& fold = g.nodes[static_cast<std::size_t>(shape.fold)];
    for (std::size_t k = 0; k < ps.nodes.size(); ++k) {
        const heinzel::Node& n = g.nodes[static_cast<std::size_t>(ps.nodes[k])];
        const Shape& made = g.values[static_cast<std::size_t>(n.output)].shape;
        const bool folds = graph::folds(n);
        if (folds && ps.nodes[k] != shape.fold && (graph::is_a_contraction(n) || graph::is_a_contraction(fold))) {
            return std::unexpected("heinzel::compile: a contraction's group holds no second fold");
        }
        if (folds && ps.nodes[k] != shape.fold) {
            std::expected<graph::Region, Refusal> sibling = graph::after_the_fold(shape.domain, n, made);
            if (!sibling) {
                return std::unexpected(sibling.error());
            }
            sibling->inside_fold = false;
            if (g.values[static_cast<std::size_t>(n.inputs[0])].shape != shape.domain || *sibling != *after) {
                return std::unexpected("heinzel::compile: sibling folds in one group must collapse the same axes of the same domain");
            }
        }
        if (!folds && static_cast<std::int32_t>(k) >= ps.inlined && made != shape.domain && made != shape.folded) {
            return std::unexpected("heinzel::compile: a node of a fold group runs over neither the fold's domain nor its result");
        }
    }
    return {};
}

// Forms node `k` of stage `here`: its statement, then its store, if anything outside the stage reads it.
constexpr Done form_node(Lowering& l, const Schedule::Stage& ps, const graph::GroupShape& shape, std::int32_t here, Formed& stage, std::size_t k) {
    const std::int32_t ni = ps.nodes[k];
    const heinzel::Node& n = l.g.nodes[static_cast<std::size_t>(ni)];
    if (static_cast<std::int32_t>(k) < ps.inlined && graph::leaves_its_stage(l.at, n.output)) {
        return std::unexpected("heinzel::compile: a producer computed inside its reader's window is never stored, and something outside the group reads this one");
    }
    if (const Done r = detail::check_window_reads(l, n, ps, here); !r) {
        return r;
    }
    const std::expected<graph::Region, Refusal> region = graph::operand_region(l.g, l.facts, ps, shape, k);
    if (!region) {
        return std::unexpected(region.error());
    }
    const std::expected<Stmt, Refusal> st = detail::statement_of(l, n, ni, *region, here, stage);
    if (!st) {
        return std::unexpected(st.error());
    }
    l.at.stmt[static_cast<std::size_t>(n.output)] = static_cast<std::int32_t>(stage.stmts.size());
    const std::expected<std::int32_t, Refusal> at = detail::add_stmt(stage, *st, region->inside_fold);
    const std::expected<graph::Region, Refusal> written = graph::store_region(l.g, ps, shape, k);
    if (!at || !written) {
        return std::unexpected(!at ? at.error() : written.error());
    }
    return detail::store_if_read_outside(l, *written, stage, static_cast<std::size_t>(*at));
}

// After a fold, the axes it collapses are one number long (gone, or kept at extent one), so no statement after the fold
// walks them: a view of a longer value read there is read where the fold's first point stands along those axes. This
// is applied once the stage is formed, so that which reads share a `Load` is decided first.
constexpr void stand_after_the_fold(const graph::GroupShape& shape, Formed& stage) {
    if (shape.fold < 0) {
        return;
    }
    for (std::size_t s = 0; s < stage.stmts.size(); ++s) {
        Stmt& st = stage.stmts[s];
        for (std::size_t k = 0; !stage.inside[s] && k <= kMaxNodeInputs; ++k) {
            Access& a = loops::access_in(st, k);
            for (std::size_t axis = 0; a.source == Source::Buffer && axis < stage.axes.size(); ++axis) {
                if ((shape.collapsed >> axis & 1u) != 0) {
                    a.buffer_axis[axis] = -1;
                    a.step[axis] = 1;
                }
            }
        }
    }
}

// Forms a planned stage as one stage. Its domain is the result's axes, followed by the contracted axis for a
// contraction. It takes its skeleton from the schedule, makes its relaid copy's buffer and the past its nodes keep (and
// what its links read), and forms its statements, each inside or after the fold, with their stores.
constexpr Done form_stage(Lowering& l, std::int32_t here) {
    const Schedule::Stage& ps = l.schedule.stages[static_cast<std::size_t>(here)];
    if (ps.inlined < 0 || static_cast<std::size_t>(ps.inlined) >= ps.nodes.size()) {
        return std::unexpected("heinzel::compile: a group inlines a producer and holds nothing of its own to read it");
    }
    if (l.stages.size() == kMaxStages) {
        return std::unexpected("heinzel::compile: more stages than kMaxStages; rebuild with -DHEINZEL_MAX_STAGES=N");
    }
    const graph::GroupShape shape = graph::shape_of_group(l.g, ps);
    Formed stage{.plan = here, .collapsed = shape.collapsed};
    for (std::int32_t axis = 0; axis < shape.domain.rank; ++axis) {
        stage.axes.push_back(extent_at(shape.domain, axis));
    }
    if (shape.fold >= 0 && graph::is_a_contraction(l.g.nodes[static_cast<std::size_t>(shape.fold)])) { // (`spelled` checked the axes fit)
        const heinzel::Node& fold = l.g.nodes[static_cast<std::size_t>(shape.fold)];
        stage.axes.push_back(extent_at(l.g.values[static_cast<std::size_t>(fold.inputs[0])].shape, fold.attr.matmul.a_contract));
    }
    const Spelled& spelled = l.spelled[static_cast<std::size_t>(here)];
    stage.skeleton = spelled.skeleton, stage.inner = spelled.inner, stage.resumes = spelled.resumes;
    if (const Done r = detail::copy_buffer(l, shape, stage); !r) {
        return r;
    }
    stage.turned = shape.fold >= 0 && graph::is_a_contraction(l.g.nodes[static_cast<std::size_t>(shape.fold)]) ? ps.turned : -1;
    for (std::size_t k = 0; k < ps.nodes.size(); ++k) {
        if (const Done r = detail::keep_its_past(l, l.g.nodes[static_cast<std::size_t>(ps.nodes[k])].output); !r) {
            return r;
        }
    }
    if (const Done r = detail::keep_what_links_read(l, ps, here); !r) {
        return r;
    }
    if (const std::expected<graph::Region, Refusal> window = detail::through_the_window(l.g, l.facts, ps, shape.domain); !window) {
        return std::unexpected(window.error());
    }
    if (const Done r = detail::check_region_after(l.g, ps, shape); !r) {
        return r;
    }
    for (std::size_t k = 0; k < ps.nodes.size(); ++k) {
        if (const Done r = detail::form_node(l, ps, shape, here, stage, k); !r) {
            return r;
        }
    }
    detail::stand_after_the_fold(shape, stage);
    l.stages.push_back(std::move(stage));
    return {};
}

// One part of a value made in parts by node `ni`: a Load of the operand, or of the fill value, stored into its stretch
// of the result.
constexpr std::expected<Stmt, Refusal> part_copy(Lowering& l, std::int32_t ni, const graph::Part& part, const Shape& over, std::int32_t result) {
    const heinzel::Node& n = l.g.nodes[static_cast<std::size_t>(ni)];
    const Value& made = l.g.values[static_cast<std::size_t>(n.output)];
    const graph::Region region = graph::whole_domain(over);
    Stmt copy{.kind = StmtKind::Load, .node = ni, .type = made.type, .computed_in = made.type};
    copy.in[0] = Access{.source = Source::Immediate, .immediate = n.attr.view.pad_bits};
    if (part.operand >= 0) {
        const std::int32_t v = n.inputs[static_cast<std::size_t>(part.operand)];
        const graph::Viewed& through = l.facts.through[static_cast<std::size_t>(v)];
        const Value& source = l.g.values[static_cast<std::size_t>(through.source)];
        if (source.category == ValueCategory::Const) { // a part of a constant is that constant: a fill
            copy.in[0] = detail::immediate(source.constant);
        } else if (l.at.buffer[static_cast<std::size_t>(through.source)] < 0) {
            return std::unexpected("heinzel::compile: a part of a padded or joined value copies storage: an input, or what an earlier group made");
        } else {
            const std::expected<Access, Refusal> read = detail::buffer_access(l.at.buffer[static_cast<std::size_t>(through.source)],
                                                                             l.g.values[static_cast<std::size_t>(v)].shape, region, through);
            if (!read) {
                return std::unexpected(read.error());
            }
            copy.in[0] = *read;
        }
    }
    const std::expected<Access, Refusal> store = detail::buffer_access(result, over, region, graph::read_as_itself(n.output));
    if (!store) {
        return std::unexpected(store.error());
    }
    copy.store = *store;
    copy.store.offset[static_cast<std::size_t>(n.attr.view.axis)] = part.from;
    return copy;
}

// Forms a value made in parts: its result buffer, and one stage per non-empty part, each with the planned stage's
// skeleton.
constexpr Done form_in_parts(Lowering& l, std::int32_t here) {
    const std::int32_t ni = l.schedule.stages[static_cast<std::size_t>(here)].nodes[0];
    const heinzel::Node& n = l.g.nodes[static_cast<std::size_t>(ni)];
    const Value& made = l.g.values[static_cast<std::size_t>(n.output)];
    const auto parts = graph::parts_of(l.g, n);
    if (parts.empty()) {
        return std::unexpected("heinzel::compile: a value made in parts along an axis of symbolic extent has no address here yet");
    }
    const std::expected<std::int32_t, Refusal> result =
        detail::add_buffer(l, Buffer{.type = made.type, .shape = made.shape, .value = n.output, .prefix = l.facts.deepest[static_cast<std::size_t>(n.output)]});
    if (!result) {
        return std::unexpected(result.error());
    }
    l.at.buffer[static_cast<std::size_t>(n.output)] = *result;
    for (std::size_t k = 0; k < parts.size(); ++k) {
        if (parts[k].length == 0) {
            continue;
        }
        if (l.stages.size() == kMaxStages) {
            return std::unexpected("heinzel::compile: more stages than kMaxStages; rebuild with -DHEINZEL_MAX_STAGES=N");
        }
        Shape over = made.shape;
        set_extent(over, n.attr.view.axis, literal_extent(parts[k].length));
        Formed stage{.plan = here};
        for (std::int32_t axis = 0; axis < over.rank; ++axis) {
            stage.axes.push_back(extent_at(over, axis));
        }
        stage.skeleton = l.spelled[static_cast<std::size_t>(here)].skeleton, stage.inner = l.spelled[static_cast<std::size_t>(here)].inner;
        const std::expected<Stmt, Refusal> copy = detail::part_copy(l, ni, parts[k], over, *result);
        if (!copy) {
            return std::unexpected(copy.error());
        }
        stage.stmts.push_back(*copy);
        stage.inside[0] = true;
        l.stages.push_back(std::move(stage));
    }
    return {};
}

} // namespace heinzel::loops::detail
