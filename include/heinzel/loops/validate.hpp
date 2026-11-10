#pragma once

// The rules a stage is held to, checked over its skeleton and statements. A stage has one lane axis, and lanes only
// where memory is contiguous and a runner can build a block of bodies. A stage that reads its own past walks time in
// order, a network has one inner loop, a split writes nothing two workers share, and a link stands only in a skewed
// stage; the normal form's refusals are here too. What a skeleton built from a checked schedule cannot break is not
// checked again: every axis walked by one loop, with at most one level of chunks outside it, and every enumerator below
// its strip.

#include <array>
#include <cstdint>
#include <expected>

#include "heinzel/ir/ir.hpp"
#include "heinzel/loops/implied.hpp"
#include "heinzel/loops/lower_stmts.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/tree.hpp"

namespace heinzel::loops::detail {

// Every symbolic extent of the domain must fit the size table a run binds them in (`loops::Sizes`), and the stage's
// lanes must run along one axis: a schedule may plan them along two.
constexpr Done check_axes(const Formed& f) {
    for (const Extent e : f.axes) {
        if (is_symbol(e) && symbol_of(e) >= static_cast<std::int32_t>(kMaxDimDecls)) {
            return std::unexpected("heinzel::compile: a Dim's id not below kMaxDimDecls; rebuild with -DHEINZEL_MAX_DIM_DECLS=N");
        }
    }
    std::int32_t lanes = -1;
    for (const Node& n : f.skeleton.walk) {
        if (n.kind == NodeKind::Vector && lanes >= 0 && n.axis != lanes) {
            return std::unexpected("heinzel::compile: a stage's lanes run along one axis");
        }
        lanes = n.kind == NodeKind::Vector ? n.axis : lanes;
    }
    return {};
}

// A network holds every value along its axis at once, so its stage has one inner loop, serial and exactly as long as
// the number of values it gathers.
constexpr Done check_gathering(const Formed& f, const Implied& implied) {
    for (const Stmt& st : f.stmts) {
        const std::size_t inner = static_cast<std::size_t>(implied.inner);
        if (st.kind == StmtKind::Select &&
            (inner + 1 != f.skeleton.walk.size() || !detail::serial(f.skeleton.walk[inner]) ||
             f.axes[static_cast<std::size_t>(f.skeleton.walk[inner].axis)] != literal_extent(st.gathered))) {
            return std::unexpected("heinzel::compile: an order statistic's group has one inner loop, serial, over exactly the values it gathers");
        }
    }
    return {};
}

// A fold's own loops walk exactly the axes it collapses; a stage holds a fold or a network, not both. A fold that keeps
// its axes is not chunked along one it collapses: its partial results would be kept one chunk further along an axis
// of extent one each time.
constexpr Done check_fold_loops(const Graph& g, const Formed& f, const Implied& implied) {
    const std::int32_t fold = detail::fold_node(f);
    if (implied.folds && implied.inner == static_cast<std::int32_t>(f.skeleton.walk.size())) { // (one that reads its own past)
        return std::unexpected("heinzel::compile: a fold's group has inner loops: a feedback stage's sample walks time innermost");
    }
    for (std::size_t k = static_cast<std::size_t>(implied.inner); fold >= 0 && implied.folds && !implied.sample && k < f.skeleton.walk.size(); ++k) {
        if ((f.collapsed >> f.skeleton.walk[k].axis & 1u) == 0) {
            return std::unexpected("heinzel::compile: a fold's inner loops walk exactly the axes it collapses");
        }
    }
    const heinzel::Node* keeps = fold >= 0 && g.nodes[static_cast<std::size_t>(fold)].attr.kind == AttrKind::Reduce ? &g.nodes[static_cast<std::size_t>(fold)] : nullptr;
    if (implied.resumes && keeps != nullptr && keeps->attr.reduce.keep_axes) {
        return std::unexpected("heinzel::compile: a fold that keeps its axes is not chunked along one it collapses");
    }
    if (implied.folds && implied.selects) {
        return std::unexpected("heinzel::compile: a group holds both a network and a fold");
    }
    return {};
}

// Lanes walk memory contiguously: along a Vector's axis, every buffer a lane statement touches either does not move or
// moves along the axis it is contiguous in. Lanes along a fold's own axis reorder the fold, so they are refused for a
// fold owed its strict order and for one that resumes. Inside a fold's loops an Unroll stands only directly above the
// Vector.
constexpr Done check_lanes_run(const Program& p, const Formed& f, const Implied& implied) {
    for (std::size_t k = 0; k < f.skeleton.walk.size(); ++k) {
        const Node& n = f.skeleton.walk[k];
        const bool in_fold = static_cast<std::int32_t>(k) >= implied.inner && implied.folds;
        const bool above_lanes = k + 1 < f.skeleton.walk.size() && f.skeleton.walk[k + 1].kind == NodeKind::Vector && f.skeleton.walk[k + 1].axis == n.axis;
        if (n.kind == NodeKind::Unroll && in_fold && !above_lanes) {
            return std::unexpected("heinzel::compile: only a loop outside a fold's may be unrolled into bodies");
        }
        if (n.kind != NodeKind::Vector) {
            continue;
        }
        if (in_fold && implied.resumes) {
            return std::unexpected("heinzel::compile: a fold chunked along its own axis resumes per output, so its lanes must run across outputs");
        }
        for (std::size_t s = 0; s < f.stmts.size(); ++s) {
            const Stmt& st = f.stmts[s];
            if (st.kind == StmtKind::Fold && st.strict && in_fold) {
                return std::unexpected("heinzel::compile: a fold owed its strict order (High precision) may not run in lanes");
            }
            for (std::size_t slot = 0; !(in_fold && !f.inside[s]) && slot <= kMaxNodeInputs; ++slot) {
                const Access& a = loops::access_in(st, slot);
                if (a.source != Source::Buffer || a.buffer_axis[static_cast<std::size_t>(n.axis)] < 0) {
                    continue;
                }
                const std::int32_t rank = p.buffers[static_cast<std::size_t>(a.buffer)].shape.rank;
                if (a.buffer_axis[static_cast<std::size_t>(n.axis)] != loops::contiguous_axis(rank) || a.step[static_cast<std::size_t>(n.axis)] != 1) {
                    return std::unexpected("heinzel::compile: a Vector loop runs along an axis some buffer is not contiguous in");
                }
            }
        }
    }
    return {};
}

// A Vector is the innermost walking node, or it stands outside the stage's inner loops, which then build a block of
// bodies as they go.
constexpr Done check_where_the_lanes_are(const Formed& f, const Implied& implied) {
    const bool inner = implied.inner < static_cast<std::int32_t>(f.skeleton.walk.size());
    for (std::size_t k = 0; k + 1 < f.skeleton.walk.size(); ++k) {
        if (f.skeleton.walk[k].kind == NodeKind::Vector && !(inner && static_cast<std::int32_t>(k) < implied.inner)) {
            return std::unexpected("heinzel::compile: a Vector loop is a group's innermost, or outside its fold's or its feedback's loops");
        }
    }
    return {};
}

// The rules of loop shape, checked once the schedule's choices are applied to a stage: one lane axis, lanes only where
// a runner builds them, and a fold's own loops over exactly what it collapses.
constexpr Done check_shape(const Lowering& l, const Formed& f) {
    const Implied implied = detail::shape_of(l.out, f);
    for (const Done r : {detail::check_axes(f), detail::check_fold_loops(l.g, f, implied), detail::check_lanes_run(l.out, f, implied),
                         detail::check_where_the_lanes_are(f, implied)}) {
        if (!r) {
            return r;
        }
    }
    return {};
}

// Does `read`, of the buffer `store` writes, read the element being made there along the split axis? It does not if it
// reads through a window along that axis, with a step, at another offset, or with a lag along it.
constexpr bool reads_at_the_point(const Program& p, const Access& read, const Access& store, std::int32_t axis) {
    const std::int32_t along = store.buffer_axis[static_cast<std::size_t>(axis)];
    const std::int32_t rank = p.buffers[static_cast<std::size_t>(read.buffer)].shape.rank;
    if (read.buffer_axis[static_cast<std::size_t>(axis)] != along || read.step[static_cast<std::size_t>(axis)] != 1 ||
        read.offset[static_cast<std::size_t>(along)] != store.offset[static_cast<std::size_t>(along)] || (read.back > 0 && along == rank - 1)) {
        return false;
    }
    for (std::size_t other = 0; other < kMaxAxes; ++other) {
        if (static_cast<std::int32_t>(other) != axis && read.buffer_axis[other] == along) {
            return false;
        }
    }
    return true;
}

// A stage split among workers runs whole on each of them, over a range of the split loop's axis. So the split loop must
// be the outermost loop over that axis, no fold may collapse it, every buffer the stage writes must move along it
// alone, and every read of such a buffer must be of the element being made there: then no two workers touch an
// element either of them writes.
constexpr Done check_split(const Program& p, const Formed& f, const Implied& implied) {
    if (f.split < 0) {
        return {};
    }
    const std::int32_t axis = f.skeleton.walk[static_cast<std::size_t>(f.split)].axis;
    for (std::size_t up = 0; up < static_cast<std::size_t>(f.split); ++up) {
        if (f.skeleton.walk[up].axis == axis) {
            return std::unexpected("heinzel::compile: a group is split along the outermost loop over an axis");
        }
    }
    for (std::size_t k = static_cast<std::size_t>(implied.inner); (implied.folds || implied.selects) && k < f.skeleton.walk.size(); ++k) {
        if (f.skeleton.walk[k].axis == axis) {
            return std::unexpected("heinzel::compile: a group is not split along an axis its fold collapses");
        }
    }
    if (implied.sample && (f.collapsed >> axis & 1u) != 0) { // (its voices' strip stands outside the time loop)
        return std::unexpected("heinzel::compile: a group is not split along an axis its fold collapses");
    }
    std::array<std::int32_t, kMaxBuffers> writer{}; // 1 + the statement storing each buffer (0: none)
    for (std::size_t s = 0; s < f.stmts.size(); ++s) { // writes disjoint across the ranges: each moves along the axis, alone
        const Access& store = f.stmts[s].store;
        const std::int32_t along = store.buffer_axis[static_cast<std::size_t>(axis)];
        for (std::size_t other = 0; store.source == Source::Buffer && other < kMaxAxes; ++other) {
            if (along < 0 || (static_cast<std::int32_t>(other) != axis && store.buffer_axis[other] == along)) {
                return std::unexpected("heinzel::compile: every buffer a split group writes moves along the split axis, alone");
            }
        }
    }
    for (std::size_t s = 0; s < f.stmts.size(); ++s) {
        if (f.stmts[s].store.source == Source::Buffer) {
            writer[static_cast<std::size_t>(f.stmts[s].store.buffer)] = static_cast<std::int32_t>(s) + 1;
        }
    }
    for (const Stmt& st : f.stmts) {
        for (const Access& read : st.in) {
            const std::int32_t w = read.source == Source::Buffer ? writer[static_cast<std::size_t>(read.buffer)] : 0;
            if (w > 0 && !detail::reads_at_the_point(p, read, f.stmts[static_cast<std::size_t>(w - 1)].store, axis)) {
                return std::unexpected("heinzel::compile: a split group reads what it writes only at the point it makes");
            }
        }
    }
    return {};
}

// Feedback: when a statement reads the past of a buffer its own stage writes, the walk along that buffer's time must
// run one sample at a time, in order. A stage that reads a feed's past runs its first samples one at a time, so it may
// not walk time in chunks.
constexpr Done check_feedback(const Program& p, const Formed& f) {
    for (const Stmt& reader : f.stmts) {
        for (const Access& read : reader.in) {
            if (read.source != Source::Buffer || read.back == 0) {
                continue;
            }
            const Buffer& buffer = p.buffers[static_cast<std::size_t>(read.buffer)];
            const std::int32_t time = buffer.shape.rank - 1;
            bool written_here = false;
            for (const Stmt& writer : f.stmts) {
                written_here = written_here || (writer.store.source == Source::Buffer && writer.store.buffer == read.buffer);
            }
            for (const Node& n : f.skeleton.walk) {
                const bool along = read.buffer_axis[static_cast<std::size_t>(n.axis)] == time;
                if (buffer.home == Home::Feed && along && detail::chunks(n)) {
                    return std::unexpected("heinzel::compile: a group that reads a feed's past may not walk time in chunks");
                }
                if (written_here && along && is_enumerator(n)) {
                    return std::unexpected("heinzel::compile: a group that reads its own past walks time one sample at a time, in order");
                }
            }
        }
    }
    return {};
}

// A delay line's buffer must keep silence as deep as its position may reach, plus one sample for its far tap.
constexpr Done check_delay_lines(const Program& p, const Formed& f) {
    for (const Stmt& st : f.stmts) {
        if (st.kind == StmtKind::Gather && st.gather == Gather::Back && p.buffers[static_cast<std::size_t>(st.in[0].buffer)].prefix < st.reach + 1) {
            return std::unexpected("heinzel::compile: a delay line reads further into the past than its buffer keeps silence for");
        }
    }
    return {};
}

// The normal form's refusals, and a prefetch's precondition. A Loop outside the register point over an axis the inner
// loops walk must chunk a fold, by more than one. Lanes along a fold's own axis may have no Unroll or Vector outside
// them: normalization has only one chain then, the fold-lanes chain, and would leave the outer ones out of every nest.
// A prefetch runs along a Loop outside the register point that cuts no chunks, in a stage that has a register point.
constexpr Done check_normal_form(const Formed& f, const Implied& implied) {
    for (std::size_t k = 0; k < static_cast<std::size_t>(implied.inner); ++k) {
        const Node& n = f.skeleton.walk[k];
        if (implied.fold_lanes && is_enumerator(n)) {
            return std::unexpected("heinzel::compile: lanes along a fold's own axis with rows or lanes outside it");
        }
        for (std::size_t in = static_cast<std::size_t>(implied.inner); n.kind == NodeKind::Loop && in < f.skeleton.walk.size(); ++in) {
            if (f.skeleton.walk[in].axis == n.axis && (n.step == 1 || !implied.folds || implied.sample)) { // (a sample's time: no fold's axis)
                return std::unexpected("heinzel::compile: a loop outside the inner loops over an axis they walk chunks a fold, by more than one");
            }
        }
    }
    const std::int32_t ahead = f.prefetch;
    if (ahead >= 0 && (ahead >= implied.inner || implied.inner == static_cast<std::int32_t>(f.skeleton.walk.size()) ||
                       f.skeleton.walk[static_cast<std::size_t>(ahead)].kind != NodeKind::Loop || detail::chunks(f.skeleton.walk[static_cast<std::size_t>(ahead)]))) {
        return std::unexpected("heinzel::compile: a prefetch runs along a loop outside the register point that is no chunk, in a stage with one");
    }
    return {};
}

// A feedback stage whose sample holds a fold (Plan/23 10): one fold; each axis it collapses walked in lanes outside the
// time loop, in one chunk of their strip (the extent literal, the strip's step that extent, or more with its masked
// tail), nothing fetched ahead; its part before the fold made before any of its lanes; no lane reading what the part
// after the fold makes. The runner's shape has no other place for them.
constexpr Done check_sample(const Formed& f, const Implied& implied) {
    if (!implied.sample) {
        return {};
    }
    std::int32_t folds = 0;
    for (const Stmt& st : f.stmts) {
        folds += st.kind == StmtKind::Fold ? 1 : 0;
    }
    if (folds != 1 || f.prefetch >= 0) {
        return std::unexpected("heinzel::compile: a feedback stage's sample holds one fold, and fetches nothing ahead");
    }
    for (std::size_t axis = 0; axis < f.axes.size(); ++axis) {
        bool one_chunk = (f.collapsed >> axis & 1u) == 0;
        for (std::size_t k = 0; !one_chunk && k + 1 < f.skeleton.walk.size(); ++k) {
            const Node& strip = f.skeleton.walk[k];
            const std::int64_t length = is_literal(f.axes[axis]) ? literal_of(f.axes[axis]) : -1;
            one_chunk = strip.kind == NodeKind::Loop && strip.axis == static_cast<std::int32_t>(axis) && strip.tail != Tail::Clip && length > 0 &&
                        (strip.step == length || (strip.tail == Tail::Masked && strip.step > length));
        }
        if (!one_chunk) {
            return std::unexpected("heinzel::compile: a fold in a feedback stage's sample takes its voices in lanes, all in one chunk of their strip");
        }
    }
    bool lanes = false;
    for (std::size_t s = 0; s < f.stmts.size(); ++s) {
        lanes = lanes || f.inside[s];
        if (!f.inside[s] && lanes && static_cast<std::int32_t>(s) < implied.fold) {
            return std::unexpected("heinzel::compile: a node over a fold's result stands among the lanes of its sample");
        }
        for (const Access& a : f.stmts[s].in) {
            if (f.inside[s] && a.source == Source::Local && !f.inside[static_cast<std::size_t>(a.local)] && a.local > implied.fold) {
                return std::unexpected("heinzel::compile: a lane of a feedback stage's sample reads what its part after the fold makes");
            }
        }
    }
    return {};
}

// Does access `a` move along its buffer's time, its last axis and longer than one sample: walked or indexed there?
constexpr bool walks_time(const Program& p, const Access& a) {
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

// A link stands only in a skewed stage, whose lanes stand on different samples. There what moves along
// time is read or stored at one lane, at that lane's sample (`Implied::lane`): the link's fill, the slice. A state that
// closes moves along none: its buffer holds its past alone (`held_in_place`).
constexpr Done check_links(const Lowering& l, const Formed& f, const Implied& implied) {
    const bool skewed = f.plan >= 0 && l.schedule.stages[static_cast<std::size_t>(f.plan)].skewed;
    std::array<bool, kMaxBuffers> held{};
    for (std::size_t s = 0; s < f.stmts.size(); ++s) {
        if (f.stmts[s].store.source == Source::Buffer && implied.closes[s]) {
            held[static_cast<std::size_t>(f.stmts[s].store.buffer)] = true;
        }
    }
    for (std::size_t s = 0; s < f.stmts.size(); ++s) {
        if (detail::is_a_link(f.stmts[s]) && !skewed) {
            return std::unexpected("heinzel::compile: a link stands only in a skewed stage");
        }
        for (std::size_t slot = 0; skewed && implied.lane[s] < 0 && slot <= kMaxNodeInputs; ++slot) {
            const Access& a = loops::access_in(f.stmts[s], slot);
            if (detail::walks_time(l.out, a) && !held[static_cast<std::size_t>(a.buffer)]) {
                return std::unexpected("heinzel::compile: a skewed stage reads and stores what moves along time at one lane only");
            }
        }
    }
    return {};
}

// Stage `f` against every rule, in order: the first broken is the refusal.
constexpr Done validate(const Lowering& l, const Formed& f, const Implied& implied) {
    for (const Done r : {detail::check_axes(f), detail::check_fold_loops(l.g, f, implied), detail::check_gathering(f, implied),
                         detail::check_lanes_run(l.out, f, implied), detail::check_split(l.out, f, implied), detail::check_feedback(l.out, f),
                         detail::check_delay_lines(l.out, f), detail::check_where_the_lanes_are(f, implied), detail::check_normal_form(f, implied),
                         detail::check_sample(f, implied), detail::check_links(l, f, implied)}) {
        if (!r) {
            return r;
        }
    }
    return {};
}

} // namespace heinzel::loops::detail
