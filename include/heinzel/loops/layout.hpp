#pragma once

// What a loop-tree program fixes about its storage, worked out while it compiles: how many elements a buffer holds and
// how far a step along each of its axes moves, where each region of the workspace's one allocation stands, and which
// regions a run carries to the next. Each table is a variable template over the program, of exactly its entries, so
// that a run or a construction reads constants and never the program.
//
// The program's symbol decides how much is constant (`kKindOf`):
// - `Static`, no symbol: everything, the regions' places and the allocation's size included;
// - `Stream`, one symbol, on time: everything but the places, which `place` works out for a length when a workspace
//   is made, from the same table a static program is placed from while it compiles;
// - `Symbolic`, any other symbol (tests, through `loops::run`): each sample's size too, resolved when a workspace is
//   made from the buffers' shapes.

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "heinzel/loops/program.hpp"
#include "heinzel/loops/streams.hpp"

namespace heinzel::loops {

// ---- a buffer's storage --------------------------------------------------------------------------------

// How many elements a buffer holds — a relaid one to the end of its last, zero-padded block.
constexpr std::int64_t element_count(const Buffer& b, const Sizes& sizes) {
    std::int64_t n = 1;
    for (std::int32_t axis = 0; axis < b.shape.rank; ++axis) {
        const std::int64_t extent = loops::resolved(extent_at(b.shape, axis), sizes);
        n *= axis == b.blocked_axis ? (extent + b.block - 1) / b.block * b.block : extent;
    }
    return n;
}

// ...and how many of them are one sample: everything but the streamed axis, the shape's last.
constexpr std::int64_t elements_per_sample(const Buffer& b, const Sizes& sizes) {
    const std::int64_t samples = b.shape.rank >= 1 ? loops::resolved(extent_at(b.shape, b.shape.rank - 1), sizes) : 1;
    return samples > 0 ? element_count(b, sizes) / samples : 0;
}

// How many elements one step along each axis of a buffer moves. Sample-major: the last axis outermost, then the
// others in order; a relaid buffer puts the block index first among the element axes and the position within a
// block last, and its entry for the blocked axis is a walk by whole blocks, per column.
constexpr std::array<std::int64_t, kMaxRank> element_strides(const Buffer& b, const Sizes& sizes) {
    const Shape& s = b.shape;
    std::array<std::int64_t, kMaxRank> stride{};
    std::int64_t run = b.blocked_axis >= 0 ? b.block : 1;
    for (std::int32_t axis = s.rank - 2; axis >= 0; --axis) {
        if (axis != b.blocked_axis) {
            stride[static_cast<std::size_t>(axis)] = run;
            run *= loops::resolved(extent_at(s, axis), sizes);
        }
    }
    if (b.blocked_axis >= 0) {
        const std::int64_t columns = loops::resolved(extent_at(s, b.blocked_axis), sizes);
        stride[static_cast<std::size_t>(b.blocked_axis)] = run / b.block;
        run = run / b.block * ((columns + b.block - 1) / b.block * b.block);
    }
    if (s.rank >= 1) {
        stride[static_cast<std::size_t>(s.rank - 1)] = s.rank == 1 ? 1 : run;
    }
    return stride;
}

// Where element `e` of one sample of the value a buffer holds lies in it, `e` counted in the element order a sample is
// fed in (the contiguous axis fastest): at `e`; in a turned copy at the index with its two axes swapped back; in a
// relaid one at its place among the blocks. Of a static buffer.
constexpr std::int64_t element_offset(const Buffer& b, std::int64_t e) {
    if (b.blocked_axis < 0 && b.turned < 0) {
        return e;
    }
    const std::int32_t contiguous = loops::contiguous_axis(b.shape.rank);
    std::array<std::int64_t, kMaxRank> index{}; // per axis of the buffer
    for (std::int32_t axis = b.shape.rank - 2; axis >= 0; --axis) { // (the value's axes, the contiguous one first)
        const std::int32_t held = b.turned < 0 ? axis : axis == b.turned ? contiguous : axis == contiguous ? b.turned : axis;
        const std::int64_t extent = loops::resolved(extent_at(b.shape, held), Sizes{});
        index[static_cast<std::size_t>(held)] = e % extent;
        e /= extent;
    }
    const std::array<std::int64_t, kMaxRank> stride = element_strides(b, Sizes{});
    std::int64_t at = 0;
    for (std::int32_t axis = 0; axis + 1 < b.shape.rank; ++axis) {
        const std::int64_t i = index[static_cast<std::size_t>(axis)];
        at += axis == b.blocked_axis ? (i - i % b.block) * stride[static_cast<std::size_t>(axis)] + i % b.block
                                     : i * stride[static_cast<std::size_t>(axis)];
    }
    return at;
}

// The bytes of one sample, every axis but the last: the same whatever the length when only the last is a symbol.
constexpr std::int64_t sample_bytes(const Buffer& b, const Sizes& sizes) {
    std::int64_t n = 1;
    for (std::int32_t axis = 0; axis + 1 < b.shape.rank; ++axis) {
        const std::int64_t extent = loops::resolved(extent_at(b.shape, axis), sizes);
        n *= axis == b.blocked_axis ? (extent + b.block - 1) / b.block * b.block : extent;
    }
    return n * loops::bytes_of(b.type);
}
constexpr std::int64_t bytes_per_sample(const Buffer& b) { return sample_bytes(b, Sizes{}); }

// ---- tables --------------------------------------------------------------------------------------------

template <std::size_t N>
consteval std::size_t count_of(const std::array<bool, N>& keep) {
    std::size_t n = 0;
    for (const bool k : keep) {
        n += k ? 1 : 0;
    }
    return n;
}
// The indices where `Keep` holds: a table of exactly its entries.
template <std::array Keep>
consteval auto indices_where() {
    std::array<std::size_t, count_of(Keep)> at{};
    std::size_t n = 0;
    for (std::size_t i = 0; i < Keep.size(); ++i) {
        if (Keep[i]) {
            at[n++] = i;
        }
    }
    return at;
}

// ---- the three kinds of program ------------------------------------------------------------------------

enum class Kind : std::uint8_t { Static, Stream, Symbolic };

template <const Program& P>
inline constexpr StreamSymbol kSymbolOf = stream_symbol(P);
template <const Program& P>
inline constexpr Kind kKindOf = kSymbolOf<P>.id < 0 ? Kind::Static : kSymbolOf<P>.on_time ? Kind::Stream : Kind::Symbolic;

// The sizes of a stream program run over `n` samples.
template <const Program& P>
constexpr Sizes sizes_of_length(std::int64_t n) {
    Sizes sizes{};
    sizes[static_cast<std::size_t>(kSymbolOf<P>.id)] = n;
    return sizes;
}

// How many inputs a run needs pointers for: one past the last feed the program reads.
template <const Program& P>
inline constexpr std::size_t kFeedsOf = [] {
    std::size_t n = 0;
    for (const Buffer& b : P.buffers) {
        n = b.home == Home::Feed && static_cast<std::size_t>(b.feed) + 1 > n ? static_cast<std::size_t>(b.feed) + 1 : n;
    }
    return n;
}();

// Does a split band keep buffers per worker? Without one, a workspace keeps one copy of everything.
template <const Program& P>
inline constexpr bool kSplitsABandOf = [] {
    for (const BandInfo& band : P.bands) {
        if (band.parallel) {
            return true;
        }
    }
    return false;
}();

// ---- the regions of the arena --------------------------------------------------------------------------

// A region of the arena: a workspace buffer's (the past it keeps before its block, then its block), or a feed's tail
// (only the samples of its past that a run reads before its block).
struct Piece {
    std::int32_t buffer{-1};      // the workspace buffer, or the feed's buffer
    std::int32_t feed{-1};        // a tail: the feed it continues
    ElemType type{ElemType::Unknown};
    std::int32_t prefix{0};       // samples before the block: the past a lag reads (a tail: all of it)
    std::int64_t sample_bytes{0}; // one sample (`Symbolic`: resolved when a workspace is made)
    Extent last{};                // samples in a run: the buffer's last extent (a tail: the feed's)
    std::int32_t last_block{1};   // a buffer relaid along its last axis pads it to whole blocks
    bool band{false};             // a band buffer: its band carries its rows
    bool split{false};            //   in a split band: one copy per worker
    bool ramp{false};             // holds its element index within a sample, filled when the workspace is made
    bool padded{false};           // relaid: its padding is read, so its block starts zeroed
    bool only_past{false};        // a state in registers: its past alone, closed into in place (no block, no carry)
};

// A piece's block, in bytes (a tail has none).
constexpr std::int64_t block_bytes(const Piece& p, const Sizes& sizes) {
    const std::int64_t n = loops::resolved(p.last, sizes);
    return p.feed >= 0 || p.only_past ? 0 : p.sample_bytes * ((n + p.last_block - 1) / p.last_block * p.last_block);
}

namespace layout_detail {

constexpr bool keeps_a_tail(const Buffer& b, const std::array<std::int32_t, kMaxIO>& depth) {
    return b.home == Home::Feed && depth[static_cast<std::size_t>(b.feed)] > 0;
}

constexpr std::size_t piece_count(const Program& p) {
    const std::array<std::int32_t, kMaxIO> depth = feed_depths(p);
    std::size_t n = 0;
    for (const Buffer& b : p.buffers) {
        n += b.home == Home::Workspace || keeps_a_tail(b, depth) ? 1 : 0;
    }
    return n;
}

constexpr Piece piece_of(const Program& p, std::size_t buffer, std::int32_t feed, std::int32_t prefix) {
    const Buffer& b = p.buffers[buffer];
    const bool held = feed < 0;
    return Piece{.buffer = static_cast<std::int32_t>(buffer),
                 .feed = feed,
                 .type = b.type,
                 .prefix = prefix,
                 .sample_bytes = bytes_per_sample(b),
                 .last = b.shape.rank >= 1 ? extent_at(b.shape, b.shape.rank - 1) : literal_extent(1),
                 .last_block = held && b.shape.rank >= 1 && b.blocked_axis == b.shape.rank - 1 ? b.block : 1,
                 .band = held && b.band >= 0,
                 .split = held && b.band >= 0 && p.bands[static_cast<std::size_t>(b.band)].parallel,
                 .ramp = held && b.ramp,
                 .padded = held && b.blocked_axis >= 0,
                 .only_past = held && b.only_past};
}

// The pieces in placement order: every workspace buffer, then every feed read in its past, each in buffer order.
template <const Program& P>
consteval auto pieces_of() {
    std::array<Piece, piece_count(P)> out{};
    const std::array<std::int32_t, kMaxIO> depth = feed_depths(P);
    std::size_t n = 0;
    for (std::size_t i = 0; i < P.buffers.size(); ++i) {
        if (P.buffers[i].home == Home::Workspace) {
            out[n++] = piece_of(P, i, -1, P.buffers[i].prefix);
        }
    }
    for (std::size_t i = 0; i < P.buffers.size(); ++i) {
        if (keeps_a_tail(P.buffers[i], depth)) {
            out[n++] = piece_of(P, i, P.buffers[i].feed, depth[static_cast<std::size_t>(P.buffers[i].feed)]);
        }
    }
    return out;
}

} // namespace layout_detail

template <const Program& P>
inline constexpr auto kPiecesOf = layout_detail::pieces_of<P>();

// Each buffer's piece (-1: a feed, which the workspace does not hold).
template <const Program& P>
inline constexpr auto kPieceOf = [] {
    std::array<std::int32_t, P.buffers.size()> piece{};
    piece.fill(-1);
    for (std::size_t k = 0; k < kPiecesOf<P>.size(); ++k) {
        if (kPiecesOf<P>[k].feed < 0) {
            piece[static_cast<std::size_t>(kPiecesOf<P>[k].buffer)] = static_cast<std::int32_t>(k);
        }
    }
    return piece;
}();

// Feed `f`'s tail (-1: a feed read in no past).
template <std::size_t N>
constexpr std::int32_t tail_of(const std::array<Piece, N>& pieces, std::size_t f) {
    for (std::size_t k = 0; k < N; ++k) {
        if (pieces[k].feed == static_cast<std::int32_t>(f)) {
            return static_cast<std::int32_t>(k);
        }
    }
    return -1;
}

// The pieces a run carries to the next (`Workspace::carry`): every tail, and every buffer outside a band that keeps a
// past. A band buffer's past is carried by its band; a reset zeroes it in every copy. A state in registers is closed
// into its past in place, so nothing carries it; a reset zeroes it.
constexpr bool is_carried(const Piece& p) { return p.prefix > 0 && !p.band && !p.only_past; }
constexpr bool is_only_past(const Piece& p) { return p.only_past; }
constexpr bool is_a_band_past(const Piece& p) { return p.prefix > 0 && p.band; }
constexpr bool is_a_ramp(const Piece& p) { return p.ramp; }
constexpr bool is_padded(const Piece& p) { return p.padded; }

template <const Program& P, bool (*Keep)(const Piece&)>
consteval auto pieces_kept() {
    std::array<bool, kPiecesOf<P>.size()> keep{};
    for (std::size_t k = 0; k < keep.size(); ++k) {
        keep[k] = Keep(kPiecesOf<P>[k]);
    }
    return keep;
}
template <const Program& P>
inline constexpr auto kCarriedOf = indices_where<pieces_kept<P, is_carried>()>();
template <const Program& P>
inline constexpr auto kBandPastsOf = indices_where<pieces_kept<P, is_a_band_past>()>();
template <const Program& P>
inline constexpr auto kOnlyPastsOf = indices_where<pieces_kept<P, is_only_past>()>();
template <const Program& P>
inline constexpr auto kRampsOf = indices_where<pieces_kept<P, is_a_ramp>()>();
template <const Program& P>
inline constexpr auto kPaddedOf = indices_where<pieces_kept<P, is_padded>()>();

// ---- constants ---------------------------------------------------------------------------------------------

// A constant's sample lies on a cache line, as a workspace block does (pieces are packed at 64 B): a relaid one read
// in blocks off the line split a load in two every other strip (a 128 x 128 weight ran 7 % slower on zen2).
inline constexpr std::size_t kConstantAlignment = 64;

// A ramp's one sample, `0 … N − 1` in its element type: two ramps of the same numbers share it.
template <ElemType E, std::size_t N>
alignas(kConstantAlignment) inline constexpr auto kRampOf = [] {
    std::array<element_t<E>, N> sample{};
    for (std::size_t e = 0; e < N; ++e) {
        sample[e] = static_cast<element_t<E>>(e);
    }
    return sample;
}();
// A constant's one sample that no other buffer shares: a constant tensor's numbers (from the program's pool, which the
// frontend rounded to its element type), or a relaid or turned ramp's, each at its place; a relaid one's padding is
// zero. Read at
// constant indices only, so no run-time code holds a reference into the program.
template <const Program& P, std::size_t B>
alignas(kConstantAlignment) inline constexpr auto kSampleOf = [] {
    constexpr Buffer b = P.buffers[B];
    std::array<element_t<b.type>, static_cast<std::size_t>(bytes_per_sample(b) / loops::bytes_of(b.type))> sample{};
    for (std::int64_t e = 0; e < numbers_per_sample(b.shape); ++e) {
        const double number = b.first >= 0 ? std::bit_cast<double>(P.constants[static_cast<std::size_t>(b.first + e)]) : static_cast<double>(e);
        sample[static_cast<std::size_t>(element_offset(b, e))] = static_cast<element_t<b.type>>(number);
    }
    return sample;
}();
template <const Program& P, std::size_t B>
consteval const auto& numbers_of() {
    if constexpr (P.buffers[B].ramp && P.buffers[B].blocked_axis < 0 && P.buffers[B].turned < 0) {
        return kRampOf<P.buffers[B].type, static_cast<std::size_t>(bytes_per_sample(P.buffers[B]) / loops::bytes_of(P.buffers[B].type))>;
    } else {
        return kSampleOf<P, B>;
    }
}
// Buffer B's numbers, a constant's (`Home::Constant`) or a filled ramp's: one sample.
template <const Program& P, std::size_t B>
inline constexpr const auto& kConstantOf = numbers_of<P, B>();
// ...and where each buffer's constant lies (null: it has none), for a run-time index.
template <const Program& P, std::size_t B>
constexpr const void* constant_at() {
    if constexpr (P.buffers[B].home == Home::Constant) {
        return kConstantOf<P, B>.data();
    } else {
        return nullptr;
    }
}
template <const Program& P>
inline constexpr auto kConstantsOf = []<std::size_t... B>(std::index_sequence<B...>) {
    return std::array<const void*, P.buffers.size()>{constant_at<P, B>()...};
}(std::make_index_sequence<P.buffers.size()>{});

// A `Symbolic` program's buffers, whose shapes size its samples when a workspace is made.
template <const Program& P>
inline constexpr auto kBuffersOf = [] {
    std::array<Buffer, P.buffers.size()> buffers{};
    for (std::size_t i = 0; i < buffers.size(); ++i) {
        buffers[i] = P.buffers[i];
    }
    return buffers;
}();

// ---- outputs in the caller's memory -----------------------------------------------------------------------

// May an output's block be the caller's (`Plan/18-zero-copy/02`)? A workspace buffer that keeps no past, neither
// generated (a ramp filled once, a constant of no number), a band buffer (its rows) nor relaid (its padding): a run
// writes it whole and reads only what it wrote.
constexpr bool in_place(const Buffer& b) {
    return b.home == Home::Workspace && b.prefix == 0 && !b.ramp && b.first < 0 && b.band < 0 && b.blocked_axis < 0;
}

template <const Program& P>
consteval auto in_place_buffers() {
    std::array<bool, P.buffers.size()> keep{};
    for (const std::int32_t out : P.outputs) {
        keep[static_cast<std::size_t>(out)] = in_place(P.buffers[static_cast<std::size_t>(out)]);
    }
    return keep;
}
// The buffers an output may write in the caller's memory, and each buffer's entry among them (-1: none).
template <const Program& P>
inline constexpr auto kInPlaceOf = indices_where<in_place_buffers<P>()>();
template <const Program& P>
inline constexpr auto kInPlaceSlotOf = [] {
    std::array<std::int32_t, P.buffers.size()> slot{};
    slot.fill(-1);
    for (std::size_t k = 0; k < kInPlaceOf<P>.size(); ++k) {
        slot[kInPlaceOf<P>[k]] = static_cast<std::int32_t>(k);
    }
    return slot;
}();

// ---- placement --------------------------------------------------------------------------------------------

inline constexpr std::size_t kPage = 4096;
inline constexpr std::size_t kStagger = 5 * 64;  // cache-line aligned, never a page apart
inline constexpr std::size_t kLine = 64;                      // a packed block's alignment

// The most bytes a static program's arena may take and stay inside its workspace, packed: no allocation, and an
// `Executor` on the stack keeps it there. Within one page no two blocks can 4K-alias each other, so packing loses
// nothing the stagger buys inside the arena; where it lies against the caller's buffers is luck, as for any buffer of
// the caller's. `-DHEINZEL_MAX_INLINE_ARENA=N` moves the line (0: always the heap), the same in every translation unit:
// it changes a workspace's layout.
#ifndef HEINZEL_MAX_INLINE_ARENA
#define HEINZEL_MAX_INLINE_ARENA 4096
#endif
inline constexpr std::size_t kMaxInlineArena = HEINZEL_MAX_INLINE_ARENA;

// Where each piece's block stands from the arena's first byte, how far apart a split band buffer's copies stand, and
// the arena's size: exactly the pieces and the padding their placement needs.
template <std::size_t N>
struct Placement {
    std::array<std::size_t, N> at{};
    std::array<std::size_t, N> copy_bytes{};
    std::size_t size{0};
};

// Because of 4K aliasing, placement is a performance fact, pinned by `loops_workspace`'s goldens: each piece's past,
// then its block starting mid-page, each staggered from the last, and a split band's buffers once per copy, a page and
// a stagger apart. Packed at cache-line alignment instead, a block was up to 1.26 times slower where a caller's feed
// aliased with it (`Plan/probes/15-zero-overhead/placement/`), so only an arena inside its workspace, within a page,
// is `packed`: each block at the next line after its past. `guard` bytes stay free before every piece and after the
// last (`canaried`'s canaries: 0 for a plain workspace).
template <std::size_t N>
constexpr Placement<N> place(const std::array<Piece, N>& pieces, unsigned copies, std::size_t guard, const Sizes& sizes, bool packed = false) {
    Placement<N> out{};
    std::size_t bytes = 0;
    for (std::size_t k = 0; k < N; ++k) {
        const Piece& p = pieces[k];
        const auto prefix = static_cast<std::size_t>(p.prefix * p.sample_bytes);
        const auto block = static_cast<std::size_t>(block_bytes(p, sizes));
        bytes += guard + prefix;
        if (packed) {
            bytes = (bytes + kLine - 1) / kLine * kLine;
        } else {
            const std::size_t want = (kPage / 2 + k * kStagger) % kPage;
            bytes += (want + kPage - bytes % kPage) % kPage;
        }
        out.at[k] = bytes;
        out.copy_bytes[k] = p.split ? (prefix + block + kPage - 1) / kPage * kPage + kStagger : 0;
        bytes += block + (p.split ? (copies - 1) * out.copy_bytes[k] : 0);
    }
    out.size = N > 0 ? bytes + guard : 0;
    return out;
}

// Does a static program keep its arena inside its workspace? Measured without guards, so that a `canaried` workspace
// keeps the storage its plain twin keeps.
template <const Program& P>
inline constexpr bool kInlineOf =
    kKindOf<P> == Kind::Static && !kSplitsABandOf<P> && place(kPiecesOf<P>, 1, 0, Sizes{}, true).size <= kMaxInlineArena;

// A `Static` program's placement, with `Copies` copies of a split band's buffers: packed where its arena is inline.
template <const Program& P, unsigned Copies, std::size_t Guard>
inline constexpr Placement<kPiecesOf<P>.size()> kPlacementOf = place(kPiecesOf<P>, Copies, Guard, Sizes{}, kInlineOf<P>);

} // namespace heinzel::loops
