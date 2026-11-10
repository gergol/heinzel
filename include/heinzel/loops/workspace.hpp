#pragma once

// Storage for a loop-tree program: one arena holds every workspace buffer in its own element type, inside the workspace
// for a small static program (`kInlineOf`), else one allocation made once. A run allocates nothing, and a feed is read
// where the caller left it. Where everything stands is the program's
// (`loops/layout.hpp`): a workspace holds only what its program leaves open, the places of a length's regions or a
// symbolic program's sizes, and reads every other fact as a constant.

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <span>
#include <type_traits>
#include <utility>

#include "heinzel/loops/layout.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel::loops {

// One pointer per named input, in graph order, each at the input's element type, sample-major.
using Feeds = std::array<const void*, kMaxIO>;

// A run of bytes: a buffer's region (its past, its block and a relaid buffer's padding), or a feed's tail.
struct ByteRange {
    std::byte* begin;
    std::byte* end;
};

// ---- the last bits of the kept stages (`docs/knob-math.md`) -------------------------------------------------

constexpr std::size_t watch_count(const Stage& s) {
    std::size_t n = 0;
    for (const std::uint64_t word : s.watched) {
        n += static_cast<std::size_t>(std::popcount(word));
    }
    return n;
}

// Per stage, its first slot among the last bits (the kept stages' one after another, a slot per watched feed) and its
// place among the kept stages; and how many of each there are.
struct KeptPlaces {
    std::array<std::size_t, kMaxStages> first{};
    std::array<std::size_t, kMaxStages> ordinal{};
    std::size_t stages{0};
    std::size_t slots{0};
};
template <const Program& P>
inline constexpr KeptPlaces kKeptOf = [] {
    KeptPlaces k{};
    for (std::size_t s = 0; s < P.stages.size(); ++s) {
        if (P.stages[s].kept) {
            k.first[s] = k.slots;
            k.ordinal[s] = k.stages++;
            k.slots += watch_count(P.stages[s]);
        }
    }
    return k;
}();

// Stage S's watched feeds, in the feeds' order.
template <const Program& P, std::size_t S>
inline constexpr auto kWatchedOf = [] {
    std::array<std::size_t, watch_count(P.stages[S])> feeds{};
    std::size_t n = 0;
    for (std::size_t f = 0; f < 64 * kWatchWords; ++f) {
        if ((P.stages[S].watched[f / 64] >> (f % 64)) & 1u) {
            feeds[n++] = f;
        }
    }
    return feeds;
}();

// Feed F's bytes, its element type's (a watched feed is a Param the stage reads, so it has a buffer).
template <const Program& P, std::size_t F>
inline constexpr std::int32_t kFeedBytesOf = [] {
    std::int32_t bytes = 0;
    for (const Buffer& b : P.buffers) {
        bytes = b.home == Home::Feed && b.feed == static_cast<std::int32_t>(F) ? loops::bytes_of(b.type) : bytes;
    }
    return bytes;
}();

namespace detail {

// State carries from one run to the next (`docs/decisions.md`, Decision 52): after a run, `carry` moves the last
// `prefix` samples of every buffer whose past is read in front of its block, and keeps the last samples of every
// feed read in its past as its tail, which the next run's head reads (`loops/run_stage.hpp`); a stream band carries
// its own rows (`loops/run_band.hpp`). `reset` starts over from silence. `Copies`: how many workers may run a split
// band at once, each with band buffers of its own (`loops::Workspace` makes it 1 without a split band). `Guard`: free
// bytes before every region and after the last, where `canaried` keeps its canaries (0 for a plain workspace).
template <const Program& P, unsigned Copies, std::size_t Guard = 0>
class Workspace {
    static constexpr Kind kind = kKindOf<P>;
    static constexpr std::size_t kPieces = kPiecesOf<P>.size();
    static constexpr bool kInline = kInlineOf<P>;
    static constexpr std::size_t kInPlace = kInPlaceOf<P>.size();
    static constexpr std::size_t kInlineBytes = [] {
        if constexpr (kInline) {
            return kPlacementOf<P, Copies, Guard>.size;
        } else {
            return std::size_t{0};
        }
    }();

public:
    // A static program's arena; for any other, nothing until it is made for sizes.
    Workspace() {
        if constexpr (kind == Kind::Static) {
            make(Sizes{});
        }
    }
    explicit Workspace(const Sizes& sizes) { make(sizes); }
    // A stream program's, for runs of up to `n` samples.
    explicit Workspace(std::int64_t n) requires(kKindOf<P> == Kind::Stream) { make(sizes_of_length<P>(n)); }

