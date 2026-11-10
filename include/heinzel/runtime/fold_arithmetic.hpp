#pragma once

// The arithmetic a fold applies to its numbers: one step of a fold and its finish, keyed on `ReduceOp`.
// It names no operation and no attribute, so it lives beside the cores rather than in `runtime/compute.hpp`,
// the only file that writes an operation's arithmetic. The loop tree calls it with the fold its statement carries;
// the reference walk reads the fold off the node's attribute (`tests/reference/compute_reference.hpp`).

#include <cstdint>

#include <limits>

#include "heinzel/ir/vocab.hpp"
#include "heinzel/runtime/hardware_twins.hpp"
#include "heinzel/runtime/value_type.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel {

// One step of a fold.
//
// A fold starts at its first element rather than at an identity, and that is a decision. `Max` and
// `Min` would need an infinity to start from, and an infinity is then a value the arithmetic has to
// be trusted to remove again; starting at the first element needs none. Nothing refuses an axis of no
// elements, so a fold over one has no first element: whether to refuse it or define it (a `Mean` of
// none divides by zero) is open. `Mean` is a sum with a division at the end, which is why finishing is a
// step of its own rather than something each caller remembers to do.
template <class V>
constexpr V fold_step(ReduceOp op, V so_far, V x) {
    switch (op) {
    case ReduceOp::Sum:
    case ReduceOp::Mean:
        return so_far + x;
    case ReduceOp::Prod:
        return so_far * x;
    case ReduceOp::Max:
        return select(x > so_far, x, so_far);
    case ReduceOp::Min:
        return select(x < so_far, x, so_far);
    }
    fail("heinzel::fold_step: a reduction operation with no step written");
}

// What one point contributes to a fold, by the kind of collapse the node's attribute describes: a plain
// fold contributes its operand; a contraction, of either spelling, the product of its two, and so does a plain sum
// fused with the product it folds (`fused`: the product's operands are its two).
template <class V>
constexpr V contribution_as(AttrKind kind, bool fused, V a, V b) {
    return kind == AttrKind::Reduce && !fused ? a : a * b;
}

// One step of a fold from its operands. `fused` is a decision the precision had to grant: a
// contraction's multiply and the sum's add — or a sum's add and the product it folds — evaluated as one instruction,
// rounded as the machine rounds — once where the build has the instruction (another, more accurate number than the
// two), twice where it does not, never a software core (`_madd`'s arithmetic, `runtime/compute.hpp`).
template <class V>
constexpr V fold_in(AttrKind kind, ReduceOp op, bool fused, V so_far, V a, V b) {
    if (fused && (op == ReduceOp::Sum || op == ReduceOp::Mean)) {
        return multiply_add_as_the_machine_rounds(a, b, so_far);
    }
    return fold_step(op, so_far, contribution_as(kind, fused, a, b));
}

// The value a fold may start from instead of its first element: the one `fold_step` leaves every `x`
// unchanged against. A fold in one strict order does not need it (it starts at its first element, which
// is what keeps a sum of negative zeros negative). A fold split into lanes does: each partial fold has
// to start somewhere, and a lane past the end of a short fold must not move the answer.
constexpr double fold_identity(ReduceOp op) {
    switch (op) {
    case ReduceOp::Sum:
    case ReduceOp::Mean:
        return 0.0;
    case ReduceOp::Prod:
        return 1.0;
    case ReduceOp::Max:
        return -std::numeric_limits<double>::infinity();
    case ReduceOp::Min:
        return std::numeric_limits<double>::infinity();
    }
    fail("heinzel::fold_identity: a reduction operation with no identity written");
}

// The finish, where a mean divides by how many it summed and every other fold hands back what it has.
template <class V>
constexpr V fold_finish(ReduceOp op, V so_far, std::int64_t count) {
    return op == ReduceOp::Mean ? so_far / Broadcast<V>::of(static_cast<double>(count)) : so_far;
}

} // namespace heinzel
