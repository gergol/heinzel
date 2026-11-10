#pragma once

// What a stage holds in registers while its tree runs: a fold's accumulators, a value handed to the next turn, and a
// network's gathered values. Each `Registers` node owns one set, sized from its chain, and the leaves below it use it,
// finding the set by a compile-time lookup of the `Registers` node above them that holds their statement. Every index
// into a set is a constant after inlining (`body`, `turn`): one that is not would keep the whole set in memory.

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>

#include "heinzel/loops/program.hpp"
#include "heinzel/loops/tree.hpp"
#include "heinzel/loops/values.hpp"
#include "heinzel/loops/walk.hpp"
#include "heinzel/runtime/compute.hpp"
#include "heinzel/runtime/lane_value.hpp"
#include "heinzel/support/order_net.hpp"

namespace heinzel::loops {

template <class A, std::size_t Lanes, std::size_t Count>
struct Accumulator {
    static constexpr std::size_t kLanes = Lanes;
    static constexpr std::size_t kCount = Count;
    std::array<ValueOf<A, Lanes>, Count> lanes;
    A rest; // where a fold's lanes run along its own axis: what no whole lane covered
};

// A value's last `Slots` samples a body, the newest in slot 0: a reader of k samples back takes slot k − 1.
template <class T, std::size_t Lanes, std::size_t Count, std::size_t Slots>
struct State {
    std::array<std::array<ValueOf<T, Lanes>, Count>, Slots> was;
};

// In a skewed stage's head or drain step, which lanes of register `body` have their sample in the block: lane l of body
// b is section b·W + l, on sample s − b·W − l (`Plan/probes/audio-iir/skewed_lanes.cpp`'s `real`).
template <class T, std::size_t W, std::size_t... I>
HEINZEL_LOOPS_INLINE Mask<T, W> in_block_of(const auto& walk, std::size_t body, std::index_sequence<I...>) {
    using Wide = std::int64_t __attribute__((vector_size(8 * W)));
    const Wide section{static_cast<std::int64_t>(body * W + I)...};
    const auto real = (section <= (Wide{} + walk.step)) & (section > (Wide{} + (walk.step - walk.block)));
    return {__builtin_convertvector(real, typename Mask<T, W>::vec)};
}
template <class T, std::size_t W>
HEINZEL_LOOPS_INLINE auto in_block(const auto& walk, std::size_t body) {
    if constexpr (W == 1) {
        const std::int64_t sample = walk.step - static_cast<std::int64_t>(body);
        return sample >= 0 && sample < walk.block;
    } else {
        return in_block_of<T, W>(walk, body, std::make_index_sequence<W>{});
    }
}

template <class V>
HEINZEL_LOOPS_INLINE V blended(const auto& real, const V& made, const V& kept) {
    if constexpr (std::is_arithmetic_v<V>) {
        return real ? made : kept;
    } else {
        return select(real, made, kept);
    }
}

// A new value of `body`: the others move one slot on, the oldest leaves.
template <class T, std::size_t Lanes, std::size_t Count, std::size_t Slots, class V>
HEINZEL_LOOPS_INLINE void hand_on(State<T, Lanes, Count, Slots>& state, std::size_t body, const V& made) {
    for (std::size_t k = Slots - 1; k > 0; --k) {
        state.was[k][body] = state.was[k - 1][body];
    }
    state.was[0][body] = made;
}

// ...where the walk is in a skewed stage's head or drain step, a lane whose sample lies outside the block keeps every
// slot (`Plan/probes/audio-iir/skewed_lanes.cpp`'s `blend`).
template <class T, std::size_t Lanes, std::size_t Count, std::size_t Slots, class V>
HEINZEL_LOOPS_INLINE void hand_on(const auto& walk, State<T, Lanes, Count, Slots>& state, std::size_t body, const V& made) {
    if constexpr (kAtAnEdge<std::remove_cvref_t<decltype(walk)>>) {
        const auto real = in_block<T, Lanes>(walk, body);
        for (std::size_t k = Slots - 1; k > 0; --k) {
            state.was[k][body] = blended(real, state.was[k - 1][body], state.was[k][body]);
        }
        state.was[0][body] = blended<ValueOf<T, Lanes>>(real, made, state.was[0][body]);
    } else {
        hand_on(state, body, made);
    }
}

// The State register one lane up, as the link reads it: lane 0 takes `before`'s last lane (the body below, or the
// fill), lane l `now`'s lane l − 1 (`skewed_lanes.cpp`); at width 1, `before`.
template <class T, std::size_t W, std::size_t... I>
HEINZEL_LOOPS_INLINE Lane<T, W> shifted_in(Lane<T, W> before, Lane<T, W> now, std::index_sequence<I...>) {
    return {__builtin_shufflevector(before.v, now.v, (W - 1 + I)...)};
}
template <class T, std::size_t W>
HEINZEL_LOOPS_INLINE Lane<T, W> shifted_in(Lane<T, W> before, Lane<T, W> now) {
    return shifted_in(before, now, std::make_index_sequence<W>{});
}
template <class T>
HEINZEL_LOOPS_INLINE T shifted_in(T before, T) {
    return before;
}

template <class T, std::size_t Lanes, std::size_t Count, std::size_t Turns>
struct Gathered {
    std::array<std::array<ValueOf<T, Lanes>, Turns>, Count> values;
};

// Where a leaf stands: which body and which turn, and, where a loop chunks a fold, whether this is the first chunk and
// whether it is the last.
struct At {
    std::size_t body{0};
    std::size_t turn{0};
    bool first{true};
    bool last{true};
};

// An enumeration, `Unroll → … → Unroll [→ Vector]` with one kid each, is one set of bodies, enumerated at once rather
// than level by level: body b stands at a constant offset along each level's axis, and the Vector sets the width. A
// strip below Registers node `Full` (-1: none) counts as a full chunk, which is how a set is sized.
struct Level {
    std::int32_t axis{-1};
    std::int32_t step{1};
    std::size_t count{1};  // sub-chunks at this level
    std::size_t inner{1};  // bodies inside one of them
};
struct Chain {
    std::array<Level, 2 * kMaxAxes> level{};
    std::size_t levels{0};
    std::size_t bodies{1};
    std::size_t width{0};  // width 0: the width it is entered at
    std::int32_t end{-1};                       // the node whose kids run for every body
};
template <const Program& P, std::int32_t N, std::uint32_t Mode, std::int32_t Full = -1>
consteval Chain chain_from() {
    Chain c{};
    for (std::int32_t at = N; at >= 0; at = chain_next(P, at)) {
        const Node& n = node_at(P, at);
        const std::int64_t chunk = chunk_in(P, at, Mode, Full);
        c.end = at;
        if (n.kind == NodeKind::Vector) {
            c.width = static_cast<std::size_t>(chunk);
        } else {
            const std::size_t count = static_cast<std::size_t>(chunk > n.step ? chunk / n.step : 1);
            c.level[c.levels++] = Level{n.axis, n.step, count};
            c.bodies *= count;
        }
    }
    std::size_t inner = 1;
    for (std::size_t l = c.levels; l-- > 0;) {
        c.level[l].inner = inner;
        inner *= c.level[l].count;
    }
    return c;
}

template <const Program& P, std::size_t S, std::size_t K>
using accumulated_t = std::conditional_t<kStmt<P, S, K>.strict, double, element_t<kStmt<P, S, K>.type>>;

// A Registers node's set: one register per body of its chain, as wide as the chain's lanes.
template <const Program& P, std::size_t S, std::int32_t R, std::uint32_t Mode>
consteval auto set_of() {
    constexpr const Node& r = node_at(P, R);
    constexpr std::size_t k = static_cast<std::size_t>(r.stmt);
    constexpr Chain c = chain_from<P, r.link, Mode, R>();
    constexpr std::size_t lanes = c.width > 0 ? c.width : 1;
    using T = element_t<kStmt<P, S, k>.type>;
    if constexpr (r.role == Role::Accumulator) {
        return Accumulator<accumulated_t<P, S, k>, lanes, c.bodies>{};
    } else if constexpr (r.role == Role::State) {
        return State<T, lanes, c.bodies, static_cast<std::size_t>(r.slots)>{};
    } else {
        return Gathered<T, lanes, c.bodies, static_cast<std::size_t>(kStmt<P, S, k>.gathered)>{};
    }
}
template <const Program& P, std::size_t S, std::int32_t R, std::uint32_t Mode>
using SetOf = decltype(set_of<P, S, R, Mode>());

// Where node R's set sits in the registers tuple: after the resolved extents, one place further on for each Registers
// node above it.
template <const Program& P, std::int32_t R>
consteval std::size_t depth_of() {
    std::size_t depth = 1;
    for (std::int32_t at = node_at(P, R).parent; node_at(P, at).kind != NodeKind::Stage; at = node_at(P, at).parent) {
        depth += node_at(P, at).kind == NodeKind::Registers ? 1 : 0;
    }
    return depth;
}

// The set holding statement `K` in `role`, for leaf N.
template <const Program& P, std::int32_t N, std::int32_t K, Role role>
HEINZEL_LOOPS_INLINE auto& set_for(auto& regs) {
    constexpr std::int32_t r = registers_of(P, N, K, role);
    return std::get<depth_of<P, r>()>(regs);
}

// The axes of every Loop between a fold's Registers and its Accumulate leaves (N is one of the fold's leaves). The
// product of their resolved extents is how many points the fold folds; an axis walked by two loops counts twice. Only
// a mean reads it.
struct FoldedAxes {
    std::array<std::int32_t, 2 * kMaxAxes> axis{};
    std::size_t count{0};
};
template <const Program& P, std::int32_t N>
consteval FoldedAxes folded_axes() {
    const std::int32_t r = registers_of(P, N, node_at(P, N).stmt, Role::Accumulator);
    FoldedAxes f{};
    for (std::size_t n = static_cast<std::size_t>(r); n < P.nodes.size(); ++n) {
        if (P.nodes[n].kind != NodeKind::Accumulate || P.nodes[n].stmt != node_at(P, N).stmt || !encloses(P, r, static_cast<std::int32_t>(n))) {
            continue;
        }
        for (std::int32_t at = P.nodes[n].parent; at != r; at = node_at(P, at).parent) {
            if (node_at(P, at).kind == NodeKind::Loop && f.count < f.axis.size()) {
                f.axis[f.count++] = node_at(P, at).axis;
            }
        }
    }
    return f;
}

// Does a Loop that chunks the fold enclose its Registers (N being one of the fold's leaves)? Then the fold resumes
// between chunks.
template <const Program& P, std::int32_t N>
consteval bool resumes() {
    const std::int32_t r = registers_of(P, N, node_at(P, N).stmt, Role::Accumulator);
    return ancestor(P, r, [](const Node& a) { return a.kind == NodeKind::Loop && a.chunks_fold; }) >= 0;
}

// The strip of a fold's lanes where it stands outside the fold's Registers, which then restart at every turn of the
// loops between (a feedback stage whose sample holds a fold, Plan/23 10), the strip one chunk; -1: none. N is one of
// the fold's leaves.
template <const Program& P, std::int32_t N>
consteval std::int32_t strip_outside() {
    const std::int32_t r = registers_of(P, N, node_at(P, N).stmt, Role::Accumulator);
    const std::int32_t link = node_at(P, r).link;
    const std::int32_t strip = link < 0 ? -1 : governing_strip(P, link);
    return strip >= 0 && !encloses(P, r, strip) ? strip : -1;
}

// The lanes of a fold restarted every sample, inside one chunk of their strip: lane l of body b is element b·W + l of
// the fold's axis, its bodies one level of its chain side by side (W lanes each).
template <const Program& P, std::int32_t N, std::size_t W>
consteval bool bodies_side_by_side() {
    const std::int32_t r = registers_of(P, N, node_at(P, N).stmt, Role::Accumulator);
    constexpr std::uint32_t full = 0;
    const Chain c = chain_from<P, node_at(P, r).link, full, r>();
    return c.levels <= 1 && (c.levels == 0 || c.level[0].step == static_cast<std::int32_t>(W));
}

// A fold's lanes along its own axis, `acc`, folded into one number at width 1 (N one of its leaves). Inside the strip of
// its lanes: the bodies in order, then across the lane, then with the rest as the first operand (an Accumulate below a
// rest run one at a time folds into it; the operand order decides a max or min of +0 and -0). Restarted every sample,
// inside one chunk of the strip: no Accumulate runs one at a time, so no rest is added (a `+0` costs a loop's chain its
// latency), a masked tail's missing lanes take the identity, and a strict fold adds every real element in element order,
// from the first, as the reference walk does.
template <const Program& P, std::int32_t N, ReduceOp Op, bool Strict, class Acc>
HEINZEL_LOOPS_INLINE auto lanes_folded(const auto& walk, const Acc& acc) {
    using A = decltype(acc.rest);
    constexpr std::size_t W = Acc::kLanes;
    constexpr std::size_t bodies = Acc::kCount;
    if constexpr (strip_outside<P, N>() < 0) {
        auto lanes = acc.lanes[0];
        each<bodies - 1>([&] [[gnu::always_inline]] (std::size_t u) { lanes = fold_step(Op, lanes, acc.lanes[u + 1]); });
        return fold_step(Op, acc.rest, folded(Op, lanes));
    } else {
        static_assert(bodies_side_by_side<P, N, W>(), "heinzel::loops: a fold restarted every sample runs its bodies side by side along its axis");
        constexpr bool tail = std::remove_cvref_t<decltype(walk)>::tail;
        const std::int64_t real = tail ? walk.valid : static_cast<std::int64_t>(bodies * W); // (the strip's one chunk)
        if constexpr (Strict) {
            A so_far = acc.lanes[0][0];
            each<bodies * W - 1>([&] [[gnu::always_inline]] (std::size_t e) {
                if (static_cast<std::int64_t>(e) + 1 < real) {
                    so_far = fold_step(Op, so_far, acc.lanes[(e + 1) / W][(e + 1) % W]);
                }
            });
            return so_far;
        } else {
            const auto real_lanes = [&] [[gnu::always_inline]] (std::size_t b) {
                if constexpr (tail) {
                    const auto at = [&]<std::size_t... I>(std::index_sequence<I...>) { return Lane<A, W>{{static_cast<A>(b * W + I)...}}; };
                    return select(at(std::make_index_sequence<W>{}) < Lane<A, W>::splat(static_cast<A>(real)), acc.lanes[b],
                                  Lane<A, W>::splat(static_cast<A>(fold_identity(Op))));
                } else {
                    return acc.lanes[b];
                }
            };
            auto lanes = real_lanes(0);
            each<bodies - 1>([&] [[gnu::always_inline]] (std::size_t u) { lanes = fold_step(Op, lanes, real_lanes(u + 1)); });
            return folded(Op, lanes);
        }
    }
}

// ---- the leaves that use registers --------------------------------------------------------------------------

// Opens a fold's accumulators at its identity. Where they are wider than the width here (lanes along the fold's own
// axis), all of them and the rest are opened; otherwise only this body's, which after the first chunk resumes from the
// partial result in its store.
template <const Program& P, std::size_t S, std::int32_t N, std::size_t W>
HEINZEL_LOOPS_INLINE void init(const auto& walk, auto& regs, At at) {
    constexpr std::size_t k = static_cast<std::size_t>(node_at(P, N).stmt);
    constexpr ReduceOp op = kStmt<P, S, k>.fold;
    using A = accumulated_t<P, S, k>;
    auto& acc = set_for<P, N, static_cast<std::int32_t>(k), Role::Accumulator>(regs);
    using Acc = std::remove_cvref_t<decltype(acc)>;
    if constexpr (Acc::kLanes > W) {
        acc.rest = static_cast<A>(fold_identity(op));
        each<Acc::kCount>([&] [[gnu::always_inline]] (std::size_t b) { acc.lanes[b] = Broadcast<ValueOf<A, Acc::kLanes>>::of(fold_identity(op)); });
    } else {
        acc.lanes[at.body] = Broadcast<ValueOf<A, W>>::of(fold_identity(op));
        if constexpr (resumes<P, N>()) {
            if (!at.first) {
                acc.lanes[at.body] = read<P, S, k * kSlots + kMaxNodeInputs, W, A, element_t<kStmt<P, S, k>.type>>(walk);
            }
        }
    }
}

// Folds a contribution in. Below lanes along the fold's own axis, at width 1, it goes into the rest. Where the fold
// opens at its first element, the first contribution is taken as it is.
template <const Program& P, std::size_t S, std::int32_t N, std::size_t W, bool First, class... Locals>
HEINZEL_LOOPS_INLINE void accumulate(const auto& walk, auto& regs, At at, const Locals&... locals) {
    constexpr std::size_t k = static_cast<std::size_t>(node_at(P, N).stmt);
    constexpr const Stmt& st = kStmt<P, S, k>;
    using A = accumulated_t<P, S, k>;
    auto& acc = set_for<P, N, static_cast<std::int32_t>(k), Role::Accumulator>(regs);
    const ValueOf<A, W> a = operand<P, S, k, 0, W, A>(walk, locals...);
    const ValueOf<A, W> b = operand<P, S, k, 1, W, A>(walk, locals...);
    if constexpr (std::remove_cvref_t<decltype(acc)>::kLanes > W) {
        acc.rest = fold_in(st.collapse, st.fold, st.fused, acc.rest, a, b);
    } else if constexpr (First && node_at(P, registers_of(P, N, static_cast<std::int32_t>(k), Role::Accumulator)).opens_first) {
        acc.lanes[at.body] = contribution_as(st.collapse, st.fused, a, b);
    } else {
        acc.lanes[at.body] = fold_in(st.collapse, st.fold, st.fused, acc.lanes[at.body], a, b);
    }
}

// Where the fold's value is kept as state, it is written into its State register too (and stored only by its Close,
// where that closes it): a feedback stage reads its fold's past (Plan/23 10).
template <const Program& P, std::size_t S, std::int32_t N, std::size_t W, class V>
HEINZEL_LOOPS_INLINE void kept(const auto& walk, auto& regs, At at, const V& made) {
    constexpr const Node& n = node_at(P, N);
    if constexpr (!n.closes) {
        stored<P, S, static_cast<std::size_t>(n.stmt), W>(walk, made);
    }
    if constexpr (n.keeps >= 0) {
        hand_on(walk, set_for<P, N, n.keeps, Role::State>(regs), at.body, made);
    }
}

// Finishes the fold, stores its value and hands it on. With lanes along the fold's own axis, they are folded together
// at width 1 (`lanes_folded`); otherwise this body's accumulator is finished. A mean divides by the points folded,
// restarted every sample those of its strip's one chunk.
template <const Program& P, std::size_t S, std::int32_t N, std::size_t W>
HEINZEL_LOOPS_INLINE auto finish(const auto& walk, auto& regs, At at) {
    constexpr std::size_t k = static_cast<std::size_t>(node_at(P, N).stmt);
    constexpr ReduceOp op = kStmt<P, S, k>.fold;
    using A = accumulated_t<P, S, k>;
    using T = element_t<kStmt<P, S, k>.type>;
    auto& acc = set_for<P, N, static_cast<std::int32_t>(k), Role::Accumulator>(regs);
    using Acc = std::remove_cvref_t<decltype(acc)>;
    constexpr FoldedAxes f = folded_axes<P, N>();
    const Extents& whole = std::get<0>(regs);
    std::int64_t points = 1;
    each<f.count>([&] [[gnu::always_inline]] (std::size_t i) { points *= whole[static_cast<std::size_t>(f.axis[i])]; });
    if constexpr (Acc::kLanes > W) {
        static_assert(W == 1, "heinzel::loops: a fold's lanes are folded together outside them");
        if constexpr (constexpr std::int32_t strip = strip_outside<P, N>(); strip >= 0) {
            points *= whole[static_cast<std::size_t>(node_at(P, strip).axis)];
        }
        const T made = static_cast<T>(fold_finish(op, lanes_folded<P, N, op, kStmt<P, S, k>.strict>(walk, acc), points));
        kept<P, S, N, 1>(walk, regs, at, made);
        return made;
    } else {
        const auto made = converted<T, A, W>(fold_finish(op, acc.lanes[at.body], points));
        kept<P, S, N, W>(walk, regs, at, made);
        return made;
    }
}

// Between chunks, the accumulator is stored where the next chunk resumes from.
template <const Program& P, std::size_t S, std::int32_t N, std::size_t W>
HEINZEL_LOOPS_INLINE void partial(const auto& walk, auto& regs, At at) {
    constexpr std::size_t k = static_cast<std::size_t>(node_at(P, N).stmt);
    auto& acc = set_for<P, N, static_cast<std::int32_t>(k), Role::Accumulator>(regs);
    stored<P, S, k, W>(walk, converted<element_t<kStmt<P, S, k>.type>, accumulated_t<P, S, k>, W>(acc.lanes[at.body]));
}

// State handed on is read from memory once per block, before the loop it is handed through: it is the sample before
// the block.
template <const Program& P, std::size_t S, std::int32_t N, std::size_t W>
HEINZEL_LOOPS_INLINE void open(const auto& walk, auto& regs, At at) {
    constexpr const Node& n = node_at(P, N);
    auto& state = set_for<P, N, n.keeps, Role::State>(regs);
    state.was[static_cast<std::size_t>(n.slot)][at.body] =
        operand<P, S, static_cast<std::size_t>(n.stmt), 0, W, element_t<kStmt<P, S, static_cast<std::size_t>(n.keeps)>.type>>(walk);
}

// ...and where every read of it is its register, it is written back after that loop where it was opened from: its past
// held in place (`Node::closes`, `Buffer::only_past`).
template <const Program& P, std::size_t S, std::int32_t N, std::size_t W>
HEINZEL_LOOPS_INLINE void close(const auto& walk, auto& regs, At at) {
    constexpr const Node& n = node_at(P, N);
    const auto& state = set_for<P, N, n.keeps, Role::State>(regs);
    written_back<P, S, static_cast<std::size_t>(n.stmt), 0, W>(walk, state.was[static_cast<std::size_t>(n.slot)][at.body]);
}

// The link's value (`Node::shifted`): the State register of statement `reads` one lane up, body 0's lane 0 taking the
// link's operand 0, the fill. The section before a lane, at the lane's sample, was made one step ago one lane down; the
// bodies run last first, so the body below is still as that step left it.
template <const Program& P, std::size_t S, std::int32_t N, std::size_t W, class... Locals>
HEINZEL_LOOPS_INLINE auto linked(const auto& walk, auto& regs, At at, const Locals&... locals) {
    constexpr const Node& n = node_at(P, N);
    constexpr std::size_t k = static_cast<std::size_t>(n.stmt);
    using From = element_t<kStmt<P, S, static_cast<std::size_t>(n.reads)>.type>;
    const auto& was = set_for<P, N, n.reads, Role::State>(regs).was[0];
    const ValueOf<From, W> below = at.body == 0 ? operand<P, S, k, 0, W, From>(walk, locals...) : was[at.body - 1];
    return converted<element_t<kStmt<P, S, k>.type>, From, W>(shifted_in(below, was[at.body]));
}

// Computes a Load, a Map or a Gather (or, where it reads state, takes the State register instead of its operand),
// stores the value and hands it on. Where the value is kept as state, it is also written into its State register (and
// stored only by its Close, where that closes it). In a skewed stage the link reads its register shifted, and a
// Compute at one lane reads and stores at that lane's sample.
template <const Program& P, std::size_t S, std::int32_t N, std::size_t W, class... Locals>
HEINZEL_LOOPS_INLINE auto compute(const auto& walk, auto& regs, At at, const Locals&... locals) {
    constexpr const Node& n = node_at(P, N);
    constexpr std::size_t k = static_cast<std::size_t>(n.stmt);
    using T = element_t<kStmt<P, S, k>.type>;
    const auto made = [&] [[gnu::always_inline]] {
        if constexpr (n.shifted) {
            return linked<P, S, N, W>(walk, regs, at, locals...);
        } else if constexpr (n.reads >= 0) {
            using From = element_t<kStmt<P, S, static_cast<std::size_t>(n.reads)>.type>;
            return converted<T, From, W>(set_for<P, N, n.reads, Role::State>(regs).was[static_cast<std::size_t>(n.slot)][at.body]);
        } else if constexpr (n.lane >= 0) {
            return made_at_lane<P, S, k, n.lane, W>(walk, at.body, locals...);
        } else {
            return made_by<P, S, k, W>(walk, locals...);
        }
    }();
    if constexpr (n.lane >= 0) {
        stored_at_lane<P, S, k, n.lane, W>(walk, at.body, made);
    } else if constexpr (!n.closes) {
        stored<P, S, k, W>(walk, made);
    }
    if constexpr (n.keeps >= 0) {
        hand_on(walk, set_for<P, N, n.keeps, Role::State>(regs), at.body, made);
    }
    return made;
}

template <const Program& P, std::size_t S, std::int32_t N, std::size_t W, class... Locals>
HEINZEL_LOOPS_INLINE void collect(const auto& walk, auto& regs, At at, const Locals&... locals) {
    constexpr std::size_t k = static_cast<std::size_t>(node_at(P, N).stmt);
    set_for<P, N, static_cast<std::int32_t>(k), Role::Gathered>(regs).values[at.body][at.turn] =
        operand<P, S, k, 0, W, element_t<kStmt<P, S, k>.type>>(walk, locals...);
}

// One compare-exchange: the smaller to `low`, the larger to `high`, by the same `<` the reference walk uses.
template <class V>
HEINZEL_LOOPS_INLINE void compare_exchange(V& low, V& high) {
    const V a = low;
    const V b = high;
    if constexpr (std::is_arithmetic_v<V>) {
        low = a < b ? a : b;
        high = a < b ? b : a;
    } else {
        low = select(a < b, a, b);
        high = select(a < b, b, a);
    }
}

// The value at the network's rank among what one body gathered, sorted only as far as that rank needs; it is stored
// and handed on.
template <const Program& P, std::size_t S, std::int32_t N, std::size_t W>
HEINZEL_LOOPS_INLINE auto rank(const auto& walk, auto& regs, At at) {
    constexpr std::size_t k = static_cast<std::size_t>(node_at(P, N).stmt);
    constexpr const Stmt& st = kStmt<P, S, k>;
    static constexpr auto net = order::net_for<st.gathered, st.rank, st.rank>();
    auto values = set_for<P, N, static_cast<std::int32_t>(k), Role::Gathered>(regs).values[at.body];
    each<net.size()>([&] [[gnu::always_inline]] (std::size_t c) {
        compare_exchange(values[static_cast<std::size_t>(net.ce[c].a)], values[static_cast<std::size_t>(net.ce[c].b)]);
    });
    const auto made = values[static_cast<std::size_t>(st.rank)];
    stored<P, S, k, W>(walk, made);
    return made;
}

} // namespace heinzel::loops
