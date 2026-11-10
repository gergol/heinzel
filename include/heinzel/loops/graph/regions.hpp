#pragma once

// The shapes a stage's nodes (a `Schedule::Stage`) run at, read off its nodes, and whether a lane may run along one of
// its axes, with refusals returned as values. Inside a fold's loops (and everywhere in a stage without a fold) a node
// is a map over the stage's domain.
// After them it is a map over the fold's result, whose axes are the domain's axes that did not collapse. A producer
// computed inside the loops of the window that reads it is a map over its own shape, seen through that window. A
// `Region` is one of those shapes plus how the domain's axes walk it.

#include <cstdint>
#include <expected>
#include <string_view>

#include "heinzel/ir/ir.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/graph/views.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/schedule.hpp"

namespace heinzel::loops::graph {

struct Region {
    Shape over{};
    // Per axis of the stage's DOMAIN: which axis of `over` one step along it walks (-1: none — it collapsed, or the
    // domain does not have it), and by how many elements.
    std::int32_t axis[kMaxAxes]{-1, -1, -1, -1, -1, -1, -1, -1};
    std::int32_t step[kMaxAxes]{1, 1, 1, 1, 1, 1, 1, 1};
    // ...and, per axis of `over`, where the domain's first point stands along it: past the front, where what is
    // seen through starts there (a slice under the window), and zero otherwise.
    std::int32_t offset[kMaxRank]{};
    bool inside_fold{true};

