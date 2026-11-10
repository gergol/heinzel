#pragma once

// The layout of a band, derived from the graph and the stages it runs: which axis of each stage the band walks, how
// many rows ahead of the last stage each stage runs (its `lead`, the reach of the windows between them), and which
// values it keeps per chunk instead of whole. When a band cannot be laid out, the answer is the reason. The lowering
// and the planner both ask here, so they cannot disagree. It asks access maps and the rule table and names no
// operation.

#include <cstdint>
#include <expected>
#include <string_view>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/ir/timing.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/graph/places.hpp"
#include "heinzel/loops/graph/regions.hpp"
#include "heinzel/loops/graph/views.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/schedule.hpp"
#include "heinzel/support/fixed_vector.hpp"

namespace heinzel::loops {

// How a node of a stage touches a stored value along one of the stage's domain axes, as an operand (`band_read`) or as
// its own store (`band_store`). It records the value (read through views), the axis of the value that the domain axis
// walks, at what step and from which offset, and how far past the chunk's last row its window reaches.
struct BandTouch {
    std::int32_t value{kNoValue};
    std::int32_t value_axis{-1}; // -1: does not walk the value along that axis
    std::int32_t step{0};
    std::int32_t offset{0};
    std::int32_t ahead{0};  // Σ (extent - 1) * step over the stage's OTHER axes walking the same value axis (a window's taps)
    bool indirect{false};   // a lag (`back > 0`) or a gather
    bool unbounded{false};  // a symbolic extent reaches along the value axis: no finite `ahead`
};

using Stages = fixed_vector<Schedule::Stage, kMaxStages>;

namespace detail {

constexpr BandTouch touch_along(const Shape& domain, const graph::Address& at, std::int32_t value, std::int32_t axis) {
    BandTouch t{.value = value};
    if (!at.why_not.empty() || axis < 0 || axis >= static_cast<std::int32_t>(kMaxAxes)) {
        return t;
    }
    t.value_axis = at.axis[axis];
    t.step = at.step[axis];
    t.offset = t.value_axis >= 0 ? at.offset[t.value_axis] : 0;
    for (std::int32_t other = 0; other < domain.rank && t.value_axis >= 0; ++other) {
        if (other == axis || at.axis[other] != t.value_axis) {
            continue;
        }
        const Extent e = extent_at(domain, other);
        t.unbounded = t.unbounded || !is_literal(e);
        t.ahead += is_literal(e) ? (e.encoded - 1) * at.step[other] : 0;
    }
    return t;
}

} // namespace detail

// Operand `operand` of node `k` (a position in `stage.nodes`), along domain axis `axis` — as the lowering computes it.
constexpr std::expected<BandTouch, Refusal> band_read(const Graph& g, const graph::Facts& facts, const Schedule::Stage& stage, const graph::GroupShape& shape,
                                                      std::size_t k, std::size_t operand, std::int32_t axis) {
    const heinzel::Node& n = g.nodes[static_cast<std::size_t>(stage.nodes[k])];
    if (graph::is_a_contraction(n) || graph::is_made_in_parts(n) || operand >= rules_of(n.op).arity) {
        return BandTouch{}; // read through its own axes, or its own region: no band crosses such a stage
    }
    if (graph::gathers(n) != Gather::None && operand == (graph::gathers(n) == Gather::Table ? 1u : 0u)) {
        return BandTouch{.value = facts.read_as[static_cast<std::size_t>(n.inputs[operand])], .indirect = true}; // read where a position says
    }
    const std::expected<graph::Region, Refusal> region = graph::operand_region(g, facts, stage, shape, k);
    if (!region) {
        return std::unexpected(region.error());
    }
    if (graph::is_an_affine_view(n)) {
        const graph::Viewed through = graph::through_view(g, n, facts.through[static_cast<std::size_t>(n.inputs[0])]);
        return detail::touch_along(shape.domain, graph::address_over(g.values[static_cast<std::size_t>(n.output)].shape, *region, through), through.source, axis);
    }
    const std::int32_t v = facts.read_as[static_cast<std::size_t>(n.inputs[operand])];
    if (graph::hands_its_operand_on(n) && samples_back(n) > 0) {
        BandTouch t = detail::touch_along(shape.domain, graph::address_over(g.values[static_cast<std::size_t>(v)].shape, *region, graph::read_as_itself(v)), v, axis);
        t.indirect = true;
        return t;
    }
    const graph::Viewed& through = facts.through[static_cast<std::size_t>(v)];
    return detail::touch_along(shape.domain, graph::address_over(g.values[static_cast<std::size_t>(v)].shape, *region, through), through.source, axis);
}

// Where node `k` stores its own result, along domain axis `axis`.
constexpr std::expected<BandTouch, Refusal> band_store(const Graph& g, const Schedule::Stage& stage, const graph::GroupShape& shape, std::size_t k, std::int32_t axis) {
    const heinzel::Node& n = g.nodes[static_cast<std::size_t>(stage.nodes[k])];
    if (graph::is_a_contraction(n) || graph::is_made_in_parts(n)) {
        return BandTouch{.value = n.output};
    }
    const std::expected<graph::Region, Refusal> region = graph::store_region(g, stage, shape, k);
    if (!region) {
        return std::unexpected(region.error());
    }
    return detail::touch_along(shape.domain, graph::address_over(g.values[static_cast<std::size_t>(n.output)].shape, *region, graph::read_as_itself(n.output)),
                               n.output, axis);
}

// A band's layout, or why it cannot be laid out. For each stage of the band, in band order, it holds the axis of the
// stage's domain the band walks and how many rows the stage runs ahead of the last one. For each value kept per chunk
// (a band buffer) it holds the stage that makes it (`maker`, a position in the band), the stage that reads it from
// furthest behind (`reader`, whose chunk start is the buffer's row 0), the value axis it is walked along, and the
// maker's `lead`: the buffer holds `step + lead` rows.
struct BandLayout {
    Refusal why_not{}; // empty: the band is legal
    fixed_vector<std::int32_t, kMaxStages> axis{};
    fixed_vector<std::int32_t, kMaxStages> lead{};
    struct Kept {
        std::int32_t value{kNoValue};
        std::int32_t maker{-1};
        std::int32_t reader{-1};
        std::int32_t value_axis{-1};
        std::int32_t lead{0};
    };
    fixed_vector<Kept, kMaxBandBuffers> kept{};
};

// Why a stage cannot be in a band at all; empty: it can. (A stage that reads a relaid copy holds a contraction.)
constexpr Refusal why_not_in_any_band(const Graph& g, const graph::Facts& facts, const Schedule::Stage& stage) {
    if (stage.skewed) {
        return "heinzel::compile: a band holds no skewed stage (each chunk would drain its sections)";
    }
    if (stage.peeled) {
        return "heinzel::compile: a band holds no peeled stage (each chunk would start a run)";
    }
    for (const std::int32_t ni : stage.nodes) {
        const heinzel::Node& n = g.nodes[static_cast<std::size_t>(ni)];
        if (graph::is_made_in_parts(n) || graph::is_a_contraction(n)) {
            return "heinzel::compile: a band holds no value made in parts and no contraction";
        }
    }
    return graph::reads_a_feeds_past(g, facts, stage) ? "heinzel::compile: in a band, no group reads a feed's past (its first samples run apart)" : "";
}

// Why a stage cannot walk `axis` in a band; empty: it can. A fold collapsing the axis would collapse the rows the band
// cuts; this is asked of the graph, and the fold's loops give the same answer for every schedule the lowering accepts.
constexpr Refusal why_not_in_a_band(const Graph& g, const Schedule::Stage& stage, std::int32_t axis) {
    const graph::GroupShape shape = graph::shape_of_group(g, stage);
    if (axis < 0 || axis >= shape.domain.rank || (shape.collapsed >> axis & 1u) != 0) {
        return "heinzel::compile: a band walks an axis of each group's domain that no fold of it collapses";
    }
    return "";
}

using Reads = std::vector<BandTouch>; // a stage's operand reads along one axis (`detail::reads_of`)

namespace detail {

// Every operand read of a stage along its band axis, worked out once per band. A band asks it of every value an
// earlier stage makes; worked out afresh each time, a long chain took minutes to plan.
constexpr std::expected<Reads, Refusal> reads_of(const Graph& g, const graph::Facts& facts, const Schedule::Stage& stage, std::int32_t axis) {
    const graph::GroupShape shape = graph::shape_of_group(g, stage);
    Reads reads{};
    for (std::size_t k = 0; k < stage.nodes.size(); ++k) {
        for (std::size_t o = 0; o < rules_of(g.nodes[static_cast<std::size_t>(stage.nodes[k])].op).arity; ++o) {
            const std::expected<BandTouch, Refusal> t = loops::band_read(g, facts, stage, shape, k, o, axis);
            if (!t) {
                return std::unexpected(t.error());
            }
            reads.push_back(*t);
        }
    }
    return reads;
}

// Finds the reads of value `v` among a band stage's `reads`: the axis of `v` they walk and how far past the chunk the
// furthest one reaches, or why the band cannot keep up with them.
struct Reach {
    Refusal why_not{};
    std::int32_t value_axis{-1};
    std::int32_t ahead{-1}; // -1: the stage does not read `v`
};
constexpr Reach reach_of_reads(const Reads& reads, std::int32_t v) {
    Reach r{};
    for (const BandTouch& t : reads) {
        if (t.value != v) {
            continue;
        }
        if (t.indirect) {
            return Reach{.why_not = "heinzel::compile: in a band, a later group reads a band value through a lag or a gather"};
        }
        if (t.value_axis < 0 || t.offset != 0 || t.unbounded) {
            return Reach{.why_not = "heinzel::compile: in a band, a later group reads a band value from an offset, or off the band"};
        }
        if (t.step != 1) {
            return Reach{.why_not = "heinzel::compile: in a band, a later group reads a band value at a step other than one"};
        }
        if (r.ahead >= 0 && t.value_axis != r.value_axis) {
            return Reach{.why_not = "heinzel::compile: in a band, a group's band values disagree on the axis it walks"};
        }
        r.value_axis = t.value_axis;
        r.ahead = t.ahead > r.ahead ? t.ahead : r.ahead;
    }
    return r;
}

// The axis of stage `pos`'s domain along which its store `k` walks `value_axis` by one from the start (-1: none).
constexpr std::expected<std::int32_t, Refusal> store_axis(const Graph& g, const Schedule::Stage& stage, const graph::GroupShape& shape, std::size_t k,
                                                          std::int32_t value_axis) {
    for (std::int32_t x = 0; x < shape.domain.rank; ++x) {
        const std::expected<BandTouch, Refusal> t = loops::band_store(g, stage, shape, k, x);
        if (!t) {
            return std::unexpected(t.error());
        }
        if (t->value_axis == value_axis && t->step == 1 && t->offset == 0) {
            return x;
        }
    }
    return -1;
}

// Lays out stage `pos` of the band: its band values (what it stores that a later stage of the band reads), how far
// ahead they are read, which is its lead, and the axis of its domain along which its stores walk with the band.
constexpr Refusal lead_and_axis(const Graph& g, const Stages& stages, const Schedule::Band& band, std::size_t pos, const std::vector<Reads>& reads, BandLayout& layout) {
    const Schedule::Stage& stage = stages[static_cast<std::size_t>(band.first) + pos];
    const graph::GroupShape shape = graph::shape_of_group(g, stage);
    std::int32_t lead = -1;
    std::int32_t axis = -1;
    for (std::size_t k = 0; k < stage.nodes.size(); ++k) {
        const std::int32_t v = g.nodes[static_cast<std::size_t>(stage.nodes[k])].output;
        std::int32_t needs = -1;
        std::int32_t value_axis = -1;
        for (std::size_t later = pos + 1; later < static_cast<std::size_t>(band.count); ++later) {
            const Reach r = detail::reach_of_reads(reads[later], v);
            if (!r.why_not.empty()) {
                return r.why_not;
            }
            if (r.ahead >= 0) {
                if (value_axis >= 0 && r.value_axis != value_axis) {
                    return "heinzel::compile: in a band, a group's band values disagree on the axis it walks";
                }
                value_axis = r.value_axis;
                needs = layout.lead[later] + r.ahead > needs ? layout.lead[later] + r.ahead : needs;
            }
        }
        if (needs < 0) {
            continue; // no later stage of the band reads it
        }
        const std::expected<std::int32_t, Refusal> here = detail::store_axis(g, stage, shape, k, value_axis);
        if (!here) {
            return here.error();
        }
        if (*here < 0 || (axis >= 0 && *here != axis)) {
            return "heinzel::compile: in a band, a group's band values disagree on the axis it walks";
        }
        axis = *here;
        lead = needs > lead ? needs : lead;
    }
    if (axis < 0) {
        return "heinzel::compile: in a band, every group but the last makes something a later one reads";
    }
    layout.axis[pos] = axis;
    layout.lead[pos] = lead;
    return loops::why_not_in_a_band(g, stage, axis);
}

// A band buffer: what it keeps, which stage makes it and which reads it from furthest behind, or why it cannot be kept
// per chunk (a prefix along any axis but time, or rows that are not contiguous).
constexpr Refusal keep(const Graph& g, const graph::Facts& facts, const Schedule::Band& band, std::size_t maker, std::int32_t v, const std::vector<Reads>& reads,
                       BandLayout& layout) {
    const Value& held = g.values[static_cast<std::size_t>(v)];
    BandLayout::Kept kept{.value = v, .maker = static_cast<std::int32_t>(maker), .lead = layout.lead[maker]};
    for (std::size_t later = maker + 1; later < static_cast<std::size_t>(band.count); ++later) {
        const Reach r = detail::reach_of_reads(reads[later], v);
        kept.value_axis = r.ahead >= 0 ? r.value_axis : kept.value_axis;
        if (r.ahead >= 0 && (kept.reader < 0 || layout.lead[later] < layout.lead[static_cast<std::size_t>(kept.reader)])) {
            kept.reader = static_cast<std::int32_t>(later);
        }
    }
    if (facts.deepest[static_cast<std::size_t>(v)] > 0 && kept.value_axis != held.shape.rank - 1) {
        return "heinzel::compile: a band buffer keeps silence before its block only along time";
    }
    for (const std::int32_t axis : graph::memory_order(held.shape.rank)) {
        if (axis == kept.value_axis) {
            break;
        }
        if (extent_at(held.shape, axis) != literal_extent(1)) {
            return "heinzel::compile: a band buffer's rows must be contiguous: its outer axes all of extent one";
        }
    }
    if (layout.kept.size() == kMaxBandBuffers) {
        return "heinzel::compile: a band keeps more values than a band holds (kMaxBandBuffers)";
    }
    layout.kept.push_back(kept);
    return "";
}

// Checks that every stage's extent along the band is the last one's plus its lead, and lays out the values the band
// keeps per chunk.
constexpr Refusal extents_and_kept(const Graph& g, const graph::Facts& facts, const graph::ValuePlaces& at, const Stages& stages, const Schedule::Band& band,
                                   const std::vector<Reads>& reads, BandLayout& layout) {
    const std::size_t last = static_cast<std::size_t>(band.count) - 1;
    const Extent tail = extent_at(graph::shape_of_group(g, stages[static_cast<std::size_t>(band.first) + last]).domain, band.axis);
    for (std::size_t pos = 0; pos < last; ++pos) {
        const Schedule::Stage& stage = stages[static_cast<std::size_t>(band.first) + pos];
        const Extent e = extent_at(graph::shape_of_group(g, stage).domain, layout.axis[pos]);
        const bool fits = is_literal(e) && is_literal(tail) ? e.encoded == tail.encoded + layout.lead[pos] : e == tail && layout.lead[pos] == 0;
        if (!fits) {
            return "heinzel::compile: in a band, a group's extent along it is not the last group's plus its lead";
        }
        for (const std::int32_t ni : stage.nodes) {
            const std::int32_t v = g.nodes[static_cast<std::size_t>(ni)].output;
            bool read_later = false;
            for (std::size_t later = pos + 1; later <= last && !read_later; ++later) {
                read_later = detail::reach_of_reads(reads[later], v).ahead >= 0;
            }
            if (read_later && !graph::read_outside(at, v, band.first, band.first + band.count)) {
                if (const Refusal why = detail::keep(g, facts, band, pos, v, reads, layout); !why.empty()) {
                    return why;
                }
            }
        }
    }
    return "";
}

} // namespace detail

// Each stage's reads along an axis, kept across the layouts of the runs that end at one stage: the planner tries the
// longest run first, then shorter ones, and the axes are the same for all of them. `band.step` is not read, since a
// band buffer's rows are `step + lead` wherever they are needed.
static_assert(kMaxRank <= kMaxAxes, "a stage's axes index the memo below");
struct ReadsMemo {
    std::vector<Reads> reads{}; // per stage, per axis of its domain: `reads[s * kMaxAxes + axis]`
    std::vector<bool> known{};
};
constexpr std::expected<const Reads*, Refusal> memo_reads(ReadsMemo& memo, const Graph& g, const graph::Facts& facts, const Stages& stages, std::size_t s,
                                                         std::int32_t axis) {
    if (memo.known.size() != stages.size() * kMaxAxes) {
        memo.reads.assign(stages.size() * kMaxAxes, Reads{});
        memo.known.assign(stages.size() * kMaxAxes, false);
    }
    const std::size_t at = s * kMaxAxes + static_cast<std::size_t>(axis);
    if (!memo.known[at]) {
        std::expected<Reads, Refusal> reads = detail::reads_of(g, facts, stages[s], axis);
        if (!reads) {
            return std::unexpected(reads.error());
        }
        memo.reads[at] = static_cast<Reads&&>(*reads);
        memo.known[at] = true;
    }
    return &memo.reads[at];
}

// The layout of `band`, given its first stage, how many stages it runs, and the axis of the last one's domain it walks
// (`at`: where the stages make and read each value). The last stage is laid out first, then each earlier one from what
// the later ones read.
constexpr BandLayout band_layout(const Graph& g, const graph::Facts& facts, const graph::ValuePlaces& at, const Stages& stages, const Schedule::Band& band,
                                 ReadsMemo& memo) {
    BandLayout layout{};
    if (band.count < 2 || band.first < 0 || static_cast<std::size_t>(band.first) >= stages.size() ||
        static_cast<std::size_t>(band.count) > stages.size() - static_cast<std::size_t>(band.first)) {
        layout.why_not = "heinzel::compile: a band runs two or more of the schedule's groups";
        return layout;
    }
    for (std::int32_t pos = 0; pos < band.count; ++pos) {
        layout.axis.push_back(-1);
        layout.lead.push_back(0);
        const std::size_t s = static_cast<std::size_t>(band.first + pos);
        const Refusal why = loops::why_not_in_any_band(g, facts, stages[s]);
        layout.why_not = layout.why_not.empty() ? why : layout.why_not;
    }
    if (!layout.why_not.empty()) {
        return layout;
    }
    const std::size_t last = static_cast<std::size_t>(band.count) - 1;
    layout.axis[last] = band.axis;
    layout.why_not = loops::why_not_in_a_band(g, stages[static_cast<std::size_t>(band.first) + last], band.axis);
    std::vector<Reads> reads(static_cast<std::size_t>(band.count));
    const auto read = [&](std::size_t pos) -> Refusal { // stage `pos`'s reads along its band axis, into `reads`
        const std::expected<const Reads*, Refusal> r = loops::memo_reads(memo, g, facts, stages, static_cast<std::size_t>(band.first) + pos, layout.axis[pos]);
        reads[pos] = r ? **r : Reads{};
        return r ? Refusal{} : r.error();
    };
    if (layout.why_not.empty()) {
        layout.why_not = read(last);
    }
    for (std::size_t pos = last; pos-- > 0 && layout.why_not.empty();) {
        layout.why_not = detail::lead_and_axis(g, stages, band, pos, reads, layout);
        if (layout.why_not.empty() && pos > 0) {
            layout.why_not = read(pos);
        }
    }
    if (layout.why_not.empty()) {
        layout.why_not = detail::extents_and_kept(g, facts, at, stages, band, reads, layout);
    }
    return layout;
}

} // namespace heinzel::loops
