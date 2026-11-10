#pragma once

// What a well-formed tree's walks keep, beyond its structure (`well_formed.hpp`): the fields tying stages, bands and
// buffers together; along the lane axis, elements side by side and a relaid buffer walked within its blocks; every
// point of the domain made; the past a lag or a delay line reads kept before its buffer; and a split that writes
// nothing two workers share. Each check is linear in the nodes and statements, or bounded per node by the depth of
// the tree.

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

#include "heinzel/loops/program.hpp"
#include "heinzel/loops/tree.hpp"

namespace heinzel::loops {

// What only the checks ask of the tree, beside what the runner asks too (`loops/tree.hpp`).
constexpr bool is_leaf(NodeKind k) { return k >= NodeKind::Compute; }
constexpr bool is_strip(const Node& n) { return n.kind == NodeKind::Loop && n.tail != Tail::Clip; }
constexpr bool power_of_two(std::int64_t x) { return x > 0 && (x & (x - 1)) == 0; }

// Does the chain from `link` end in lanes?
constexpr bool chain_has_lanes(const Program& p, std::int32_t link) {
    for (std::int32_t at = link; at >= 0; at = chain_next(p, at)) {
        if (node_at(p, at).kind == NodeKind::Vector) {
            return true;
        }
    }
    return false;
}

// The loop through which a State hands its value on that encloses node `n`: the nearest Loop above it that State
// Registers enclose (-1: none).
constexpr std::int32_t loop_handing_state_on(const Program& p, std::int32_t n) {
    const auto state = [](const Node& a) { return a.kind == NodeKind::Registers && a.role == Role::State; };
    for (std::int32_t at = node_at(p, n).parent; at >= 0 && node_at(p, at).kind != NodeKind::Stage; at = node_at(p, at).parent) {
        if (node_at(p, at).kind == NodeKind::Loop && ancestor(p, at, state) >= 0) {
            return at;
        }
    }
    return -1;
}

// Do fold Registers `regs` restart at every sample of a feedback stage (Plan/23 10): right below the loop through which
// a State hands its value on, inside the strip of their lanes?
constexpr bool restarts_each_sample(const Program& p, std::int32_t regs) {
    const std::int32_t link = node_at(p, regs).link;
    const std::int32_t strip = link < 0 ? -1 : governing_strip(p, link);
    return strip >= 0 && !encloses(p, regs, strip) && loop_handing_state_on(p, regs) == node_at(p, regs).parent;
}

} // namespace heinzel::loops

