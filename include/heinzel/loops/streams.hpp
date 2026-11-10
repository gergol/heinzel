#pragma once

// What a loop-tree program carries from one run to the next, and whether that continues a stream. A run leaves, for
// the next run on the same workspace, the last `prefix` samples of every buffer whose past is read, the last `D`
// samples of every feed read `D` samples deep (its tail), and each stream band's last rows. A signal processed in
// blocks then gives the numbers of processing it whole, provided all of that state runs along the stream (`streams`):
// one-sample state broadcast over a longer signal would step once per run, not once per sample.

#include <array>
#include <cstddef>
#include <cstdint>

#include "heinzel/loops/program.hpp"

namespace heinzel::loops {

// The one runtime dimension a program may have: the symbol's id (-1: none), and whether every use of it is on
// time — the last axis of a buffer, or a loop that is its stage's time.
struct StreamSymbol {
    std::int32_t id{-1};
    bool on_time{true};
};

namespace stream_detail {

constexpr void claim(StreamSymbol& s, Extent e, bool on_time) {
    if (!is_symbol(e)) {
        return;
    }
    s.on_time = s.on_time && on_time && (s.id < 0 || s.id == symbol_of(e));
    s.id = symbol_of(e);
}

// Is axis `a` of stage `st` its time: some store walks its buffer's last axis along it, and no access walks
// another axis of a buffer along it?
constexpr bool is_time_axis(const Program& p, const Stage& st, std::size_t a) {
    bool stored = false;
    for (std::int32_t k = 0; k < st.count; ++k) {
        const Stmt& s = p.stmts[static_cast<std::size_t>(st.first + k)];
        for (std::size_t i = 0; i <= kMaxNodeInputs; ++i) {
            const Access& x = loops::access_in(s, i);
            if (x.source != Source::Buffer || x.buffer_axis[a] < 0) {
                continue;
            }
            if (x.buffer_axis[a] != p.buffers[static_cast<std::size_t>(x.buffer)].shape.rank - 1) {
                return false;
            }
            stored = stored || i == kMaxNodeInputs;
        }
    }
    return stored;
}

constexpr Extent last_extent(const Buffer& b) { return extent_at(b.shape, b.shape.rank - 1); }

} // namespace stream_detail

constexpr StreamSymbol stream_symbol(const Program& p) {
    StreamSymbol s{};
    for (const Buffer& b : p.buffers) {
        for (std::int32_t k = 0; k < b.shape.rank; ++k) {
            stream_detail::claim(s, extent_at(b.shape, k), k == b.shape.rank - 1);
        }
    }
    for (const Stage& st : p.stages) {
        for (std::size_t a = 0; a < st.axes.size(); ++a) {
            stream_detail::claim(s, st.axes[a], !is_symbol(st.axes[a]) || stream_detail::is_time_axis(p, st, a));
        }
    }
    return s;
}

// How many samples deep each named input is read into its past (0: not at all): the tail a workspace keeps of it.
constexpr std::array<std::int32_t, kMaxIO> feed_depths(const Program& p) {
    std::array<std::int32_t, kMaxIO> depth{};
    for (const Stmt& s : p.stmts) {
        for (const Access& x : s.in) {
            if (x.source == Source::Buffer && p.buffers[static_cast<std::size_t>(x.buffer)].home == Home::Feed) {
                std::int32_t& d = depth[static_cast<std::size_t>(p.buffers[static_cast<std::size_t>(x.buffer)].feed)];
                d = x.back > d ? x.back : d;
            }
        }
    }
    return depth;
}

// The stream's extent: the program's symbol if it has one (the first met, if there are several); otherwise the longest
// literal last extent of any buffer outside a band that has an axis at all; otherwise 1.
constexpr Extent stream_extent(const Program& p) {
    std::int64_t longest = 1;
    for (const Buffer& b : p.buffers) {
        if (b.shape.rank < 1) {
            continue;
        }
        const Extent last = stream_detail::last_extent(b);
        if (is_symbol(last)) {
            return last;
        }
        longest = b.band < 0 && literal_of(last) > longest ? literal_of(last) : longest;
    }
    return literal_extent(static_cast<std::int32_t>(longest));
}

// Does everything a run carries run along the stream?
constexpr bool streams(const Program& p) {
    const Extent stream = stream_extent(p);
    const std::array<std::int32_t, kMaxIO> depth = feed_depths(p);
    for (const Buffer& b : p.buffers) {
        const bool carried = b.home == Home::Feed ? depth[static_cast<std::size_t>(b.feed)] > 0 : b.band < 0 && b.prefix > 0;
        if (carried && (b.shape.rank < 1 || !same_extent(stream_detail::last_extent(b), stream))) {
            return false;
        }
    }
    for (const Buffer& b : p.buffers) { // a band buffer's rows run along its band's last stage's band axis
        if (b.band >= 0 && b.prefix > 0) {
            const BandInfo& band = p.bands[static_cast<std::size_t>(b.band)];
            const Stage& last = p.stages[static_cast<std::size_t>(band.first + band.count - 1)];
            if (!same_extent(last.axes[static_cast<std::size_t>(last.band_axis)], stream)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace heinzel::loops
