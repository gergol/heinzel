#pragma once

// Running a band: consecutive stages under one loop over rows of the last stage's band axis, each stage running `lead`
// rows ahead of the last. A band buffer keeps one chunk's rows plus what the next chunk reads again, which is copied to
// its front between chunks (the carry), so every address stays affine and only the starting pointers move. Each stage
// is placed in locals per chunk: a placement kept in memory across chunks ran 2.3 times the instructions. A band split
// among workers gives each worker a run of chunks and band buffers of its own.

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

#include "heinzel/loops/program.hpp"
#include "heinzel/loops/run_stage.hpp"
#include "heinzel/loops/walk.hpp"
#include "heinzel/loops/workspace.hpp"

namespace heinzel::loops {

struct Rows {
    std::int64_t lo{0};
    std::int64_t hi{0};
};

// The rows of its band axis a stage runs in chunk `c`: from where it stopped in the chunk before (in the first chunk of
// a run, `first`, from the last stage's first row of that chunk) up to the last stage's chunk end plus the stage's
// lead.
inline Rows rows_in_chunk(std::int64_t c, std::int64_t step, std::int64_t lead, std::int64_t extent, std::int64_t first = 0) {
    const auto clipped = [extent](std::int64_t row) { return row < extent ? row : extent; };
    return Rows{clipped(c == first ? c * step : c * step + lead), clipped((c + 1) * step + lead)};
}

// A band buffer's rows are contiguous: one row is its stride along the kept axis.
constexpr std::int64_t row_bytes(const Buffer& b, const Sizes& sizes) {
    return loops::element_strides(b, sizes)[static_cast<std::size_t>(b.kept_axis)] * loops::bytes_of(b.type);
}
// ...buffer B's, a constant where no axis but time is a symbol (a row is one step along an axis that is not time, or
// one sample). A symbolic program reads its buffer's copy in `kBuffersOf`, never the program: a reference into the
// program that reaches a function left out of line (at -Os, or with asserts on) would put all of it in the binary.
template <const Program& P, std::size_t B>
std::int64_t row_bytes(const Sizes& sizes) {
    if constexpr (kKindOf<P> == Kind::Symbolic) {
        return row_bytes(kBuffersOf<P>[B], sizes);
    } else {
        constexpr std::int64_t row = row_bytes(P.buffers[B], Sizes{});
        return row;
    }
}

// Band B's buffers, by index: those whose `band` is B.
template <const Program& P, std::size_t B>
consteval auto band_buffers() {
    std::array<bool, P.buffers.size()> held{};
    for (std::size_t b = 0; b < held.size(); ++b) {
        held[b] = P.buffers[b].band == static_cast<std::int32_t>(B);
    }
    return held;
}
template <const Program& P, std::size_t B>
inline constexpr auto kBandBuffers = indices_where<band_buffers<P, B>()>();

// Buffer `b`'s place among band B's.
template <const Program& P, std::size_t B>
consteval std::size_t band_place(std::size_t b) {
    std::size_t at = 0;
    while (kBandBuffers<P, B>[at] != b) {
        ++at;
    }
    return at;
}
// Where each of band B's buffers starts a chunk: the row of its reader's chunk that is its row 0.
template <const Program& P, std::size_t B>
using BandBase = std::array<std::int64_t, kBandBuffers<P, B>.size()>;

// The carry, after a chunk and after a run: copies `count` rows from row `from` to row `-prefix` (the two ranges may
// overlap). Rows [-prefix, 0) are what a lag reads before the block: silence in a fresh workspace (`Workspace::reset`),
// and the previous run's last rows after a run.
template <const Program& P, std::size_t B>
void carry(auto& work, std::int64_t from, std::int64_t count, unsigned copy = 0) {
    constexpr std::int64_t prefix = P.buffers[B].prefix;
    constexpr Extent kept = extent_at(P.buffers[B].shape, P.buffers[B].kept_axis);
    const std::int64_t row = row_bytes<P, B>(work.sizes());
    const std::int64_t rows = loops::resolved(kept, work.sizes());
    assert(from >= -prefix && from + count <= rows && count <= rows + prefix);
    (void)rows;
    std::byte* at = work.bytes_of(std::integral_constant<std::size_t, B>{}, copy);
    std::memmove(at - prefix * row, at + from * row, static_cast<std::size_t>(count * row));
}

// Runs stage S of band B over rows `rows` of its band axis, placed in locals. Every slot moving along that axis starts
// that many rows on; in a band buffer the rows count from its row 0 (`base`), in worker copy `copy`.
template <const Program& P, std::size_t B, std::size_t S>
HEINZEL_LOOPS_INLINE void run_rows(const auto& feeds, auto& work, Rows rows, const BandBase<P, B>& base, unsigned copy) {
    constexpr std::int32_t A = P.stages[S].band_axis;
    if (rows.hi <= rows.lo) {
        return;
    }
    const Extents whole = resolved_axes<P, S>(work.sizes());
    Extents extent = whole;
    extent[static_cast<std::size_t>(A)] = rows.hi - rows.lo;
    StridesHeld<P, S> stride{};
    StillHeld<P, S> still{};
    WalkOf<P, S> walk{};
    place<P, S>(walk, stride, still, feeds, work, copy);
    each_at<indices_where<moves_along<P, S, A>()>()>([&] [[gnu::always_inline]] (auto slot) {
        constexpr auto buffer = static_cast<std::size_t>(access_of<P, S>(slot).buffer);
        std::int64_t from = rows.lo;
        if constexpr (P.buffers[buffer].band >= 0) {
            from -= base[band_place<P, B>(buffer)];
        }
        walk.at[slot] += from * (*walk.stride)[slot][static_cast<std::size_t>(A)];
    });
    run_body<P, S>(walk, extent, whole);
}

// Runs chunks [c0, c1) of band B in copy `copy` of its band buffers: each stage over its rows of a chunk, with the band
// buffers' carried rows copied to their front between chunks. Chunk c0 starts afresh.
template <const Program& P, std::size_t B, std::size_t... I>
HEINZEL_LOOPS_INLINE void run_chunks(const auto& feeds, auto& work, const std::array<std::int64_t, sizeof...(I)>& extent, std::int64_t c0,
                                     std::int64_t c1, unsigned copy) {
    constexpr const BandInfo& band = P.bands[B];
    constexpr std::size_t first = static_cast<std::size_t>(band.first);
    BandBase<P, B> base{};
    for (std::int64_t c = c0; c < c1; ++c) {
        const std::array<Rows, sizeof...(I)> rows{rows_in_chunk(c, band.step, P.stages[first + I].lead, extent[I], c0)...};
        each<kBandBuffers<P, B>.size()>([&] [[gnu::always_inline]] (std::size_t k) {
            base[k] = rows[static_cast<std::size_t>(P.buffers[kBandBuffers<P, B>[k]].reader) - first].lo;
        });
        (run_rows<P, B, first + I>(feeds, work, rows[I], base, copy), ...);
        if (c + 1 >= c1) {
            break;
        }
        each_at<kBandBuffers<P, B>>([&] [[gnu::always_inline]] (auto b) {
            constexpr const Buffer& held = P.buffers[b];
            constexpr std::size_t maker = static_cast<std::size_t>(held.maker) - first;
            constexpr std::size_t reader = static_cast<std::size_t>(held.reader) - first;
            const std::int64_t next = rows_in_chunk(c + 1, band.step, P.stages[first + reader].lead, extent[reader], c0).lo;
            const std::int64_t count = rows[maker].hi - next + held.prefix;
            if (count > 0) {
                carry<P, b>(work, next - rows[reader].lo - held.prefix, count, copy);
            }
        });
    }
}

// Runs band B: its chunks in order, or, split among workers (`BandInfo::parallel`), a run of consecutive chunks for
// each worker, with as many workers as its work pays for and the workspace holds band buffers for.
template <const Program& P, std::size_t B, std::size_t... I, class... Workers>
HEINZEL_LOOPS_INLINE void run_band(const auto& feeds, auto& work, std::index_sequence<I...>, Workers&... workers) {
    constexpr const BandInfo& band = P.bands[B];
    constexpr std::size_t first = static_cast<std::size_t>(band.first);
    const std::array<std::int64_t, sizeof...(I)> extent{
        loops::resolved(P.stages[first + I].axes[static_cast<std::size_t>(P.stages[first + I].band_axis)], work.sizes())...};
    const std::int64_t tail = extent[sizeof...(I) - 1];
    const std::int64_t chunks = (tail + band.step - 1) / band.step;
    if (tail == 0) { // the last stage has no rows: every other runs all of its own, read by nobody in the band
        const BandBase<P, B> base{};
        (run_rows<P, B, first + I>(feeds, work, Rows{0, extent[I]}, base, 0), ...);
        return;
    }
    if constexpr (band.parallel && sizeof...(Workers) == 1) {
        const std::int64_t all = (work_of<P, first + I>(resolved_axes<P, first + I>(work.sizes())) + ...);
        const unsigned held = (workers.size(), ...);
        const unsigned available = held < work.copies() ? held : work.copies();
        const Split split = split_of(chunks, 1, all, P.parallel_work, available);
        if (split.workers > 1) {
            auto task = [&feeds, &work, &extent, split, chunks](unsigned w) {
                const std::int64_t c0 = static_cast<std::int64_t>(w) * split.per;
                const std::int64_t c1 = c0 + split.per < chunks ? c0 + split.per : chunks;
                run_chunks<P, B, I...>(feeds, work, extent, c0, c1, w);
            };
            (workers.run(split.workers, task), ...);
            return;
        }
    }
    run_chunks<P, B, I...>(feeds, work, extent, 0, chunks, 0);
    // So that the next run continues the stream, each band buffer's last `prefix` rows of its value are copied to its
    // front: the maker's last rows, counted from the row where the last chunk's reader started. A split band keeps no
    // prefix.
    each_at<kBandBuffers<P, B>>([&] [[gnu::always_inline]] (auto b) {
        constexpr const Buffer& held = P.buffers[b];
        if constexpr (held.prefix > 0) {
            constexpr std::size_t maker = static_cast<std::size_t>(held.maker) - first;
            constexpr std::size_t reader = static_cast<std::size_t>(held.reader) - first;
            const std::int64_t base = rows_in_chunk(chunks - 1, band.step, P.stages[first + reader].lead, extent[reader], 0).lo;
            carry<P, b>(work, extent[maker] - base - held.prefix, held.prefix);
        }
    });
}

} // namespace heinzel::loops
