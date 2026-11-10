#pragma once

// The structural operations, and the members that spell them: `x.sum(1)`, `x.transpose({1, 0})`,
// `x.unfold(0, 3)`, `x.median(2)`. A structural operation carries an attribute besides its operands, so it
// has a builder of its own that appends one node and decides nothing; the notation's lowerings call these
// builders too. A member counts axes from the front and refuses a negative one (`-1` would name time); an
// axis or a window that does not fit is the analysis's to refuse, with the shapes in hand. An `Output` and a
// `Variable` take the same members (`forwarding::members`, at the end).

#include <bit>
#include <concepts>
#include <cstdint>
#include <limits>

#include "heinzel/frontend/arena.hpp"
#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/vocab.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel {

namespace detail {

// Append a one-operand node carrying `attr` beside its operand, and hand back a handle to it.
constexpr Expr append_unary(Expr x, OpCode code, OpAttr attr) {
    x.catch_up();
    BuildNode n{};
    n.op = code;
    n.inputs[0] = x.position();
    n.attr = attr;
    x.arena()->nodes.push_back(n);
    return Expr{x.arena(), static_cast<std::int32_t>(x.arena()->nodes.size() - 1)};
}

// A reduction: the named axes collapse, folded the way `how` says; `keepdims` keeps each at extent 1.
constexpr Expr reduce(Expr x, ReduceOp how, Axes axes, bool keepdims = false) {
    OpAttr attr{};
    attr.kind = AttrKind::Reduce;
    attr.reduce.op = how;
    attr.reduce.axes = axes;
    attr.reduce.keep_axes = keepdims;
    return append_unary(x, op::Reduce, attr);
}

// Output axis k reads source axis `order.axes[k]`. The analysis checks it is a permutation that leaves time
// last, and completes an order one entry short with the last axis.
constexpr Expr transpose(Expr x, Axes order) {
    OpAttr attr{};
    attr.kind = AttrKind::View;
    attr.view.kind = ViewKind::Transpose;
    attr.view.order = order;
    return append_unary(x, op::Transpose, attr);
}

// The same elements read with other extents: `target` is the full output shape.
constexpr Expr reshape(Expr x, Shape target) {
    OpAttr attr{};
    attr.kind = AttrKind::View;
    attr.view.kind = ViewKind::Reshape;
    attr.view.shape = target;
    return append_unary(x, op::Reshape, attr);
}

// The same elements regrouped as a spec writes it: the sizes it gives its labels, and where each side's groups start
// (`ViewAttr::operand_groups`). The analysis works the target out once the operand has its shape.
constexpr Expr regroup(Expr x, Shape sizes, LabelMask operand_groups, LabelMask result_groups) {
    OpAttr attr{};
    attr.kind = AttrKind::View;
    attr.view.kind = ViewKind::Reshape;
    attr.view.operand_groups = operand_groups;
    attr.view.result_groups = result_groups;
    attr.view.shape = sizes;
    return append_unary(x, op::Reshape, attr);
}

// A sliding window of `window` elements at stride `hop` over `axis`: that axis becomes the frame count and a
// window axis appears at `axis + 1`. Over a signal's time it frames the stream: the frame count becomes the
// stream's length.
constexpr Expr unfold(Expr x, std::int32_t axis, std::int32_t window, std::int32_t hop) {
    OpAttr attr{};
    attr.kind = AttrKind::View;
    attr.view.kind = ViewKind::Unfold;
    attr.view.axis = axis;
    attr.view.window = window;
    attr.view.hop = hop;
    attr.view.frames = -1;
    return append_unary(x, op::Unfold, attr);
}

// Two outer axes windowed at once, each at stride 1; `axis2 > axis`.
constexpr Expr unfold2d(Expr x, std::int32_t axis, std::int32_t window, std::int32_t axis2, std::int32_t window2) {
    OpAttr attr{};
    attr.kind = AttrKind::View;
    attr.view.kind = ViewKind::Unfold2d;
    attr.view.axis = axis;
    attr.view.window = window;
    attr.view.axis2 = axis2;
    attr.view.window2 = window2;
    return append_unary(x, op::Unfold2d, attr);
}

// A strided sub-array of one axis: `length` elements from `offset`, `step` apart.
constexpr Expr slice(Expr x, std::int32_t axis, std::int32_t offset, std::int32_t length, std::int32_t step = 1) {
    OpAttr attr{};
    attr.kind = AttrKind::View;
    attr.view.kind = ViewKind::Slice;
    attr.view.axis = axis;
    attr.view.offset = offset;
    attr.view.length = length;
    attr.view.step = step;
    return append_unary(x, op::Slice, attr);
}

// An axis of extent 1 dropped, or inserted (both `_reshape`s, as autodiff emits them).
constexpr Expr squeeze(Expr x, std::int32_t axis) {
    OpAttr attr{};
    attr.kind = AttrKind::View;
    attr.view.kind = ViewKind::Squeeze;
    attr.view.axis = axis;
    return append_unary(x, op::Reshape, attr);
}
constexpr Expr unsqueeze(Expr x, std::int32_t axis) {
    OpAttr attr{};
    attr.kind = AttrKind::View;
    attr.view.kind = ViewKind::Unsqueeze;
    attr.view.axis = axis;
    return append_unary(x, op::Reshape, attr);
}

// A materialisation barrier: the identity, which no pass fuses away.
constexpr Expr realize(Expr x) { return append_unary(x, op::Realize, OpAttr{}); }

// A general contraction: a chosen axis of each operand summed, batch axes shared (`MatMulAttr`).
constexpr Expr matmul(Expr a, Expr b, MatMulAttr binding) {
    Arena* arena = bring_together(a, b);
    BuildNode n{};
    n.op = op::MatMul;
    n.inputs[0] = a.position();
    n.inputs[1] = b.position();
    n.attr.kind = AttrKind::MatMul;
    n.attr.matmul = binding;
    arena->nodes.push_back(n);
    return Expr{arena, static_cast<std::int32_t>(arena->nodes.size() - 1)};
}

// How many cells a table holds: the numbers of one sample of it, which a leaf states where it is made (an `Input` of
// a shape, a `constant(…)`) and only the analysis knows of a computed value. A signal of one number is no table.
constexpr std::int32_t cells_of(Expr table) {
    table.catch_up();
    const Shape& shape = table.arena()->nodes[static_cast<std::size_t>(table.position())].shape;
    const std::int32_t cells = numbers_per_sample(shape);
    if (shape.rank < 2 || cells == kWidthNotKnown) {
        fail("heinzel: a table is an Input with a shape or a constant(...), its cells known where it is read");
    }
    return cells;
}

// Where the last of `cells` cells lies, in turns: (N - 1)/N, rounded down to a float where it is none, so that no
// value's type rounds it past the last cell (`lookup`). Exact for N a power of two; the product is exact below 2^29.
constexpr double last_cell(std::int32_t cells) {
    const double exact = static_cast<double>(cells - 1) / static_cast<double>(cells);
    const auto nearest = static_cast<float>(exact);
    const bool past = static_cast<double>(nearest) * static_cast<double>(cells) > static_cast<double>(cells - 1);
    return past ? std::bit_cast<float>(std::bit_cast<std::uint32_t>(nearest) - 1U) : nearest;
}

// `lookup`'s scale from x to its phase, the last cell's place over hi - lo. N points from lo to hi lie (hi - lo)/(N - 1)
// apart, which one cell does not space; and where a double does not hold the bounds, their span or the scale, the read
// is a NaN (at every x for an infinite bound). Each is compared before anything is computed from it: a constant
// expression that makes a NaN stops in other words than these.
constexpr double lookup_scale(std::int32_t cells, double lo, double hi) {
    constexpr double most = std::numeric_limits<double>::max();
    if (cells < 2) {
        fail("heinzel: lookup(table, x, lo, hi) needs a table of at least 2 cells");
    }
    if (!(lo >= -most && hi <= most && hi - lo <= most)) { // (lo at +inf or hi at -inf passes, and is crossed)
        fail("heinzel: lookup(table, x, lo, hi) needs lo, hi and hi - lo finite");
    }
    if (!(lo < hi)) {
        fail("heinzel: lookup(table, x, lo, hi) has lo at or above hi");
    }
    const double scale = last_cell(cells) / (hi - lo);
    if (!(scale <= most)) {
        fail("heinzel: lookup(table, x, lo, hi) has hi - lo too small to space its cells");
    }
    return scale;
}

// A table read: `table`'s `cells` read at `phase`, in turns (`wavetable`, `graph_builder.hpp`).
constexpr Expr table_read(Expr phase, Expr table, std::int32_t cells) {
    Arena* arena = bring_together(phase, table);
    BuildNode n{};
    n.op = op::TableRead;
    n.inputs[0] = phase.position();
    n.inputs[1] = table.position();
    n.attr.kind = AttrKind::Table;
    n.attr.table.size = cells;
    arena->nodes.push_back(n);
    return Expr{arena, static_cast<std::int32_t>(arena->nodes.size() - 1)};
}

// A delay line: `signal` read `samples` ago, a distance that changes, at most `most` samples (`x[n - d.at_most(120.0) *
// ms]`, `declarations.hpp`).
constexpr Expr delay_line(Expr signal, Expr samples, std::int32_t most) {
    Arena* arena = bring_together(signal, samples);
    BuildNode n{};
    n.op = op::DelayLine;
    n.inputs[0] = signal.position();
    n.inputs[1] = samples.position();
    n.attr.kind = AttrKind::SignalledDelay;
    n.attr.signalled_delay.max_delay = most;
    arena->nodes.push_back(n);
    return Expr{arena, static_cast<std::int32_t>(arena->nodes.size() - 1)};
}

// An order statistic over one axis: Sort keeps it, TopK resizes it to `k`, Quantile drops it to `q`. `k` is 0 but
// for TopK, as the hand-built path carries it.
constexpr Expr order(Expr x, OrderOp how, std::int32_t axis, std::int32_t k = 0, double q = 0.0) {
    OpAttr attr{};
    attr.kind = AttrKind::Order;
    attr.order.op = how;
    attr.order.axis = axis;
    attr.order.k = k;
    attr.order.q = q;
    return append_unary(x, op::Order, attr);
}

// 1 at a run's first sample, 0 at every other: the `_first` node, one number with no axis, typed where it is read as a
// literal is. Not spelled for users (`dsp/` promises no new operation): `dsp::smoothed` reads it.
constexpr Expr first_sample() {
    BuildNode n{};
    n.op = op::First;
    return one_node(n);
}

constexpr std::int32_t from_the_front(std::int32_t axis) {
    if (axis < 0) {
        fail("heinzel: axes are counted from the front");
    }
    return axis;
}
constexpr Axes from_the_front(Axes axes) {
    for (std::int32_t k = 0; k < axes.rank; ++k) {
        from_the_front(axes.axes[k]);
    }
    return axes;
}

} // namespace detail

