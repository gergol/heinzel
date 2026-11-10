#pragma once

// The schedule: what the planner decides (`loops/planner/`) and what the lowering takes (`loops/lower.hpp`). For each
// stage it holds the nodes, an ordered list of loops and whatever else the planner chose; then the bands. The graph
// facts (`loops/graph/`) read a stage's nodes alone.

#include <array>
#include <cstdint>

#include "heinzel/loops/program.hpp"
#include "heinzel/support/fixed_vector.hpp"

namespace heinzel::loops {

// The most rows a band's chunk holds, so that rows and leads add up in an int32.
inline constexpr std::int32_t kMaxBandStep = std::int32_t{1} << 30;

// The most samples of a value's past a stage hands on in registers (a run of lags, a bit each in a 64-bit word).
inline constexpr std::int32_t kMaxStateDepth = 64;

// How a schedule's loop walks its axis; the lowering spells each kind as tree nodes (`NodeKind`).
enum class LoopKind : std::uint8_t { Serial, Vector, Unroll, Tile };

struct Schedule {
    struct Loop {
        std::int32_t axis{0};
        std::int32_t step{1};  // a Vector's lanes, an Unroll's bodies, a Tile's chunk
        LoopKind kind{LoopKind::Serial};
        std::int32_t unroll{1};        // a Vector: blocks of lanes side by side

        constexpr bool operator==(const Loop&) const = default;
    };

    // A stage: the graph nodes it computes, in the order they run, and the loops that walk them. The first `inlined`
    // nodes are a producer computed inside the stage — once per tap, never stored — through window node `through`
    // (`planner/inline.hpp`; -1: none).
    struct Stage {
        fixed_vector<std::int32_t, kMaxStageStmts> nodes{}; // (tests assign it whole: `.nodes.push_back` is gate_one_spelling's)
        std::int32_t inlined{0};
        std::int32_t through{-1};
        fixed_vector<Loop, kMaxStageLoops> loops{};         // outermost first
        std::int32_t inner_from{0};                         // loops[inner_from..] are the inner loops (loops.size(): none)
        bool contract{false};
        std::int32_t relaid{-1};
        std::int32_t relaid_block{0};
        std::int32_t turned{-1};                            // the operand read from a copy turned around: its axis that
                                                            // carries the lanes swapped with its contiguous one
        bool fetches{false};                                // the next block's stores asked for, a line at a time
        std::int32_t parallel{-1};
        std::int32_t state_depth{kMaxStateDepth};           // the most samples of a value's past handed on in registers
        bool skewed{false};                                 // its lanes its sections, each a sample behind the one before
                                                            //   (`planner/feedback.hpp`; the program's `Stage::skewed`)
        bool peeled{false};                                 // reads a run's first sample, which its time loop runs apart
                                                            //   (`planner/run_start.hpp`; the program's `peels_start`)
        bool kept{false};                                   // run only when a Param it watches changed (`planner/kept.hpp`)
        std::array<std::uint64_t, kWatchWords> watched{};   //   a bit per named input; none where it is not kept

        constexpr bool operator==(const Stage&) const = default;
    };

    // A band: `count` stages from `first`, run chunk by chunk along `axis`, `step` rows a chunk (at most `kMaxBandStep`).
    struct Band {
        std::int32_t first{0};
        std::int32_t count{0};
        std::int32_t axis{0};
        std::int32_t step{0};
        bool parallel{false};

        constexpr bool operator==(const Band&) const = default;
    };

    fixed_vector<Stage, kMaxStages> stages{};
    fixed_vector<Band, kMaxBands> bands{};
    std::int64_t parallel_work{0};
    std::int32_t line_bytes{0};

    constexpr bool operator==(const Schedule&) const = default;
};

} // namespace heinzel::loops
