#pragma once

// Running a stage's tree: one template per node kind, every node reached by index through `template <const Program&
// P, …>`. A node knows at compile time its statements, which slots move along its axis, the width it runs at, and the
// chunk an enumerator is handed, from the strip above it and the mode (two bits per strip: a full chunk, the rest in
// sub-chunks, or the rest one at a time). A value a leaf makes is handed to its later siblings as a pack indexed by
// statement; what a loop makes does not leave the loop.

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <utility>

#include "heinzel/loops/program.hpp"
#include "heinzel/loops/registers.hpp"
#include "heinzel/loops/tree.hpp"
#include "heinzel/loops/values.hpp"
#include "heinzel/loops/walk.hpp"

namespace heinzel::loops {

template <const Program& P, std::size_t S, std::int32_t K, std::uint32_t Mode, bool First, std::size_t W, class... Locals>
HEINZEL_LOOPS_INLINE void run_kids(const auto& walk, const Extents& extent, auto& regs, At at, const Locals&... locals);

// Where body `b` of a chain stands: every slot moved by its offset along every level's axis.
template <const Program& P, std::size_t S, Chain C, std::size_t... J>
HEINZEL_LOOPS_INLINE auto at_body(const auto& walk, std::size_t b, std::index_sequence<J...>) {
    auto next = walk;
    (enumerated<P, S, C.level[J].axis>(next, static_cast<std::int64_t>(b / C.level[J].inner % C.level[J].count) * C.level[J].step), ...);
    return next;
}

template <const Program& P, std::size_t S, std::int32_t N, std::uint32_t Mode, bool First, std::size_t W, class... Locals>
HEINZEL_LOOPS_INLINE void run_chain(const auto& walk, const Extents& extent, auto& regs, At at, const Locals&... locals) {
    constexpr Chain c = chain_from<P, N, Mode>();
    constexpr std::size_t width = c.width > 0 ? c.width : W;
    const auto body = [&] [[gnu::always_inline]] (std::size_t b) {
        run_kids<P, S, node_at(P, c.end).first_kid, Mode, First, width>(at_body<P, S, c>(walk, b, std::make_index_sequence<c.levels>{}), extent,
                                                                           regs, At{at.body * c.bodies + b, at.turn, at.first, at.last}, locals...);
    };
    if constexpr (P.stages[S].skewed) {
        each_down<c.bodies>(body); // each body reads the body below as the last step left it
    } else {
        each<c.bodies>(body);
    }
}

// The turns of a network's axis, written out: which turn a value was gathered in is a constant wherever it is used.
template <const Program& P, std::size_t S, std::int32_t N, std::uint32_t Mode, bool First, std::size_t W, class... Locals>
HEINZEL_LOOPS_INLINE void run_turns(const auto& walk, const Extents& extent, auto& regs, At at, const Locals&... locals) {
    constexpr const Node& n = node_at(P, N);
    each<static_cast<std::size_t>(literal_of(P.stages[S].axes[static_cast<std::size_t>(n.axis)]))>([&] [[gnu::always_inline]] (std::size_t t) {
        auto next = walk;
        enumerated<P, S, n.axis>(next, static_cast<std::int64_t>(t));
        run_kids<P, S, n.first_kid, Mode, First, W>(next, extent, regs, At{at.body, t, at.first, at.last}, locals...);
    });
}

// A strip runs whole chunks and then the rest: with `Ones`, one at a time; with `LanesThenOnes`, in sub-chunks of
// `rest` and then one at a time; with `Masked`, as one more chunk at full width in which only the first lanes are real.
template <const Program& P, std::size_t S, std::int32_t N, std::uint32_t Mode, bool First, std::size_t W, class... Locals>
HEINZEL_LOOPS_INLINE void run_strip(const auto& walk, const Extents& extent, auto& regs, At at, const Locals&... locals) {
    constexpr const Node& n = node_at(P, N);
    constexpr std::uint32_t shift = 2 * static_cast<std::uint32_t>(n.strip);
    constexpr std::uint32_t full = Mode & ~(3u << shift);
    // Where each part ends is worked out from the count, not from where a loop stopped: with a constant count, the
    // rest is a constant too, whatever the optimizer makes of the loops (a lane tail's size among it).
    const std::int64_t count = extent[static_cast<std::size_t>(n.axis)];
    const auto chunks = static_cast<std::int64_t>(static_cast<std::uint64_t>(count) / n.step * n.step); // (a count is never negative)
    for (std::int64_t i = 0; i < chunks; i += n.step) {
        run_kids<P, S, n.first_kid, full, First, W>(loop_moved<P, S, n.axis, n.step>(walk, i), extent, regs, at, locals...);
    }
    std::int64_t i = chunks;
    if constexpr (n.tail == Tail::LanesThenOnes) {
        for (const std::int64_t rests = chunks + (count - chunks) / n.rest * n.rest; i < rests; i += n.rest) {
            run_kids<P, S, n.first_kid, full | (1u << shift), First, W>(loop_moved<P, S, n.axis, n.step>(walk, i), extent, regs, at, locals...);
        }
    }
    if constexpr (n.tail == Tail::Masked) {
        static_assert(!std::remove_cvref_t<decltype(walk)>::tail, "heinzel::loops: a masked strip inside another's ragged chunk");
        if (chunks < count) {
            run_kids<P, S, n.first_kid, full, First, W>(as_tail(loop_moved<P, S, n.axis, n.step>(walk, chunks), count - chunks), extent, regs, at, locals...);
        }
    } else {
        for (; i < count; ++i) {
            run_kids<P, S, n.first_kid, full | (2u << shift), First, W>(loop_moved<P, S, n.axis, n.step>(walk, i), extent, regs, at, locals...);
        }
    }
}

// A Clip loop runs one at a time, or in chunks that its kids see as the axis's extent. A loop that chunks a fold tells
// it which chunk is the first and which the last, and a peeled loop runs its first iteration with what it inherits, the
// fold's "first" (`peel_first`) or the run's start (`peels_start`), and the others without.
template <const Program& P, std::size_t S, std::int32_t N, std::uint32_t Mode, bool First, std::size_t W, class... Locals>
HEINZEL_LOOPS_INLINE void run_clip(const auto& walk, const Extents& extent, auto& regs, At at, const Locals&... locals) {
    constexpr const Node& n = node_at(P, N);
    constexpr std::size_t axis = static_cast<std::size_t>(n.axis);
    const std::int64_t count = extent[axis];
    const auto iteration = [&]<bool F> [[gnu::always_inline]] (const auto& from, std::int64_t i) {
        const At here = n.chunks_fold ? At{at.body, at.turn, at.first && i == 0, at.last && i + n.step >= count} : at;
        if constexpr (n.step == 1) {
            run_kids<P, S, n.first_kid, Mode, F, W>(loop_moved<P, S, n.axis, 1>(from, i), extent, regs, here, locals...);
        } else {
            Extents within = extent;
            within[axis] = count - i < n.step ? count - i : n.step;
            run_kids<P, S, n.first_kid, Mode, F, W>(loop_moved<P, S, n.axis, n.step>(from, i), within, regs, here, locals...);
        }
    };
    std::int64_t i = 0;
    if constexpr (n.peel_first || n.peels_start) {
        if (count > 0) {
            iteration.template operator()<First>(walk, 0);
        }
        i = n.step;
    }
    for (; i < count; i += n.step) {
        iteration.template operator()<n.peel_first ? false : First>(past_the_start<n.peels_start>(walk), i);
    }
}

// A skewed stage's time loop, its B samples in B + k − 1 steps over two kinds of step, with blends in the head
// [0, min(k − 1, B)) and the drain [B, B + k − 1), without in the plain steps [k − 1, B) (`skewed_lanes.cpp`'s `run`).
// Each step's walk stands at step s and carries which steps it is in, s and B.
template <const Program& P, std::size_t S, std::int32_t N, std::uint32_t Mode, bool First, std::size_t W, class... Locals>
HEINZEL_LOOPS_INLINE void run_skewed(const auto& walk, const Extents& extent, auto& regs, At at, const Locals&... locals) {
    constexpr const Node& n = node_at(P, N);
    constexpr std::int64_t last = kSectionsOf<P, S> - 1;
    const std::int64_t block = extent[static_cast<std::size_t>(n.axis)];
    const auto step = [&]<Steps E> [[gnu::always_inline]] (std::int64_t s) {
        const Skewed<std::remove_cvref_t<decltype(walk)>, E> here{loop_moved<P, S, n.axis, 1>(walk, s), s, block};
        run_kids<P, S, n.first_kid, Mode, First, W>(here, extent, regs, at, locals...);
    };
    const std::int64_t head = block < last ? block : last;
    for (std::int64_t s = 0; s < head; ++s) {
        step.template operator()<Steps::Head>(s);
    }
    for (std::int64_t s = last; s < block; ++s) {
        step.template operator()<Steps::Plain>(s);
    }
    for (std::int64_t s = block; s < block + last; ++s) {
        step.template operator()<Steps::Drain>(s);
    }
}

// A Registers node makes a set of its own for this run of its kids, which the leaves below find through the registers
// tuple. Entering a fold's Registers makes the next point the fold's first.
template <const Program& P, std::size_t S, std::int32_t N, std::uint32_t Mode, bool First, std::size_t W, class... Locals>
HEINZEL_LOOPS_INLINE void run_registers(const auto& walk, const Extents& extent, auto& regs, At at, const Locals&... locals) {
    constexpr const Node& n = node_at(P, N);
    SetOf<P, S, N, Mode> set;
    if constexpr (P.stages[S].skewed && n.role == Role::State) {
        set = {}; // a State only the link reads is opened by none, and a head step's lanes not yet real read it
    }
    auto more = std::tuple_cat(regs, std::tie(set));
    run_kids<P, S, n.first_kid, Mode, n.role == Role::Accumulator ? true : First, W>(walk, extent, more, at, locals...);
}

template <const Program& P, std::size_t S, std::int32_t N, std::uint32_t Mode, bool First, std::size_t W, class... Locals>
HEINZEL_LOOPS_INLINE void run_node(const auto& walk, const Extents& extent, auto& regs, At at, const Locals&... locals) {
    constexpr const Node& n = node_at(P, N);
    if constexpr (n.kind == NodeKind::Loop && P.stages[S].skewed && N == P.stages[S].time_loop) {
        run_skewed<P, S, N, Mode, First, W>(walk, extent, regs, at, locals...);
    } else if constexpr (n.kind == NodeKind::Loop && n.tail == Tail::Clip) {
        run_clip<P, S, N, Mode, First, W>(walk, extent, regs, at, locals...);
    } else if constexpr (n.kind == NodeKind::Loop) {
        run_strip<P, S, N, Mode, First, W>(walk, extent, regs, at, locals...);
    } else if constexpr (n.kind == NodeKind::Turns) {
        run_turns<P, S, N, Mode, First, W>(walk, extent, regs, at, locals...);
    } else if constexpr (n.kind == NodeKind::Unroll || n.kind == NodeKind::Vector) {
        run_chain<P, S, N, Mode, First, W>(walk, extent, regs, at, locals...);
    } else if constexpr (n.kind == NodeKind::Registers) {
        run_registers<P, S, N, Mode, First, W>(walk, extent, regs, at, locals...);
    } else if constexpr (n.kind == NodeKind::WhenLast || n.kind == NodeKind::WhenNotLast) {
        if (at.last == (n.kind == NodeKind::WhenLast)) {
            run_kids<P, S, n.first_kid, Mode, First, W>(walk, extent, regs, at, locals...);
        }
    }
}

// ---- leaves ---------------------------------------------------------------------------------------------------

// A Prefetch asks, as its block opens, for the cache lines the next iteration of loop `link` will store to: for each
// row of the enclosing Registers' chain, the lane chunk's bytes of every statement written after the inner loops. A
// chain's rows are its level along an axis other than the lane axis (there is at most one; with none, one row).
constexpr Level rows_of(const Chain& c, std::int32_t lanes) {
    for (std::size_t l = 0; l < c.levels; ++l) {
        if (c.level[l].axis != lanes) {
            return c.level[l];
        }
    }
    return Level{};
}

// ...and how many numbers of the lane axis one of its rows covers: the lanes, times the Unroll along them.
constexpr std::size_t lane_chunk_of(const Chain& c, std::int32_t lanes) {
    std::size_t chunk = c.width > 0 ? c.width : 1;
    for (std::size_t l = 0; l < c.levels; ++l) {
        chunk *= c.level[l].axis == lanes ? c.level[l].count : 1;
    }
    return chunk;
}

// The stores a Prefetch asks for: of the value-making leaves in its Registers that stand outside the inner loops.
template <const Program& P, std::size_t S, std::int32_t N>
consteval auto prefetched_slots() {
    const std::int32_t regs = node_at(P, N).parent;
    const auto a_loop = [](const Node& a) { return a.kind == NodeKind::Loop; };
    std::array<bool, kSlotsOf<P, S>> picked{};
    for (std::size_t n = static_cast<std::size_t>(regs) + 1; n < P.nodes.size(); ++n) {
        const auto leaf = static_cast<std::int32_t>(n);
        if (!makes_a_value(P.nodes[n].kind) || !encloses(P, regs, leaf) || ancestor(P, leaf, a_loop) != ancestor(P, regs, a_loop)) {
            continue;
        }
        const std::size_t slot = static_cast<std::size_t>(P.nodes[n].stmt) * kSlots + kMaxNodeInputs;
        picked[slot] = access_of<P, S>(slot).source == Source::Buffer;
    }
    return picked;
}

template <const Program& P, std::size_t S, std::int32_t N, std::uint32_t Mode>
HEINZEL_LOOPS_INLINE void prefetch(const auto& walk) {
    constexpr const Node& n = node_at(P, N);
    constexpr const Node& along = node_at(P, n.link);
    constexpr std::uint32_t m = along.tail == Tail::Clip ? 0 : mode_of(Mode, along.strip);
    constexpr std::int64_t by = m == 2 ? 1 : m == 1 ? along.rest : along.step;
    const auto next = loop_moved<P, S, along.axis, along.step>(walk, by);
    constexpr Chain c = chain_from<P, node_at(P, n.parent).link, Mode>();
    constexpr std::int32_t lanes = kLaneAxis<P, S>;
    constexpr Level rows_level = rows_of(c, lanes);
    constexpr std::size_t lane_chunk = lane_chunk_of(c, lanes);
    constexpr std::size_t line = static_cast<std::size_t>(P.line_bytes);
    each_at<indices_where<prefetched_slots<P, S, N>()>()>([&] [[gnu::always_inline]] (auto slot) {
        const bool spans = lanes >= 0 && access_of<P, S>(slot).buffer_axis[static_cast<std::size_t>(lanes)] >= 0;
        const std::size_t span = (spans ? lane_chunk : 1) * static_cast<std::size_t>(loops::bytes_of(P.stmts[static_cast<std::size_t>(P.stages[S].first) + slot / kSlots].type));
        each<rows_level.count>([&] [[gnu::always_inline]] (std::size_t row) {
            const std::byte* to = next.at[slot] + (rows_level.axis >= 0 ? static_cast<std::ptrdiff_t>(row) * (*walk.stride)[slot][static_cast<std::size_t>(rows_level.axis)] : 0);
            for (std::size_t l = 0; l < (span + line - 1) / line; ++l) {
                __builtin_prefetch(to + l * line, 1);
            }
            __builtin_prefetch(to + span - 1, 1);
        });
    });
}

// Kid K of a node and the kids after it. A leaf that makes a value hands it to the kids after it, the pack padded
// to its statement's index first; a leaf that makes none runs here, one inline layer less per body.
template <const Program& P, std::size_t S, std::int32_t K, std::uint32_t Mode, bool First, std::size_t W, std::size_t... I, class... Locals>
HEINZEL_LOOPS_INLINE void run_after(std::index_sequence<I...>, const auto& made, const auto& walk, const Extents& extent, auto& regs, At at,
                                    const Locals&... locals) {
    run_kids<P, S, node_at(P, K).next_sibling, Mode, First, W>(walk, extent, regs, at, locals..., ((void)I, Absent{})..., made);
}

template <const Program& P, std::size_t S, std::int32_t K, std::uint32_t Mode, bool First, std::size_t W, class... Locals>
HEINZEL_LOOPS_INLINE void run_kids(const auto& walk, const Extents& extent, auto& regs, At at, const Locals&... locals) {
    if constexpr (K >= 0) {
        constexpr const Node& kid = node_at(P, K);
        constexpr std::int32_t next = kid.next_sibling;
        if constexpr (makes_a_value(kid.kind)) {
            static_assert(static_cast<std::size_t>(kid.stmt) >= sizeof...(Locals), "heinzel::loops: values are made in statement order");
            const auto made = [&] [[gnu::always_inline]] {
                if constexpr (kid.kind == NodeKind::Compute) {
                    return compute<P, S, K, W>(walk, regs, at, locals...);
                } else if constexpr (kid.kind == NodeKind::Finish) {
                    return finish<P, S, K, W>(walk, regs, at);
                } else {
                    return rank<P, S, K, W>(walk, regs, at);
                }
            }();
            run_after<P, S, K, Mode, First, W>(std::make_index_sequence<static_cast<std::size_t>(kid.stmt) - sizeof...(Locals)>{}, made, walk, extent, regs,
                                              at, locals...);
        } else {
            if constexpr (kid.kind == NodeKind::Accumulate) {
                accumulate<P, S, K, W, First>(walk, regs, at, locals...);
            } else if constexpr (kid.kind == NodeKind::Init) {
                init<P, S, K, W>(walk, regs, at);
            } else if constexpr (kid.kind == NodeKind::Partial) {
                partial<P, S, K, W>(walk, regs, at);
            } else if constexpr (kid.kind == NodeKind::Open) {
                open<P, S, K, W>(walk, regs, at);
            } else if constexpr (kid.kind == NodeKind::Close) {
                close<P, S, K, W>(walk, regs, at);
            } else if constexpr (kid.kind == NodeKind::Collect) {
                collect<P, S, K, W>(walk, regs, at, locals...);
            } else if constexpr (kid.kind == NodeKind::Prefetch) {
                prefetch<P, S, K, Mode>(walk);
            } else {
                run_node<P, S, K, Mode, First, W>(walk, extent, regs, at, locals...);
            }
            run_kids<P, S, next, Mode, First, W>(walk, extent, regs, at, locals...);
        }
    }
}

} // namespace heinzel::loops