constexpr Expr Expr::sum(std::int32_t axis) const { return sum(Axes{detail::from_the_front(axis)}); }
constexpr Expr Expr::sum(std::int32_t axis, keepdims_t) const { return sum(Axes{detail::from_the_front(axis)}, keepdims); }
constexpr Expr Expr::sum(Axes axes) const { return detail::reduce(*this, ReduceOp::Sum, detail::from_the_front(axes)); }
constexpr Expr Expr::sum(Axes axes, keepdims_t) const { return detail::reduce(*this, ReduceOp::Sum, detail::from_the_front(axes), true); }
constexpr Expr Expr::mean(std::int32_t axis) const { return mean(Axes{detail::from_the_front(axis)}); }
constexpr Expr Expr::mean(std::int32_t axis, keepdims_t) const { return mean(Axes{detail::from_the_front(axis)}, keepdims); }
constexpr Expr Expr::mean(Axes axes) const { return detail::reduce(*this, ReduceOp::Mean, detail::from_the_front(axes)); }
constexpr Expr Expr::mean(Axes axes, keepdims_t) const { return detail::reduce(*this, ReduceOp::Mean, detail::from_the_front(axes), true); }
constexpr Expr Expr::max(std::int32_t axis) const { return max(Axes{detail::from_the_front(axis)}); }
constexpr Expr Expr::max(std::int32_t axis, keepdims_t) const { return max(Axes{detail::from_the_front(axis)}, keepdims); }
constexpr Expr Expr::max(Axes axes) const { return detail::reduce(*this, ReduceOp::Max, detail::from_the_front(axes)); }
constexpr Expr Expr::max(Axes axes, keepdims_t) const { return detail::reduce(*this, ReduceOp::Max, detail::from_the_front(axes), true); }
constexpr Expr Expr::min(std::int32_t axis) const { return min(Axes{detail::from_the_front(axis)}); }
constexpr Expr Expr::min(std::int32_t axis, keepdims_t) const { return min(Axes{detail::from_the_front(axis)}, keepdims); }
constexpr Expr Expr::min(Axes axes) const { return detail::reduce(*this, ReduceOp::Min, detail::from_the_front(axes)); }
constexpr Expr Expr::min(Axes axes, keepdims_t) const { return detail::reduce(*this, ReduceOp::Min, detail::from_the_front(axes), true); }
constexpr Expr Expr::prod(std::int32_t axis) const { return prod(Axes{detail::from_the_front(axis)}); }
constexpr Expr Expr::prod(std::int32_t axis, keepdims_t) const { return prod(Axes{detail::from_the_front(axis)}, keepdims); }
constexpr Expr Expr::prod(Axes axes) const { return detail::reduce(*this, ReduceOp::Prod, detail::from_the_front(axes)); }
constexpr Expr Expr::prod(Axes axes, keepdims_t) const { return detail::reduce(*this, ReduceOp::Prod, detail::from_the_front(axes), true); }

