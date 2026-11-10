#pragma once

// Plans a contraction's loops: its output walked in memory order, then the contracted axis. Lanes run across its
// outputs where memory allows, otherwise along the contracted axis. The loop around the lanes is unroll-and-jammed to
// fill the registers, tiles fit the caches, an operand striding along the lanes is read from a copy turned around and a
// strided one from a copy blocked along the lanes, each where the copy pays, and a fetch ahead is planned where a
// block's results move further than a prefetcher follows. Every
// constant is `Machine`'s or a measured literal. It names no operation.

#include <cstdint>

#include "heinzel/ir/ir.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/schedule.hpp"
#include "heinzel/schedule/machine.hpp"
#include "heinzel/support/fixed_vector.hpp"

namespace heinzel::loops::planner {

static_assert(kMaxRank + 3 <= kMaxStageLoops, "a contraction's loops — its result's axes, the contracted one, two tiles — fit a stage");

namespace detail {

// Tile a contraction so that what its inner loops reuse stays in cache. The innermost loops read, for one block of
// `rows` x `columns` outputs, `rows` rows of one operand and a `columns`-wide strip of the other, each as long as the
// contracted axis: chunk THAT axis so both fit the first-level cache (a strided strip costs a cache line per step).
// Then chunk the unrolled axis so that one chunk's rows of the first operand — read again for every block of columns
// — fit half the second level, and walk the columns OUTSIDE the rows within a chunk, so the strip stays put while the
// rows go by. A chunked contracted axis leaves partial sums in the result between chunks, rounded to its type — so
// not for a fold owed its strict order. Literal extents only: a chunk no larger than the axis. A product's rows are not
// tiled along time where its stage reads a feed's past (that stage runs its first samples apart).
constexpr void tile_for_the_caches(Schedule::Stage& planned, const Graph& g, const graph::Facts& facts, const heinzel::Node& n, const Machine& m,
                                   std::int32_t rows, std::int32_t columns) {
    const Value& made = g.values[static_cast<std::size_t>(n.output)];
    const std::int32_t bytes = loops::bytes_of(made.type);
    const std::int32_t contracted = made.shape.rank;
    const Extent along = extent_at(g.values[static_cast<std::size_t>(n.inputs[0])].shape, n.attr.matmul.a_contract);
    const std::size_t lane_at = planned.loops.size() - 2;
    const std::size_t rows_at = planned.loops.size() - 3;
    const Schedule::Loop lanes = planned.loops[lane_at];
    const Schedule::Loop unrolled = planned.loops[rows_at];
    if (unrolled.kind != LoopKind::Unroll || !numerics::grants(made.precision).reorder) {
        return;
    }
    // (Sizes in 64 bits: a `Machine` is data, and a register count or a line near the ends of 32 bits overflowed.)
    const std::int64_t line = m.line_bytes;
    const std::int64_t wide = std::int64_t{columns} * bytes;
    const std::int64_t strip = line > 0 ? (wide + line - 1) / line * line : wide; // (no line size: no rounding)
    std::int32_t chunk = static_cast<std::int32_t>(m.l1_bytes / (std::int64_t{rows} * bytes + strip));
    chunk -= chunk % 8;
    const bool chunk_contracted = is_literal(along) && along.encoded > chunk + chunk / 2 && chunk >= 8;
    const std::int32_t reach = chunk_contracted ? chunk : (is_literal(along) ? along.encoded : chunk);
    if (reach < 1) {
        return; // a contracted axis of no elements, or a symbolic one no chunk of which fits the first level
    }
    std::int32_t slab = static_cast<std::int32_t>(m.l2_bytes / 2 / (std::int64_t{reach} * bytes));
    slab -= slab % rows;
    const Extent down = extent_at(made.shape, unrolled.axis);
    const bool time_runs_apart = unrolled.axis == made.shape.rank - 1 && graph::reads_a_feeds_past(g, facts, planned);
    const bool chunk_rows = is_literal(down) && down.encoded > slab + slab / 2 && slab >= rows && !time_runs_apart;
    if (!chunk_contracted && !chunk_rows) {
        return;
    }
    // [chunks of the contracted axis] [chunks of rows] [columns in lanes] [rows unrolled] [the contracted axis]
    fixed_vector<Schedule::Loop, kMaxStageLoops> loops{};
    for (std::size_t l = 0; l < rows_at; ++l) {
        loops.push_back(planned.loops[l]);
    }
    if (chunk_contracted) {
        loops.push_back(Schedule::Loop{.axis = contracted, .step = chunk, .kind = LoopKind::Tile});
    }
    if (chunk_rows) {
        loops.push_back(Schedule::Loop{.axis = unrolled.axis, .step = slab, .kind = LoopKind::Tile});
    }
    loops.push_back(lanes);
    loops.push_back(unrolled);
    planned.inner_from = static_cast<std::int32_t>(loops.size());
    loops.push_back(planned.loops[planned.loops.size() - 1]);
    planned.loops = loops;
    // With the rows inside the columns, consecutive blocks write `rows` result rows apart. Once that is a page or more
    // no hardware prefetcher follows, and every store would wait for a line nothing had asked for (a short contracted
    // axis, 2048 x 64 x 2048: 7.1 -> 4.7 ms; rows 64 bytes long: no gain).
    const Extent across = extent_at(made.shape, lanes.axis);
    if (line > 0 && (!is_literal(across) || std::int64_t{rows} * across.encoded * bytes >= m.page_bytes)) {
        planned.fetches = true;
    }
}

// The operand that carries the lane axis is walked along the CONTRACTED axis by the innermost loop, a whole row apart
// per step — at a power-of-two width every line of a block's strip falls in the same one or two cache sets, and past
// a page apart every step is a new page. Read from a copy blocked along the lane axis (`relaid`), a block's strip is
// contiguous. The copy is a pass over the operand, so it has to pay:
//   * what has to stay in cache while a block's rows go by is ONE sample's, one batch element's strips; where those
//     fit the first-level cache they are served from it however they are walked;
//   * a strip is read once per block of rows, over every result axis the operand does not have (a symbolic one counts
//     as long), and has to be read `Machine::relay_pays_after` times. Measured on zen2 with 256 x 256 strips: the copy
//     breaks even at 64 rows — about ten blocks of six — and at ONE row, a matrix-vector product, costs 2.4 times the
//     product.
constexpr void relay_where_it_pays(Schedule::Stage& planned, const Graph& g, const heinzel::Node& n, const Machine& m, std::int32_t rows,
                                   std::int32_t block) {
    const std::int64_t walks_that_pay = m.relay_pays_after;
    const Shape& result = g.values[static_cast<std::size_t>(n.output)].shape;
    const std::int32_t streamed = result.rank - 1;
    const std::int32_t lane_axis = result.rank - 2;
    const std::int32_t batch = n.attr.matmul.batch_a.rank;
    for (std::int32_t which = 0; which < 2; ++which) {
        const Value& held = g.values[static_cast<std::size_t>(n.inputs[static_cast<std::size_t>(which)])];
        const auto axes = graph::contraction_axes(g, n, which);
        bool has[kMaxRank + 1]{};
        bool known = true;
        std::int64_t strips = loops::bytes_of(held.type);
        for (std::size_t k = 0; k < axes.size(); ++k) {
            has[axes[k]] = true;
            const Extent e = extent_at(held.shape, static_cast<std::int32_t>(k));
            if (axes[k] != streamed && axes[k] >= batch) {
                known = known && is_literal(e);
                strips *= is_literal(e) ? e.encoded : 1;
            }
        }
        const std::int64_t want = walks_that_pay * rows;
        std::int64_t walks = 1;
        for (std::int32_t axis = batch; axis < streamed && walks < want; ++axis) {
            const Extent e = extent_at(result, axis);
            const std::int64_t by = has[axis] ? 1 : is_literal(e) ? e.encoded : want;
            walks = by > 0 && walks >= (want + by - 1) / by ? want : walks * by; // (enough is `want`: no product past it)
        }
        if (has[lane_axis] && known && strips > m.l1_bytes && walks >= want) {
            planned.relaid = which;
            planned.relaid_block = block;
        }
    }
}

// Where lanes cannot run across a contraction's outputs — an operand strides along that axis, as the second operand of
// a row-by-row dot product does (`Q . K^T`), or a read after the fold does (`a . b + c^T`) — they may run along the
// CONTRACTED axis instead, as a plain fold's do: both operands must be contiguous in it and it must hold a lane, and it
// reorders each output's sum, so the precision has to grant that. Only the fold's own statements walk that axis.
constexpr void lanes_along_the_contracted_axis(Schedule::Stage& planned, const Graph& g, const heinzel::Node& n, std::int32_t lanes) {
    const Value& made = g.values[static_cast<std::size_t>(n.output)];
    const Extent along = extent_at(g.values[static_cast<std::size_t>(n.inputs[0])].shape, n.attr.matmul.a_contract);
    if (!numerics::grants(made.precision).reorder || (is_literal(along) && along.encoded < lanes)) {
        return;
    }
    for (std::int32_t which = 0; which < 2; ++which) {
        const auto axes = graph::contraction_axes(g, n, which);
        if (axes[static_cast<std::size_t>(loops::contiguous_axis(static_cast<std::int32_t>(axes.size())))] != made.shape.rank) {
            return;
        }
    }
    planned.loops[planned.loops.size() - 1] = Schedule::Loop{.axis = made.shape.rank, .step = lanes, .kind = LoopKind::Vector, .unroll = 4};
}

// How many times a product reads each number of its operand `which`: the extents of the result's axes the operand lacks,
// up to `enough` (a symbolic one counts as that many).
constexpr std::int64_t reads_of_each_number(const Graph& g, const heinzel::Node& n, std::int32_t which, std::int64_t enough) {
    const Shape& result = g.values[static_cast<std::size_t>(n.output)].shape;
    bool has[kMaxRank + 1]{};
    for (const std::int32_t axis : graph::contraction_axes(g, n, which)) {
        has[axis] = true;
    }
    std::int64_t reads = 1;
    for (std::int32_t axis = 0; axis < result.rank; ++axis) { // (every axis: an empty one makes no read at all)
        const Extent e = extent_at(result, axis);
        const std::int64_t by = has[axis] ? 1 : is_literal(e) ? e.encoded : enough;
        reads = by == 0 ? 0 : reads >= enough ? reads : reads * by;
    }
    return reads;
}

} // namespace detail

// The loops of a contraction's stage: its output walked in memory order, then the contracted axis. Lanes run along the
// output's contiguous axis — across OUTPUTS, so no sum is reordered and any precision allows it — when each operand
// either lacks that axis or is contiguous in it too, and every other read and store of the stage walks memory along it
// (`across`, asked of the stage by the caller). One operand that strides along it (`K` of `Q . K^T`), is read as itself
// and has each of its numbers read `Machine::turn_pays_after` times or more is read from a copy turned around
// (`turned`), so that it is contiguous in it too: attention's score product 1.8-3x faster than with lanes along the
// contracted axis from 32 to 512 rows (`Plan/probes/v5-score/`). The loop around the lanes is unroll-and-jammed, and
// the lanes unrolled, until the accumulators fill the registers: `rows * unroll` of them, `unroll` loaded operands and
// one broadcast one.
constexpr void plan_contraction_loops(Schedule::Stage& planned, const Graph& g, const graph::Facts& facts, const heinzel::Node& n, const Machine& m,
                                      bool across) {
    const Value& made = g.values[static_cast<std::size_t>(n.output)];
    const auto order = graph::memory_order(made.shape.rank);
    for (const std::int32_t axis : order) {
        planned.loops.push_back(Schedule::Loop{.axis = axis});
    }
    planned.inner_from = static_cast<std::int32_t>(planned.loops.size());
    planned.loops.push_back(Schedule::Loop{.axis = made.shape.rank});

    planned.contract = m.fma && numerics::grants(made.precision).reorder;

    const std::int32_t lanes = m.vector_bytes / loops::bytes_of(made.type);
    if (order.size() < 2 || lanes < 2) {
        return;
    }
    const std::int32_t lane_axis = order[order.size() - 1];
    if (!across) {
        detail::lanes_along_the_contracted_axis(planned, g, n, lanes); // a read after the fold strides along it
        return;
    }
    for (std::int32_t which = 0; which < 2; ++which) {
        const auto axes = graph::contraction_axes(g, n, which);
        const std::int32_t rank = static_cast<std::int32_t>(axes.size());
        const std::int32_t v = n.inputs[static_cast<std::size_t>(which)];
        for (std::int32_t k = 0; k < rank; ++k) {
            if (axes[static_cast<std::size_t>(k)] != lane_axis || k == loops::contiguous_axis(rank)) {
                continue;
            }
            if (planned.turned >= 0 || facts.through[static_cast<std::size_t>(v)].source != v ||
                detail::reads_of_each_number(g, n, which, m.turn_pays_after) < m.turn_pays_after) {
                planned.turned = -1;
                detail::lanes_along_the_contracted_axis(planned, g, n, lanes); // (two such, one through a view, or too few reads)
                return;
            }
            planned.turned = which;
        }
    }
    constexpr std::int32_t unroll = 2;
    const auto rows = static_cast<std::int32_t>((std::int64_t{m.registers} - unroll - 1) / unroll);
    planned.loops[order.size() - 1] = Schedule::Loop{.axis = lane_axis, .step = lanes, .kind = LoopKind::Vector, .unroll = unroll};
    if (rows > 1) {
        planned.loops[order.size() - 2] = Schedule::Loop{.axis = order[order.size() - 2], .step = rows, .kind = LoopKind::Unroll};
    }
    detail::tile_for_the_caches(planned, g, facts, n, m, rows, lanes * unroll);
    detail::relay_where_it_pays(planned, g, n, m, rows, lanes * unroll);
}

} // namespace heinzel::loops::planner
