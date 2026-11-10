#pragma once

// Running a loop-tree program: its stages and bands in order. It decides nothing — every loop, order, width,
// register and address arrives in the program — and checks nothing: a program comes only from the lowering, which
// the tests hold to what the runner accepts (`tests/support/well_formed.hpp`). It asks one question, of a kept stage
// alone: whether a Param it watches changed since it last ran (`Workspace::changed`); where none did, its
// results are still in their buffers, and it does not run. The whole runner is inlined into the caller of `run`: left
// to the inliner, a stage or a band of a program at external linkage (an `Executor`'s) stayed a call, and its code
// differed from the same program's at internal linkage (`Plan/15-zero-overhead/03`).

#include <cstddef>
#include <cstdint>
#include <utility>

#include "heinzel/loops/program.hpp"
#include "heinzel/loops/run_band.hpp"
#include "heinzel/loops/run_stage.hpp"
#include "heinzel/loops/workspace.hpp"

namespace heinzel::loops {

// The band whose first stage is S (-1: none).
template <const Program& P, std::size_t S>
consteval std::int32_t band_from() {
    for (std::size_t b = 0; b < P.bands.size(); ++b) {
        if (P.bands[b].first == static_cast<std::int32_t>(S)) {
            return static_cast<std::int32_t>(b);
        }
    }
    return -1;
}

// Stage S and the stages after it, in order: a stage by itself, or the band starting at it with all of its stages.
template <const Program& P, std::size_t S, class... Workers>
HEINZEL_LOOPS_INLINE void run_from(const auto& feeds, auto& work, Workers&... workers) {
    if constexpr (S < P.stages.size()) {
        constexpr std::int32_t b = band_from<P, S>();
        if constexpr (b < 0) {
            if constexpr (P.stages[S].kept) {
                if (work.template changed<S>(feeds)) {
                    run_stage<P, S>(feeds, work, workers...);
                }
            } else {
                run_stage<P, S>(feeds, work, workers...);
            }
            run_from<P, S + 1>(feeds, work, workers...);
        } else {
            constexpr std::size_t count = static_cast<std::size_t>(P.bands[static_cast<std::size_t>(b)].count);
            run_band<P, static_cast<std::size_t>(b)>(feeds, work, std::make_index_sequence<count>{}, workers...);
            run_from<P, S + count>(feeds, work, workers...);
        }
    }
}

// A run continues from the state the workspace's last run left (`Workspace::carry`). `feeds` holds a pointer for every
// input the program reads, at least.
template <const Program& P, unsigned C, std::size_t G, std::size_t N>
HEINZEL_LOOPS_INLINE void run(const std::array<const void*, N>& feeds, detail::Workspace<P, C, G>& work) {
    static_assert(N >= kFeedsOf<P>, "heinzel::loops::run: a pointer for every input the program reads");
    run_from<P, 0>(feeds, work);
    work.carry(feeds);
}

// Runs the same program with a split stage's ranges and a split band's chunks on `workers`: anything with `size()` and
// `run(count, task)` that calls `task(w)` for every `w` below `count` and returns once all have. The caller brings the
// pool, so `loops/` names no thread. The result is bit for bit the serial run's: a split writes nothing two workers
// share.
template <const Program& P, unsigned C, std::size_t G, std::size_t N, class Workers>
HEINZEL_LOOPS_INLINE void run(const std::array<const void*, N>& feeds, detail::Workspace<P, C, G>& work, Workers& workers) {
    static_assert(N >= kFeedsOf<P>, "heinzel::loops::run: a pointer for every input the program reads");
    run_from<P, 0>(feeds, work, workers);
    work.carry(feeds);
}

} // namespace heinzel::loops