    // A move steals the arena and leaves the source empty, as a stream program's workspace is before it is made; an arena
    // inside the workspace is copied, and the source keeps it. The kept stages' last bits go with the arena; a source
    // left empty keeps its own, which nothing reads: it runs only once made again, or moved into, and both replace them.
    Workspace(Workspace&& from) noexcept
        : bytes_(static_cast<decltype(bytes_)&&>(from.bytes_)), bound_(from.bound_), state_(from.state_), last_(from.last_) {
        from.state_ = State{};
    }
    Workspace& operator=(Workspace&& from) noexcept {
        if (this != &from) {
            bytes_ = static_cast<decltype(bytes_)&&>(from.bytes_);
            bound_ = from.bound_;
            state_ = from.state_;
            last_ = from.last_;
            from.state_ = State{};
        }
        return *this;
    }

    static constexpr unsigned copies() { return Copies; }
    Sizes sizes() const {
        if constexpr (kind == Kind::Static) {
            return Sizes{};
        } else if constexpr (kind == Kind::Stream) {
            return sizes_of_length<P>(state_.length);
        } else {
            return state_.sizes;
        }
    }
    // The arena from its first byte; none for no bytes, or when empty.
    std::byte* arena() {
        if constexpr (!kInline) {
            return bytes_.get();
        } else if constexpr (kInlineBytes > 0) {
            return bytes_.bytes.data();
        } else {
            return nullptr;
        }
    }
    std::size_t size_in_bytes() const {
        if constexpr (kInline) {
            return kInlineBytes;
        } else {
            return bytes_ ? placement().size : 0;
        }
    }

    // A buffer's block, in copy `copy` of a split band's buffers; the caller's, where `bind` gave one; a constant's one
    // sample, in read-only data (a write there faults, and `well_formed` refuses a store); a feed's is the arena's first
    // byte. The run-time index is for tests; the runner names its buffer as a constant.
    std::byte* bytes_of(std::size_t buffer, unsigned copy = 0) {
        if (const void* constant = kConstantsOf<P>[buffer]) {
            return const_cast<std::byte*>(static_cast<const std::byte*>(constant));
        }
        if constexpr (kInPlace > 0) {
            if (const std::int32_t k = kInPlaceSlotOf<P>[buffer]; k >= 0 && bound_[static_cast<std::size_t>(k)] != nullptr) {
                return bound_[static_cast<std::size_t>(k)];
            }
        }
        return at(kPieceOf<P>[buffer], copy);
    }
    template <std::size_t B>
    std::byte* bytes_of(std::integral_constant<std::size_t, B>, unsigned copy = 0) {
        if constexpr (P.buffers[B].home == Home::Constant) {
            return const_cast<std::byte*>(reinterpret_cast<const std::byte*>(kConstantOf<P, B>.data()));
        } else if constexpr (kInPlaceSlotOf<P>[B] >= 0) {
            std::byte* bound = bound_[static_cast<std::size_t>(kInPlaceSlotOf<P>[B])];
            return bound != nullptr ? bound : at(kPieceOf<P>[B], copy);
        } else {
            return at(kPieceOf<P>[B], copy);
        }
    }
    // Output buffer B's block in the caller's memory, or with null its own again: only a buffer `kInPlaceOf` names. The
    // caller holds its whole block there, and makes sure that nothing a run reads overlaps it.
    template <std::size_t B>
    void bind(std::integral_constant<std::size_t, B>, void* block) {
        static_assert(kInPlaceSlotOf<P>[B] >= 0, "heinzel::loops::Workspace::bind: only an output kInPlaceOf names is written in the caller's memory");
        if constexpr (kInPlaceSlotOf<P>[B] >= 0) { // (so the assert is the one error)
            bound_[static_cast<std::size_t>(kInPlaceSlotOf<P>[B])] = static_cast<std::byte*>(block);
        }
    }
    // A buffer as its element type, which the caller read off the program.
    template <class T>
    T* data(std::size_t buffer, unsigned copy = 0) { return reinterpret_cast<T*>(bytes_of(buffer, copy)); }

