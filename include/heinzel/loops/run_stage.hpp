#pragma once

// Running a stage: its extents are resolved, its accesses placed in locals, and then its tree runs. The walk, the
// stride table and the extents are kept apart, so the walk's pointers stay in registers and, with literal extents, the
// table is a constant. A stage that reads a feed's past runs its first samples one at a time, reading what lies before
// the block from the feed's tail, which the workspace keeps (`Workspace::carry`). A split stage runs over workers,
// each placing it for itself over a range of its split loop.

#include <array>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <tuple>
#include <type_traits>

#include "heinzel/loops/program.hpp"
#include "heinzel/loops/registers.hpp"
#include "heinzel/loops/run_nodes.hpp"
#include "heinzel/loops/walk.hpp"
#include "heinzel/loops/workspace.hpp"

namespace heinzel::loops {

template <const Program& P, std::size_t S>
using WalkOf = Walk<kSlotsOf<P, S>, P.stages[S].axes.size()>;

// The stage's extents for `sizes`, each axis read at a constant index: a loop over the program's array that stays a
// loop (-O1) would read the program at run time, and put all of it in the binary.
// (No lambda: clang 22 crashed generating one that folds over the program's axes.)
template <const Program& P, std::size_t S, std::size_t... A>
HEINZEL_LOOPS_INLINE Extents resolved_axes(const Sizes& sizes, std::index_sequence<A...>) {
    return Extents{loops::resolved(P.stages[S].axes[A], sizes)...};
}
template <const Program& P, std::size_t S>
HEINZEL_LOOPS_INLINE Extents resolved_axes(const Sizes& sizes) {
    return resolved_axes<P, S>(sizes, std::make_index_sequence<P.stages[S].axes.size()>{});
}

// The stage's tree over the part of its domain `walk` and `extent` describe. `whole`: the resolved extents, which
// a fold's count of points reads.
template <const Program& P, std::size_t S>
HEINZEL_LOOPS_INLINE void run_body(const auto& walk, const Extents& extent, const Extents& whole) {
    std::tuple<const Extents&> regs{whole};
    run_kids<P, S, node_at(P, P.stages[S].root).first_kid, 0u, false, 1>(walk, extent, regs, At{});
}

// The walk `sample` samples into the block, with every read of a feed's past that would reach before the block moved
// into the feed's tail. The slot keeps its own offset into the feed (a slice's, a reversal's, or a worker's of a split
// along another axis), carried over to the tail's end, where the feed's sample 0 would stand.
template <const Program& P, std::size_t S>
HEINZEL_LOOPS_INLINE auto at_sample(const auto& walk, std::int64_t sample, const auto& feeds, auto& work) {
    constexpr const Node& time = node_at(P, P.stages[S].time_loop);
    auto here = loop_moved<P, S, time.axis, time.step>(walk, sample);
    each_at<indices_where<buffer_slots<P, S>()>()>([&] [[gnu::always_inline]] (auto slot) {
        constexpr const Access& a = access_of<P, S>(slot);
        constexpr const Buffer& b = P.buffers[static_cast<std::size_t>(a.buffer)];
        if constexpr (b.home == Home::Feed && a.back > 0) {
            if (a.back > sample) {
                constexpr auto f = static_cast<std::size_t>(b.feed);
                here.at[slot] = work.tail_end(std::integral_constant<std::size_t, f>{}) + (here.at[slot] - static_cast<const std::byte*>(feeds[f]));
            }
        }
    });
    return here;
}

// `FromTheStart`: the part starts at the block's first sample, where a feed's past lies before the block and the run
// starts: a stage that peels the run's first sample is handed its walk there marked (`AtStart`).
template <const Program& P, std::size_t S, bool FromTheStart>
HEINZEL_LOOPS_INLINE void run_part(const auto& walk, Extents extent, const Extents& whole, const auto& feeds, auto& work) {
    constexpr const Stage& stage = P.stages[S];
    constexpr bool starts = FromTheStart && peels_a_start<P, S>();
    using Started = AtStart<std::remove_cvref_t<decltype(walk)>>;
    if constexpr (stage.head == 0 || !FromTheStart) {
        if constexpr (starts) {
            run_body<P, S>(Started{walk}, extent, whole);
        } else {
            run_body<P, S>(walk, extent, whole);
        }
    } else {
        constexpr std::size_t time = static_cast<std::size_t>(node_at(P, stage.time_loop).axis);
        const std::int64_t samples = extent[time];
        const std::int64_t head = samples < stage.head ? samples : stage.head;
        extent[time] = 1;
        std::int64_t sample = 0;
        if constexpr (starts) {
            if (head > 0) {
                run_body<P, S>(Started{at_sample<P, S>(walk, 0, feeds, work)}, extent, whole);
                sample = 1;
            }
        }
        for (; sample < head; ++sample) {
            run_body<P, S>(at_sample<P, S>(walk, sample, feeds, work), extent, whole);
        }
        extent[time] = samples - head;
        run_body<P, S>(at_sample<P, S>(walk, head, feeds, work), extent, whole);
    }
}

// ---- a stage split among workers -------------------------------------------------------------------------

struct Split {
    std::int64_t per{0}; // iterations of the split loop per worker: a whole number of units
    unsigned workers{1}; // how many ranges that makes
};

// `n` iterations, cut in whole `unit`s, `work` statement-points in all and at least `per_worker` of them each,
// among at most `available` workers. One worker when the work is small, when there is one unit, or when
// `per_worker` is 0 (the program splits nothing).
inline Split split_of(std::int64_t n, std::int64_t unit, std::int64_t work, std::int64_t per_worker, unsigned available) {
    if (n <= 0 || per_worker <= 0 || available <= 1) {
        return Split{n, 1};
    }
    const std::int64_t units = (n + unit - 1) / unit;
    std::int64_t k = work / per_worker;
    k = k < units ? k : units;
    k = k < static_cast<std::int64_t>(available) ? k : static_cast<std::int64_t>(available);
    if (k <= 1) {
        return Split{n, 1};
    }
    const std::int64_t per = (units + k - 1) / k * unit;
    return Split{per, static_cast<unsigned>((n + per - 1) / per)};
}

// A worker's range is a whole number of the split loop's steps (a strip's chunk, a Clip loop's chunk, or one) and,
// where a store is contiguous along the split axis, a whole number of cache lines' worth of its elements.
template <const Program& P, std::size_t S>
consteval std::int64_t split_unit() {
    const Node& loop = node_at(P, P.stages[S].split);
    std::int64_t unit = loop.step;
    for (std::int32_t k = 0; k < P.stages[S].count; ++k) {
        const Stmt& st = P.stmts[static_cast<std::size_t>(P.stages[S].first + k)];
        if (st.store.source != Source::Buffer) {
            continue;
        }
        const Buffer& buffer = P.buffers[static_cast<std::size_t>(st.store.buffer)];
        const std::int32_t contiguous = loops::contiguous_axis(buffer.shape.rank);
        const std::int64_t line = P.line_bytes / loops::bytes_of(buffer.type);
        if (P.line_bytes > 0 && st.store.buffer_axis[static_cast<std::size_t>(loop.axis)] == contiguous && buffer.blocked_axis != contiguous && line > 1) {
            unit = std::lcm(unit, line);
        }
    }
    return unit;
}

// A run's work: its points (the resolved extents multiplied) times its statements.
template <const Program& P, std::size_t S>
HEINZEL_LOOPS_INLINE std::int64_t work_of(const Extents& extent) {
    std::int64_t points = 1;
    for (std::size_t a = 0; a < P.stages[S].axes.size(); ++a) {
        points *= extent[a];
    }
    return points * P.stages[S].count;
}

// Runs stage S over as many workers as its work pays for, one range of its split loop each. When that is one worker it
// runs nothing and returns false. When the split loop walks the time along which a feed's past is read, the head
// samples are worker 0's.
template <const Program& P, std::size_t S, class Workers>
HEINZEL_LOOPS_INLINE bool run_split(const auto& feeds, auto& work, Workers& workers) {
    constexpr const Stage& stage = P.stages[S];
    constexpr const Node& loop = node_at(P, stage.split);
    constexpr std::size_t axis = static_cast<std::size_t>(loop.axis);
    constexpr bool along_time = stage.head > 0 && stage.time_loop == stage.split;
    const Extents whole = resolved_axes<P, S>(work.sizes());
    const std::int64_t n = whole[axis];
    const std::int64_t head = along_time ? (n < stage.head ? n : stage.head) : 0;
    const Split split = split_of(n - head, split_unit<P, S>(), work_of<P, S>(whole), P.parallel_work, workers.size());
    if (split.workers <= 1) {
        return false;
    }
    auto task = [&feeds, &work, &whole, split, n, head](unsigned w) {
        StridesHeld<P, S> stride{};
        StillHeld<P, S> still{};
        WalkOf<P, S> walk{};
        place<P, S>(walk, stride, still, feeds, work);
        const std::int64_t lo = head + static_cast<std::int64_t>(w) * split.per;
        const std::int64_t hi = lo + split.per < n ? lo + split.per : n;
        Extents part = whole;
        if (along_time && w == 0) {
            part[axis] = hi;
            run_part<P, S, true>(walk, part, whole, feeds, work);
        } else {
            part[axis] = hi - lo;
            run_part<P, S, !along_time>(loop_moved<P, S, loop.axis, loop.step>(walk, lo), part, whole, feeds, work);
        }
    };
    workers.run(split.workers, task);
    return true;
}

// `workers` is empty for a serial run, whose code then has no trace of splitting, or holds what a split stage's ranges
// run on.
template <const Program& P, std::size_t S, class... Workers>
HEINZEL_LOOPS_INLINE void run_stage(const auto& feeds, auto& work, Workers&... workers) {
    if constexpr (P.stages[S].split >= 0 && sizeof...(Workers) == 1) {
        if (run_split<P, S>(feeds, work, workers...)) {
            return;
        }
    }
    const Extents whole = resolved_axes<P, S>(work.sizes());
    StridesHeld<P, S> stride{};
    StillHeld<P, S> still{};
    WalkOf<P, S> walk{};
    place<P, S>(walk, stride, still, feeds, work);
    run_part<P, S, true>(walk, whole, whole, feeds, work);
}

} // namespace heinzel::loops
