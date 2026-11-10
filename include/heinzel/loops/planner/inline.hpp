#pragma once

// Decides whether a producer read only through one window is stored once or computed inside its reader. Stored, it is
// computed once per element and costs a pass over its buffer each way. Inside its reader's loops
// (`Schedule::Stage::through`) it is computed once per tap and costs no memory. The rule, with its measurements, is at
// the end. It names no operation.

#include <cstdint>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/graph/regions.hpp"
#include "heinzel/loops/graph/views.hpp"
#include "heinzel/loops/schedule.hpp"
#include "heinzel/schedule/machine.hpp"

namespace heinzel::loops::planner {

namespace detail {

// Is node `ni` in this stage?
constexpr bool holds(const Schedule::Stage& stage, std::int32_t ni) {
    for (const std::int32_t member : stage.nodes) {
        if (member == ni) {
            return true;
        }
    }
    return false;
}

// Does what node `ni` makes reach nothing but `window` — read by it, or by views with no statement of their own that
// lead only there? (What is computed inside a window's reader is stored nowhere, so nothing else may want it.)
constexpr bool leads_only_to(const Graph& g, const graph::Facts& facts, std::size_t ni, std::int32_t window) {
    if (static_cast<std::int32_t>(ni) == window) {
        return true;
    }
    if (facts.statement[ni] || graph::folds_into_its_readers(g.nodes[ni])) {
        return false;
    }
    for (std::size_t r = 0; r < g.nodes.size(); ++r) {
        for (std::size_t k = 0; k < rules_of(g.nodes[r].op).arity; ++k) {
            if (g.nodes[r].inputs[k] == g.nodes[ni].output && !detail::leads_only_to(g, facts, r, window)) {
                return false;
            }
        }
    }
    return true;
}

// May stage `made`, maps only, run inside `reader`, seen through `window`? Only if nothing but the window reads what it
// makes, nothing but the reader reads the window, what it reads itself is stored plainly, and the reader's lanes
// still walk all of that contiguously.
constexpr bool may_run_through(const Graph& g, const graph::Facts& facts, const Schedule::Stage& made, const Schedule::Stage& reader, std::int32_t window) {
    for (const std::int32_t ni : made.nodes) { // maps only — asked first: what is read over the window's region is a map's operand
        const heinzel::Node& n = g.nodes[static_cast<std::size_t>(ni)];
        if (!graph::is_a_plain_map(n) || rules_of(n.op).cost != CostClass::Cheap || is_a_result(g, n.output)) {
            return false;
        }
    }
    const heinzel::Node& view = g.nodes[static_cast<std::size_t>(window)];
    const graph::Region seen = graph::seen_through(g, facts, view);
    std::int32_t lanes_along = -1;
    for (const Schedule::Loop& loop : reader.loops) {
        lanes_along = loop.kind == LoopKind::Vector ? loop.axis : lanes_along;
    }
    for (std::size_t ni = 0; ni < g.nodes.size(); ++ni) {
        const heinzel::Node& n = g.nodes[ni];
        const bool in_made = detail::holds(made, static_cast<std::int32_t>(ni));
        const bool in_reader = detail::holds(reader, static_cast<std::int32_t>(ni));
        if (in_reader && graph::is_a_contraction(n)) {
            return false; // a contraction reads its operands from storage
        }
        for (std::size_t k = 0; k < rules_of(n.op).arity; ++k) {
            const std::int32_t v = n.inputs[k];
            const std::int32_t maker = facts.producer[static_cast<std::size_t>(v)];
            const bool from_made = maker >= 0 && detail::holds(made, maker);
            if (from_made && !in_made && !detail::leads_only_to(g, facts, ni, window)) {
                return false;
            }
            if (v == view.output && !in_reader) {
                return false;
            }
            if (in_made && !from_made && g.values[static_cast<std::size_t>(v)].category != ValueCategory::Const &&
                (facts.through[static_cast<std::size_t>(v)].source != v ||
                 (lanes_along >= 0 && !graph::a_lane_could_read(g, g.values[static_cast<std::size_t>(v)].shape, seen, graph::read_as_itself(v), lanes_along)))) {
                return false;
            }
        }
    }
    return true;
}

// Would storing `v` take half the last-level cache, or more?
constexpr bool outgrows_the_caches(const Graph& g, std::int32_t v, const Machine& m) {
    const Value& held = g.values[static_cast<std::size_t>(v)];
    std::int64_t bytes = loops::bytes_of(held.type);
    for (std::int32_t axis = 0; axis < held.shape.rank; ++axis) {
        const Extent e = extent_at(held.shape, axis);
        if (!is_literal(e)) {
            return true;
        }
        bytes *= e.encoded;
    }
    return bytes >= m.l3_bytes / 2;
}

// Stage `made` becomes the first nodes of stage `reader`, computed through `window`; its own place in the schedule goes
// (no band is planned yet, so nothing refers to it by its index). The two hold at most `kMaxGroupStmts` nodes together.
constexpr void put_inside(Schedule& s, std::size_t made, std::size_t reader, std::int32_t window) {
    Schedule::Stage merged = s.stages[reader];
    merged.nodes = s.stages[made].nodes;
    for (const std::int32_t ni : s.stages[reader].nodes) {
        merged.nodes.push_back(ni);
    }
    merged.inlined = static_cast<std::int32_t>(s.stages[made].nodes.size());
    merged.through = window;
    s.stages[reader] = merged;
    for (std::size_t k = made; k + 1 < s.stages.size(); ++k) {
        s.stages[k] = s.stages[k + 1];
    }
    s.stages.pop_back();
}

} // namespace detail

// What decides is what the recomputing costs per output — (taps - 1) x nodes operations. Measured on zen2:
//   * 2 or 3 extra operations (a square under 3 or 4 taps): inside wins 1.7-2.3x at EVERY buffer size tried, 2 MB to
//     16 MB — the stored form pays a second pass whatever cache it lands in;
//   * 15 or 31 (a square under 16 or 32 taps): stored is 3-12 % faster while the buffer is 4 MB or less, inside
//     1.5-1.9x faster from 8 MB (half the last-level cache) up;
//   * 63 and more, or a forty-operation producer under two taps or more: stored.
// So: inside, when every node of the producer is a Cheap map and the recomputing costs at most
// `Machine::recompute_always` operations — or at most `recompute_if_uncached` and the buffer would take half the
// last-level cache or more (a symbolic extent counts as long).
constexpr void compute_cheap_producers_inside_their_windows(Schedule& s, const Graph& g, const graph::Facts& facts, const Machine& m) {
    const std::int64_t few = m.recompute_always;
    const std::int64_t recomputed = m.recompute_if_uncached;
    for (std::size_t window = 0; window < g.nodes.size(); ++window) {
        const heinzel::Node& view = g.nodes[window];
        if (!graph::folds_into_its_readers(view) || !graph::why_no_address(g, facts, view).empty()) {
            continue;
        }
        // What the window reads, through whatever views stand between: they are addresses, not producers.
        const std::int32_t source = facts.through[static_cast<std::size_t>(view.inputs[0])].source;
        const std::int32_t maker = facts.producer[static_cast<std::size_t>(source)];
        std::size_t made = s.stages.size();
        std::size_t reader = s.stages.size();
        for (std::size_t k = 0; k < s.stages.size() && maker >= 0; ++k) {
            made = detail::holds(s.stages[k], maker) ? k : made;
            for (const std::int32_t ni : s.stages[k].nodes) {
                for (std::size_t in = 0; in < rules_of(g.nodes[static_cast<std::size_t>(ni)].op).arity; ++in) {
                    reader = g.nodes[static_cast<std::size_t>(ni)].inputs[in] == view.output ? k : reader;
                }
            }
        }
        if (made == s.stages.size() || reader == s.stages.size() || made == reader || s.stages[reader].through >= 0) {
            continue;
        }
        const Schedule::Stage& producer = s.stages[made];
        const std::int64_t extra = (graph::taps_of(view) - 1) * static_cast<std::int64_t>(producer.nodes.size());
        const bool pays = extra <= few || (extra <= recomputed && detail::outgrows_the_caches(g, source, m));
        if (!pays || producer.nodes.size() + s.stages[reader].nodes.size() > graph::kMaxGroupStmts ||
            !detail::may_run_through(g, facts, producer, s.stages[reader], static_cast<std::int32_t>(window))) {
            continue;
        }
        detail::put_inside(s, made, reader, static_cast<std::int32_t>(window));
    }
}

} // namespace heinzel::loops::planner