    // A buffer's bytes, the past before its block (its prefix) and a relaid buffer's padding included.
    ByteRange region(std::size_t buffer, unsigned copy = 0) {
        const std::int32_t k = kPieceOf<P>[buffer];
        std::byte* block = k < 0 ? at(k) : bytes_of(buffer, copy);
        return k < 0 ? ByteRange{block, block} : ByteRange{block - past_bytes(piece(k)), block + block_bytes(piece(k), built())};
    }
    // Feed `f`'s tail (empty: a feed read in no past), ending where its sample 0 would stand.
    ByteRange tail(std::size_t f) {
        const std::int32_t k = tail_of(kPiecesOf<P>, f);
        std::byte* end = at(k);
        return k < 0 ? ByteRange{end, end} : ByteRange{end - past_bytes(piece(k)), end};
    }
    std::byte* tail_end(std::size_t f) {
        const std::int32_t k = tail_of(kPiecesOf<P>, f);
        return k < 0 ? nullptr : at(k);
    }
    template <std::size_t F>
    std::byte* tail_end(std::integral_constant<std::size_t, F>) {
        constexpr std::int32_t k = tail_of(kPiecesOf<P>, F);
        return k < 0 ? nullptr : at(k);
    }

    // After a run over `feeds`: every carried region continues from the run's last samples (see above the class).
    template <std::size_t N>
    void carry(const std::array<const void*, N>& feeds) {
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (carry_piece<kCarriedOf<P>[I]>(feeds), ...);
        }(std::make_index_sequence<kCarriedOf<P>.size()>{});
    }

    // Before kept stage S runs: has a Param it watches changed since it last ran, bit for bit (-0 against 0, a NaN's
    // payload), or has it not run since the workspace was made? Each watched Param is compared with no branch of its
    // own; its bits and the flag are stored only where the stage runs, so a stage at rest stores nothing (stored
    // every run, they were most of the check's cost).
    template <std::size_t S, std::size_t N>
    bool changed(const std::array<const void*, N>& feeds) {
        constexpr std::size_t first = kKeptOf<P>.first[S];
        constexpr std::size_t k = kKeptOf<P>.ordinal[S];
        bool differs = !last_.ran[k];
        [&]<std::size_t... W>(std::index_sequence<W...>) {
            ((differs |= bits_of<kWatchedOf<P, S>[W]>(feeds) != last_.bits[first + W]), ...);
        }(std::make_index_sequence<kWatchedOf<P, S>.size()>{});
        if (differs) {
            [&]<std::size_t... W>(std::index_sequence<W...>) {
                ((last_.bits[first + W] = bits_of<kWatchedOf<P, S>[W]>(feeds)), ...);
            }(std::make_index_sequence<kWatchedOf<P, S>.size()>{});
            last_.ran[k] = true;
        }
        return differs;
    }

    // Back to silence: every carried region zeroed — the prefixes, the tails and the band buffers' prefixes in every
    // copy — and every state's past held in place, and nothing else.
    void reset() {
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (zero_past(kCarriedOf<P>[I], 0), ...);
        }(std::make_index_sequence<kCarriedOf<P>.size()>{});
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (zero_band_past<kBandPastsOf<P>[I]>(), ...);
        }(std::make_index_sequence<kBandPastsOf<P>.size()>{});
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (zero_past(kOnlyPastsOf<P>[I], 0), ...);
        }(std::make_index_sequence<kOnlyPastsOf<P>.size()>{});
    }

    // Buffer `b`'s past before the next run: `history[k * width + e]` is element `e` of the value `k + 1`
    // samples before the block; deeper than the history is silence.
    template <class T>
    void seed(std::size_t b, std::span<const T> history) {
        const std::int32_t k = kPieceOf<P>[b];
        if (k < 0 || !(is_carried(piece(k)) || piece(k).only_past)) {
            fail("heinzel: seed: this value keeps no past to seed");
        }
        const Piece& p = piece(k);
        const auto width = static_cast<std::size_t>(p.sample_bytes) / sizeof(T);
        if (p.type != (std::is_same_v<T, float> ? ElemType::F32 : ElemType::F64) || width == 0 || history.size() % width != 0) {
            fail("heinzel: seed: the history is not whole samples of the value's type");
        }
        if (static_cast<std::int64_t>(history.size() / width) > p.prefix) {
            fail("heinzel: seed: the history reaches deeper than the value keeps its past");
        }
        write_past(static_cast<std::size_t>(k), history);
    }

    // A stream program's: the most samples a run may take, as made.
    std::int64_t built_for() const requires(kKindOf<P> == Kind::Stream) { return state_.built_for; }

    // Run the next runs over `n` samples of a workspace built for at least that many: capacity mode.
    void set_length(std::int64_t n) requires(kKindOf<P> == Kind::Stream) {
#ifndef NDEBUG
        if (n < 1 || n > state_.built_for) {
            fail("heinzel: set_length: the length is outside 1 and what the workspace was built for");
        }
#endif
        state_.length = n;
    }

