#pragma once

// The elements one output of an `_order` is an ordered rank of: the `ElementKind::Ordered` meaning. An
// order statistic reads all L elements along an axis and produces a joint function of them (an ascending
// sort, the k largest, or an interpolated quantile) through a fixed compare-exchange network
// (`support/order_net.hpp`), not a fold. `order_plan` reads the attribute; the indexing below names no
// operation and no attribute.

#include <cstddef>
#include <cstdint>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/vocab.hpp"
#include "heinzel/support/fail.hpp"
#include "heinzel/support/order_net.hpp"

#include "elements.hpp"

namespace heinzel {

// The contiguous ascending rank range an order op keeps, into the sorted-ascending order — the whole
// vocabulary the network generator needs to prune to (`support/order_net.hpp`). Sort keeps every rank; TopK
// the top k (the L-k'th ascending rank upwards); Quantile the one or two ranks its interpolation reads.
struct OrderRanks {
    std::int32_t r0{0};
    std::int32_t r1{0};
    constexpr bool operator==(const OrderRanks&) const = default;
};

constexpr OrderRanks order_ranks(OrderOp op, std::int32_t length, std::int32_t k, double q) {
    if (op == OrderOp::Sort) {
        return OrderRanks{0, length - 1};
    }
    if (op == OrderOp::TopK) {
        return OrderRanks{length - k, length - 1};
    }
    const order::QRank qr = order::quantile_rank(length, q);
    return OrderRanks{qr.lo, qr.hi};
}

// Everything the walk needs to run one `_order` node, read off its attribute once: which statistic, the
// ordered axis and its extent L, how many groups (batches) there are, the ascending rank range the network
// prunes to, whether the axis is kept, and the resolved quantile ranks. The guards are the ones a
// hand-built node reaching the walk without the analysis pass could violate, refused rather than read into
// a wrong index.
struct OrderPlan {
    OrderOp op{OrderOp::Sort};
    std::int32_t axis{0};
    std::int32_t length{0}; // L, the ordered extent
    std::int32_t groups{1};
    std::int32_t k{1};
    std::int32_t r0{0};
    std::int32_t r1{0};
    bool kept{true};
    order::QRank q{}; // the resolved quantile ranks, meaningful only for Quantile
    constexpr bool operator==(const OrderPlan&) const = default;
};

constexpr OrderPlan order_plan(const Node& n, const Elements& from) {
    if (n.attr.kind != AttrKind::Order) {
        fail("heinzel::order_plan: this is an order statistic and carries no order attribute");
    }
    const OrderAttr a = n.attr.order;
    if (a.axis < 0 || a.axis >= from.rank) {
        fail("heinzel::order_plan: an order along an axis that is not an element axis here");
    }
    OrderPlan p{};
    p.op = a.op;
    p.axis = a.axis;
    p.length = from.extent[a.axis];
    if (p.length < 1) {
        fail("heinzel::order_plan: an ordered axis of no elements");
    }
    if (a.op == OrderOp::TopK && (a.k < 1 || a.k > p.length)) {
        fail("heinzel::order_plan: a top-k count outside one to the ordered extent");
    }
    p.groups = from.count / p.length;
    p.k = a.k;
    p.kept = order_keeps_axis(a.op);
    const OrderRanks rr = order_ranks(a.op, p.length, a.k, a.q);
    p.r0 = rr.r0;
    p.r1 = rr.r1;
    p.q = order::quantile_rank(p.length, a.q); // fails on a q outside [0,1], the analysis check's backstop
    return p;
}

// Apply a compare-exchange network to a buffer in place: min → the low wire, max → the high wire, every CE
// having a < b so the result is ascending. The walk and the loop tree (`loops/registers.hpp`) apply the same
// comparator list, pruned to the ranks kept (`support/order_net.hpp`), with the same `<`, so they move every
// value the same way and agree bit for bit.
constexpr void order_compare_exchange(const order::CE* ce, std::size_t count, double* buf) {
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t a = static_cast<std::size_t>(ce[i].a);
        const std::size_t b = static_cast<std::size_t>(ce[i].b);
        const double lo = buf[a] < buf[b] ? buf[a] : buf[b];
        const double hi = buf[a] < buf[b] ? buf[b] : buf[a];
        buf[a] = lo;
        buf[b] = hi;
    }
}

