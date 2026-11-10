#pragma once

// Questions about the tree: node kinds, ancestors, the strip that governs an enumerator and the chunk it is handed, and
// a Registers node's chain.
// The runner and the tests' checks (`tests/support/well_formed.hpp`) ask the same questions, so they are answered once,
// here.

#include <cstddef>
#include <cstdint>

#include "heinzel/loops/program.hpp"

namespace heinzel::loops {

constexpr const Node& node_at(const Program& p, std::int32_t n) { return p.nodes[static_cast<std::size_t>(n)]; }

constexpr bool makes_a_value(NodeKind k) { return k == NodeKind::Compute || k == NodeKind::Finish || k == NodeKind::Rank; }
constexpr bool is_enumerator(const Node& n) { return n.kind == NodeKind::Unroll || n.kind == NodeKind::Vector; }

// The nearest ancestor of `n` (not `n` itself), below its Stage node, for which `is(node)` holds; -1: none.
template <class Is>
constexpr std::int32_t ancestor(const Program& p, std::int32_t n, const Is& is) {
    for (std::int32_t at = node_at(p, n).parent; at >= 0 && node_at(p, at).kind != NodeKind::Stage; at = node_at(p, at).parent) {
        if (is(node_at(p, at))) {
            return at;
        }
    }
    return -1;
}

// Does `above` enclose `n` (an ancestor of it, or `n` itself)?
constexpr bool encloses(const Program& p, std::int32_t above, std::int32_t n) {
    for (std::int32_t at = n; at >= 0; at = node_at(p, at).parent) {
        if (at == above) {
            return true;
        }
    }
    return false;
}

// The strip governing an enumerator: the nearest Loop above it over its axis (it must be a strip).
constexpr std::int32_t governing_strip(const Program& p, std::int32_t n) {
    const std::int32_t axis = node_at(p, n).axis;
    return ancestor(p, n, [axis](const Node& a) { return a.kind == NodeKind::Loop && a.axis == axis; });
}

// The mode word holds two bits per strip: 0 runs a full chunk, 1 the rest in sub-chunks of `rest`, 2 the rest one at
// a time.
constexpr std::uint32_t mode_of(std::uint32_t mode, std::int32_t strip) { return (mode >> (2 * strip)) & 3u; }

// The chunk enumerator `n` is handed while its strip runs in mode `m`: the step of the nearest enumerator above it over
// its axis, else its strip's step (mode 0) or rest (mode 1); in mode 2, one.
constexpr std::int64_t chunk_at(const Program& p, std::int32_t n, std::uint32_t m) {
    const std::int32_t axis = node_at(p, n).axis;
    for (std::int32_t at = node_at(p, n).parent; m != 2 && at >= 0; at = node_at(p, at).parent) {
        const Node& a = node_at(p, at);
        if (a.axis == axis && (is_enumerator(a) || a.kind == NodeKind::Loop)) {
            return a.kind == NodeKind::Loop && m == 1 ? a.rest : a.step;
        }
    }
    return 1;
}

// ...in the mode word `mode`. A strip below Registers node `full` (-1: none) counts as a full chunk, which is how a set
// is sized.
constexpr std::int64_t chunk_in(const Program& p, std::int32_t n, std::uint32_t mode, std::int32_t full = -1) {
    const std::int32_t g = governing_strip(p, n);
    return chunk_at(p, n, full >= 0 && g != full && encloses(p, full, g) ? 0u : mode_of(mode, node_at(p, g).strip));
}

// The next node of a chain after `n`: its one kid, where that is an enumerator and `n` an Unroll; -1: the end.
constexpr std::int32_t chain_next(const Program& p, std::int32_t n) {
    const Node& here = node_at(p, n);
    if (here.kind != NodeKind::Unroll || here.first_kid < 0 || node_at(p, here.first_kid).next_sibling >= 0) {
        return -1;
    }
    return is_enumerator(node_at(p, here.first_kid)) ? here.first_kid : -1;
}

// The Registers node above `n` holding statement `stmt` in `role`; -1: none.
constexpr std::int32_t registers_of(const Program& p, std::int32_t n, std::int32_t stmt, Role role) {
    return ancestor(p, n, [stmt, role](const Node& a) { return a.kind == NodeKind::Registers && a.stmt == stmt && a.role == role; });
}

} // namespace heinzel::loops
