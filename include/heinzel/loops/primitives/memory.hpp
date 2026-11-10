#pragma once

// Relaid operands: a contraction reads an operand from a copy blocked along the operand's contiguous axis. The copy is
// a blocked buffer filled by a stage of its own before the reader (`relay`), or an identical copy an earlier stage
// made; a constant's copy is a constant too, laid out when the program compiles (`kConstantOf`), with no stage. The
// buffer is made as its first reader is formed, before that reader's own buffers. The copy stages are inserted once
// every stage is formed, in run order: a copy's lane loop is sized by its reader's lanes, and a copy made for a later
// stage is never read before it is written.
//
// Turned operands: an operand that would stride along its contraction's lanes (`K` of `Q . K^T`) is read from a copy
// turned around, the axis carrying the lanes swapped with its contiguous one, filled by a stage of its own one element
// at a time (a blocked copy is filled in lanes along its blocked axis, which would read the operand a row apart); a
// relay then blocks that copy as it blocks any operand.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <utility>

#include "heinzel/ir/ir.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/graph/regions.hpp"
#include "heinzel/loops/lower_stmts.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/schedule.hpp"

namespace heinzel::loops::detail {

// The stage's contraction statement (-1: none).
constexpr std::int32_t contraction_in(const Graph& g, const Formed& f) {
    for (std::size_t s = 0; s < f.stmts.size(); ++s) {
        if (f.stmts[s].kind == StmtKind::Fold && f.stmts[s].node >= 0 && graph::is_a_contraction(g.nodes[static_cast<std::size_t>(f.stmts[s].node)])) {
            return static_cast<std::int32_t>(s);
        }
    }
    return -1;
}

// A shape with axis `axis` and its contiguous one swapped: a turned copy's.
constexpr Shape turned_shape(Shape s, std::int32_t axis) {
    const std::int32_t contiguous = loops::contiguous_axis(s.rank);
    const Extent along = extent_at(s, axis);
    const Extent across = extent_at(s, contiguous);
    set_extent(s, axis, across);
    set_extent(s, contiguous, along);
    return s;
}

// The axis of contraction operand `which` that carries stage f's lanes, turned to be its contiguous one (-1: it carries
// none, or carries them along its contiguous axis already).
constexpr std::int32_t turned_axis(const Graph& g, const heinzel::Node& n, std::int32_t which, const Formed& f) {
    std::int32_t lanes = -1;
    for (const Node& w : f.skeleton.walk) {
        lanes = w.kind == NodeKind::Vector ? w.axis : lanes;
    }
    const auto axes = graph::contraction_axes(g, n, which);
    const std::int32_t contiguous = loops::contiguous_axis(static_cast<std::int32_t>(axes.size()));
    for (std::size_t k = 0; lanes >= 0 && k < axes.size(); ++k) {
        if (axes[k] == lanes && static_cast<std::int32_t>(k) != contiguous) {
            return static_cast<std::int32_t>(k);
        }
    }
    return -1;
}

// The buffer of the copy `stage`'s schedule asks its contraction to read an operand from: an identical one an earlier
// stage made, or a new one. A turned operand's copy is of its turned copy.
constexpr Done copy_buffer(Lowering& l, const graph::GroupShape& shape, Formed& stage) {
    const Schedule::Stage& ps = l.schedule.stages[static_cast<std::size_t>(stage.plan)];
    if (shape.fold < 0 || !graph::is_a_contraction(l.g.nodes[static_cast<std::size_t>(shape.fold)]) || (ps.relaid != 0 && ps.relaid != 1)) {
        return {}; // (any other operand is refused by name: `cache_read`)
    }
    const heinzel::Node& fold = l.g.nodes[static_cast<std::size_t>(shape.fold)];
    const std::int32_t v = fold.inputs[static_cast<std::size_t>(ps.relaid)];
    const Value& held = l.g.values[static_cast<std::size_t>(v)];
    const std::int32_t turned = ps.turned == ps.relaid ? detail::turned_axis(l.g, fold, ps.relaid, stage) : -1;
    if (ps.turned == ps.relaid && turned < 0) {
        return std::unexpected("heinzel::compile: a turned operand carries its stage's lanes, off its contiguous axis");
    }
    Buffer copied{.type = held.type, .shape = turned >= 0 ? detail::turned_shape(held.shape, turned) : held.shape, .value = v,
                  .blocked_axis = loops::contiguous_axis(held.shape.rank), .block = ps.relaid_block, .turned = turned};
    const std::int32_t from = l.at.buffer[static_cast<std::size_t>(v)];
    const bool constant = from >= 0 && l.out.buffers[static_cast<std::size_t>(from)].home == Home::Constant;
    if (constant) {
        const Buffer& source = l.out.buffers[static_cast<std::size_t>(from)];
        copied.home = Home::Constant, copied.ramp = source.ramp, copied.first = source.first;
    }
    for (std::size_t b = 0; b < l.out.buffers.size(); ++b) {
        if (l.out.buffers[b] == copied) {
            stage.copy = static_cast<std::int32_t>(b);
            return {};
        }
    }
    const std::expected<std::int32_t, Refusal> b = detail::add_buffer(l, copied);
    if (!b) {
        return std::unexpected(b.error());
    }
    stage.copy = *b, stage.makes_copy = !constant;
    return {};
}

// Stage `f`, which holds a contraction, reads its operand `operand` from a copy in blocks of `block`.
constexpr Done cache_read(Formed& f, std::int32_t operand, std::int32_t block) {
    if (operand != 0 && operand != 1) {
        return std::unexpected("heinzel::compile: a CacheRead relays operand 0 or 1 of its contraction");
    }
    f.relaid = operand;
    f.relaid_block = block;
    return {};
}

// The width of the stage's lanes (1: none).
constexpr std::int32_t lanes_of(const Formed& f) {
    for (const Node& n : f.skeleton.walk) {
        if (n.kind == NodeKind::Vector) {
            return n.step;
        }
    }
    return 1;
}

// The strip the stage's lanes read their axis in: the loop above its Vector over that axis, which hands it the lanes
// times their blocks (a width of 0: no lanes).
struct Strip {
    std::int32_t axis{-1};
    std::int32_t width{0};
    Tail tail{Tail::Clip};
};
constexpr Strip lane_strip(const Formed& f) {
    const auto& walk = f.skeleton.walk;
    for (std::size_t k = walk.size(); k-- > 0;) {
        if (walk[k].kind != NodeKind::Vector) {
            continue;
        }
        for (std::size_t j = k; j-- > 0;) {
            if (walk[j].kind == NodeKind::Loop && walk[j].axis == walk[k].axis) {
                return Strip{.axis = walk[k].axis, .width = walk[j].step, .tail = walk[j].tail};
            }
        }
        return Strip{.axis = walk[k].axis, .width = walk[k].step};
    }
    return Strip{};
}

// The copy stage walks time, then the blocked axis in chunks of a block, then the other axes, with the block's columns
// in lanes.
constexpr Formed copy_stage(const Formed& reader, ElemType type, const Shape& shape, std::int32_t from, std::int32_t into, std::int32_t along,
                            std::int32_t lanes) {
    Formed copy{};
    Stmt load{.kind = StmtKind::Load, .type = type, .computed_in = type};
    load.in[0] = Access{.source = Source::Buffer, .buffer = from};
    load.store = Access{.source = Source::Buffer, .buffer = into};
    for (std::int32_t axis = 0; axis < shape.rank; ++axis) {
        copy.axes.push_back(extent_at(shape, axis));
        load.in[0].buffer_axis[static_cast<std::size_t>(axis)] = load.store.buffer_axis[static_cast<std::size_t>(axis)] = axis;
    }
    copy.stmts.push_back(load);
    copy.inside[0] = true;
    if (shape.rank >= 1) {
        copy.skeleton.walk.push_back(Node{.kind = NodeKind::Loop, .axis = shape.rank - 1, .step = 1});
    }
    if (shape.rank >= 2) {
        copy.skeleton.walk.push_back(Node{.kind = NodeKind::Loop, .axis = along, .step = reader.relaid_block});
        for (std::int32_t axis = 0; axis < along; ++axis) {
            copy.skeleton.walk.push_back(Node{.kind = NodeKind::Loop, .axis = axis, .step = 1});
        }
        copy.skeleton.walk.push_back(Node{.kind = NodeKind::Loop, .axis = along, .step = lanes, .tail = Tail::Ones});
        copy.skeleton.walk.push_back(Node{.kind = NodeKind::Vector, .axis = along, .step = lanes});
    }
    copy.inner = static_cast<std::int32_t>(copy.skeleton.walk.size());
    return copy;
}

// Stage `at`'s requested copy, with a copy stage inserted before it where it reads the copy first. Its contraction's
// Load of the operand then reads the copy. Returns how many stages were inserted.
constexpr std::expected<std::int32_t, Refusal> relay(Lowering& l, std::size_t at) {
    Formed& f = l.stages[at];
    const std::int32_t product = detail::contraction_in(l.g, f);
    const heinzel::Node& n = l.g.nodes[static_cast<std::size_t>(f.stmts[static_cast<std::size_t>(product)].node)];
    const std::int32_t v = n.inputs[static_cast<std::size_t>(f.relaid)];
    const bool turned = f.relaid == f.turned; // (then it copies the turned copy, already read with its axes swapped)
    const Shape held = turned ? l.out.buffers[static_cast<std::size_t>(f.turn)].shape : l.g.values[static_cast<std::size_t>(v)].shape;
    const std::int32_t along = loops::contiguous_axis(held.rank);
    const std::int32_t lanes = detail::lanes_of(f);
    const std::int32_t from = turned ? f.turn : l.at.buffer[static_cast<std::size_t>(v)];
    auto axes = graph::contraction_axes(l.g, n, f.relaid);
    if (turned) {
        std::swap(axes[static_cast<std::size_t>(along)], axes[static_cast<std::size_t>(l.out.buffers[static_cast<std::size_t>(f.turn)].turned)]);
    }
    if (from < 0 || f.relaid_block < 1 || f.relaid_block % lanes != 0) {
        return std::unexpected("heinzel::compile: a relaid operand must be stored, in blocks of a whole number of lanes");
    }
    // Exactly one strip a block, as the planner relays (a block of several strips would need a loop over whole blocks
    // outside them); the copy walks its block in more than one Vector, and the reader in a strip the copy can match.
    const Strip strip = detail::lane_strip(f);
    if (strip.width != f.relaid_block) {
        return std::unexpected("heinzel::compile: a relaid operand is read in lanes, a block by each strip of them");
    }
    if (axes[static_cast<std::size_t>(along)] != strip.axis) {
        return std::unexpected("heinzel::compile: a relaid operand is blocked along the axis its lanes walk");
    }
    if (f.relaid_block <= lanes || (strip.tail != Tail::Clip && strip.tail != Tail::Masked)) {
        return std::unexpected("heinzel::compile: a relaid block spans more than one Vector, under a Clip or Masked strip");
    }
    for (const Node& above : f.skeleton.walk) { // (the strip is one of them, and so is a split loop)
        if (above.kind == NodeKind::Loop && above.axis == strip.axis && above.step % f.relaid_block != 0) {
            return std::unexpected("heinzel::compile: a relaid operand's lanes are tiled in whole blocks");
        }
    }
    const std::int32_t into = f.copy;
    std::int32_t inserted = 0;
    if (f.makes_copy) {
        if (l.stages.size() == kMaxStages) {
            return std::unexpected("heinzel::compile: more stages than kMaxStages; rebuild with -DHEINZEL_MAX_STAGES=N");
        }
        Formed copy = detail::copy_stage(f, l.g.values[static_cast<std::size_t>(v)].type, held, from, into, along, lanes);
        l.stages.insert(l.stages.begin() + static_cast<std::ptrdiff_t>(at), std::move(copy)); // (`f` is gone)
        inserted = 1;
    }
    Formed& reader = l.stages[at + static_cast<std::size_t>(inserted)];
    const Access& read = reader.stmts[static_cast<std::size_t>(product)].in[static_cast<std::size_t>(reader.relaid)];
    reader.stmts[static_cast<std::size_t>(read.local)].in[0].buffer = into;
    return inserted;
}

// The stage that fills a turned copy: one element at a time, reading the operand with the two axes swapped, walked in
// tiles of 8 x 8 of those two axes, the copy's contiguous one innermost, so that the 8 lines it reads a row apart stay
// in the first-level cache (8-way on zen2). Untiled, a 512 x 1024 operand ran 4-5x slower than tiled: its rows a page
// apart, every read a new line of one cache set (`Plan/probes/v5-score/README.md`).
constexpr Formed turn_stage(ElemType type, const Shape& turned, std::int32_t axis, std::int32_t from, std::int32_t into) {
    Formed copy{};
    Stmt load{.kind = StmtKind::Load, .type = type, .computed_in = type};
    load.in[0] = Access{.source = Source::Buffer, .buffer = from};
    load.store = Access{.source = Source::Buffer, .buffer = into};
    const std::int32_t contiguous = loops::contiguous_axis(turned.rank);
    for (std::int32_t a = 0; a < turned.rank; ++a) {
        copy.axes.push_back(extent_at(turned, a));
        load.store.buffer_axis[static_cast<std::size_t>(a)] = a;
        load.in[0].buffer_axis[static_cast<std::size_t>(a)] = a == axis ? contiguous : a == contiguous ? axis : a;
    }
    copy.stmts.push_back(load);
    copy.inside[0] = true;
    if (turned.rank >= 1) {
        copy.skeleton.walk.push_back(Node{.kind = NodeKind::Loop, .axis = turned.rank - 1, .step = 1});
    }
    constexpr std::int32_t tile = 8;
    for (std::int32_t a = 0; a + 1 < turned.rank; ++a) {
        copy.skeleton.walk.push_back(Node{.kind = NodeKind::Loop, .axis = a, .step = a == axis || a == contiguous ? tile : 1});
    }
    copy.skeleton.walk.push_back(Node{.kind = NodeKind::Loop, .axis = axis, .step = 1});
    copy.skeleton.walk.push_back(Node{.kind = NodeKind::Loop, .axis = contiguous, .step = 1});
    copy.inner = static_cast<std::int32_t>(copy.skeleton.walk.size());
    return copy;
}

// Stage f's turned operand, read from its copy turned around (a constant's is a constant, laid out turned when the
// program compiles): the copy's buffer, an identical one an earlier stage reads or a new one, and the contraction's
// Load of the operand redirected to it, with the two axes swapped. A stage fills a new copy (`turn_all`).
constexpr Done turn_read(Lowering& l, Formed& f) {
    const std::int32_t product = detail::contraction_in(l.g, f);
    if (product < 0 || (f.turned != 0 && f.turned != 1)) {
        return std::unexpected("heinzel::compile: a turned operand is operand 0 or 1 of its stage's contraction");
    }
    const heinzel::Node& n = l.g.nodes[static_cast<std::size_t>(f.stmts[static_cast<std::size_t>(product)].node)];
    const std::int32_t v = n.inputs[static_cast<std::size_t>(f.turned)];
    const std::int32_t axis = detail::turned_axis(l.g, n, f.turned, f);
    const std::int32_t from = l.at.buffer[static_cast<std::size_t>(v)];
    const std::int32_t local = f.stmts[static_cast<std::size_t>(product)].in[static_cast<std::size_t>(f.turned)].local;
    if (from < 0 || local < 0 || l.facts.through[static_cast<std::size_t>(v)].source != v) {
        return std::unexpected("heinzel::compile: a turned operand is stored and read at the point, as itself");
    }
    Access& load = f.stmts[static_cast<std::size_t>(local)].in[0];
    if (load.source != Source::Buffer || load.buffer != from || load.back != 0 || load.indexed_axis >= 0) {
        return std::unexpected("heinzel::compile: a turned operand is stored and read at the point, as itself");
    }
    if (axis < 0 || axis == l.g.values[static_cast<std::size_t>(v)].shape.rank - 1) { // (the time axis is no element axis to turn)
        return std::unexpected("heinzel::compile: a turned operand carries its stage's lanes, off its contiguous axis");
    }
    const Value& held = l.g.values[static_cast<std::size_t>(v)];
    const Buffer& source = l.out.buffers[static_cast<std::size_t>(from)];
    Buffer turned{.type = held.type, .shape = detail::turned_shape(held.shape, axis), .value = v, .turned = axis};
    if (source.home == Home::Constant) {
        turned.home = Home::Constant, turned.ramp = source.ramp, turned.first = source.first;
    }
    f.turn = -1;
    for (std::size_t b = 0; b < l.out.buffers.size() && f.turn < 0; ++b) {
        f.turn = l.out.buffers[b] == turned ? static_cast<std::int32_t>(b) : -1;
    }
    if (f.turn < 0) {
        const std::expected<std::int32_t, Refusal> b = detail::add_buffer(l, turned);
        if (!b) {
            return std::unexpected(b.error());
        }
        f.turn = *b, f.makes_turn = turned.home != Home::Constant;
    }
    const std::int32_t contiguous = loops::contiguous_axis(held.shape.rank);
    load.buffer = f.turn;
    for (std::int32_t& along : load.buffer_axis) {
        along = along == axis ? contiguous : along == contiguous ? axis : along;
    }
    std::swap(load.offset[static_cast<std::size_t>(axis)], load.offset[static_cast<std::size_t>(contiguous)]);
    return {};
}

// Every turned operand's read, before the stages' shape is checked: a lane then walks the copy along its contiguous axis.
constexpr Done turn_reads(Lowering& l) {
    for (Formed& f : l.stages) {
        if (const Done r = f.turned >= 0 ? detail::turn_read(l, f) : Done{}; !r) {
            return r;
        }
    }
    return {};
}

// Every new turned copy's stage, before its first reader and the relay that copies it.
constexpr Done turn_all(Lowering& l) {
    for (std::size_t at = 0; at < l.stages.size(); ++at) {
        if (!l.stages[at].makes_turn) {
            continue;
        }
        if (l.stages.size() == kMaxStages) {
            return std::unexpected("heinzel::compile: more stages than kMaxStages; rebuild with -DHEINZEL_MAX_STAGES=N");
        }
        const Buffer& turned = l.out.buffers[static_cast<std::size_t>(l.stages[at].turn)];
        const std::int32_t from = l.at.buffer[static_cast<std::size_t>(turned.value)];
        Formed copy = detail::turn_stage(turned.type, turned.shape, turned.turned, from, l.stages[at].turn);
        l.stages.insert(l.stages.begin() + static_cast<std::ptrdiff_t>(at), std::move(copy));
        ++at;
    }
    return {};
}

// Every requested copy, in run order.
constexpr Done relay_all(Lowering& l) {
    for (std::size_t at = 0; at < l.stages.size(); ++at) {
        if (l.stages[at].relaid < 0) {
            continue;
        }
        const std::expected<std::int32_t, Refusal> inserted = detail::relay(l, at);
        if (!inserted) {
            return std::unexpected(inserted.error());
        }
        at += static_cast<std::size_t>(*inserted);
    }
    return {};
}

} // namespace heinzel::loops::detail
