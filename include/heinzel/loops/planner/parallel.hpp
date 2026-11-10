#pragma once

// Which stages may be split among workers, and along which loop (`Schedule::Stage::parallel`), and which bands along
// their chunks (`Schedule::Band::parallel`). A worker runs the whole stage over a range of the split loop's axis, so a
// split is legal where no two workers can touch an element either writes: the lowering's rule, asked here of the graph.
// This pass only says where, and how much work a worker is owed at least; the run decides from the work it resolves
// whether to split, and among how many. A read or store with no address answers no here: the lowering refuses it by
// name.

#include <array>
#include <cstdint>
#include <expected>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/timing.hpp"
#include "heinzel/loops/band_layout.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/graph/places.hpp"
#include "heinzel/loops/graph/regions.hpp"
#include "heinzel/loops/planner/bands.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/schedule.hpp"
#include "heinzel/schedule/machine.hpp"

namespace heinzel::loops::planner {
namespace detail {

// Does `stage` make value `v` (one of its nodes' outputs)?
constexpr bool makes(const Graph& g, const Schedule::Stage& stage, std::int32_t v) {
    for (const std::int32_t ni : stage.nodes) {
        if (g.nodes[static_cast<std::size_t>(ni)].output == v) {
            return true;
        }
    }
    return false;
}

// Does node `k` of the stage make its value moving along domain axis `axis`? A contraction does along any axis of its
// result (its contracted axis is numbered past them); anything else does as `band_store` walks it.
constexpr bool made_along(const Graph& g, const Schedule::Stage& stage, const graph::GroupShape& shape, std::size_t k, std::int32_t axis) {
    const heinzel::Node& n = g.nodes[static_cast<std::size_t>(stage.nodes[k])];
    if (graph::is_a_contraction(n)) {
        return axis < g.values[static_cast<std::size_t>(n.output)].shape.rank;
    }
    const std::expected<BandTouch, Refusal> made = loops::band_store(g, stage, shape, k, axis);
    return made && made->value_axis >= 0 && made->step != 0;
}

// May the stage be split along axis `axis` of its domain, one that no fold of it collapses (the pass asks about no
// other)? It may when every node it stores, or may store, makes its value moving along that axis (a producer computed
// inside its window is never stored and is not asked), and no node reads the past of what the stage itself makes along
// it, or reads it at a position. This is stricter than the lowering, never looser: every node of a stage makes its
// value over the domain or over the fold's result, and what the lowering stores is some of them.
constexpr bool splits_along(const Graph& g, const graph::Facts& facts, const Schedule::Stage& stage, std::int32_t axis) {
    if (stage.nodes.size() == 1 && graph::is_made_in_parts(g.nodes[static_cast<std::size_t>(stage.nodes[0])])) {
        return false; // copies of parts: cheap, and not worth a rule
    }
    if (stage.peeled) {
        return false; // a worker's range would not start where the run does (Plan/33 S3: until a benchmark asks)
    }
    const graph::GroupShape shape = graph::shape_of_group(g, stage);
    for (std::size_t k = static_cast<std::size_t>(stage.inlined); k < stage.nodes.size(); ++k) {
        const heinzel::Node& n = g.nodes[static_cast<std::size_t>(stage.nodes[k])];
        if (!detail::made_along(g, stage, shape, k, axis) || graph::is_a_link(n)) {
            return false; // (a link reads, along the sections, what its stage makes at the element before)
        }
        const std::size_t operand = graph::gathers(n) == Gather::Table ? 1 : 0;
        const bool indirect = graph::gathers(n) != Gather::None || (graph::hands_its_operand_on(n) && samples_back(n) > 0);
        const std::int32_t read = indirect ? facts.read_as[static_cast<std::size_t>(n.inputs[operand])] : kNoValue;
        if (read == kNoValue || !detail::makes(g, stage, read)) {
            continue;
        }
        const std::expected<BandTouch, Refusal> t = graph::gathers(n) != Gather::None ? BandTouch{} : loops::band_read(g, facts, stage, shape, k, 0, axis);
        if (graph::gathers(n) != Gather::None || !t || t->value_axis == g.values[static_cast<std::size_t>(read)].shape.rank - 1) {
            return false; // feedback along time, or a position into what the stage makes
        }
    }
    return true;
}

// May `band` be split along its chunks? It may when nothing but its band buffers is carried from chunk to chunk (no
// stage reads its own past along the band, and no band buffer keeps silence), and a stage running ahead of the last
// stores nothing whole, since its first rows of a worker's run are also the previous worker's last. What it stores
// whole is what something outside the band reads and what keeps its past (the lowering gives that a whole buffer).
// This is the planner's side of the lowering's check of a split band, asked of the band's layout.
constexpr bool band_splits(const Graph& g, const graph::Facts& facts, const graph::ValuePlaces& at, const Stages& stages, const Schedule::Band& band,
                           const BandLayout& layout) {
    for (const BandLayout::Kept& kept : layout.kept) {
        if (facts.deepest[static_cast<std::size_t>(kept.value)] > 0) {
            return false;
        }
    }
    for (std::int32_t pos = 0; pos < band.count; ++pos) {
        const Schedule::Stage& stage = stages[static_cast<std::size_t>(band.first + pos)];
        const graph::GroupShape shape = graph::shape_of_group(g, stage);
        for (std::size_t k = 0; k < stage.nodes.size(); ++k) {
            const heinzel::Node& n = g.nodes[static_cast<std::size_t>(stage.nodes[k])];
            const bool whole = graph::read_outside(at, n.output, band.first, band.first + band.count) || facts.deepest[static_cast<std::size_t>(n.output)] > 0;
            if (layout.lead[static_cast<std::size_t>(pos)] > 0 && whole) {
                return false; // a band is split only where no stage running ahead stores a value whole
            }
            const std::int32_t read = graph::hands_its_operand_on(n) && samples_back(n) > 0 ? facts.read_as[static_cast<std::size_t>(n.inputs[0])] : kNoValue;
            if (read == kNoValue || !detail::makes(g, stage, read)) {
                continue;
            }
            const std::expected<BandTouch, Refusal> t = loops::band_read(g, facts, stage, shape, k, 0, layout.axis[static_cast<std::size_t>(pos)]);
            if (!t || t->value_axis == g.values[static_cast<std::size_t>(read)].shape.rank - 1) {
                return false;
            }
        }
    }
    return true;
}

// Does anything the stage makes move along domain axis `axis`?
constexpr bool moves_what_it_makes(const Graph& g, const Schedule::Stage& stage, const graph::GroupShape& shape, std::int32_t axis) {
    for (std::size_t k = static_cast<std::size_t>(stage.inlined); k < stage.nodes.size(); ++k) {
        if (detail::made_along(g, stage, shape, k, axis)) {
            return true;
        }
    }
    return false;
}

} // namespace detail

// On a machine with more than one core, every stage outside a band is split along the outermost loop over an axis that
// `splits_along` allows. An axis of literal extent one has nothing to cut, and only the outermost loop over an axis may
// be split, since a worker's walk starts where that loop stands. The stage is split only where no loop outside that one
// moves what it makes, so that each worker writes one contiguous stretch of memory. Split further in, every worker
// writes a slice of every row, all in the same pages at once, and the lines at the edges pass between cores on every
// row: a bank of 1024 voices split along the voices inside its time loop ran 2.5 times slower on 16 cores, and so did
// a plain map of that layout. A contraction dense enough in arithmetic would still win split that way (along its
// columns, 9.5x against 5x for its row Tile at 1024^3); this rule leaves that gain untaken. Every band is split along
// its chunks where its layout (`layouts`, as the band pass worked them out) allows it.
constexpr void split_among_workers(Schedule& s, const Graph& g, const graph::Facts& facts, const graph::ValuePlaces& at, const Machine& m,
                                   const BandLayouts& layouts) {
    s.parallel_work = m.parallel_work;
    s.line_bytes = m.line_bytes;
    if (m.cores < 2) {
        return;
    }
    std::array<bool, kMaxStages> in_a_band{};
    const Stages& stages = s.stages;
    for (std::size_t b = 0; b < s.bands.size(); ++b) {
        Schedule::Band& band = s.bands[b];
        for (std::int32_t pos = 0; pos < band.count; ++pos) {
            in_a_band[static_cast<std::size_t>(band.first + pos)] = true;
        }
        band.parallel = detail::band_splits(g, facts, at, stages, band, layouts[b]);
    }
    for (std::size_t si = 0; si < s.stages.size(); ++si) {
        Schedule::Stage& stage = s.stages[si];
        const graph::GroupShape shape = graph::shape_of_group(g, stage);
        const Shape& domain = shape.domain;
        // (a fold's own loops collapse their axes — and a fold that keeps them stores along them at extent one; a
        // feedback stage that folds its voices every sample walks them outside its inner loop, Plan/23 10)
        const std::size_t outer = shape.fold >= 0 ? static_cast<std::size_t>(stage.inner_from) : stage.loops.size();
        std::array<bool, kMaxAxes> walked{};
        for (std::size_t l = 0; l < outer && !in_a_band[si] && stage.parallel < 0; ++l) {
            const std::int32_t axis = stage.loops[l].axis;
            if (axis < 0 || axis >= domain.rank || walked[static_cast<std::size_t>(axis)] || (shape.collapsed >> axis & 1u) != 0) {
                continue;
            }
            walked[static_cast<std::size_t>(axis)] = true;
            if (extent_at(domain, axis) == literal_extent(1)) {
                continue;
            }
            if (detail::splits_along(g, facts, stage, axis)) {
                stage.parallel = static_cast<std::int32_t>(l);
            } else if (detail::moves_what_it_makes(g, stage, shape, axis)) {
                break; // a split further in would interleave the workers' writes
            }
        }
    }
}

} // namespace heinzel::loops::planner