    constexpr bool operator==(const Region&) const = default;
};

constexpr Region whole_domain(const Shape& domain) {
    Region r{};
    r.over = domain;
    for (std::int32_t a = 0; a < domain.rank; ++a) {
        r.axis[static_cast<std::size_t>(a)] = a;
    }
    return r;
}

constexpr std::expected<Region, Refusal> after_the_fold(const Shape& domain, const heinzel::Node& fold, const Shape& folded) {
    Region r{};
    r.over = folded;
    r.inside_fold = false;
    std::int32_t kept = 0;
    const bool keeps_its_axes = fold.attr.kind == AttrKind::Reduce && fold.attr.reduce.keep_axes;
    for (std::int32_t a = 0; a < domain.rank; ++a) {
        if (keeps_its_axes || !graph::collapses(fold, a)) {
            r.axis[static_cast<std::size_t>(a)] = kept++;
        }
    }
    if (kept != folded.rank) {
        return std::unexpected("heinzel::compile: a fold's result does not have the axes its operand keeps");
    }
    return r;
}

// The source of `window` (a folded view whose result has the domain's shape), as the domain walks it.
constexpr Region seen_through(const Graph& g, const Facts& facts, const heinzel::Node& window) {
    const Viewed& read = facts.through[static_cast<std::size_t>(window.output)];
    Region r{};
    r.over = g.values[static_cast<std::size_t>(read.source)].shape;
    for (std::int32_t a = 0; a < g.values[static_cast<std::size_t>(window.output)].shape.rank; ++a) {
        r.axis[static_cast<std::size_t>(a)] = read.axis[a];
        r.step[static_cast<std::size_t>(a)] = read.step[a];
    }
    for (std::size_t k = 0; k < kMaxRank; ++k) {
        r.offset[k] = read.offset[k];
    }
    return r;
}

// Which axis of a value of shape `held` does a step along domain axis `along` walk, read over `region` (-1: none)?
// Shapes align at their LAST axis; an extent of one against a larger one is a broadcast.
constexpr std::expected<std::int32_t, Refusal> walked_axis(const Shape& held, const Region& region, std::size_t along) {
    const std::int32_t in_region = region.axis[along];
    const std::int32_t lead = region.over.rank - held.rank;
    if (lead < 0) {
        return std::unexpected("heinzel::compile: an operand has more axes than the shape it is read over");
    }
    if (in_region < lead) {
        return -1;
    }
    const bool one = extent_at(held, in_region - lead) == literal_extent(1) && extent_at(region.over, in_region) != literal_extent(1);
    if (!one && extent_at(held, in_region - lead) != extent_at(region.over, in_region)) {
        return std::unexpected("heinzel::compile: an operand's extent is neither the domain's nor one");
    }
    return one ? -1 : in_region - lead;
}

// An affine address, before it is tied to a buffer: for each axis of the stage's domain, the axis of the value that a
// step walks (-1: none) and by how many elements; for each axis of the value, where the stage's first point stands
// along it. `why_not` is set when there is no address, and says why.
struct Address {
    Refusal why_not{};
    std::int32_t axis[kMaxAxes]{-1, -1, -1, -1, -1, -1, -1, -1};
    std::int32_t step[kMaxAxes]{1, 1, 1, 1, 1, 1, 1, 1};
    std::int32_t offset[kMaxRank]{};
};

// The address of a value of shape `held`, read from a statement of `region`. For each axis of the domain it finds the
// axis of `held` a step walks (shapes align at the last axis, and a broadcast stands still), maps it to the source
// axis `through` names, and multiplies the two steps. It never fails: an operand with no address carries the reason.
constexpr Address address_over(const Shape& held, const Region& region, const Viewed& through) {
    Address a{};
    for (std::size_t along = 0; along < kMaxAxes && a.why_not.empty(); ++along) {
        const std::expected<std::int32_t, Refusal> k = graph::walked_axis(held, region, along);
        if (!k) {
            a.why_not = k.error();
        } else if (*k >= 0) {
            a.axis[along] = through.axis[*k];
            a.step[along] = region.step[along] * through.step[*k];
        }
    }
    for (std::size_t k = 0; k < kMaxRank; ++k) {
        a.offset[k] = through.offset[k];
    }
    // Where the region itself starts past the front (a producer seen through a window over a slice), what is read
    // over it starts there too — along every axis it has and does not broadcast.
    const std::int32_t lead = region.over.rank - held.rank;
    for (std::int32_t j = lead < 0 ? 0 : lead; j < region.over.rank; ++j) {
        if (region.offset[j] != 0 && extent_at(held, j - lead) == extent_at(region.over, j)) {
            a.offset[through.axis[j - lead]] += region.offset[j] * through.step[j - lead];
        }
    }
    return a;
}

// Would a lane along domain axis `along` read this value contiguously, or find it standing still? Where the value has
// no address over the region the answer is no; the pass computing the region refuses it.
constexpr bool a_lane_could_read(const Graph& g, const Shape& held, const Region& region, const Viewed& through, std::int32_t along) {
    const std::expected<std::int32_t, Refusal> k = graph::walked_axis(held, region, static_cast<std::size_t>(along));
    if (!k) {
        return false;
    }
    const std::int32_t source_rank = g.values[static_cast<std::size_t>(through.source)].shape.rank;
    return *k < 0 || (through.axis[*k] == loops::contiguous_axis(source_rank) && region.step[along] * through.step[*k] == 1);
}

// What a stage's loops walk, read off its nodes: its fold (the first fold, order statistic or contraction), its domain,
// the fold's result and the axes of the domain it collapses (`collapsed_axes`). The domain is the fold's operand (for a
// contraction, its result, to which the lowering adds the contracted axis) or, without a fold, what the first node
// after any inlined producer makes.
struct GroupShape {
    std::int32_t fold{-1};
    Shape domain{};
    Shape folded{};
    std::uint32_t collapsed{0};

