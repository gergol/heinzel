#pragma once

// `well_formed`'s rules for a state kept in its registers (`Node::closes`, `NodeKind::Close`, `Buffer::only_past`): a
// Close mirrors an Open, after the loop walking time; its keeper closes; and the buffer the keeper stores holds its past
// alone, read by the State's readers only, walked along no time, so that every run, chunk and head sample opens and
// closes it in place.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

#include "heinzel/loops/program.hpp"
#include "heinzel/loops/tree.hpp"

namespace heinzel::loops::detail {

// The leaf keeping statement `stmt`'s value as state, its Compute or a fold's Finish, in the stage holding node `n`
// (-1: none).
constexpr std::int32_t keeper_of(const Program& p, std::int32_t n, std::int32_t stmt) {
    std::int32_t root = n;
    while (node_at(p, root).kind != NodeKind::Stage) {
        root = node_at(p, root).parent;
    }
    for (std::size_t k = static_cast<std::size_t>(root) + 1; k < p.nodes.size() && encloses(p, root, static_cast<std::int32_t>(k)); ++k) {
        if ((p.nodes[k].kind == NodeKind::Compute || p.nodes[k].kind == NodeKind::Finish) && p.nodes[k].stmt == stmt) {
            return static_cast<std::int32_t>(k);
        }
    }
    return -1;
}

// Beyond what an Open owes (`check_leaf` holds a Close to the same): its State's keeper closes, and it stands after the
// loop walking time, not in it nor before it (where each run would close the state it has not yet computed).
constexpr std::expected<void, Refusal> check_close(const Program& p, std::int32_t n, const Stage& stage) {
    const Node& here = node_at(p, n);
    const std::int32_t regs = registers_of(p, n, here.keeps, Role::State);
    const std::int32_t keeper = keeper_of(p, n, here.keeps);
    if (keeper < 0 || !node_at(p, keeper).closes) {
        return std::unexpected("heinzel::loops: a Close writes back the State of a keeper that closes");
    }
    const auto time = static_cast<std::int32_t>(stage.axes.size()) - 1;
    for (std::int32_t at = node_at(p, n).parent; at != regs; at = node_at(p, at).parent) {
        if (node_at(p, at).kind == NodeKind::Loop && node_at(p, at).axis == time) {
            return std::unexpected("heinzel::loops: a Close stands after the loop walking time, not in it");
        }
    }
    std::int32_t walk = keeper; // (the keeper's loop walking time, if any: a head sample has none; nodes run in order)
    while (walk != regs && !(node_at(p, walk).kind == NodeKind::Loop && node_at(p, walk).axis == time)) {
        walk = node_at(p, walk).parent;
    }
    if (walk != regs && n < walk) {
        return std::unexpected("heinzel::loops: a Close stands after the loop walking time, not in it");
    }
    return {};
}

// A keeper that closes stores a buffer that holds its past alone: read by its State's readers alone, no output, its
// past within the State, no access walking time through it, and every slot opened closed again. A buffer that holds
// its past alone is a closing keeper's.
constexpr std::expected<void, Refusal> check_closes(const Program& p, const std::vector<std::int32_t>& stage_of, const std::vector<std::int32_t>& reads) {
    std::vector<char> closed(p.buffers.size(), 0);
    for (std::size_t n = 0; n < p.nodes.size(); ++n) {
        const Node& here = p.nodes[n];
        if ((here.kind != NodeKind::Compute && here.kind != NodeKind::Finish) || !here.closes) {
            continue;
        }
        const Stage& stage = p.stages[static_cast<std::size_t>(stage_of[n])];
        const auto time = stage.axes.size() - 1;
        const std::int32_t b = p.stmts[static_cast<std::size_t>(stage.first + here.stmt)].store.buffer;
        const std::int32_t regs = registers_of(p, static_cast<std::int32_t>(n), here.keeps, Role::State);
        bool alone = regs >= 0 && p.buffers[static_cast<std::size_t>(b)].only_past && p.buffers[static_cast<std::size_t>(b)].prefix <= node_at(p, regs).slots;
        closed[static_cast<std::size_t>(b)] = 1;
        for (const std::int32_t out : p.outputs) {
            alone = alone && out != b;
        }
        for (std::size_t st = 0; st < p.stmts.size(); ++st) {
            const bool mine = static_cast<std::int32_t>(st) >= stage.first && static_cast<std::int32_t>(st) < stage.first + stage.count;
            for (std::size_t slot = 0; slot <= kMaxNodeInputs; ++slot) {
                const Access& a = access_in(p.stmts[st], slot);
                const bool reader = slot < kMaxNodeInputs && reads[st] == here.stmt;
                alone = alone && (a.source != Source::Buffer || a.buffer != b || (mine && (reader || st == static_cast<std::size_t>(stage.first + here.stmt)) &&
                                                                                 a.buffer_axis[time] < 0));
            }
        }
        std::int32_t opened = 0;
        for (std::size_t k = 0; k < p.nodes.size(); ++k) {
            const bool keeps = p.nodes[k].keeps == here.keeps && stage_of[k] == stage_of[n];
            opened += keeps && p.nodes[k].kind == NodeKind::Open ? 1 : keeps && p.nodes[k].kind == NodeKind::Close ? -1 : 0;
        }
        if (!alone || opened != 0) {
            return std::unexpected("heinzel::loops: a state kept in its registers holds its past alone, read by them and walked along no time, "
                                   "every slot opened closed again");
        }
    }
    for (std::size_t b = 0; b < p.buffers.size(); ++b) {
        if (p.buffers[b].only_past && !closed[b]) {
            return std::unexpected("heinzel::loops: a buffer of its past alone is a closing keeper's");
        }
    }
    return {};
}

} // namespace heinzel::loops::detail