// The order names the element axes: the analysis appends the last, time's, once the value's rank is one more than
// the order is long.
constexpr Expr Expr::transpose(Axes order) const {
    OpAttr attr{};
    attr.kind = AttrKind::View;
    attr.view.kind = ViewKind::Transpose;
    attr.view.elements_only = true;
    attr.view.order = detail::from_the_front(order);
    return detail::append_unary(*this, op::Transpose, attr);
}

// The target names the element axes only: the analysis appends the operand's last extent, so a
// tensor's `{H * W}` and a signal's `{H * W}` both keep their time.
constexpr Expr Expr::reshape(Shape target) const {
    if (target.rank > static_cast<std::int32_t>(kMaxRank) - 1) {
        fail("heinzel: a reshape target has at most kMaxRank - 1 axes");
    }
    OpAttr attr{};
    attr.kind = AttrKind::View;
    attr.view.kind = ViewKind::Reshape;
    attr.view.elements_only = true;
    attr.view.shape = target;
    return detail::append_unary(*this, op::Reshape, attr);
}

constexpr Expr Expr::slice(std::int32_t axis, std::int32_t offset, std::int32_t length, std::int32_t step) const {
    return detail::slice(*this, detail::from_the_front(axis), offset, length, step);
}
constexpr Expr Expr::unfold(std::int32_t axis, std::int32_t window, std::int32_t hop) const {
    return detail::unfold(*this, detail::from_the_front(axis), window, hop);
}
constexpr Expr Expr::unfold2d(std::int32_t axis, std::int32_t window, std::int32_t axis2, std::int32_t window2) const {
    return detail::unfold2d(*this, detail::from_the_front(axis), window, detail::from_the_front(axis2), window2);
}
constexpr Expr Expr::squeeze(std::int32_t axis) const { return detail::squeeze(*this, detail::from_the_front(axis)); }
constexpr Expr Expr::unsqueeze(std::int32_t axis) const { return detail::unsqueeze(*this, detail::from_the_front(axis)); }
constexpr Expr Expr::realize() const { return detail::realize(*this); }
constexpr Expr Expr::sort(std::int32_t axis) const { return detail::order(*this, OrderOp::Sort, detail::from_the_front(axis)); }
constexpr Expr Expr::topk(std::int32_t axis, std::int32_t k) const {
    return detail::order(*this, OrderOp::TopK, detail::from_the_front(axis), k);
}
constexpr Expr Expr::quantile(std::int32_t axis, double q) const {
    return detail::order(*this, OrderOp::Quantile, detail::from_the_front(axis), 0, q);
}
constexpr Expr Expr::median(std::int32_t axis) const { return quantile(axis, 0.5); }

