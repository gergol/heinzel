#pragma once

// Where a stage's buffer accesses stand while its tree runs, and how a node moves them. Every access (a statement's
// operands, then its store) has a slot holding a byte pointer. A loop moves the slots that move along its axis, by a
// stride worked out once per placement. Which slots move, and by how much along the lane axis, are compile-time facts
// of the program, so an unused slot costs nothing and a lane's pointers are walked by one induction variable.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

#include "heinzel/loops/layout.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/tree.hpp"
#include "heinzel/loops/workspace.hpp"

namespace heinzel::loops {

// `static`: LLVM's always-inliner keeps a template's inlined-away body until the end when it has a comdat, as every
// external template does, and goes on inlining the later layers into it too — work that grows with the square of a
// body's inline depth (the runner's kids recurse sibling by sibling). With internal linkage each layer is deleted as
// soon as it is inlined: the same machine code, half the runner's compile time. Forced only when optimizing: at -O0 the
// runner folded into one function took over 33 GB to compile under -fsanitize=address (150 statements; 1.6 GB unforced).
#if defined(__OPTIMIZE__)
#define HEINZEL_LOOPS_INLINE [[gnu::always_inline]] static inline
#else
#define HEINZEL_LOOPS_INLINE static inline
#endif

inline constexpr std::size_t kSlots = kMaxNodeInputs + 1; // a statement's operands, then its store

using Extents = std::array<std::int64_t, kMaxAxes>;

// Calls `run(0) … run(N - 1)` written out, so that each index is a constant where it is used. A loop over bodies,
// slots or turns would be left to the optimizer to unroll, and past a handful of statements it stops unrolling.
template <std::size_t... I>
HEINZEL_LOOPS_INLINE void each_of(std::index_sequence<I...>, const auto& run) {
    (run(I), ...);
}
template <std::size_t N>
HEINZEL_LOOPS_INLINE void each(const auto& run) {
    each_of(std::make_index_sequence<N>{}, run);
}
// ...over chosen indices only, each handed over as a constant: `run(At[0]) … run(At[n - 1])`.
template <std::array At, std::size_t... K>
HEINZEL_LOOPS_INLINE void each_at_of(std::index_sequence<K...>, const auto& run) {
    (run(std::integral_constant<std::size_t, At[K]>{}), ...);
}
template <std::array At>
HEINZEL_LOOPS_INLINE void each_at(const auto& run) {
    each_at_of<At>(std::make_index_sequence<At.size()>{}, run);
}
// `each` in reverse: `run(N - 1) … run(0)`.
template <std::size_t N, std::size_t... I>
HEINZEL_LOOPS_INLINE void each_down_of(std::index_sequence<I...>, const auto& run) {
    (run(N - 1 - I), ...);
}
template <std::size_t N>
HEINZEL_LOOPS_INLINE void each_down(const auto& run) {
    each_down_of<N>(std::make_index_sequence<N>{}, run);
}

// Every slot's pointer, and a pointer to one stride table per placement. The table is pointed at rather than held
// because a walk is copied at every step, and a copy carrying the table stopped dissolving into registers at five
// statements. Per slot, the table has a stride for each axis of the stage's domain (a tree may walk an axis with
// several loops) and one more for the axis a gather's position indexes. `Tail` marks the ragged last chunk of a
// masked strip, in which `valid` lanes are real.
template <std::size_t Slots, std::size_t Axes, bool Tail = false>
struct Walk {
    static constexpr bool tail = Tail;
    using Strides = std::array<std::array<std::ptrdiff_t, Axes + 1>, Slots>;
    std::array<std::byte*, Slots> at{};
    const Strides* stride{nullptr};
    std::int64_t valid{0};
};

template <std::size_t Slots, std::size_t Axes>
HEINZEL_LOOPS_INLINE Walk<Slots, Axes, true> as_tail(const Walk<Slots, Axes, false>& w, std::int64_t valid) {
    return {w.at, w.stride, valid};
}

// How many of a lane's `W` numbers exist, in a tail.
template <std::size_t W>
HEINZEL_LOOPS_INLINE std::size_t lanes_valid(const auto& walk) {
    return walk.valid <= 0 ? 0 : walk.valid >= static_cast<std::int64_t>(W) ? W : static_cast<std::size_t>(walk.valid);
}

// A skewed stage's time loop: at step s, lane j computes sample s − j, so a block of B samples takes
// B + k − 1 steps. In the head steps [0, min(k − 1, B)) and the drain [B, B + k − 1) a lane may lie outside the block;
// in the plain steps [k − 1, B) every lane is real. A walk inside that loop carries which steps it is in, as a tail
// carries `Tail`, and the step s and B, which no other walk holds.
enum class Steps : std::uint8_t { Head, Plain, Drain };

template <class Inner, Steps E>
struct Skewed : Inner {
    static constexpr Steps steps = E;
    std::int64_t step{0};
    std::int64_t block{0};
};

// Is it a walk in a head or drain step?
template <class Wk>
inline constexpr bool kAtAnEdge = false;
template <class Inner>
inline constexpr bool kAtAnEdge<Skewed<Inner, Steps::Head>> = true;
template <class Inner>
inline constexpr bool kAtAnEdge<Skewed<Inner, Steps::Drain>> = true;

// A run's start (Plan/33 S3): a walk at the run's first sample, which a stage whose time loop peels that sample
// (`Node::peels_start`) is handed, marked in its type as a tail is. The loop's first iteration keeps the mark and the
// others take it off (`past_the_start`), so a read of the run's start (`Source::First`) is a constant in each: in the
// samples after the first, what only a start computes is code the compiler drops.
template <class Inner>
struct AtStart : Inner {
    using Unmarked = Inner;
};

template <class Wk>
inline constexpr bool kAtStart = false;
template <class Inner>
inline constexpr bool kAtStart<AtStart<Inner>> = true;

// The walk past the run's start, where `Peels`: without its mark.
template <bool Peels>
HEINZEL_LOOPS_INLINE const auto& past_the_start(const auto& walk) {
    using Wk = std::remove_cvref_t<decltype(walk)>;
    if constexpr (Peels && kAtStart<Wk>) {
        return static_cast<const typename Wk::Unmarked&>(walk);
    } else {
        return walk;
    }
}

// Does stage S peel a run's first sample (a loop of it `peels_start`)?
template <const Program& P, std::size_t S>
consteval bool peels_a_start() {
    const std::size_t end = S + 1 < P.stages.size() ? static_cast<std::size_t>(P.stages[S + 1].root) : P.nodes.size();
    bool peels = false;
    for (auto n = static_cast<std::size_t>(P.stages[S].root); n < end; ++n) {
        peels = peels || P.nodes[n].peels_start;
    }
    return peels;
}

// ---- what a stage's accesses are, at compile time ---------------------------------------------------------

template <const Program& P, std::size_t S>
constexpr const Access& access_of(std::size_t slot) {
    return loops::access_in(P.stmts[static_cast<std::size_t>(P.stages[S].first) + slot / kSlots], slot % kSlots);
}

template <const Program& P, std::size_t S>
inline constexpr std::size_t kSlotsOf = static_cast<std::size_t>(P.stages[S].count) * kSlots;

template <const Program& P, std::size_t S>
consteval auto buffer_slots() {
    std::array<bool, kSlotsOf<P, S>> held{};
    for (std::size_t slot = 0; slot < held.size(); ++slot) {
        held[slot] = access_of<P, S>(slot).source == Source::Buffer;
    }
    return held;
}

// The slots that move along axis A: buffer accesses that walk a buffer axis along it.
template <const Program& P, std::size_t S, std::int32_t A>
consteval auto moves_along() {
    std::array<bool, kSlotsOf<P, S>> moves{};
    for (std::size_t slot = 0; slot < moves.size(); ++slot) {
        const Access& a = access_of<P, S>(slot);
        moves[slot] = a.source == Source::Buffer && a.buffer_axis[static_cast<std::size_t>(A)] >= 0;
    }
    return moves;
}

// The stage's lane axis: the one axis its Vector nodes walk (-1: it has none). A variable, found once per stage.
template <const Program& P, std::size_t S>
inline constexpr std::int32_t kLaneAxis = [] {
    for (std::size_t n = 0; n < P.nodes.size(); ++n) {
        if (P.nodes[n].kind == NodeKind::Vector && encloses(P, P.stages[S].root, static_cast<std::int32_t>(n))) {
            return P.nodes[n].axis;
        }
    }
    return -1;
}();

// A skewed stage's sections, k: its lane axis' literal extent.
template <const Program& P, std::size_t S>
inline constexpr std::int64_t kSectionsOf = literal_of(P.stages[S].axes[static_cast<std::size_t>(kLaneAxis<P, S>)]);

// ...and its lanes a vector: the step of its Vector nodes.
template <const Program& P, std::size_t S>
inline constexpr std::int64_t kWidthOf = [] {
    for (std::size_t n = 0; n < P.nodes.size(); ++n) {
        if (P.nodes[n].kind == NodeKind::Vector && encloses(P, P.stages[S].root, static_cast<std::int32_t>(n))) {
            return static_cast<std::int64_t>(P.nodes[n].step);
        }
    }
    return std::int64_t{1};
}();

// A slot's element size, which is how far it moves per lane.
template <const Program& P, std::size_t S>
consteval auto lane_bytes() {
    std::array<std::ptrdiff_t, kSlotsOf<P, S>> bytes{};
    for (std::size_t slot = 0; slot < bytes.size(); ++slot) {
        const Access& a = access_of<P, S>(slot);
        const Stmt& st = P.stmts[static_cast<std::size_t>(P.stages[S].first) + slot / kSlots];
        bytes[slot] = a.source != Source::Buffer ? 0
                      : slot % kSlots < kMaxNodeInputs ? loops::bytes_of(P.buffers[static_cast<std::size_t>(a.buffer)].type)
                                                       : loops::bytes_of(st.type);
    }
    return bytes;
}

// Along the lane axis, the slots of a relaid buffer walking its blocked axis (`ByBlocks`), or the others.
template <const Program& P, std::size_t S, bool ByBlocks>
consteval auto lane_slots() {
    constexpr std::int32_t A = kLaneAxis<P, S>;
    std::array<bool, kSlotsOf<P, S>> picked{};
    for (std::size_t slot = 0; A >= 0 && slot < picked.size(); ++slot) {
        const Access& a = access_of<P, S>(slot);
        const bool moves = a.source == Source::Buffer && a.buffer_axis[static_cast<std::size_t>(A)] >= 0;
        const bool blocked = moves && a.buffer_axis[static_cast<std::size_t>(A)] == P.buffers[static_cast<std::size_t>(a.buffer)].blocked_axis;
        picked[slot] = moves && blocked == ByBlocks;
    }
    return picked;
}

// Which relaid slots along the lane axis a loop of chunks of `Step` walks by whole blocks.
template <const Program& P, std::size_t S, std::int32_t Step>
consteval auto by_whole_blocks() {
    constexpr auto relaid = lane_slots<P, S, true>();
    std::array<bool, kSlotsOf<P, S>> whole{};
    for (std::size_t slot = 0; slot < whole.size(); ++slot) {
        whole[slot] = relaid[slot] && Step % P.buffers[static_cast<std::size_t>(access_of<P, S>(slot).buffer)].block == 0;
    }
    return whole;
}

// ---- moving -----------------------------------------------------------------------------------------------

// Moves the walk for a Loop of chunks of `Step` to element offset `i` along axis A. Along the lane axis a slot moves by
// `i` elements, a constant, unless it is a relaid slot walked by whole blocks, which moves by the table. Along any
// other axis every slot moves by the table.
template <const Program& P, std::size_t S, std::int32_t A, std::int32_t Step>
HEINZEL_LOOPS_INLINE auto loop_moved(const auto& w, std::int64_t i) {
    auto next = w;
    if constexpr (A == kLaneAxis<P, S>) {
        constexpr auto bytes = lane_bytes<P, S>();
        each_at<indices_where<lane_slots<P, S, false>()>()>([&] [[gnu::always_inline]] (auto s) { next.at[s] = w.at[s] + i * bytes[s]; });
        constexpr auto whole = by_whole_blocks<P, S, Step>();
        each_at<indices_where<lane_slots<P, S, true>()>()>([&] [[gnu::always_inline]] (auto s) {
            next.at[s] = w.at[s] + i * (whole[s] ? (*w.stride)[s][static_cast<std::size_t>(A)] : bytes[s]);
        });
    } else {
        each_at<indices_where<moves_along<P, S, A>()>()>([&] [[gnu::always_inline]] (auto s) {
            next.at[s] = w.at[s] + i * (*w.stride)[s][static_cast<std::size_t>(A)];
        });
    }
    return next;
}

// Moves the walk to an enumerator's sub-chunk at offset `e` along axis A. Along the lane axis slots move by `e`
// elements (within a block, for a relaid slot) and a tail's valid lanes are counted down; along any other axis slots
// move by the table.
template <const Program& P, std::size_t S, std::int32_t A>
HEINZEL_LOOPS_INLINE void enumerated(auto& next, std::int64_t e) {
    if constexpr (A == kLaneAxis<P, S>) {
        constexpr auto bytes = lane_bytes<P, S>();
        each_at<indices_where<moves_along<P, S, A>()>()>([&] [[gnu::always_inline]] (auto s) { next.at[s] += e * bytes[s]; });
        if constexpr (std::remove_cvref_t<decltype(next)>::tail) {
            next.valid -= e;
        }
    } else {
        each_at<indices_where<moves_along<P, S, A>()>()>([&] [[gnu::always_inline]] (auto s) {
            next.at[s] += e * (*next.stride)[s][static_cast<std::size_t>(A)];
        });
    }
}

// ---- placement --------------------------------------------------------------------------------------------

// Slot `slot`'s steps in bytes along each axis of the stage's domain (a tree may walk an axis with several loops), and
// along the axis a gather's position indexes.
template <const Program& P, std::size_t S>
constexpr auto slot_strides(std::size_t slot, const Sizes& sizes) {
    constexpr std::size_t axes = P.stages[S].axes.size();
    std::array<std::ptrdiff_t, axes + 1> stride{};
    const Access& a = access_of<P, S>(slot);
    const Buffer& buffer = P.buffers[static_cast<std::size_t>(a.buffer)];
    const std::ptrdiff_t held = loops::bytes_of(buffer.type);
    const auto per_axis = loops::element_strides(buffer, sizes);
    for (std::size_t axis = 0; axis < axes; ++axis) {
        const std::int32_t along = a.buffer_axis[axis];
        stride[axis] = along < 0 ? 0 : per_axis[static_cast<std::size_t>(along)] * a.step[axis] * held;
    }
    stride[axes] = a.indexed_axis >= 0 ? per_axis[static_cast<std::size_t>(a.indexed_axis)] * held : 0;
    return stride;
}

// ...and where it starts from its buffer's block, in bytes: a read of the past that many samples early, in the silence
// before the block; a slice past the front; a reversed axis at its far end.
template <const Program& P, std::size_t S>
constexpr std::ptrdiff_t slot_start(std::size_t slot, const Sizes& sizes) {
    const Access& a = access_of<P, S>(slot);
    const Buffer& buffer = P.buffers[static_cast<std::size_t>(a.buffer)];
    const auto per_axis = loops::element_strides(buffer, sizes);
    std::ptrdiff_t at = a.back > 0 ? -a.back * per_axis[static_cast<std::size_t>(buffer.shape.rank - 1)] : 0;
    for (std::size_t k = 0; k < kMaxRank; ++k) {
        at += a.offset[k] * per_axis[k];
    }
    return at * loops::bytes_of(buffer.type);
}

template <const Program& P, std::size_t S>
using StridesOf = typename Walk<kSlotsOf<P, S>, P.stages[S].axes.size()>::Strides;

// Where no axis but time is a symbol, every slot's strides and start are constants, worked out while the program
// compiles; a `Symbolic` program's are worked out at each placement, into a table the placement's caller holds.
template <const Program& P, std::size_t S>
inline constexpr StridesOf<P, S> kStridesOf = [] {
    StridesOf<P, S> stride{};
    for (const std::size_t slot : indices_where<buffer_slots<P, S>()>()) {
        stride[slot] = slot_strides<P, S>(slot, Sizes{});
    }
    return stride;
}();
template <const Program& P, std::size_t S>
inline constexpr auto kStartsOf = [] {
    std::array<std::ptrdiff_t, kSlotsOf<P, S>> start{};
    for (const std::size_t slot : indices_where<buffer_slots<P, S>()>()) {
        start[slot] = slot_start<P, S>(slot, Sizes{});
    }
    return start;
}();
struct NoStrides {};
template <const Program& P, std::size_t S>
using StridesHeld = std::conditional_t<kKindOf<P> == Kind::Symbolic, StridesOf<P, S>, NoStrides>;

// The reads that stand still while stage S runs: a read that moves along none of its axes (and is no gather), of a
// workspace buffer the stage does not store (a Close writes back only into what its keeper stores, `well_formed`).
// In a skewed stage, held in one chunk, a read that moves only along the lanes, an element a lane (its buffer's
// contiguous axis, `lane_slots`), reads the same k sections at every step, so it stands still too.
// Each is read from a copy in the frame that places the stage: no pointer the stage stores through can reach a fresh
// local, so clang reads it once, as it does a Param's copy (`Executor::run_bound`), where a store of the value's type
// kept the arena's in the loop (`docs/knob-math.md`, the frame copy).
template <const Program& P, std::size_t S>
consteval auto still_slots() {
    std::array<bool, P.buffers.size()> stored{};
    for (std::size_t slot = 0; slot < kSlotsOf<P, S>; ++slot) {
        const Access& a = access_of<P, S>(slot);
        if (a.source == Source::Buffer && slot % kSlots == kMaxNodeInputs) {
            stored[static_cast<std::size_t>(a.buffer)] = true;
        }
    }
    std::int32_t lanes = -1;
    std::array<bool, kSlotsOf<P, S>> along_lanes{};
    if constexpr (P.stages[S].skewed) {
        lanes = kLaneAxis<P, S>;
        along_lanes = lane_slots<P, S, false>();
    }
    std::array<bool, kSlotsOf<P, S>> still{};
    for (std::size_t slot = 0; slot < still.size(); ++slot) {
        const Access& a = access_of<P, S>(slot);
        bool moves = a.indexed_axis >= 0;
        for (std::size_t axis = 0; axis < P.stages[S].axes.size(); ++axis) {
            const bool sections = static_cast<std::int32_t>(axis) == lanes && along_lanes[slot];
            moves = moves || (a.buffer_axis[axis] >= 0 && !sections);
        }
        const bool read = a.source == Source::Buffer && slot % kSlots < kMaxNodeInputs;
        still[slot] = read && !moves && P.buffers[static_cast<std::size_t>(a.buffer)].home == Home::Workspace && !stored[static_cast<std::size_t>(a.buffer)];
    }
    return still;
}
template <const Program& P, std::size_t S>
inline constexpr auto kStillOf = indices_where<still_slots<P, S>()>();
// Slot `slot`'s copy among them (-1: it has none).
template <const Program& P, std::size_t S>
consteval std::int32_t still_cell(std::size_t slot) {
    for (std::size_t k = 0; k < kStillOf<P, S>.size(); ++k) {
        if (kStillOf<P, S>[k] == slot) {
            return static_cast<std::int32_t>(k);
        }
    }
    return -1;
}
// How many elements slot `slot`'s copy takes: a read along a skewed stage's lanes its k sections, any other read one.
template <const Program& P, std::size_t S>
consteval std::int64_t copied_of(std::size_t slot) {
    if constexpr (P.stages[S].skewed) {
        if (access_of<P, S>(slot).buffer_axis[static_cast<std::size_t>(kLaneAxis<P, S>)] >= 0) {
            return kSectionsOf<P, S>;
        }
    }
    return 1;
}
// Their copies, a cell each: an element, 8 bytes apart; k sections in whole vectors of the stage's lanes, the
// cell aligned as one, so that every lane load from it is an aligned load within it. Where each cell starts (the last
// entry: where they end), and the alignment they need.
template <std::size_t N>
struct Cells {
    std::array<std::size_t, N + 1> at{};
    std::size_t align{8};
};
template <const Program& P, std::size_t S>
consteval auto still_cells() {
    Cells<kStillOf<P, S>.size()> cells{};
    std::size_t end = 0;
    for (std::size_t c = 0; c < kStillOf<P, S>.size(); ++c) {
        std::size_t bytes = 8;
        std::size_t align = 8;
        if constexpr (P.stages[S].skewed) {
            const std::size_t slot = kStillOf<P, S>[c];
            if (copied_of<P, S>(slot) > 1) {
                const auto width = static_cast<std::size_t>(kWidthOf<P, S>);
                const auto element = static_cast<std::size_t>(lane_bytes<P, S>()[slot]);
                const std::size_t vectors = (static_cast<std::size_t>(kSectionsOf<P, S>) + width - 1) / width;
                bytes = vectors * width * element;
                align = width * element;
            }
        }
        end = (end + align - 1) / align * align;
        cells.at[c] = end;
        end += bytes;
        cells.align = align > cells.align ? align : cells.align;
    }
    cells.at[kStillOf<P, S>.size()] = end;
    return cells;
}
template <const Program& P, std::size_t S>
inline constexpr auto kCellsOf = still_cells<P, S>();
template <const Program& P, std::size_t S>
struct StillHeld {
    alignas(kCellsOf<P, S>.align) std::array<std::byte, kCellsOf<P, S>.at.back()> bytes;
};

// Places stage S: where every buffer slot starts, the table of its strides, and the copies of its still reads. This
// happens once per run, per band chunk and per worker, each copying what it reads then; a split band's buffers are
// placed in worker `copy`'s copy of them.
template <const Program& P, std::size_t S>
HEINZEL_LOOPS_INLINE void place(auto& walk, StridesHeld<P, S>& held, StillHeld<P, S>& still, const auto& feeds, auto& work, unsigned copy = 0) {
    constexpr bool constant = kKindOf<P> != Kind::Symbolic;
    if constexpr (constant) {
        walk.stride = &kStridesOf<P, S>;
    } else {
        walk.stride = &held;
    }
    each_at<indices_where<buffer_slots<P, S>()>()>([&] [[gnu::always_inline]] (auto slot) {
        constexpr const Access& a = access_of<P, S>(slot);
        constexpr const Buffer& buffer = P.buffers[static_cast<std::size_t>(a.buffer)];
        std::byte* base = nullptr;
        if constexpr (buffer.home == Home::Feed) {
            base = const_cast<std::byte*>(static_cast<const std::byte*>(feeds[static_cast<std::size_t>(buffer.feed)]));
        } else {
            base = work.bytes_of(std::integral_constant<std::size_t, static_cast<std::size_t>(a.buffer)>{}, copy);
        }
        if constexpr (constant) {
            walk.at[slot] = base + kStartsOf<P, S>[slot];
        } else {
            walk.at[slot] = base + slot_start<P, S>(slot, work.sizes());
            held[slot] = slot_strides<P, S>(slot, work.sizes());
        }
        // The cell is a constant: copied in a loop of their own, by an index known only once inlined, the walk stayed in
        // memory long enough that clang stopped folding a literal run length (`knobs1/heinzel-coefficients`).
        if constexpr (constexpr std::int32_t cell = still_cell<P, S>(slot); cell >= 0) {
            constexpr std::size_t copied_bytes = static_cast<std::size_t>(loops::bytes_of(buffer.type) * copied_of<P, S>(slot));
            std::byte* copied = still.bytes.data() + kCellsOf<P, S>.at[static_cast<std::size_t>(cell)];
            std::memcpy(copied, walk.at[slot], copied_bytes);
            walk.at[slot] = copied;
        }
    });
}

} // namespace heinzel::loops
