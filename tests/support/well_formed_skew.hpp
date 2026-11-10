#pragma once

// `well_formed`'s rules for a skewed stage (`docs/sections-in-lanes.md`): sections linked in lanes, lane j on the
// sample j behind lane 0's, its time loop running k − 1 steps past the block. That loop is serial and innermost, under
// every State; the lanes are the k sections, one whole chunk of a literal extent; no head samples, band or split cut
// the skew. The link reads its keeper's State one lane up (`Node::shifted`); what moves along time is read or stored by
// a Compute at one lane (`Node::lane`), at that lane's sample, and what it makes is read at that lane alone (or by the
// link, as its fill from lane 0); such a Compute keeps no State, reads none and stores its one number, and a leaf at no
// lane stores only a State it keeps; no fold runs along the lanes; and every State closes, so that its registers carry
// the sections across runs.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

#include "heinzel/loops/program.hpp"
#include "heinzel/loops/tree.hpp"

#include "well_formed_closes.hpp"
#include "well_formed_walks.hpp"

namespace heinzel::loops::detail {

// Does access `a` move along its buffer's time, its last axis and longer than one sample: walked or indexed there?
constexpr bool along_time(const Program& p, const Access& a) {
    if (a.source != Source::Buffer) {
        return false;
    }
    const Shape& shape = p.buffers[static_cast<std::size_t>(a.buffer)].shape;
    const std::int32_t time = shape.rank - 1;
    bool walked = time >= 0 && a.indexed_axis == time;
    for (const std::int32_t along : a.buffer_axis) {
        walked = walked || (time >= 0 && along == time);
    }
    return walked && !(is_literal(extent_at(shape, time)) && literal_of(extent_at(shape, time)) == 1);
}

// Skewed stage s's own rules; its sections, k, the lane axis' extent.
constexpr std::expected<std::int64_t, Refusal> skewed_stage(const Program& p, std::size_t s, std::int32_t lanes, const std::vector<std::int32_t>& stage_of) {
    const Stage& stage = p.stages[s];
    if (stage.head != 0 || band_of(p, s) >= 0 || stage.split >= 0) {
        return std::unexpected("heinzel::loops: a skewed stage runs no head samples, stands in no band and is never split");
    }
    const std::int32_t time = stage.time_loop;
    const bool literal = lanes >= 0 && is_literal(stage.axes[static_cast<std::size_t>(lanes)]);
    const std::int64_t k = literal ? literal_of(stage.axes[static_cast<std::size_t>(lanes)]) : 0;
    bool serial = time >= 0 && node_at(p, time).tail == Tail::Clip && node_at(p, time).step == 1;
    bool whole = literal;
    bool closes = true;
    for (auto n = static_cast<std::size_t>(stage.root) + 1; n < p.nodes.size() && stage_of[n] == static_cast<std::int32_t>(s); ++n) {
        const Node& here = p.nodes[n]; // (a stage's nodes are numbered in one run)
        const auto at = static_cast<std::int32_t>(n);
        const bool state = here.kind == NodeKind::Registers && here.role == Role::State;
        serial = serial && !(here.kind == NodeKind::Loop && at != time && encloses(p, time, at)) && !(state && !encloses(p, at, time));
        whole = whole && !(here.kind == NodeKind::Loop && here.axis == lanes && here.step != k && !(here.tail == Tail::Masked && here.step > k));
        const std::int32_t keeper = state ? keeper_of(p, at, here.stmt) : -1;
        closes = closes && (!state || (keeper >= 0 && node_at(p, keeper).closes));
    }
    if (!serial) {
        return std::unexpected("heinzel::loops: a skewed stage's time loop is serial, of step 1, innermost, under every State");
    }
    if (!whole) {
        return std::unexpected("heinzel::loops: a skewed stage's lanes are a Vector over a literal k, in one chunk of its strip");
    }
    if (!closes) {
        return std::unexpected("heinzel::loops: every State of a skewed stage closes");
    }
    return k;
}

// Does shifted Compute `here` read its State at slot 0 before the keeper, its operand 1 the keeper's store one element
// back along the lanes (the register of the lane below)?
constexpr bool reads_the_lane_below(const Program& p, const Node& here, const Stage& stage, std::int32_t lanes) {
    if (here.kind != NodeKind::Compute || here.reads < 0 || here.stmt >= here.reads || here.slot != 0) {
        return false;
    }
    const Access& kept = p.stmts[static_cast<std::size_t>(stage.first + here.reads)].store;
    const std::int32_t along = kept.source == Source::Buffer ? kept.buffer_axis[static_cast<std::size_t>(lanes)] : -1;
    if (along < 0) {
        return false;
    }
    Access back = kept;
    back.offset[static_cast<std::size_t>(along)] -= 1;
    return p.stmts[static_cast<std::size_t>(stage.first + here.stmt)].in[1] == back;
}

// The lane of the Compute making statement `k` of skewed stage `s` (-1: it stands at none).
constexpr std::int32_t lane_of_maker(const Program& p, std::size_t s, std::int32_t k, const std::vector<std::int32_t>& stage_of) {
    for (auto m = static_cast<std::size_t>(p.stages[s].root) + 1; m < p.nodes.size() && stage_of[m] == static_cast<std::int32_t>(s); ++m) {
        if (p.nodes[m].kind == NodeKind::Compute && p.nodes[m].stmt == k) {
            return p.nodes[m].lane;
        }
    }
    return -1;
}

// Does leaf `here` read a value made at one lane only where it stands at that lane, or, the link, as its fill from lane
// 0? A value made at lane L holds lane L's sample in every lane: a reader at another lane would take it for its own.
constexpr bool reads_lanes_at_their_lane(const Program& p, const Node& here, std::size_t s, const std::vector<std::int32_t>& stage_of) {
    const Stmt& st = p.stmts[static_cast<std::size_t>(p.stages[s].first + here.stmt)];
    for (std::size_t slot = 0; slot < kMaxNodeInputs; ++slot) {
        const std::int32_t lane = st.in[slot].source == Source::Local ? lane_of_maker(p, s, st.in[slot].local, stage_of) : -1;
        const bool fill = here.shifted && slot == 0 && lane == 0;
        if (lane >= 0 && !fill && !(here.kind == NodeKind::Compute && here.lane == lane)) {
            return false;
        }
    }
    return true;
}

// What leaf `here` of a skewed stage, storing `to`, does at its lane. A Compute at one lane makes that lane's number
// alone, broadcast: it keeps no State and reads none (each lane's register holds its own section), and stores that one
// number, not along the lanes. A leaf at no lane stores only a State it keeps and closes after the time loop: stored
// every step, the drain's steps would leave lanes past the block in it.
constexpr Checked stores_at_its_lane(const Node& here, const Access& to, std::int32_t lanes) {
    if (here.lane >= 0 && (here.keeps >= 0 || here.reads >= 0)) {
        return std::unexpected("heinzel::loops: a Compute at a lane keeps no State and reads none, shifted or not");
    }
    if (here.lane >= 0 && to.source == Source::Buffer && to.buffer_axis[static_cast<std::size_t>(lanes)] >= 0) {
        return std::unexpected("heinzel::loops: a Compute at a lane stores its lane's number, not along the lanes");
    }
    const bool stores = here.kind == NodeKind::Compute || here.kind == NodeKind::Finish || here.kind == NodeKind::Rank || here.kind == NodeKind::Partial;
    if (stores && here.lane < 0 && to.source == Source::Buffer && !(here.keeps >= 0 && here.closes)) {
        return std::unexpected("heinzel::loops: a leaf of a skewed stage at no lane stores only a State it keeps and closes");
    }
    return {};
}

// Node n of skewed stage s, of k sections along `lanes`: the link's State read, a Compute at one lane, what moves along
// time read or stored by one, a value made at one lane read at that lane, no fold over the sections (their sum every
// sample: each lane holds another sample), and what each leaf stores.
constexpr Checked skewed_node(const Program& p, std::int32_t n, std::size_t s, std::int32_t lanes, std::int64_t k, const std::vector<std::int32_t>& stage_of) {
    const Stage& stage = p.stages[s];
    const Node& here = node_at(p, n);
    if (here.shifted && !reads_the_lane_below(p, here, stage, lanes)) {
        return std::unexpected("heinzel::loops: shifted: a Compute reading slot 0 before its keeper, operand 1 one element back");
    }
    if (here.lane != -1 && (here.kind != NodeKind::Compute || here.lane < 0 || here.lane >= k)) {
        return std::unexpected("heinzel::loops: a lane stands on a Compute, within [0, k)");
    }
    if (!is_leaf(here.kind) || here.kind == NodeKind::Prefetch) {
        return {};
    }
    const bool compute = here.kind == NodeKind::Compute;
    const std::size_t from_state = here.shifted ? 1 : 0; // (a reader of state takes its register for that operand)
    bool moves = false;
    for (std::size_t slot = 0; slot <= kMaxNodeInputs; ++slot) {
        const bool register_read = compute && here.reads >= 0 && slot == from_state;
        moves = moves || (!register_read && along_time(p, loops::access_in(p.stmts[static_cast<std::size_t>(stage.first + here.stmt)], slot)));
    }
    if (moves && compute && here.lane < 0) {
        return std::unexpected("heinzel::loops: a Compute of a skewed stage that reads or stores along time stands at a lane");
    }
    if (moves && !compute) {
        return std::unexpected("heinzel::loops: no other leaf of a skewed stage reads or stores a buffer along time");
    }
    if (!reads_lanes_at_their_lane(p, here, s, stage_of)) {
        return std::unexpected("heinzel::loops: a value made at one lane is read at that lane, or as the link's fill from lane 0");
    }
    if (here.kind == NodeKind::Finish && lanes_fold_together(p, registers_of(p, n, here.stmt, Role::Accumulator))) {
        return std::unexpected("heinzel::loops: no fold of a skewed stage runs along its lanes, whose sections stand on different samples");
    }
    return stores_at_its_lane(here, p.stmts[static_cast<std::size_t>(stage.first + here.stmt)].store, lanes);
}

// Every skewed stage keeps its rules, and `shifted` and `lane` stand in no other.
constexpr Checked check_skew(const Program& p, const LaneAxes& lanes, const std::vector<std::int32_t>& stage_of) {
    std::vector<std::int64_t> sections(p.stages.size(), 0);
    for (std::size_t s = 0; s < p.stages.size(); ++s) {
        const std::expected<std::int64_t, Refusal> k = p.stages[s].skewed ? skewed_stage(p, s, lanes[s], stage_of) : std::expected<std::int64_t, Refusal>{0};
        if (!k) {
            return std::unexpected(k.error());
        }
        sections[s] = *k;
    }
    for (std::size_t n = 0; n < p.nodes.size(); ++n) {
        const auto s = static_cast<std::size_t>(stage_of[n]);
        if ((p.nodes[n].shifted || p.nodes[n].lane != -1) && !p.stages[s].skewed) {
            return std::unexpected("heinzel::loops: shifted and lane stand only in a skewed stage");
        }
        const Checked r = p.stages[s].skewed ? skewed_node(p, static_cast<std::int32_t>(n), s, lanes[s], sections[s], stage_of) : Checked{};
        if (!r) {
            return r;
        }
    }
    return {};
}

} // namespace heinzel::loops::detail
