#pragma once

// Placing a schedule's bands. A band runs consecutive stages under one loop over an axis of the last, in chunks of
// rows, each stage keeping only the rows a chunk needs; a split band cuts its chunks into runs among workers. A band is
// placed once every stage and copy is formed: its layout is derived (`loops/band_layout.hpp`), every access is checked
// against it, and its buffers are cut to a chunk. A split band is held to its rules on the finished program, which
// holds the store a resuming fold was given.

#include <cstdint>
#include <expected>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/loops/band_layout.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/graph/regions.hpp"
#include "heinzel/loops/lower_stmts.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/schedule.hpp"

namespace heinzel::loops::detail {

// The schedule's bands are in group order, disjoint, and a chunk is one row or more.
constexpr Done check_bands(const Schedule& s) {
    for (std::size_t b = 0; b < s.bands.size(); ++b) {
        const Schedule::Band& band = s.bands[b];
        if (band.step < 1 || band.step > kMaxBandStep || (b > 0 && band.first < std::int64_t{s.bands[b - 1].first} + s.bands[b - 1].count)) {
            return std::unexpected("heinzel::compile: bands are in group order, disjoint, and a chunk is one row or more");
        }
    }
    return {};
}

// Does an access the lowering wrote walk `axis` as the band layout says it does?
constexpr bool agrees(const Access& a, const BandTouch& t, std::int32_t axis) {
    const std::size_t along = static_cast<std::size_t>(axis);
    if (a.buffer_axis[along] != t.value_axis) {
        return false;
    }
    return t.value_axis < 0 || (a.step[along] == t.step && a.offset[static_cast<std::size_t>(t.value_axis)] == t.offset);
}

// Checks every buffer a band stage's node reads an operand from, and every store, along the band's axis. It catches the
// accesses the lowering wrote and those the layout was derived from drifting apart.
constexpr Done check_band_accesses(const Graph& g, const graph::Facts& facts, const Schedule::Stage& ps, const Formed& f, std::int32_t axis) {
    const graph::GroupShape shape = graph::shape_of_group(g, ps);
    constexpr Refusal drift = "heinzel::compile: a band's accesses disagree with what its layout was derived from";
    for (const Stmt& st : f.stmts) {
        std::size_t k = 0;
        while (st.node >= 0 && k < ps.nodes.size() && ps.nodes[k] != st.node) {
            ++k;
        }
        if (st.node < 0 || k == ps.nodes.size()) {
            continue; // a Load of an operand: checked as its reader's operand
        }
        const heinzel::Node& n = g.nodes[static_cast<std::size_t>(st.node)];
        const bool table = graph::gathers(n) == Gather::Table;
        for (std::size_t o = 0; o < rules_of(n.op).arity; ++o) {
            Access a = st.in[table ? 1 - o : o]; // (a table read holds its storage first)
            if (a.source == Source::Local) {
                const Stmt& load = f.stmts[static_cast<std::size_t>(a.local)];
                a = load.kind == StmtKind::Load && load.node < 0 ? load.in[0] : Access{};
            }
            const std::expected<BandTouch, Refusal> t = loops::band_read(g, facts, ps, shape, k, o, axis);
            if (!t || (a.source == Source::Buffer && !(t->indirect && t->value_axis < 0 && graph::gathers(n) != Gather::None) && !detail::agrees(a, *t, axis))) {
                return std::unexpected(drift);
            }
        }
        const std::expected<BandTouch, Refusal> t = loops::band_store(g, ps, shape, k, axis);
        if (!t || (st.store.source == Source::Buffer && !detail::agrees(st.store, *t, axis))) {
            return std::unexpected(drift);
        }
    }
    return {};
}

// The program stage formed first from planned stage `plan` (-1: none).
constexpr std::int32_t formed_from(const Lowering& l, std::int32_t plan) {
    for (std::size_t k = 0; k < l.stages.size(); ++k) {
        if (l.stages[k].plan == plan) {
            return static_cast<std::int32_t>(k);
        }
    }
    return -1;
}

// Places band `b`: its layout, its stages' axes and leads, and its buffers cut to the rows a chunk keeps.
constexpr Done place_band(Lowering& l, std::size_t b, ReadsMemo& memo) {
    const Schedule::Band& band = l.schedule.bands[b];
    const BandLayout layout = loops::band_layout(l.g, l.facts, l.at, l.schedule.stages, band, memo);
    if (!layout.why_not.empty()) {
        return std::unexpected(layout.why_not);
    }
    BandInfo info{.first = detail::formed_from(l, band.first), .count = band.count, .step = band.step, .parallel = band.parallel};
    for (std::int32_t pos = 0; pos < band.count; ++pos) {
        Formed& f = l.stages[static_cast<std::size_t>(info.first + pos)];
        if (f.plan != band.first + pos) {
            return std::unexpected("heinzel::compile: a band's groups lower to consecutive nest groups");
        }
        if (f.split >= 0) {
            return std::unexpected("heinzel::compile: a group in a band is not split; its band is");
        }
        f.band_axis = layout.axis[static_cast<std::size_t>(pos)]; // (its accesses are held to the layout once normalization
        f.lead = layout.lead[static_cast<std::size_t>(pos)];      //  has given a resuming fold its store: `settle`)
    }
    // A chunk is at most the last stage's whole extent: one taller than that would only hold rows nobody makes.
    const Extent tail = l.stages[static_cast<std::size_t>(info.first + band.count - 1)].axes[static_cast<std::size_t>(band.axis)];
    const std::int32_t rows = is_literal(tail) && tail.encoded < band.step ? static_cast<std::int32_t>(tail.encoded) : band.step;
    for (const BandLayout::Kept& kept : layout.kept) {
        const std::int32_t index = l.at.buffer[static_cast<std::size_t>(kept.value)];
        Buffer& held = l.out.buffers[static_cast<std::size_t>(index)];
        set_extent(held.shape, kept.value_axis, literal_extent(rows + kept.lead));
        held.band = static_cast<std::int32_t>(b);
        held.kept_axis = kept.value_axis;
        held.maker = info.first + kept.maker;
        held.reader = info.first + kept.reader;
    }
    l.out.bands.push_back(info);
    return {};
}

constexpr Done place_bands(Lowering& l) {
    ReadsMemo memo{};
    for (std::size_t b = 0; b < l.schedule.bands.size(); ++b) {
        if (const Done r = detail::place_band(l, b, memo); !r) {
            return r;
        }
    }
    return {};
}

// A band split among workers: each worker's first chunk starts afresh, so nothing may be carried from chunk to chunk
// but the band buffers (no stage reads its own past along the band axis, held in a buffer or in registers, which hold
// it alone, `Buffer::only_past`, and no band buffer keeps silence). A stage running ahead of the last makes a worker's
// first rows again after the previous worker did, so it may write them only into band buffers.
constexpr Done check_split_band(const Program& p, std::size_t index) {
    constexpr Refusal why = "heinzel::compile: a split band carries only band buffers, and what runs ahead writes only those";
    const BandInfo& band = p.bands[index];
    for (const Buffer& b : p.buffers) {
        if (b.band == static_cast<std::int32_t>(index) && b.prefix > 0) {
            return std::unexpected(why);
        }
    }
    for (std::int32_t pos = 0; pos < band.count; ++pos) {
        const Stage& stage = p.stages[static_cast<std::size_t>(band.first + pos)];
        for (std::int32_t s = stage.first; s < stage.first + stage.count; ++s) {
            const Stmt& st = p.stmts[static_cast<std::size_t>(s)];
            if (st.store.source == Source::Buffer && stage.lead > 0 && p.buffers[static_cast<std::size_t>(st.store.buffer)].band < 0) {
                return std::unexpected(why);
            }
            for (const Access& read : st.in) {
                const Buffer& past = p.buffers[static_cast<std::size_t>(read.source == Source::Buffer ? read.buffer : 0)];
                if (read.source != Source::Buffer || read.back == 0 ||
                    (read.buffer_axis[static_cast<std::size_t>(stage.band_axis)] != past.shape.rank - 1 && !past.only_past)) {
                    continue;
                }
                for (std::int32_t w = stage.first; w < stage.first + stage.count; ++w) {
                    const Access& store = p.stmts[static_cast<std::size_t>(w)].store;
                    if (store.source == Source::Buffer && store.buffer == read.buffer) {
                        return std::unexpected(why);
                    }
                }
            }
        }
    }
    return {};
}

} // namespace heinzel::loops::detail