namespace heinzel::loops::detail {

using Checked = std::expected<void, Refusal>;

// Statement k of stage s.
constexpr const Stmt& stmt_of(const Program& p, std::size_t s, std::int32_t k) {
    return p.stmts[static_cast<std::size_t>(p.stages[s].first + k)];
}
constexpr bool walks(const Node& n) {
    return n.kind == NodeKind::Loop || n.kind == NodeKind::Unroll || n.kind == NodeKind::Vector || n.kind == NodeKind::Turns;
}

// The band stage s runs in (-1: none).
constexpr std::int32_t band_of(const Program& p, std::size_t s) {
    for (std::size_t k = 0; k < p.bands.size(); ++k) {
        if (static_cast<std::int32_t>(s) >= p.bands[k].first && static_cast<std::int32_t>(s) < p.bands[k].first + p.bands[k].count) {
            return static_cast<std::int32_t>(k);
        }
    }
    return -1;
}

// Do the lanes of Registers `regs` run along an axis the fold walks inside them — its own — so its lanes are folded
// together at the end, and an Init or a Finish with no chain between covers every register? So they do where the
// strip of the lanes stands inside the Registers, and where the Registers restart every sample inside one chunk of it
// (`well_formed_restarts.hpp` holds that chunk to the whole axis).
constexpr bool lanes_fold_together(const Program& p, std::int32_t regs) {
    const std::int32_t link = node_at(p, regs).link;
    return link >= 0 && chain_has_lanes(p, link) && (encloses(p, regs, governing_strip(p, link)) || restarts_each_sample(p, regs));
}

// A stage's split and time loop are Loops of its own; head samples need a time loop and no band; buffers and
// outputs exist.
constexpr Checked check_fields(const Program& p, const std::vector<std::int32_t>& stage_of) {
    for (std::size_t s = 0; s < p.stages.size(); ++s) {
        const Stage& st = p.stages[s];
        for (const std::int32_t loop : {st.split, st.time_loop}) {
            const bool own = loop >= 0 && static_cast<std::size_t>(loop) < p.nodes.size() && node_at(p, loop).kind == NodeKind::Loop &&
                             stage_of[static_cast<std::size_t>(loop)] == static_cast<std::int32_t>(s);
            if (loop != -1 && !own) {
                return std::unexpected("heinzel::loops: a stage's split and time loop are Loop nodes of that stage");
            }
        }
        if (st.head < 0 || (st.head > 0 && st.time_loop < 0) || st.lead < 0 || st.band_axis >= static_cast<std::int32_t>(st.axes.size())) {
            return std::unexpected("heinzel::loops: head samples walk a time loop; a band axis is one of the stage's");
        }
        if (st.head > 0 && band_of(p, s) >= 0) { // (a band's rows run no head samples)
            return std::unexpected("heinzel::loops: a stage in a band runs no head samples");
        }
    }
    for (const Buffer& b : p.buffers) {
        if (b.band >= static_cast<std::int32_t>(p.bands.size()) || b.maker >= static_cast<std::int32_t>(p.stages.size()) ||
            b.reader >= static_cast<std::int32_t>(p.stages.size())) {
            return std::unexpected("heinzel::loops: a band buffer names a band and stages of the program");
        }
    }
    for (const std::int32_t out : p.outputs) {
        if (out < 0 || static_cast<std::size_t>(out) >= p.buffers.size()) {
            return std::unexpected("heinzel::loops: an output names a buffer of the program");
        }
    }
    return {};
}

// A band walks chunks of one row or more over one stage or more of the program, the bands in the stages' order and no
// two sharing a stage (the runner skips a band that overlaps the one before, and recurses without end into a band of no
// stages). It walks along a band axis of each of its stages, and its buffers (those naming it) are made and read by
// stages of the band and keep rows along an axis they have.
constexpr Checked check_bands(const Program& p) {
    for (std::size_t k = 0; k < p.bands.size(); ++k) {
        const BandInfo& band = p.bands[k];
        if (band.step < 1) {
            return std::unexpected("heinzel::loops: a band walks chunks of one row or more");
        }
        if (band.count < 1) {
            return std::unexpected("heinzel::loops: a band holds one stage or more");
        }
        if (band.first < (k > 0 ? p.bands[k - 1].first + p.bands[k - 1].count : 0)) {
            return std::unexpected("heinzel::loops: bands are in the stages' order, and no two share a stage");
        }
        if (band.first + band.count > static_cast<std::int32_t>(p.stages.size())) {
            return std::unexpected("heinzel::loops: a band's stages are stages of the program");
        }
        bool walked = true;
        for (std::int32_t s = band.first; s < band.first + band.count; ++s) {
            walked = walked && p.stages[static_cast<std::size_t>(s)].band_axis >= 0;
        }
        const auto inside = [&band](std::int32_t s) { return s >= band.first && s < band.first + band.count; };
        for (const Buffer& held : p.buffers) {
            walked = walked && (held.band != static_cast<std::int32_t>(k) ||
                                (inside(held.maker) && inside(held.reader) && held.kept_axis >= 0 && held.kept_axis < held.shape.rank));
        }
        if (!walked) {
            return std::unexpected("heinzel::loops: a band walks chunks of rows along a band axis of each of its stages, which make and read its buffers");
        }
    }
    return {};
}

// Along a stage's lane axis the runner moves every slot walking it one element per step and loads or stores a
// Vector's lanes side by side (`walk.hpp`, `values.hpp`): an access walking the lane axis steps by one over a buffer
// axis whose elements sit one apart, and a leaf in lanes stores along them.
constexpr bool one_element_apart(const Buffer& b, std::int32_t along) {
    if (b.blocked_axis >= 0) { // relaid: a lane's elements sit side by side within a block of the blocked axis
        return along == b.blocked_axis;
    }
    const std::int32_t time = b.shape.rank - 1; // sample-major: the element axes inside out from `time - 1`
    for (std::int32_t k = along == time ? 0 : along + 1; k < time; ++k) {
        if (!is_literal(extent_at(b.shape, k)) || literal_of(extent_at(b.shape, k)) != 1) {
            return false;
        }
    }
    return true;
}

using LaneAxes = std::array<std::int32_t, kMaxStages>;

// Each stage's lane axis (-1: none); a leaf in lanes stores along them, but one at one lane (`Node::lane`), that lane's.
constexpr std::expected<LaneAxes, Refusal> lane_axes(const Program& p, const std::vector<std::int32_t>& stage_of) {
    LaneAxes lanes{};
    lanes.fill(-1);
    for (std::size_t n = 0; n < p.nodes.size(); ++n) {
        const Node& here = p.nodes[n];
        if (here.kind == NodeKind::Vector) {
            lanes[static_cast<std::size_t>(stage_of[n])] = here.axis;
        }
        const bool stores = here.kind == NodeKind::Compute || here.kind == NodeKind::Finish || here.kind == NodeKind::Rank || here.kind == NodeKind::Partial;
        if (stores && here.lane < 0 && node_at(p, here.parent).kind == NodeKind::Vector) {
            const Access& to = stmt_of(p, static_cast<std::size_t>(stage_of[n]), here.stmt).store;
            if (to.source == Source::Buffer && to.buffer_axis[static_cast<std::size_t>(node_at(p, here.parent).axis)] < 0) {
                return std::unexpected("heinzel::loops: a leaf in lanes stores along them");
            }
        }
    }
    return lanes;
}

constexpr Checked check_lanes(const Program& p, const LaneAxes& lanes) {
    for (std::size_t s = 0; s < p.stages.size(); ++s) {
        const auto axis = static_cast<std::size_t>(lanes[s]);
        for (std::int32_t k = 0; lanes[s] >= 0 && k < p.stages[s].count; ++k) {
            for (std::size_t slot = 0; slot <= kMaxNodeInputs; ++slot) {
                const Access& a = loops::access_in(stmt_of(p, s, k), slot);
                const std::int32_t along = a.source == Source::Buffer ? a.buffer_axis[axis] : -1;
                if (along >= 0 && (a.step[axis] != 1 || !one_element_apart(p.buffers[static_cast<std::size_t>(a.buffer)], along))) {
                    return std::unexpected("heinzel::loops: an access walking the lane axis steps one element at a time, side by side");
                }
            }
        }
    }
    return {};
}

// A relaid buffer's elements sit side by side only within a block: its blocked axis is walked along the lane axis
// alone; a loop over it by whole blocks (moved by the table, `loop_moved`) is a Clip or Masked loop, so every
// offset it moves by is a multiple of the block; whatever moves by elements (an Unroll, a Vector, a loop of less
// than a block) is handed a chunk that divides the block, so it never leaves one.
constexpr Checked check_block_walk(const Program& p, std::size_t s, std::int32_t lanes, std::int32_t block, const std::vector<std::int32_t>& stage_of) {
    const auto over_lanes = [lanes](const Node& a) { return walks(a) && a.kind != NodeKind::Turns && a.axis == lanes; };
    for (auto n = static_cast<std::size_t>(p.stages[s].root) + 1; n < p.nodes.size() && stage_of[n] == static_cast<std::int32_t>(s); ++n) {
        const Node& here = p.nodes[n]; // (a stage's nodes are numbered in one run, in pre-order)
        if (!over_lanes(here)) {
            continue;
        }
        const bool by_blocks = here.kind == NodeKind::Loop && here.step % block == 0;
        const std::int32_t above = ancestor(p, static_cast<std::int32_t>(n), over_lanes);
        if (by_blocks ? here.tail != Tail::Clip && here.tail != Tail::Masked : above < 0 || block % node_at(p, above).step != 0) {
            return std::unexpected("heinzel::loops: a relaid buffer's lanes are walked by whole blocks, or within one");
        }
    }
    return {};
}

constexpr Checked check_blocks(const Program& p, const LaneAxes& lanes, const std::vector<std::int32_t>& stage_of) {
    for (std::size_t s = 0; s < p.stages.size(); ++s) {
        std::array<bool, kMaxBuffers> checked{}; // one walk of the stage's nodes per relaid buffer
        for (std::int32_t k = 0; k < p.stages[s].count; ++k) {
            for (std::size_t slot = 0; slot <= kMaxNodeInputs; ++slot) {
                const Access& a = loops::access_in(stmt_of(p, s, k), slot);
                const Buffer* b = a.source == Source::Buffer ? &p.buffers[static_cast<std::size_t>(a.buffer)] : nullptr;
                if (b == nullptr || b->blocked_axis < 0) {
                    continue;
                }
                if (b->block < 1) {
                    return std::unexpected("heinzel::loops: a relaid buffer is cut into blocks of one or more");
                }
                for (std::size_t axis = 0; axis < p.stages[s].axes.size(); ++axis) {
                    if (a.buffer_axis[axis] == b->blocked_axis && static_cast<std::int32_t>(axis) != lanes[s]) {
                        return std::unexpected("heinzel::loops: a relaid buffer's blocked axis is walked along the lane axis alone");
                    }
                }
                if (lanes[s] >= 0 && a.buffer_axis[static_cast<std::size_t>(lanes[s])] >= 0 && !checked[static_cast<std::size_t>(a.buffer)]) {
                    checked[static_cast<std::size_t>(a.buffer)] = true;
                    // A split, a band's chunks or head samples cut the lanes where they start: at whole blocks only.
                    const Stage& stage = p.stages[s];
                    const std::int32_t band = band_of(p, s);
                    const bool split_cuts = stage.split >= 0 && node_at(p, stage.split).axis == lanes[s] && node_at(p, stage.split).step % b->block != 0;
                    const bool band_cuts = band >= 0 && stage.band_axis == lanes[s] && p.bands[static_cast<std::size_t>(band)].step % b->block != 0;
                    const bool head_cuts = stage.head > 0 && node_at(p, stage.time_loop).axis == lanes[s];
                    if (split_cuts || band_cuts || head_cuts) {
                        return std::unexpected("heinzel::loops: a split, a band or head samples cut a relaid buffer's lanes at whole blocks");
                    }
                    if (const Checked r = check_block_walk(p, s, lanes[s], b->block, stage_of); !r) {
                        return r;
                    }
                }
            }
        }
    }
    return {};
}

// Does one of statement st's buffer accesses (its store alone: `stored`) move along `axis` — walk a buffer axis
// longer than one number?
constexpr bool walks_along(const Program& p, const Stmt& st, std::size_t axis, bool stored = false) {
    for (std::size_t slot = stored ? kMaxNodeInputs : 0; slot <= kMaxNodeInputs; ++slot) {
        const Access& a = loops::access_in(st, slot);
        if (a.source == Source::Buffer && a.buffer_axis[axis] >= 0) {
            const Extent e = extent_at(p.buffers[static_cast<std::size_t>(a.buffer)].shape, a.buffer_axis[axis]);
            if (!is_literal(e) || literal_of(e) != 1) {
                return true;
            }
        }
    }
    return false;
}

// Every point of the domain is made: above a Compute, an Accumulate or a Collect, the innermost node walking an
// axis its statement's accesses walk steps along it one at a time or is the Vector running the chunk in lanes (a
// leaf right below a strip or a chunk would run once for all of it); above a Finish, a Rank or a Partial, the same
// for the axes its store walks; an axis nothing walks is one number long where the leaf's accesses walk it — and,
// for an Accumulate, anywhere (it would fold one point for all of them). Below a Masked strip, whose ragged end runs
// as one whole chunk with its lanes masked, every leaf that reads or writes a buffer runs that axis in a Vector.
constexpr Checked check_coverage(const Program& p, const std::vector<std::int32_t>& stage_of) {
    for (std::size_t n = 0; n < p.nodes.size(); ++n) {
        const Node& leaf = p.nodes[n];
        if (!is_leaf(leaf.kind) || leaf.kind == NodeKind::Init || leaf.kind == NodeKind::Prefetch) {
            continue;
        }
        const bool stores = leaf.kind == NodeKind::Finish || leaf.kind == NodeKind::Rank || leaf.kind == NodeKind::Partial;
        const bool made = stores || leaf.kind == NodeKind::Compute || leaf.kind == NodeKind::Accumulate || leaf.kind == NodeKind::Collect;
        const auto s = static_cast<std::size_t>(stage_of[n]);
        const Stmt& st = stmt_of(p, s, leaf.stmt);
        std::array<bool, kMaxAxes> seen{};
        std::array<bool, kMaxAxes> in_lanes{};
        bool whole = true;
        bool masked = true;
        for (std::int32_t at = leaf.parent; node_at(p, at).kind != NodeKind::Stage; at = node_at(p, at).parent) {
            const Node& a = node_at(p, at);
            const auto axis = static_cast<std::size_t>(a.axis);
            if (walks(a) && !seen[axis]) {
                seen[axis] = true, in_lanes[axis] = a.kind == NodeKind::Vector;
                whole = whole && (!made || a.kind == NodeKind::Vector || a.step == 1 || !walks_along(p, st, axis, stores));
            }
            masked = masked && !(a.kind == NodeKind::Loop && a.tail == Tail::Masked && !in_lanes[axis] && walks_along(p, st, axis));
        }
        for (std::size_t axis = 0; made && axis < p.stages[s].axes.size(); ++axis) {
            const Extent e = p.stages[s].axes[axis];
            whole = whole && (seen[axis] || (is_literal(e) && literal_of(e) == 1) || (leaf.kind != NodeKind::Accumulate && !walks_along(p, st, axis, stores)));
        }
        if (!whole) {
            return std::unexpected("heinzel::loops: every point is made: a leaf walks each axis of its accesses one at a time or in lanes");
        }
        if (!masked) {
            return std::unexpected("heinzel::loops: below a Masked strip a leaf runs its axis in lanes");
        }
    }
    return {};
}

// A loop inside another over its axis walks the chunk a Clip loop of more than one hands it — a strip, an
// enumerator or a Clip loop of one hands its kids the whole extent again. A Masked strip walks the lane axis, and the
// lanes of a fold along its own axis only where the fold restarts every sample, whose Finish folds the real lanes
// alone; elsewhere its missing lanes would be folded in.
constexpr Checked check_nesting(const Program& p, const LaneAxes& lanes, const std::vector<std::int32_t>& stage_of) {
    for (std::size_t n = 0; n < p.nodes.size(); ++n) {
        const Node& here = p.nodes[n];
        if (here.kind == NodeKind::Loop) {
            const std::int32_t axis = here.axis;
            const std::int32_t above = ancestor(p, static_cast<std::int32_t>(n), [axis](const Node& a) { return walks(a) && a.axis == axis; });
            if (above >= 0 && !(node_at(p, above).kind == NodeKind::Loop && node_at(p, above).tail == Tail::Clip && node_at(p, above).step > 1)) {
                return std::unexpected("heinzel::loops: a loop inside another over its axis walks the chunk of a Clip loop of more than one");
            }
        }
        const bool masked_fold = here.kind == NodeKind::Registers && here.role == Role::Accumulator && lanes_fold_together(p, static_cast<std::int32_t>(n)) &&
                                 node_at(p, governing_strip(p, here.link)).tail == Tail::Masked && !restarts_each_sample(p, static_cast<std::int32_t>(n));
        if (masked_fold || (here.kind == NodeKind::Loop && here.tail == Tail::Masked && here.axis != lanes[static_cast<std::size_t>(stage_of[n])])) {
            return std::unexpected("heinzel::loops: a Masked strip walks the lane axis, and a fold's own lanes only where the fold restarts every sample");
        }
    }
    return {};
}

// What an access reaches is kept: a feed's past by the stage's head samples, which run against silence along its
// time loop — a lag of a feed along that loop, a delay line never over a feed (its taps are no lag the head samples
// silence) — a workspace buffer's by its prefix (a delay line reaches one sample past its deepest tap). Every access
// starts inside its buffer, but a `shifted` read's operand 1, one element back along the lanes: the register of the lane
// below, which `check_skew` holds to it. A store writes the workspace, no further back than the buffer keeps.
constexpr Checked check_reach(const Program& p, const Stage& stage, const Stmt& st, bool shifted) {
    for (std::size_t slot = 0; slot <= kMaxNodeInputs; ++slot) {
        const Access& a = loops::access_in(st, slot);
        if (a.source != Source::Buffer) {
            continue;
        }
        const Buffer& b = p.buffers[static_cast<std::size_t>(a.buffer)];
        const bool delay = st.kind == StmtKind::Gather && st.gather == Gather::Back && slot == 0;
        const bool feed = b.home == Home::Feed;
        if (slot == kMaxNodeInputs && (feed || a.back > b.prefix)) {
            return std::unexpected("heinzel::loops: a store writes the workspace, no further back than its buffer keeps");
        }
        const bool along_time = stage.time_loop >= 0 && a.buffer_axis[static_cast<std::size_t>(node_at(p, stage.time_loop).axis)] == b.shape.rank - 1;
        if (feed && (delay || (a.back > 0 && !along_time))) {
            return std::unexpected("heinzel::loops: a feed's past is read by a lag along the stage's time loop");
        }
        if (a.back + (delay ? st.reach + 1 : 0) > (feed ? stage.head : b.prefix)) {
            return std::unexpected("heinzel::loops: a lag or a delay line reads no further back than its buffer keeps, a feed's by head samples");
        }
        for (const std::int32_t offset : a.offset) {
            if (offset < 0 && !(shifted && slot == 1)) {
                return std::unexpected("heinzel::loops: an access starts inside its buffer");
            }
        }
    }
    return {};
}

// `shifted`: per statement, whether a Compute reads its operand 1 shifted.
constexpr Checked check_past(const Program& p, const std::vector<char>& shifted) {
    for (std::size_t s = 0; s < p.stages.size(); ++s) {
        for (std::int32_t k = 0; k < p.stages[s].count; ++k) {
            const bool one_back = shifted[static_cast<std::size_t>(p.stages[s].first + k)] != 0;
            if (const Checked r = check_reach(p, p.stages[s], stmt_of(p, s, k), one_back); !r) {
                return r;
            }
        }
    }
    return {};
}

// A split writes nothing two workers share, and each worker starts afresh: every store of a split stage moves along
// the split axis, no read of the stage looks back along it into what the stage writes (a lag or a delay line), no
// State registers enclose the split loop (a worker would open a state another is still making), and a split along
// time is the time loop itself (worker 0 runs the head samples). A split band likewise along its band axis but for
// its own buffers, of which every worker has a copy — which keep no silence (each worker's first chunk starts as
// chunk 0), and are all a stage running ahead writes. (A stage of a band is run by the band, not by its split.)
constexpr Checked check_splits(const Program& p) {
    for (std::size_t s = 0; s < p.stages.size(); ++s) {
        const Stage& stage = p.stages[s];
        const std::int32_t band = band_of(p, s);
        const bool band_split = band >= 0 && p.bands[static_cast<std::size_t>(band)].parallel;
        const bool stage_split = band < 0 && stage.split >= 0;
        const std::int32_t axis = band_split ? stage.band_axis : stage_split ? node_at(p, stage.split).axis : -1;
        if (axis < 0) {
            continue;
        }
        const bool in_state = stage_split && ancestor(p, stage.split, [](const Node& a) { return a.kind == NodeKind::Registers && a.role == Role::State; }) >= 0;
        const bool off_time = stage_split && stage.head > 0 && stage.split != stage.time_loop && node_at(p, stage.time_loop).axis == axis;
        std::array<bool, kMaxBuffers> written{};
        bool shared = in_state || off_time;
        for (std::int32_t k = 0; k < stage.count; ++k) {
            const Access& to = stmt_of(p, s, k).store;
            if (to.source == Source::Buffer) {
                const Buffer& b = p.buffers[static_cast<std::size_t>(to.buffer)];
                const bool own = band_split && b.band == band;
                written[static_cast<std::size_t>(to.buffer)] = true;
                shared = shared || (!own && (to.buffer_axis[static_cast<std::size_t>(axis)] < 0 || (band_split && stage.lead > 0))) || (own && b.prefix > 0);
            }
        }
        for (std::int32_t k = 0; k < stage.count; ++k) {
            const Stmt& st = stmt_of(p, s, k);
            for (std::size_t slot = 0; slot < kMaxNodeInputs; ++slot) {
                const Access& a = st.in[slot];
                const bool looks_back = a.back > 0 || (st.kind == StmtKind::Gather && st.gather == Gather::Back && slot == 0);
                const bool own = a.source == Source::Buffer && written[static_cast<std::size_t>(a.buffer)] && looks_back;
                shared = shared || (own && a.buffer_axis[static_cast<std::size_t>(axis)] == p.buffers[static_cast<std::size_t>(a.buffer)].shape.rank - 1);
            }
        }
        if (shared) {
            return std::unexpected("heinzel::loops: a split writes nothing two workers share");
        }
    }
    return {};
}

} // namespace heinzel::loops::detail