private:
    void make(const Sizes& sizes) {
        if constexpr (kind == Kind::Stream) {
            state_.length = state_.built_for = sizes[static_cast<std::size_t>(kSymbolOf<P>.id)];
            state_.placed = place(kPiecesOf<P>, Copies, Guard, sizes);
        } else if constexpr (kind == Kind::Symbolic) {
            state_.sizes = sizes;
            state_.pieces = kPiecesOf<P>;
            for (Piece& p : state_.pieces) {
                p.sample_bytes = sample_bytes(kBuffersOf<P>[static_cast<std::size_t>(p.buffer)], sizes);
            }
            state_.placed = place(state_.pieces, Copies, Guard, sizes);
        }
        if (placement().size == 0) {
            return; // a program that keeps nothing has no arena
        }
        // Page-aligned, so that the placement's page offsets are the blocks'. Zeroed: a relaid buffer's padding is read,
        // a past starts as silence, and the first run takes no page fault in the arena. An inline arena starts as a hand
        // loop's buffers do, and only what a run reads before it writes it is zeroed: every past and every relaid block.
        if constexpr (!kInline) {
            bytes_.reset(static_cast<std::byte*>(::operator new(placement().size, std::align_val_t{kPage})));
            std::memset(bytes_.get(), 0, placement().size);
        } else {
            reset();
            [&]<std::size_t... I>(std::index_sequence<I...>) {
                (std::memset(at(static_cast<std::int32_t>(kPaddedOf<P>[I])), 0, static_cast<std::size_t>(block_bytes(piece(kPaddedOf<P>[I]), built()))), ...);
            }(std::make_index_sequence<kPaddedOf<P>.size()>{});
        }
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (fill_ramp(kRampsOf<P>[I]), ...);
        }(std::make_index_sequence<kRampsOf<P>.size()>{});
    }

    // The sizes the workspace was made for (a stream program's runs may be shorter).
    Sizes built() const {
        if constexpr (kind == Kind::Stream) {
            return sizes_of_length<P>(state_.built_for);
        } else {
            return sizes();
        }
    }
    const Placement<kPieces>& placement() const {
        if constexpr (kind == Kind::Static) {
            return kPlacementOf<P, Copies, Guard>;
        } else {
            return state_.placed;
        }
    }
    const Piece& piece(std::size_t k) const {
        if constexpr (kind == Kind::Symbolic) {
            return state_.pieces[k];
        } else {
            return kPiecesOf<P>[k];
        }
    }
    std::byte* at(std::int32_t k, unsigned copy = 0) {
        return k < 0 ? arena() : arena() + placement().at[static_cast<std::size_t>(k)] + copy * placement().copy_bytes[static_cast<std::size_t>(k)];
    }
    static std::size_t past_bytes(const Piece& p) { return static_cast<std::size_t>(p.prefix * p.sample_bytes); }

    template <std::size_t K, std::size_t N>
    void carry_piece(const std::array<const void*, N>& feeds) {
        const Piece& p = piece(K);
        const std::int64_t n = loops::resolved(p.last, sizes());
        const std::int64_t keep = p.prefix * p.sample_bytes;
        std::byte* end = at(static_cast<std::int32_t>(K));
        if constexpr (kPiecesOf<P>[K].feed < 0) {
            std::memmove(end - keep, end + (n - p.prefix) * p.sample_bytes, static_cast<std::size_t>(keep));
        } else {
            const auto* feed = static_cast<const std::byte*>(feeds[static_cast<std::size_t>(p.feed)]);
            const std::byte* run_end = feed + n * p.sample_bytes;
            // A run at least as long as the tail replaces it whole: one copy of the tail's size, which a static or a
            // stream program knows at compile time, so it inlines even where the run's length is a run-time number.
            if (n >= p.prefix) {
                std::memcpy(end - keep, run_end - keep, static_cast<std::size_t>(keep));
            } else {
                const std::int64_t fresh = n * p.sample_bytes;
                std::memmove(end - keep, end - keep + fresh, static_cast<std::size_t>(keep - fresh));
                std::memcpy(end - fresh, run_end - fresh, static_cast<std::size_t>(fresh));
            }
        }
    }
    // Feed F's bits, an f32's in the low half.
    template <std::size_t F, std::size_t N>
    static std::uint64_t bits_of(const std::array<const void*, N>& feeds) {
        // A watched feed is a Param the stage reads, f32 or f64 today (`ir/vocab.hpp`): one of another width, or one
        // with no buffer (0 bytes), would be read as 8 bytes.
        static_assert(kFeedBytesOf<P, F> == 4 || kFeedBytesOf<P, F> == 8, "heinzel::loops::Workspace: a watched Param is 4 or 8 bytes");
        if constexpr (kFeedBytesOf<P, F> == 4) {
            std::uint32_t low = 0;
            std::memcpy(&low, feeds[F], sizeof low);
            return low;
        } else {
            std::uint64_t bits = 0;
            std::memcpy(&bits, feeds[F], sizeof bits);
            return bits;
        }
    }

    void zero_past(std::size_t k, unsigned copy) {
        const std::size_t bytes = past_bytes(piece(k));
        std::memset(at(static_cast<std::int32_t>(k), copy) - bytes, 0, bytes);
    }
    template <std::size_t K>
    void zero_band_past() {
        for (unsigned copy = 0; copy < (kPiecesOf<P>[K].split ? Copies : 1u); ++copy) {
            zero_past(K, copy);
        }
    }
    template <class T>
    void write_past(std::size_t k, std::span<const T> history) {
        const Piece& p = piece(k);
        const auto width = static_cast<std::size_t>(p.sample_bytes) / sizeof(T);
        zero_past(k, 0);
        std::byte* end = at(static_cast<std::int32_t>(k));
        for (std::size_t s = 0; s < history.size() / width; ++s) {
            std::memcpy(end - static_cast<std::int64_t>(s + 1) * p.sample_bytes, history.data() + s * width, width * sizeof(T));
        }
    }

    // A ramp read in its past holds its own element index within a sample, the same at every sample (a ramp read in no
    // past is a constant, and needs no fill).
    void fill_ramp(std::size_t k) {
        const Piece& p = piece(k);
        const std::int64_t held = loops::bytes_of(p.type);
        const std::int64_t per = p.sample_bytes / held;
        const std::int64_t n = block_bytes(p, built()) / held;
        std::byte* block = at(static_cast<std::int32_t>(k));
        for (std::int64_t e = 0; e < n; ++e) {
            p.type == ElemType::F32 ? void(reinterpret_cast<float*>(block)[e] = static_cast<float>(e % per))
                                    : void(reinterpret_cast<double*>(block)[e] = static_cast<double>(e % per));
        }
    }

    struct Free {
        void operator()(std::byte* p) const { ::operator delete(p, std::align_val_t{kPage}); }
    };
    template <std::size_t N>
    struct Inline {
        alignas(kLine) std::array<std::byte, N> bytes; // (no initializer: see `make`)
    };
    struct None {};
    using Arena = std::conditional_t<!kInline, std::unique_ptr<std::byte, Free>, std::conditional_t<(kInlineBytes > 0), Inline<kInlineBytes>, None>>;
    // What the program leaves open: nothing (static), a length and its regions' places (stream), or the sizes, the
    // pieces they size and their places (symbolic).
    struct Fixed {};
    struct ForLength {
        std::int64_t length{0};
        std::int64_t built_for{0};
        Placement<kPieces> placed{};
    };
    struct ForSizes {
        Sizes sizes{};
        std::array<Piece, kPieces> pieces{};
        Placement<kPieces> placed{};
    };
    using State = std::conditional_t<kind == Kind::Static, Fixed, std::conditional_t<kind == Kind::Stream, ForLength, ForSizes>>;
    // The kept stages' last bits: each watched Param's as its stage last ran, and whether it has run. They
    // are valid exactly while the kept stages' buffers hold their results: zero in a workspace just made (its arena too),
    // moved with the arena, and left alone by `reset` and `carry` (what a kept stage works out depends on Params alone,
    // not on the past). A program with no kept stage holds none: a type of its own, so that it shares an address.
    struct LastBits {
        std::array<std::uint64_t, kKeptOf<P>.slots> bits{};
        std::array<bool, kKeptOf<P>.stages> ran{};
    };
    struct NoneKept {};
    using Last = std::conditional_t<(kKeptOf<P>.stages > 0), LastBits, NoneKept>;

    [[no_unique_address]] Arena bytes_;
    [[no_unique_address]] std::conditional_t<(kInPlace > 0), std::array<std::byte*, kInPlace>, None> bound_{}; // null: its own block
    [[no_unique_address]] State state_{};
    [[no_unique_address]] Last last_{};
};

} // namespace detail

// A program's workspace. Without a split band it keeps one copy of everything, so a serial program has one workspace
// type, and the runner is instantiated once for it, whatever `Copies` a caller passes.
template <const Program& P, unsigned Copies = 1>
using Workspace = detail::Workspace<P, (kSplitsABandOf<P> && Copies > 1 ? Copies : 1)>;
// ...with `Guard` free bytes around every region, for a test's canaries.
template <const Program& P, unsigned Copies, std::size_t Guard>
using GuardedWorkspace = detail::Workspace<P, (kSplitsABandOf<P> && Copies > 1 ? Copies : 1), Guard>;

} // namespace heinzel::loops
