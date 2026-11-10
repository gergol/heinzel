#pragma once

// Store or compute where it is read (Plan/23 04, 4.3f). A map the gathering put into a stage of maps, read by one later
// stage alone, runs either there, stored and loaded back, or in its reader: the cost model decides. It names no
// operation.

#include <cstdint>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/graph/places.hpp"
#include "heinzel/loops/planner/bands.hpp"
#include "heinzel/loops/planner/chains.hpp"
#include "heinzel/loops/planner/gathering.hpp"
#include "heinzel/schedule/cost.hpp"

namespace heinzel::loops::planner {

namespace detail {

// Where each node runs (its stage's index; -1: no statement), the nodes that read each value (`reader[first[v] ..
// first[v + 1])`, through views with no statement), and whether the run, stored whole, outgrows the last-level cache.
struct Readers {
    std::vector<std::int32_t> stage_of{};
    std::vector<std::int32_t> first{};
    std::vector<std::int32_t> reader{};
    std::vector<std::int32_t> seen{}; // per value: the last pricing that counted it (a stamp)
    std::int32_t stamp{0};
    bool spills{false};
};

constexpr Readers readers_of(const Graph& g, const graph::Facts& facts, const Gathered& gathered, const Machine& m) {
    Readers r{std::vector<std::int32_t>(g.nodes.size(), -1), std::vector<std::int32_t>(g.values.size() + 1, 0), {}, std::vector<std::int32_t>(g.values.size(), -1)};
    Stages stages{};
    for (std::size_t k = 0; k < gathered.groups.size(); ++k) {
        stages.push_back(gathered.groups[k].stage);
        for (const std::int32_t ni : gathered.groups[k].stage.nodes) {
            r.stage_of[static_cast<std::size_t>(ni)] = static_cast<std::int32_t>(k);
        }
    }
    const auto each_read = [&](auto read) {
        for (std::size_t i = 0; i < g.nodes.size(); ++i) {
            for (std::size_t k = 0; k < rules_of(g.nodes[i].op).arity && facts.statement[i]; ++k) {
                read(static_cast<std::size_t>(detail::source_of(g, facts, i, k)), static_cast<std::int32_t>(i));
            }
        }
    };
    each_read([&](std::size_t v, std::int32_t) { ++r.first[v + 1]; });
    for (std::size_t v = 0; v < g.values.size(); ++v) {
        r.first[v + 1] += r.first[v];
    }
    r.reader.assign(static_cast<std::size_t>(r.first[g.values.size()]), 0);
    std::vector<std::int32_t> placed(g.values.size(), 0);
    each_read([&](std::size_t v, std::int32_t i) { r.reader[static_cast<std::size_t>(r.first[v] + placed[v]++)] = i; });
    Shape domain{}; // (the run's: the first stage's with a loop, past the work gathered ahead of every loop, `next_unit`)
    for (const OpenGroup& group : gathered.groups) {
        if (group.shape.domain.rank > 0) {
            domain = group.shape.domain;
            break;
        }
    }
    const bool literal = domain.rank > 0 && is_literal(extent_at(domain, domain.rank - 1));
    const graph::ValuePlaces at = graph::where_values_are_made(g, facts, stages);
    r.spills = literal && !stages.empty() && detail::working_set(g, facts, stages, at, 0, stages.size() - 1) > m.band_when_over;
    return r;
}

// Is value `v` stored whole, where a run that spills keeps it in memory: an input, a result, or one a stage past the one
// after its maker's reads (a band keeps only the next stage's reads, in rows)?
constexpr bool whole(const Graph& g, const graph::Facts& facts, const Readers& r, std::int32_t v) {
    const std::int32_t maker = facts.producer[static_cast<std::size_t>(v)];
    const std::int32_t made = maker >= 0 ? r.stage_of[static_cast<std::size_t>(maker)] : -1;
    bool beyond = made < 0 || is_a_result(g, v);
    for (std::int32_t q = r.first[static_cast<std::size_t>(v)]; q < r.first[static_cast<std::size_t>(v) + 1]; ++q) {
        beyond = beyond || r.stage_of[static_cast<std::size_t>(r.reader[static_cast<std::size_t>(q)])] > made + 1;
    }
    return beyond;
}

// Stage `k` of maps along time, its nodes `nodes`, as the model prices it: its slots; a load of each value it reads from
// outside, a constant apart; a store of each value it makes that something outside reads or the caller gets; its lanes,
// as many as the vector holds of the widest element it makes (`planner/loops.hpp`), a node that reads a wider one
// taking as many registers more (a conversion of doubles in a float's lanes); and where the run spills, the bytes of
// what it loads and stores whole.
constexpr cost::Stage priced_maps(const Graph& g, const graph::Facts& facts, const std::vector<std::int32_t>& nodes, std::int32_t k, Readers& r,
                                  const Machine& m) {
    cost::Stage s{};
    std::int64_t widest = 4;
    for (const std::int32_t ni : nodes) {
        const std::int64_t b = loops::bytes_of(g.values[static_cast<std::size_t>(g.nodes[static_cast<std::size_t>(ni)].output)].type);
        widest = b > widest ? b : widest;
    }
    ++r.stamp;
    const auto moved = [&](std::int32_t v) {
        const std::int64_t bytes = loops::bytes_of(g.values[static_cast<std::size_t>(v)].type);
        s.bytes += r.spills && detail::whole(g, facts, r, v) ? static_cast<double>(bytes) : 0;
    };
    for (const std::int32_t ni : nodes) {
        const std::size_t i = static_cast<std::size_t>(ni);
        const std::int64_t reads = detail::widest_of(g, i);
        s.slots += detail::cost_of(g, g.nodes[i], m).slots * static_cast<double>(reads > widest ? reads / widest : 1);
        for (std::size_t q = 0; q < rules_of(g.nodes[i].op).arity; ++q) {
            const std::int32_t v = detail::source_of(g, facts, i, q);
            const std::int32_t maker = facts.producer[static_cast<std::size_t>(v)];
            const bool outside = maker < 0 || r.stage_of[static_cast<std::size_t>(maker)] != k;
            if (outside && g.values[static_cast<std::size_t>(v)].category != ValueCategory::Const && r.seen[static_cast<std::size_t>(v)] != r.stamp) {
                r.seen[static_cast<std::size_t>(v)] = r.stamp;
                s.loads += 1;
                moved(v);
            }
        }
        const std::int32_t out = g.nodes[i].output;
        bool read_outside = is_a_result(g, out);
        for (std::int32_t q = r.first[static_cast<std::size_t>(out)]; q < r.first[static_cast<std::size_t>(out) + 1]; ++q) {
            read_outside = read_outside || r.stage_of[static_cast<std::size_t>(r.reader[static_cast<std::size_t>(q)])] != k;
        }
        s.stores += read_outside ? 1 : 0;
        if (read_outside) {
            moved(out);
        }
    }
    s.lanes = m.vector_bytes / widest > 1 ? static_cast<double>(m.vector_bytes / widest) : 1.0;
    return s;
}

// Is `group` a stage of maps along a signal: no cycle, no fold, over one axis, and no value made in parts (a stage of
// its own)?
constexpr bool maps_along_time(const Graph& g, const OpenGroup& group) {
    return group.cycle < 0 && group.shape.fold < 0 && group.shape.domain.rank == 1 && !group.stage.nodes.empty() &&
           !graph::is_made_in_parts(g.nodes[static_cast<std::size_t>(group.stage.nodes[0])]);
}

// The nodes of group `p` that one later stage of maps along time alone reads, or those of them that run there too, and
// that may run there (a plain map, no result, over its domain), each marked with that stage (`into`; -1: none). In
// reverse, so that a node read only by ones moving along with it moves too.
constexpr void mark_movers(const Graph& g, const graph::Facts& facts, const Gathered& gathered, std::size_t p, const Readers& r,
                           std::vector<std::int32_t>& into) {
    const auto& nodes = gathered.groups[p].stage.nodes;
    for (std::size_t q = nodes.size(); q-- > 0;) {
        const std::size_t i = static_cast<std::size_t>(nodes[q]);
        const std::int32_t out = g.nodes[i].output;
        std::int32_t target = -1;
        bool one = graph::is_a_plain_map(g.nodes[i]) && !is_a_result(g, out) && r.first[static_cast<std::size_t>(out)] < r.first[static_cast<std::size_t>(out) + 1];
        for (std::int32_t k = r.first[static_cast<std::size_t>(out)]; one && k < r.first[static_cast<std::size_t>(out) + 1]; ++k) {
            const std::size_t j = static_cast<std::size_t>(r.reader[static_cast<std::size_t>(k)]);
            const std::int32_t s = r.stage_of[j] == static_cast<std::int32_t>(p) ? into[j] : r.stage_of[j];
            const bool there = s > static_cast<std::int32_t>(p) && (r.stage_of[j] == static_cast<std::int32_t>(p) || graph::is_a_plain_map(g.nodes[j]));
            one = there && (target < 0 || s == target);
            target = s;
        }
        const bool fits = one && maps_along_time(g, gathered.groups[static_cast<std::size_t>(target)]) &&
                          g.values[static_cast<std::size_t>(out)].shape == gathered.groups[static_cast<std::size_t>(target)].shape.domain;
        into[i] = fits ? target : -1;
    }
}

// Moves the nodes of group `p` marked for group `t` there (first, in their order), where the model prices the two
// stages so no dearer than as gathered, and the reader still fits a stage. Returns whether they moved.
constexpr bool move_if_cheaper(const Graph& g, const graph::Facts& facts, Gathered& gathered, std::size_t p, std::size_t t, const std::vector<std::int32_t>& into,
                               Readers& r, const Machine& m) {
    std::vector<std::int32_t> stays{};
    std::vector<std::int32_t> reads{};
    for (const std::int32_t ni : gathered.groups[p].stage.nodes) {
        (into[static_cast<std::size_t>(ni)] == static_cast<std::int32_t>(t) ? reads : stays).push_back(ni);
    }
    const std::size_t moving = reads.size();
    for (const std::int32_t ni : gathered.groups[t].stage.nodes) {
        reads.push_back(ni);
    }
    const auto& before_p = gathered.groups[p].stage.nodes;
    const auto& before_t = gathered.groups[t].stage.nodes;
    const std::vector<std::int32_t> was_p(before_p.begin(), before_p.end());
    const std::vector<std::int32_t> was_t(before_t.begin(), before_t.end());
    const double before = cost::cycles(detail::priced_maps(g, facts, was_p, static_cast<std::int32_t>(p), r, m), m) +
                          cost::cycles(detail::priced_maps(g, facts, was_t, static_cast<std::int32_t>(t), r, m), m);
    for (std::size_t q = 0; q < moving; ++q) {
        r.stage_of[static_cast<std::size_t>(reads[q])] = static_cast<std::int32_t>(t);
    }
    const cost::Stage reader = detail::priced_maps(g, facts, reads, static_cast<std::int32_t>(t), r, m);
    const double after = cost::cycles(detail::priced_maps(g, facts, stays, static_cast<std::int32_t>(p), r, m), m) + cost::cycles(reader, m);
    const bool fits = reads.size() <= graph::kMaxGroupStmts && static_cast<double>(reads.size()) + reader.loads <= static_cast<double>(kMaxStageStmts);
    if (after >= before || !fits) {
        for (std::size_t q = 0; q < moving; ++q) {
            r.stage_of[static_cast<std::size_t>(reads[q])] = static_cast<std::int32_t>(p);
        }
        return false;
    }
    gathered.groups[p].stage.nodes.clear();
    for (const std::int32_t ni : stays) {
        gathered.groups[p].stage.nodes.push_back(ni);
    }
    gathered.groups[t].stage.nodes.clear();
    for (const std::int32_t ni : reads) {
        gathered.groups[t].stage.nodes.push_back(ni);
    }
    return true;
}

} // namespace detail

// Each map that a later stage of maps along time alone reads, gathered into an earlier stage of maps along time, runs in
// that reader where the cost model prices the two stages so cheaper: stored, its stage stores it and the reader loads it
// back (past the last-level cache in memory, where the run outgrows the cache and no band keeps it); in its reader, the
// reader pays its slots at the reader's width and loads what it reads, and its own stage stores what the reader now
// reads of it. Measured on eq8mix (`Plan/probes/audio-iir/cost_corpus16.cpp`): its bank's direct term, a double, in the
// sum's stage instead of beside the gain, 0.84x over 1M samples, 0.98x in blocks. A stage left empty goes.
constexpr void compute_maps_in_their_reader(Gathered& gathered, const Graph& g, const graph::Facts& facts, const Machine& m) {
    detail::Readers r = detail::readers_of(g, facts, gathered, m);
    std::vector<std::int32_t> into(g.nodes.size(), -1);
    bool emptied = false;
    for (std::size_t p = 0; p + 1 < gathered.groups.size(); ++p) {
        if (!detail::maps_along_time(g, gathered.groups[p])) {
            continue;
        }
        detail::mark_movers(g, facts, gathered, p, r, into);
        for (std::size_t t = p + 1; t < gathered.groups.size(); ++t) {
            bool marked = false;
            for (const std::int32_t ni : gathered.groups[p].stage.nodes) {
                marked = marked || into[static_cast<std::size_t>(ni)] == static_cast<std::int32_t>(t);
            }
            if (marked) {
                detail::move_if_cheaper(g, facts, gathered, p, t, into, r, m);
            }
        }
        emptied = emptied || gathered.groups[p].stage.nodes.empty();
    }
    if (!emptied) {
        return;
    }
    std::vector<OpenGroup> kept{};
    for (const OpenGroup& group : gathered.groups) {
        if (!group.stage.nodes.empty()) {
            kept.push_back(group);
        }
    }
    gathered.groups = kept;
}

} // namespace heinzel::loops::planner
