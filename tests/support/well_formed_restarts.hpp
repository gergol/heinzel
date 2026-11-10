#pragma once

// `well_formed`'s rules for a fold inside a feedback stage's sample (Plan/23 10): the stage's voices run in lanes, time
// innermost, and every sample folds them into one value the next ones read. The fold's Registers restart at every sample
// (`restarts_each_sample`): they stand right below the loop through which a State hands its value on, inside the strip
// of their lanes, which covers the fold's axis in one whole chunk; the chain walks that axis alone, its bodies side by
// side; Init and Finish stand outside it, so that the Finish folds every lane of every body, and the Accumulate in it.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

#include "heinzel/loops/program.hpp"
#include "heinzel/loops/tree.hpp"

#include "well_formed_walks.hpp"

namespace heinzel::loops::detail {

// Does the strip of the lanes of fold Registers `regs` cover its axis in one whole chunk: of a literal extent, the
// strip's step that extent, or more where its masked tail runs them; the chain, an Unroll by the lanes or none, then the
// Vector, along that axis alone?
constexpr bool one_whole_chunk(const Program& p, const Stage& stage, std::int32_t regs) {
    const std::int32_t link = node_at(p, regs).link;
    const Node& strip = node_at(p, governing_strip(p, link));
    const Extent axis = stage.axes[static_cast<std::size_t>(strip.axis)];
    const std::int64_t length = is_literal(axis) ? literal_of(axis) : -1;
    bool alone = length > 0 && (strip.step == length || (strip.tail == Tail::Masked && strip.step > length));
    std::int32_t levels = 0;
    for (std::int32_t at = link; at >= 0; at = chain_next(p, at)) {
        const Node& n = node_at(p, at);
        alone = alone && n.axis == strip.axis && (n.kind == NodeKind::Vector || n.step == node_at(p, n.first_kid).step);
        levels += n.kind == NodeKind::Unroll ? 1 : 0;
    }
    return alone && levels <= 1;
}

// Every fold's Registers inside a loop that hands state on stand right below it and restart every sample: one whole
// chunk of their strip, Init and Finish outside the chain.
constexpr Checked check_restarts(const Program& p, const std::vector<std::int32_t>& stage_of) {
    for (std::size_t n = 0; n < p.nodes.size(); ++n) {
        const Node& here = p.nodes[n];
        const auto r = static_cast<std::int32_t>(n);
        if (here.kind != NodeKind::Registers || here.role != Role::Accumulator || loop_handing_state_on(p, r) < 0) {
            continue;
        }
        if (!restarts_each_sample(p, r) || !chain_has_lanes(p, here.link)) {
            return std::unexpected("heinzel::loops: a fold's Registers inside a loop that hands state on stand right below it, inside the strip of their lanes");
        }
        if (!one_whole_chunk(p, p.stages[static_cast<std::size_t>(stage_of[n])], r)) {
            return std::unexpected("heinzel::loops: a fold restarted every sample runs its lanes in one whole chunk of their strip, its chain along that axis alone");
        }
        for (std::size_t k = n + 1; k < p.nodes.size() && encloses(p, r, static_cast<std::int32_t>(k)); ++k) {
            const Node& leaf = p.nodes[k];
            const bool ends = (leaf.kind == NodeKind::Init || leaf.kind == NodeKind::Finish) && leaf.stmt == here.stmt;
            if (ends && ancestor(p, static_cast<std::int32_t>(k), [](const Node& a) { return is_enumerator(a); }) > r) {
                return std::unexpected("heinzel::loops: a fold restarted every sample opens and finishes outside its chain");
            }
        }
    }
    return {};
}

} // namespace heinzel::loops::detail