    constexpr bool operator==(const GroupShape&) const = default;
};
constexpr GroupShape shape_of_group(const Graph& g, const Schedule::Stage& stage) {
    GroupShape shape{};
    for (const std::int32_t ni : stage.nodes) {
        const heinzel::Node& n = g.nodes[static_cast<std::size_t>(ni)];
        if (shape.fold < 0 && graph::folds(n)) {
            shape.fold = ni;
            shape.folded = g.values[static_cast<std::size_t>(n.output)].shape;
            shape.domain = graph::is_a_contraction(n) ? shape.folded : g.values[static_cast<std::size_t>(n.inputs[0])].shape;
        }
    }
    if (shape.fold < 0 && !stage.nodes.empty()) {
        const std::size_t first = stage.inlined > 0 && static_cast<std::size_t>(stage.inlined) < stage.nodes.size() ? static_cast<std::size_t>(stage.inlined) : 0;
        shape.domain = g.values[static_cast<std::size_t>(g.nodes[static_cast<std::size_t>(stage.nodes[first])].output)].shape;
    }
    shape.collapsed = shape.fold < 0 ? 0u : graph::collapsed_axes(g.nodes[static_cast<std::size_t>(shape.fold)], shape.domain.rank);
    return shape;
}

// Where node `k` of a stage (a position in `stage.nodes`) stands: inside, before the fold's loops close, or after them,
// over the fold's result. A fold is inside, and so is an inlined producer. Any other node is after when it runs over
// the fold's result: its shape tells where the fold's operand and result differ, and coming after the first fold
// tells where they do not (a contraction's domain is its result). (A feedback stage's sample runs what is over the
// result before or after its fold, as its statements' order says: `loops/normalize.hpp` `sample_tree`.)
enum class Side : std::uint8_t { Inside, After };
constexpr Side side_of(const Graph& g, const Schedule::Stage& stage, const GroupShape& shape, std::size_t k) {
    const heinzel::Node& n = g.nodes[static_cast<std::size_t>(stage.nodes[k])];
    if (shape.fold < 0 || static_cast<std::int32_t>(k) < stage.inlined || graph::folds(n)) {
        return Side::Inside;
    }
    if (shape.domain != shape.folded) {
        return g.values[static_cast<std::size_t>(n.output)].shape == shape.folded ? Side::After : Side::Inside;
    }
    for (std::size_t j = 0; j < k; ++j) {
        if (stage.nodes[j] == shape.fold) {
            return Side::After;
        }
    }
    return Side::Inside;
}

// The region the statements after a stage's fold run over: the fold's result — for a contraction, the domain's
// leading axes, which ARE its result — and the domain itself where there is no fold.
constexpr std::expected<Region, Refusal> region_after_the_fold(const Graph& g, const GroupShape& shape) {
    Region after = graph::whole_domain(shape.domain);
    if (shape.fold >= 0) {
        const heinzel::Node& fold = g.nodes[static_cast<std::size_t>(shape.fold)];
        if (!graph::is_a_contraction(fold)) {
            const std::expected<Region, Refusal> folded = graph::after_the_fold(shape.domain, fold, shape.folded);
            if (!folded) {
                return std::unexpected(folded.error());
            }
            after = *folded;
        }
        after.inside_fold = false;
    }
    return after;
}

// Where node `k` reads its operands: over the domain, over the fold's result, or, for an inlined producer, over its own
// shape seen through the window that computes it. And where its result is stored: over the fold's result if it is the
// fold or runs after it, once the fold's loops close; over the domain otherwise.
constexpr std::expected<Region, Refusal> operand_region(const Graph& g, const Facts& facts, const Schedule::Stage& stage, const GroupShape& shape,
                                                        std::size_t k) {
    if (static_cast<std::int32_t>(k) < stage.inlined && stage.through >= 0) {
        return graph::seen_through(g, facts, g.nodes[static_cast<std::size_t>(stage.through)]);
    }
    return graph::side_of(g, stage, shape, k) == Side::After ? graph::region_after_the_fold(g, shape) : graph::whole_domain(shape.domain);
}
constexpr std::expected<Region, Refusal> store_region(const Graph& g, const Schedule::Stage& stage, const GroupShape& shape, std::size_t k) {
    const heinzel::Node& n = g.nodes[static_cast<std::size_t>(stage.nodes[k])];
    const bool folds_here = shape.fold >= 0 && graph::folds(n);
    return folds_here || graph::side_of(g, stage, shape, k) == Side::After ? graph::region_after_the_fold(g, shape) : graph::whole_domain(shape.domain);
}

} // namespace heinzel::loops::graph
