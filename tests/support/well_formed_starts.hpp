#pragma once

// `well_formed`'s rules for a run's first sample apart (Plan/33 S3): a loop that peels a run's start (`peels_start`)
// walks its stage's time, one Clip loop a sample at a time, the only such loop of a stage that is neither skewed, split,
// banded nor in lanes, so that its first iteration is the run's first sample alone; and a read of a run's start
// (`Source::First`) stands in a stage whose time loop peels it, inside that loop, where the walk says which sample it is.

#include <cstddef>
#include <cstdint>
#include <expected>

#include "heinzel/loops/program.hpp"
#include "heinzel/loops/tree.hpp"

#include "well_formed_walks.hpp"

namespace heinzel::loops::detail {

// Stage `s`'s loop that peels a run's start (-1: none), or why the stage cannot have one.
constexpr std::expected<std::int32_t, Refusal> start_loop_of(const Program& p, std::size_t s) {
    const Stage& stage = p.stages[s];
    const std::size_t end = s + 1 < p.stages.size() ? static_cast<std::size_t>(p.stages[s + 1].root) : p.nodes.size();
    std::int32_t loop = -1;
    bool lanes = false;
    for (auto n = static_cast<std::size_t>(stage.root); n < end; ++n) {
        const Node& here = p.nodes[n];
        lanes = lanes || here.kind == NodeKind::Vector;
        if (!here.peels_start) {
            continue;
        }
        const bool over_time = here.axis + 1 == static_cast<std::int32_t>(stage.axes.size());
        if (loop >= 0 || here.kind != NodeKind::Loop || here.tail != Tail::Clip || here.step != 1 || !over_time) {
            return std::unexpected("heinzel::loops: a loop that peels a run's start is its stage's one Clip loop over time, by one");
        }
        loop = static_cast<std::int32_t>(n);
    }
    if (loop >= 0 && (lanes || stage.skewed || stage.split >= 0 || band_of(p, s) >= 0)) {
        return std::unexpected("heinzel::loops: a stage that peels a run's start is in no lanes, skew, split or band");
    }
    return loop;
}

constexpr Checked check_starts(const Program& p) {
    for (std::size_t s = 0; s < p.stages.size(); ++s) {
        const std::expected<std::int32_t, Refusal> loop = start_loop_of(p, s);
        if (!loop) {
            return std::unexpected(loop.error());
        }
        const std::size_t end = s + 1 < p.stages.size() ? static_cast<std::size_t>(p.stages[s + 1].root) : p.nodes.size();
        for (auto n = static_cast<std::size_t>(p.stages[s].root); n < end; ++n) {
            const Node& leaf = p.nodes[n];
            if (leaf.kind != NodeKind::Compute && leaf.kind != NodeKind::Accumulate && leaf.kind != NodeKind::Collect) {
                continue; // (the leaves that read a statement's operands)
            }
            bool reads = false;
            for (const Access& a : stmt_of(p, s, leaf.stmt).in) {
                reads = reads || a.source == Source::First;
            }
            if (reads && (*loop < 0 || !encloses(p, *loop, static_cast<std::int32_t>(n)))) {
                return std::unexpected("heinzel::loops: a read of a run's start stands inside its stage's loop that peels it");
            }
        }
    }
    return {};
}

} // namespace heinzel::loops::detail
