#pragma once

// What follows a feedback stage's last cycle (Plan/23 04, 4.3a): the work after its cycles that may run in its sample,
// and how much of it does, decided by the cost model. The planner's first pass (`stages.hpp`) asks it while it gathers,
// so what does not join is gathered later as it would have been. It names no operation.

#include <bit>
#include <cstdint>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/graph/views.hpp"
#include "heinzel/loops/planner/chains.hpp"
#include "heinzel/loops/planner/gathering.hpp"
#include "heinzel/schedule/cost.hpp"

namespace heinzel::loops::planner::detail {

// The open feedback stage as it would close: with the stage of maps just before it, every one of which leads into a
// cycle it holds (its feed-forward part), where the cost model prices them merged no dearer than the maps in lanes and
// the stage apart: a bank's voices make it cheap, a cascade's later sections dear (`Plan/probes/audio-iir/
// cost_corpus6.cpp`: right at 8 of 8; sixteen voices of a biquad 11.1 -> 6.0 cycles a sample). P2 kept such maps in
// lanes always (Decision 65). The feed is weighed against the stage without the work after its cycles, and joins a
// stage folding its voices only where its sample stays one the planner lays out (`sample_fits`). `folds`: whether the
// feed joins.
constexpr cost::Stage as_it_closes(const Graph& g, const Gathered& gathered, const Gathering& at, bool& folds) {
    const cost::Stage open = detail::priced_open(g, at);
    cost::Stage bare = open;
    bare.slots -= at.trail_slots;
    folds = false;
    const OpenGroup* feed = gathered.groups.empty() ? nullptr : &gathered.groups.back();
    if (at.open.cycle < 0 || detail::folds_each_sample(at) || detail::runs_skewed(at) || feed == nullptr || feed->cycle >= 0 || feed->shape.fold >= 0 ||
        !(feed->shape.domain == at.open.shape.domain) || feed->stage.nodes.size() + at.open.stage.nodes.size() > graph::kMaxGroupStmts) {
        return open;
    }
    std::int64_t made = 4;
    for (const std::int32_t m : feed->stage.nodes) {
        const std::int32_t c = at.target[static_cast<std::size_t>(m)];
        if (c < 0 || !at.held[static_cast<std::size_t>(c)]) {
            return open;
        }
        const std::int64_t b = loops::bytes_of(g.values[static_cast<std::size_t>(g.nodes[static_cast<std::size_t>(m)].output)].type);
        made = b > made ? b : made;
    }
    if (!detail::sample_fits(g, at, made)) {
        return open;
    }
    cost::Stage merged = bare;
    merged.slots += at.last_cost.slots, merged.depth += at.last_cost.depth;
    merged.loads = open.loads > at.last_cost.loads ? open.loads : at.last_cost.loads;
    folds = cost::cycles(merged, at.machine) <= cost::cycles(at.last_cost, at.machine) + cost::cycles(bare, at.machine);
    merged.slots += at.trail_slots;
    return folds ? merged : open;
}

// Does the open stage take work after it by the model: a feedback stage (but one folding its voices every sample or one
// running skewed, stand-ins), or a fold of a signal's voices (`folds_voices`)? And the shape that work runs over: the
// feedback stage's domain, or the fold's result.
constexpr bool has_a_trail(const Graph& g, const Gathering& at) {
    return !at.open.stage.nodes.empty() && ((at.open.cycle >= 0 && !detail::folds_each_sample(at) && !detail::runs_skewed(at)) || detail::folds_voices(g, at));
}
constexpr const Shape& trail_shape(const Graph& g, const Gathering& at) {
    return detail::folds_voices(g, at) ? at.open.shape.folded : at.open.shape.domain;
}

// The cycles holding a link (`Gathering::link_of`), and what a skewed stage takes after its cycle
// (`Gathering::to_the_slice`, `runs_skewed`): the work in no cycle, over the cycle's shape, that maps or hands on at this
// sample what the cycle or such work makes and that the slice reads (the double state's conversion down); the slice, a
// view of the last section alone; and the views over it that keep its elements in order (its reshape to one section).
// One pass forward from the links, one back from the slices: every reader outside a cycle stands after what it reads.
constexpr void find_the_slices(const Graph& g, const graph::Feedback& feedback, Gathering& at) {
    const std::size_t count = g.nodes.size();
    for (std::size_t i = 0; i < count; ++i) {
        if (feedback.cycle[i] >= 0 && graph::is_a_link(g.nodes[i])) {
            at.link_of[static_cast<std::size_t>(feedback.cycle[i])] = static_cast<std::int32_t>(i);
        }
    }
    std::vector<std::int32_t> link(count, -1); // the link whose cycle made it, or work after that over its shape
    std::vector<char> slice(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        const heinzel::Node& n = g.nodes[i];
        if (const std::int32_t c = feedback.cycle[i]; c >= 0) {
            link[i] = at.link_of[static_cast<std::size_t>(c)];
            continue;
        }
        std::int32_t from = -1; // (the link of what it reads at this sample)
        for (std::size_t k = 0; k < kMaxNodeInputs; ++k) {
            const std::int32_t m = at.maker[i][k];
            from = m >= 0 && link[static_cast<std::size_t>(m)] >= 0 ? link[static_cast<std::size_t>(m)] : from;
        }
        const Shape& sections = g.values[static_cast<std::size_t>(g.nodes[static_cast<std::size_t>(from >= 0 ? from : 0)].output)].shape;
        const Extent extent = extent_at(sections, 0);
        const ViewAttr& view = n.attr.view;
        const bool last = from >= 0 && graph::is_an_affine_view(n) && view.kind == ViewKind::Slice && view.axis == 0 && view.length == 1 &&
                          is_literal(extent) && view.offset == literal_of(extent) - 1;
        const std::int32_t source = at.maker[i][0];
        const bool reshaped = source >= 0 && slice[static_cast<std::size_t>(source)] != 0 && graph::keeps_elements_in_order(n);
        slice[i] = last || reshaped;
        const bool per_sample = (graph::is_a_plain_map(n) || graph::hands_its_operand_on(n)) && !at.lags[i];
        link[i] = from >= 0 && per_sample && g.values[static_cast<std::size_t>(n.output)].shape == sections ? from : -1;
    }
    std::vector<char> leads(count, 0); // read by the slice, or by work leading to it
    for (std::size_t r = count; r-- > 0;) {
        at.to_the_slice[r] = slice[r] != 0 || (leads[r] != 0 && link[r] >= 0 && feedback.cycle[r] < 0);
        for (std::size_t k = 0; k < kMaxNodeInputs && at.to_the_slice[r] != 0; ++k) {
            if (const std::int32_t m = at.maker[r][k]; m >= 0) {
                leads[static_cast<std::size_t>(m)] = 1;
            }
        }
    }
}

// The work after the open feedback stage's cycles, in planning order: every node not gathered yet that reads what the
// stage makes or such work, and may run in its sample — no cycle's, leading into none through cheap work (that is a later
// cycle's feed, which `chains_on` and `fold_in_the_feed` decide), no fold, over the stage's domain, reading no storage the
// stage makes at another time but through a lag (as `joins` allows) — once all it reads is made; with it, what it reads
// that may run there too and reads nothing of the stage (an allpass's `-x`). Work only the rest of the graph reads (a
// dry path to the output) stays where the gathering puts it.
constexpr std::vector<std::int32_t> trail_of(const Graph& g, const graph::Facts& facts, const graph::Feedback& feedback, const Gathering& at) {
    std::vector<char> state(g.nodes.size(), 0); // 1: may run in the stage, reading none of it; 2: listed
    std::vector<std::int32_t> trail{};
    const auto list = [&](auto& self, std::size_t m) -> void {
        if (state[m] == 1) {
            state[m] = 2;
            for (std::size_t k = 0; k < kMaxNodeInputs; ++k) {
                if (at.maker[m][k] >= 0) {
                    self(self, static_cast<std::size_t>(at.maker[m][k]));
                }
            }
            trail.push_back(static_cast<std::int32_t>(m));
        }
    };
    for (const std::int32_t ni : feedback.order) {
        const std::size_t i = static_cast<std::size_t>(ni);
        const heinzel::Node& n = g.nodes[i];
        const std::int32_t s = at.storage_of[i];
        const bool stored_here = s >= 0 && at.made_in[static_cast<std::size_t>(g.nodes[static_cast<std::size_t>(s)].output)] == kOpen && !at.lags[i];
        const bool over = g.values[static_cast<std::size_t>(n.output)].shape == detail::trail_shape(g, at);
        if (!at.runs[i] || at.visited[i] || feedback.cycle[i] >= 0 || at.target[i] >= 0 || graph::is_a_fold(n) || graph::is_an_ordering(n) ||
            graph::is_a_contraction(n) || graph::is_made_in_parts(n) || stored_here || !over) {
            continue;
        }
        bool ready = true;
        bool reads = false;
        for (std::size_t k = 0; k < kMaxNodeInputs; ++k) {
            const std::int32_t m = at.maker[i][k];
            ready = ready && (m < 0 || at.visited[static_cast<std::size_t>(m)] || state[static_cast<std::size_t>(m)] != 0);
        }
        for (std::size_t k = 0; k < rules_of(n.op).arity; ++k) {
            const std::int32_t v = detail::source_of(g, facts, i, k);
            const std::int32_t m = facts.producer[static_cast<std::size_t>(v)];
            reads = reads || at.made_in[static_cast<std::size_t>(v)] == kOpen || (m >= 0 && state[static_cast<std::size_t>(m)] == 2);
        }
        state[i] = ready ? 1 : 0;
        if (ready && reads) {
            list(list, i);
        }
    }
    return trail;
}

// The list cut after its first k nodes, for every k at once (index k): the slots and the depth (from what the list reads
// from outside it) of those first k, the values they load that the open stage does not make, what the rest reads from
// outside it (each value once: what the open stage and earlier stages make, and what the first k make for it), and the
// widest element the rest reads or makes. Each value counted once, from its first and last reader, into running sums.
struct Cuts {
    std::vector<double> slots{};
    std::vector<double> depth{};
    std::vector<std::int32_t> fresh{};
    std::vector<std::int32_t> crossing{};
    std::vector<std::int64_t> widest{};
};

constexpr Cuts cuts_of(const Graph& g, const graph::Facts& facts, const std::vector<std::int32_t>& trail, const Gathering& at) {
    const std::size_t count = trail.size();
    Cuts c{std::vector<double>(count + 1, 0), std::vector<double>(count + 1, 0), std::vector<std::int32_t>(count + 2, 0),
           std::vector<std::int32_t>(count + 2, 0), std::vector<std::int64_t>(count + 1, 4)};
    std::vector<std::int32_t> position(g.nodes.size(), -1);
    std::vector<std::int32_t> first(g.values.size(), -1);
    std::vector<std::int32_t> last(g.values.size(), -1);
    std::vector<double> path(count, 0);
    for (std::size_t q = 0; q < count; ++q) {
        const std::size_t i = static_cast<std::size_t>(trail[q]);
        const OpCost cost = detail::cost_of(g, g.nodes[i], at.machine);
        position[i] = static_cast<std::int32_t>(q);
        for (std::size_t k = 0; k < rules_of(g.nodes[i].op).arity; ++k) {
            const std::size_t v = static_cast<std::size_t>(detail::source_of(g, facts, i, k));
            const std::int32_t maker = facts.producer[v];
            const std::int32_t made = maker >= 0 ? position[static_cast<std::size_t>(maker)] : -1;
            path[q] = made >= 0 && path[static_cast<std::size_t>(made)] > path[q] ? path[static_cast<std::size_t>(made)] : path[q];
            first[v] = first[v] < 0 ? static_cast<std::int32_t>(q) : first[v];
            last[v] = static_cast<std::int32_t>(q);
        }
        path[q] += cost.latency;
        c.slots[q + 1] = c.slots[q] + cost.slots;
        c.depth[q + 1] = path[q] > c.depth[q] ? path[q] : c.depth[q];
    }
    for (std::size_t q = count; q-- > 0;) {
        const std::int64_t b = detail::widest_of(g, static_cast<std::size_t>(trail[q]));
        c.widest[q] = b > c.widest[q + 1] ? b : c.widest[q + 1];
    }
    for (const std::int32_t ni : trail) {
        for (std::size_t k = 0; k < rules_of(g.nodes[static_cast<std::size_t>(ni)].op).arity; ++k) {
            const std::size_t v = static_cast<std::size_t>(detail::source_of(g, facts, static_cast<std::size_t>(ni), k));
            if (last[v] < 0 || g.values[v].category == ValueCategory::Const) {
                continue; // (counted already, or an immediate)
            }
            const std::int32_t maker = facts.producer[v];
            const std::int32_t made = maker >= 0 ? position[static_cast<std::size_t>(maker)] : -1;
            ++c.crossing[static_cast<std::size_t>(made + 1)]; // (the rest from k on reads it from outside for made < k <= last)
            --c.crossing[static_cast<std::size_t>(last[v] + 1)];
            c.fresh[static_cast<std::size_t>(first[v] + 1)] += made < 0 && at.made_in[v] != kOpen && !at.loaded[v] ? 1 : 0;
            last[v] = -1;
        }
    }
    for (std::size_t k = 1; k <= count; ++k) {
        c.crossing[k] += c.crossing[k - 1];
        c.fresh[k] += c.fresh[k - 1];
    }
    return c;
}

// What a stage of its own over `shape` costs a sample to start its loop: once a chunk of a stream band, or once a run
// that fits the first-level cache (`Machine::stage_start`).
constexpr double stage_started(const Shape& shape, const Gathering& at) {
    const Extent time = extent_at(shape, shape.rank - 1);
    const bool whole = is_literal(time) && literal_of(time) * 4 <= at.machine.l1_bytes;
    const double rows = whole ? static_cast<double>(literal_of(time)) : at.machine.band_stream_rows > 0 ? at.machine.band_stream_rows : 1;
    return at.machine.stage_start / rows;
}

// Does fold `i` of the open voice bank's voices run in the bank's sample (Plan/23 04 4.3d)? Where the sample can hold it
// (the voices a literal count within `graph::kMostSampleBodies` lane bodies at the lanes the stage has with the fold,
// the bank's state handed on, as `planner/feedback.hpp` lays a sample out; what joins later keeps it so,
// `sample_fits`) and the cost model prices the bank with it, once a sample its bodies added and log2(lanes) steps of a
// shuffle and an add on the sample's path, no dearer than the fold in a stage of its own, reading the bank's values
// back and starting its loop. Not for a bank narrower than a lane, where the model is wrong (`cost_corpus9.cpp` d,
// `cost_corpus15.cpp`): a stand-in.
constexpr bool fold_joins(const Graph& g, const graph::Facts& facts, std::size_t i, const Gathering& at) {
    const heinzel::Node& n = g.nodes[i];
    const Shape& domain = at.open.shape.domain;
    if (at.open.cycle < 0 || detail::folds_each_sample(at) || at.open.shape.fold >= 0 || domain.rank != 2 || !graph::is_a_fold(n) ||
        !(g.values[static_cast<std::size_t>(n.inputs[0])].shape == domain) || at.made_in[static_cast<std::size_t>(detail::source_of(g, facts, i, 0))] != kOpen ||
        graph::collapsed_axes(n, domain.rank) != 1u || g.values[static_cast<std::size_t>(n.output)].shape.rank >= domain.rank) {
        return false; // (a fold of the bank's voices, axis 0, dropping them and keeping time)
    }
    const Extent voices = extent_at(domain, 0);
    const std::int64_t lanes = detail::voice_lanes(g, at, loops::bytes_of(g.values[static_cast<std::size_t>(n.output)].type));
    if (lanes < 2 || !graph::fits_a_block(voices, lanes) || !graph::hands_state_on(g, facts, at.open.stage)) {
        return false;
    }
    if (literal_of(voices) < lanes) {
        return false; // (a stand-in: for banks narrower than a lane the model prices the fold joined, which runs 1.25-1.3x)
    }
    const cost::Stage bank = detail::priced_open(g, at);
    const Machine& m = at.machine;
    const double steps = static_cast<double>(std::bit_width(static_cast<std::uint64_t>(lanes)) - 1);
    const double add = m.add.latency;
    cost::Stage joined = bank;
    joined.slots += (bank.bodies - 1 + 2 * steps) / bank.bodies;
    joined.depth += cost::lanes_folded(add, lanes, static_cast<std::int64_t>(bank.bodies), m);
    const cost::Stage alone{.depth = add + steps * (m.shuffle.latency + add), .slots = 1 + 2 * steps / bank.bodies, .loads = 1, .stores = 1,
                            .bodies = bank.bodies};
    return cost::cycles(joined, m) <= cost::cycles(bank, m) + cost::cycles(alone, m) + detail::stage_started(domain, at);
}

// Does map `i` keep its lanes in the open stage of maps along time, or cost no more there than in a stage of its own?
// A stage's lanes are as many as the vector holds of its widest element: a float map joining a stage of doubles runs
// in half the lanes it would have alone (eq8sweep's coefficient maps beside the bank's direct term in double: 13.7
// against 12.3 ns a sample), a double map halves a float stage's. Priced: the stage with it, against the stage and it
// in its own lanes, handed on and starting its loop.
constexpr bool keeps_lanes(const Graph& g, std::size_t i, const Gathering& at) {
    if (at.open.cycle >= 0 || at.open.shape.fold >= 0 || at.open.shape.domain.rank != 1 || at.open.stage.nodes.empty()) {
        return true;
    }
    std::int64_t widest = 4;
    for (const std::int32_t ni : at.open.stage.nodes) {
        const std::int64_t b = detail::widest_of(g, static_cast<std::size_t>(ni));
        widest = b > widest ? b : widest;
    }
    const std::int64_t own = detail::widest_of(g, i) > 4 ? detail::widest_of(g, i) : 4;
    if (own == widest) {
        return true;
    }
    const cost::Stage open = detail::priced_open(g, at);
    const OpCost c = detail::cost_of(g, g.nodes[i], at.machine);
    cost::Stage joined = open;
    joined.slots += c.slots;
    joined.lanes = detail::lanes_along_time(kNoLag, own > widest ? own : widest, at);
    const cost::Stage apart{.slots = static_cast<double>(c.slots), .loads = 1, .stores = 1, .lanes = detail::lanes_along_time(kNoLag, own, at)};
    return cost::cycles(joined, at.machine) <= cost::cycles(open, at.machine) + cost::cycles(apart, at.machine) +
                                                    detail::stage_started(at.open.shape.domain, at);
}

// How many of the work after the open feedback stage's cycles (`trail`) join it: the cut the cost model prices
// cheapest, the stage as it would close (`before`) with the first k, against it and the rest in lanes, handed what
// crosses (k = 0: none; on a tie, the most). Within what a stage holds, and a sample the planner lays out
// (`sample_fits`: a double after a bank's sum halves the bank's lanes).
constexpr std::size_t cheapest_cut(const Graph& g, const graph::Facts& facts, const std::vector<std::int32_t>& trail, const cost::Stage& before,
                                   const Gathering& at) {
    if (trail.empty()) {
        return 0;
    }
    const Cuts cuts = detail::cuts_of(g, facts, trail, at);
    const bool voices = detail::trail_shape(g, at).rank > 1; // (the rest's lanes along its voices, else along time)
    const double bodies = voices ? before.bodies : 1;
    const double per = detail::folds_voices(g, at) ? before.bodies : 1; // (after a fold: once a sample)
    const auto rest_from = [&](std::size_t k) {
        const double vector = static_cast<double>(at.machine.vector_bytes / cuts.widest[k]);
        return cost::Stage{.depth = cuts.depth[trail.size()], .slots = cuts.slots[trail.size()] - cuts.slots[k], .loads = static_cast<double>(cuts.crossing[k]),
                           .stores = 1, .bodies = bodies, .lanes = voices || vector < 1 ? 1.0 : vector};
    };
    // After a fold the rest is a stage of its own, over the fold's result (`stage_started`). After a feedback stage the
    // rest is gathered where it is read, with what else runs there, so no start is known to be its own.
    const double start = detail::folds_voices(g, at) ? detail::stage_started(detail::trail_shape(g, at), at) : 0;
    double least = cost::cycles(before, at.machine) + cost::cycles(rest_from(0), at.machine) + start;
    std::size_t best = 0;
    std::int64_t made = 4; // (the widest element the first k make)
    for (std::size_t k = 1; k <= trail.size() && at.open.stage.nodes.size() + k < graph::kMaxGroupStmts; ++k) {
        const std::int64_t b = loops::bytes_of(g.values[static_cast<std::size_t>(g.nodes[static_cast<std::size_t>(trail[k - 1])].output)].type);
        made = b > made ? b : made;
        if (!detail::sample_fits(g, at, made)) {
            break;
        }
        cost::Stage joined = before;
        joined.slots += cuts.slots[k] / per, joined.depth += cuts.depth[k], joined.loads += cuts.fresh[k] / per;
        if (static_cast<double>(at.open.stage.nodes.size() + k) + joined.loads > static_cast<double>(kMaxStageStmts)) {
            break;
        }
        const double total = cost::cycles(joined, at.machine) + (k < trail.size() ? cost::cycles(rest_from(k), at.machine) + start : 0);
        if (total <= least) {
            least = total, best = k;
        }
    }
    return best;
}

// Does node `i` join the open feedback stage as work after its cycles? Decided once for the stage as it stands (its first
// node, the cycles it holds), for all such work together (`cheapest_cut`), and remembered: a memo, not state.
constexpr bool trails(const Graph& g, const graph::Facts& facts, const graph::Feedback& feedback, const Gathered& gathered, std::size_t i,
                      const Gathering& at) {
    if (!detail::has_a_trail(g, at) || feedback.cycle[i] >= 0) {
        return false;
    }
    const std::int32_t cycles = static_cast<std::int32_t>(at.held_now.size());
    if (at.trail_first != at.open.stage.nodes[0] || at.trail_cycles != cycles || at.trail_fold != at.open.shape.fold) {
        for (const std::int32_t ni : at.trail_marked) {
            at.trail[static_cast<std::size_t>(ni)] = 0;
        }
        at.trail_marked.clear();
        bool folds = false;
        const std::vector<std::int32_t> trail = detail::trail_of(g, facts, feedback, at);
        const std::size_t k = detail::cheapest_cut(g, facts, trail, detail::as_it_closes(g, gathered, at, folds), at);
        for (std::size_t q = 0; q < k; ++q) {
            at.trail[static_cast<std::size_t>(trail[q])] = 1;
            at.trail_marked.push_back(trail[q]);
        }
        at.trail_first = at.open.stage.nodes[0], at.trail_cycles = cycles, at.trail_fold = at.open.shape.fold;
    }
    return at.trail[i] != 0;
}

} // namespace heinzel::loops::planner::detail