// The walk works by group, not by output element: a group is one choice of every non-ordered element axis,
// and there are `count / L` of them. Each group's L values are gathered and sorted once; sorting per output
// element would run the O(L·log²L) network up to L times per group.
//
// The operand element the `t`-th value of a group reads: decompose `group` across the NON-ordered element
// axes, set the ordered axis to `t`, recompose against the operand's extents — a scatter of indices, right
// for any rank. Read innermost outwards, the order every flat index here is laid out in.
constexpr std::int32_t order_group_input(const Elements& from, std::int32_t axis, std::int32_t group,
                                         std::int32_t t) {
    if (axis < 0 || axis >= from.rank || t < 0 || t >= from.extent[axis]) {
        fail("heinzel::order_group_input: an ordered position outside the ordered axis");
    }
    std::int32_t index[kMaxRank]{};
    std::int32_t rest = group;
    for (std::int32_t k = from.rank; k > 0; --k) {
        if (k - 1 == axis) {
            index[k - 1] = t;
        } else {
            index[k - 1] = rest % from.extent[k - 1];
            rest /= from.extent[k - 1];
        }
    }
    std::int32_t flat = 0;
    for (std::int32_t k = 0; k < from.rank; ++k) {
        flat = flat * from.extent[k] + index[k];
    }
    return flat;
}

// The output element the `r`-th kept rank of a group writes to: the same batch decomposition as the input,
// then recomposed against the OUTPUT's extents — with `r` placed at the ordered axis when it is KEPT
// (Sort/TopK), or the axis skipped when it is DROPPED (Quantile, whose output has one axis fewer, the axes
// past the ordered one shifted in). Right for any rank because it is a scatter of indices, not a stride.
constexpr std::int32_t order_group_output(const Elements& to, const Elements& from, std::int32_t axis,
                                          bool kept, std::int32_t group, std::int32_t r) {
    std::int32_t batch[kMaxRank]{};
    std::int32_t rest = group;
    for (std::int32_t k = from.rank; k > 0; --k) {
        if (k - 1 == axis) {
            continue; // the ordered axis is not part of the batch
        }
        batch[k - 1] = rest % from.extent[k - 1];
        rest /= from.extent[k - 1];
    }
    std::int32_t flat = 0;
    for (std::int32_t a = 0; a < from.rank; ++a) {
        if (a == axis) {
            if (kept) {
                flat = flat * to.extent[a] + r; // the kept ordered axis holds rank r
            }
            continue; // a dropped ordered axis is absent from the output
        }
        const std::int32_t to_axis = (kept || a < axis) ? a : a - 1; // a drop shifts axes past the ordered one in
        flat = flat * to.extent[to_axis] + batch[a];
    }
    return flat;
}

// How many output positions along the (kept or collapsed) ordered axis a group produces: Sort every rank,
// TopK its k, Quantile a single interpolated value.
constexpr std::int32_t order_output_count(const OrderPlan& p) {
    if (p.op == OrderOp::Sort) {
        return p.length;
    }
    if (p.op == OrderOp::TopK) {
        return p.k;
    }
    return 1;
}

// The value output slot `s` of a group takes, read from the group's ASCENDING-sorted buffer. Sort writes
// ascending rank s; TopK the s-th LARGEST (descending, so output[0] is the max); Quantile the value at its
// resolved rank — exact when the interpolation weight is zero (bit-exact, no arithmetic), else the linear
// blend of the two straddling ranks.
constexpr double order_output_value(const OrderPlan& p, const double* buf, std::int32_t s) {
    if (p.op == OrderOp::Sort) {
        return buf[s];
    }
    if (p.op == OrderOp::TopK) {
        return buf[p.length - 1 - s];
    }
    if (p.q.w == 0.0) {
        return buf[p.q.lo];
    }
    return (1.0 - p.q.w) * buf[p.q.lo] + p.q.w * buf[p.q.hi];
}

} // namespace heinzel