namespace forwarding {

// The tensor members on an `Output` and a `Variable`, which convert to an `Expr` but are none: `z.max(1, keepdims)`
// is `Expr(z).max(1, keepdims)`, so an `Output` read before it is assigned is marked read as `Expr(z)` marks it. One
// forwarder per overload of `Expr`'s, the deleted ones too, not a pack: a pack cannot take a braced list
// (`y.reshape({2, 3})`), and a float axis must stay refused in the same words. In a namespace of its own, which
// holds nothing else: a base's namespace joins every argument-dependent lookup on its class, and `detail`'s names
// have no business in a user's call with an `Output` in it.
template <class Self>
class members {
public:
    constexpr Expr sum(std::int32_t axis) const { return expr().sum(axis); }
    constexpr Expr sum(std::int32_t axis, keepdims_t) const { return expr().sum(axis, keepdims); }
    constexpr Expr sum(Axes axes) const { return expr().sum(axes); }
    constexpr Expr sum(Axes axes, keepdims_t) const { return expr().sum(axes, keepdims); }
    constexpr Expr mean(std::int32_t axis) const { return expr().mean(axis); }
    constexpr Expr mean(std::int32_t axis, keepdims_t) const { return expr().mean(axis, keepdims); }
    constexpr Expr mean(Axes axes) const { return expr().mean(axes); }
    constexpr Expr mean(Axes axes, keepdims_t) const { return expr().mean(axes, keepdims); }
    constexpr Expr max(std::int32_t axis) const { return expr().max(axis); }
    constexpr Expr max(std::int32_t axis, keepdims_t) const { return expr().max(axis, keepdims); }
    constexpr Expr max(Axes axes) const { return expr().max(axes); }
    constexpr Expr max(Axes axes, keepdims_t) const { return expr().max(axes, keepdims); }
    constexpr Expr min(std::int32_t axis) const { return expr().min(axis); }
    constexpr Expr min(std::int32_t axis, keepdims_t) const { return expr().min(axis, keepdims); }
    constexpr Expr min(Axes axes) const { return expr().min(axes); }
    constexpr Expr min(Axes axes, keepdims_t) const { return expr().min(axes, keepdims); }
    constexpr Expr prod(std::int32_t axis) const { return expr().prod(axis); }
    constexpr Expr prod(std::int32_t axis, keepdims_t) const { return expr().prod(axis, keepdims); }
    constexpr Expr prod(Axes axes) const { return expr().prod(axes); }
    constexpr Expr prod(Axes axes, keepdims_t) const { return expr().prod(axes, keepdims); }
    constexpr Expr transpose(Axes order) const { return expr().transpose(order); }
    constexpr Expr reshape(Shape target) const { return expr().reshape(target); }
    constexpr Expr slice(std::int32_t axis, std::int32_t offset, std::int32_t length, std::int32_t step = 1) const {
        return expr().slice(axis, offset, length, step);
    }
    constexpr Expr unfold(std::int32_t axis, std::int32_t window, std::int32_t hop = 1) const {
        return expr().unfold(axis, window, hop);
    }
    constexpr Expr unfold2d(std::int32_t axis, std::int32_t window, std::int32_t axis2, std::int32_t window2) const {
        return expr().unfold2d(axis, window, axis2, window2);
    }
    constexpr Expr squeeze(std::int32_t axis) const { return expr().squeeze(axis); }
    constexpr Expr unsqueeze(std::int32_t axis) const { return expr().unsqueeze(axis); }
    constexpr Expr realize() const { return expr().realize(); }
    constexpr Expr sort(std::int32_t axis) const { return expr().sort(axis); }
    constexpr Expr topk(std::int32_t axis, std::int32_t k) const { return expr().topk(axis, k); }
    constexpr Expr quantile(std::int32_t axis, double q) const { return expr().quantile(axis, q); }
    constexpr Expr median(std::int32_t axis) const { return expr().median(axis); }
    constexpr detail::AtMost at_most(double most) const { return expr().at_most(most); }
    template <std::floating_point F, class... Rest> Expr sum(F, Rest...) const = delete;
    template <std::floating_point F, class... Rest> Expr mean(F, Rest...) const = delete;
    template <std::floating_point F, class... Rest> Expr max(F, Rest...) const = delete;
    template <std::floating_point F, class... Rest> Expr min(F, Rest...) const = delete;
    template <std::floating_point F, class... Rest> Expr prod(F, Rest...) const = delete;
    template <std::floating_point F, class... Rest> Expr slice(F, Rest...) const = delete;
    template <std::floating_point F, class... Rest> Expr unfold(F, Rest...) const = delete;
    template <std::floating_point F, class... Rest> Expr unfold2d(F, Rest...) const = delete;
    template <std::floating_point F> Expr squeeze(F) const = delete;
    template <std::floating_point F> Expr unsqueeze(F) const = delete;
    template <std::floating_point F> Expr sort(F) const = delete;
    template <std::floating_point F, class K> Expr topk(F, K) const = delete;
    template <std::floating_point F, class Q> Expr quantile(F, Q) const = delete;
    template <std::floating_point F> Expr median(F) const = delete;

private:
    constexpr Expr expr() const { return Expr(static_cast<const Self&>(*this)); }
};

} // namespace forwarding

} // namespace heinzel
