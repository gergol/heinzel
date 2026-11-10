#pragma once

// The shape a general contraction (`_matmul`) produces, and its refusals: the transposing, batched contraction
// `_contr` cannot express. `_contr` broadcasts its operands together and collapses axes of the one joined shape; a
// matmul binds a different axis of each operand and keeps their free axes side by side.
//
// The shape runs in the sweep, before `check_matmul_attribute` can refuse a malformed attribute, so every axis access
// is bounds-guarded and a malformed attribute yields a placeholder shape: an out-of-range read under constant
// evaluation would abort the whole analysis instead of letting it return the refusal. The check is the one place a
// matmul is refused, once the shapes are resolved.

#include <cstdint>
#include <string_view>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/vocab.hpp"
#include "heinzel/sema/refusal.hpp"

namespace heinzel {

namespace detail {

// Is `axis` the contracted axis, the streamed one, or a batch axis of this operand? Batch axes are in
// the output, but once, at the front, taken from operand a alone, so the free-axis walk skips them.
// `batch` is that operand's own list (batch_a or batch_b).
constexpr bool matmul_axis_consumed(std::int32_t axis, std::int32_t contract, const Axes& batch,
                                    std::int32_t rank) {
    if (axis == contract || axis == rank - 1) {
        return true;
    }
    for (std::int32_t k = 0; k < batch.rank; ++k) {
        if (batch.axes[k] == axis) {
            return true;
        }
    }
    return false;
}

// Append operand `s`'s free axes (every element axis, 0 .. rank-2, neither contracted nor batched) to
// `out`, in the operand's own order. It writes no more than `kMaxRank` axes.
constexpr void append_free_axes(Shape& out, const Shape& s, std::int32_t contract, const Axes& batch) {
    for (std::int32_t k = 0; k + 1 < s.rank; ++k) {
        if (matmul_axis_consumed(k, contract, batch, s.rank)) {
            continue;
        }
        if (out.rank < static_cast<std::int32_t>(kMaxRank)) {
            set_extent(out, out.rank++, extent_at(s, k));
        }
    }
}

// The shape of a general contraction: `[batch] ++ [a's free axes] ++ [b's free axes] ++ [streamed]`.
// The batch extents come from operand a (the check holds b's to them), and the streamed axis
// passes through unchanged.
constexpr Shape contract_general_shape(const Shape& a, const Shape& b, const MatMulAttr& m) {
    Shape out{};
    for (std::int32_t k = 0; k < m.batch_a.rank; ++k) {
        const std::int32_t ax = m.batch_a.axes[k];
        if (ax >= 0 && ax < a.rank && out.rank < static_cast<std::int32_t>(kMaxRank)) {
            set_extent(out, out.rank++, extent_at(a, ax));
        }
    }
    append_free_axes(out, a, m.a_contract, m.batch_a);
    append_free_axes(out, b, m.b_contract, m.batch_b);
    if (a.rank >= 1 && out.rank < static_cast<std::int32_t>(kMaxRank)) {
        set_extent(out, out.rank++, extent_at(a, a.rank - 1));
    }
    return out;
}

// The same, read for a whole node.
constexpr Shape contract_general_node_shape(const Graph& g, const Node& n) {
    return contract_general_shape(g.values[static_cast<std::size_t>(n.inputs[0])].shape,
                                  g.values[static_cast<std::size_t>(n.inputs[1])].shape, n.attr.matmul);
}

// A named axis of a contraction must be an element axis of its operand: in range, and never the
// streamed one (time passes through a contraction). `what` names which axis the complaint is about.
constexpr Diagnostic matmul_axis_is_element(OpCode op, std::int32_t axis, std::int32_t rank, std::string_view what) {
    if (is_element_axis(axis, rank)) {
        return Diagnostic{};
    }
    if (axis < 0 || axis >= rank) {
        return refusal(op, "'s ", what, " axis ", axis, " is not an axis of a rank-", rank, " operand");
    }
    return refusal(op, " may not contract the streamed (innermost) axis ", axis, "; a contraction is over element axes");
}

// The two operands' contracted axes must have the same extent, and so must each batch pair and the
// streamed axis. After resolution `same_extent` answers for a literal or an already-unified symbol; two
// unrelated symbolic extents read as a disagreement, since the graph has said less than a matmul needs.
constexpr Diagnostic matmul_extents_agree(OpCode op, Extent a, Extent b, std::string_view what) {
    if (same_extent(a, b)) {
        return Diagnostic{};
    }
    return refusal(op, "'s ", what, " extents disagree: ", a, " and ", b);
}

// The whole check: the two axis bindings are element axes, the batch lists pair up, and every axis a
// contraction rests on agrees in extent between the two operands.
constexpr Diagnostic check_matmul_attribute(const Graph& g, const Node& n) {
    const Shape& a = g.values[static_cast<std::size_t>(n.inputs[0])].shape;
    const Shape& b = g.values[static_cast<std::size_t>(n.inputs[1])].shape;
    const MatMulAttr& m = n.attr.matmul;

    Diagnostic d = matmul_axis_is_element(n.op, m.a_contract, a.rank, std::string_view{"first operand's contracted"});
    if (!d.ok) { return d; }
    d = matmul_axis_is_element(n.op, m.b_contract, b.rank, std::string_view{"second operand's contracted"});
    if (!d.ok) { return d; }

    if (m.batch_a.rank != m.batch_b.rank) {
        return refusal(n.op, "'s batch axis lists must pair up, but name ", m.batch_a.rank, " and ", m.batch_b.rank, " axes");
    }
    for (std::int32_t k = 0; k < m.batch_a.rank; ++k) {
        d = matmul_axis_is_element(n.op, m.batch_a.axes[k], a.rank, std::string_view{"first operand's batch"});
        if (!d.ok) { return d; }
        d = matmul_axis_is_element(n.op, m.batch_b.axes[k], b.rank, std::string_view{"second operand's batch"});
        if (!d.ok) { return d; }
        d = matmul_extents_agree(n.op, extent_at(a, m.batch_a.axes[k]), extent_at(b, m.batch_b.axes[k]),
                                 std::string_view{"batch"});
        if (!d.ok) { return d; }
    }

    d = matmul_extents_agree(n.op, extent_at(a, m.a_contract), extent_at(b, m.b_contract),
                             std::string_view{"contracted"});
    if (!d.ok) { return d; }
    if (a.rank >= 1 && b.rank >= 1) {
        d = matmul_extents_agree(n.op, extent_at(a, a.rank - 1), extent_at(b, b.rank - 1),
                                 std::string_view{"streamed"});
        if (!d.ok) { return d; }
    }
    return Diagnostic{};
}

} // namespace detail

} // namespace heinzel
