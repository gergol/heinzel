// The tensor members (Plan/archive/14-dsl/01 §5, 03 §5): each builds the node its old spelling, a `detail::` builder since
// L9, builds, so the graphs are equal; the member `reshape` names element axes only and the analysis completes it.
// Axes are counted from the front: a negative one is refused, an integer of any type is an axis, and a
// floating-point one does not compile (`gates/refusals/members.cpp`).
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"

using namespace heinzel;

namespace {

constexpr Graph analysed(Graph g) {
    if (const Diagnostic d = infer(g); !d.ok) {
        fail(d.message.view());
    }
    return g;
}

// Each member beside its old spelling, on a signal `{4, 6, 8, 16}` and a tensor `{6, 3}`.
template <bool Member>
constexpr Graph folds() {
    Input x{"x", Signal{4, 6, 8, 16}};
    Output a{"a"};
    Output b{"b"};
    Output c{"c"};
    Output d{"d"};
    Output e{"e"};
    Output f{"f"};
    if constexpr (Member) {
        a = x.sum(1);
        b = x.mean(Axes{0, 2}, keepdims);
        c = x.max(2, keepdims);
        d = x.min(Axes{1});
        e = x.prod(0);
        f = x.sum(std::size_t{1}) + x.sum(1u); // any integer is an axis
    } else {
        a = detail::reduce(x, ReduceOp::Sum, Axes{1});
        b = detail::reduce(x, ReduceOp::Mean, Axes{0, 2}, true);
        c = detail::reduce(x, ReduceOp::Max, Axes{2}, true);
        d = detail::reduce(x, ReduceOp::Min, Axes{1});
        e = detail::reduce(x, ReduceOp::Prod, Axes{0});
        f = detail::reduce(x, ReduceOp::Sum, Axes{1}) + detail::reduce(x, ReduceOp::Sum, Axes{1});
    }
    return finish(a, b, c, d, e, f);
}
static_assert(folds<true>() == folds<false>());
// ...and the overloads `folds` leaves out: each fold, by an axis or by axes, with and without keepdims.
template <bool Member>
constexpr Graph every_fold() {
    Input x{"x", Signal{4, 6, 8, 16}};
    Output a{"a"};
    Output b{"b"};
    Output c{"c"};
    Output d{"d"};
    Output e{"e"};
    Output f{"f"};
    Output g{"g"};
    Output h{"h"};
    Output i{"i"};
    Output j{"j"};
    Output k{"k"};
    Output l{"l"};
    Output m{"m"};
    Output o{"o"};
    Output p{"p"};
    const auto twin = [&](ReduceOp how, Axes axes, bool kept) { return detail::reduce(x, how, axes, kept); };
    if constexpr (Member) {
        a = x.sum(Axes{0, 2}), b = x.sum(1, keepdims), c = x.sum(Axes{1, 2}, keepdims);
        d = x.mean(2), e = x.mean(Axes{1}), f = x.mean(0, keepdims);
        g = x.max(Axes{0, 1}), h = x.max(Axes{2}, keepdims);
        i = x.min(1), j = x.min(2, keepdims), k = x.min(Axes{0, 2}, keepdims);
        l = x.prod(Axes{1, 2}), m = x.prod(2, keepdims), o = x.prod(Axes{0}, keepdims), p = x.max(1);
    } else {
        a = twin(ReduceOp::Sum, Axes{0, 2}, false), b = twin(ReduceOp::Sum, Axes{1}, true), c = twin(ReduceOp::Sum, Axes{1, 2}, true);
        d = twin(ReduceOp::Mean, Axes{2}, false), e = twin(ReduceOp::Mean, Axes{1}, false), f = twin(ReduceOp::Mean, Axes{0}, true);
        g = twin(ReduceOp::Max, Axes{0, 1}, false), h = twin(ReduceOp::Max, Axes{2}, true);
        i = twin(ReduceOp::Min, Axes{1}, false), j = twin(ReduceOp::Min, Axes{2}, true), k = twin(ReduceOp::Min, Axes{0, 2}, true);
        l = twin(ReduceOp::Prod, Axes{1, 2}, false), m = twin(ReduceOp::Prod, Axes{2}, true), o = twin(ReduceOp::Prod, Axes{0}, true);
        p = twin(ReduceOp::Max, Axes{1}, false);
    }
    return finish(a, b, c, d, e, f, g, h, i, j, k, l, m, o, p);
}
static_assert(every_fold<true>() == every_fold<false>());

// The builders the members and the notation share, against nodes written by hand: a fold, a slice (its step
// defaulted, and one of 2), a window (its frames left to the analysis) and two order statistics.
template <bool Member>
constexpr Graph shared_builders() {
    Input x{"x", Signal{4, 6, 8, 16}};
    Output a{"a"};
    Output b{"b"};
    Output c{"c"};
    Output d{"d"};
    Output e{"e"};
    Output f{"f"};
    if constexpr (Member) {
        a = x.sum(Axes{1, 2}, keepdims);
        b = x.slice(1, 2, 3);
        c = x.unfold(2, 3);
        d = x.topk(1, 2);
        e = x.slice(0, 1, 2, 2);
        f = x.quantile(2, 0.25);
    } else {
        OpAttr fold{};
        OpAttr part{};
        OpAttr frame{};
        OpAttr order{};
        OpAttr stride{};
        OpAttr quarter{};
        fold.kind = AttrKind::Reduce;
        fold.reduce.op = ReduceOp::Sum;
        fold.reduce.axes = Axes{1, 2};
        fold.reduce.keep_axes = true;
        part.kind = frame.kind = AttrKind::View;
        part.view.kind = ViewKind::Slice;
        part.view.axis = 1, part.view.offset = 2, part.view.length = 3, part.view.step = 1;
        frame.view.kind = ViewKind::Unfold;
        frame.view.axis = 2, frame.view.window = 3, frame.view.hop = 1, frame.view.frames = -1;
        order.kind = AttrKind::Order;
        order.order.op = OrderOp::TopK;
        order.order.axis = 1, order.order.k = 2;
        stride.kind = AttrKind::View;
        stride.view.kind = ViewKind::Slice;
        stride.view.axis = 0, stride.view.offset = 1, stride.view.length = 2, stride.view.step = 2;
        quarter.kind = AttrKind::Order;
        quarter.order.op = OrderOp::Quantile;
        quarter.order.axis = 2, quarter.order.k = 0, quarter.order.q = 0.25;
        a = detail::append_unary(x, op::Reduce, fold);
        b = detail::append_unary(x, op::Slice, part);
        c = detail::append_unary(x, op::Unfold, frame);
        d = detail::append_unary(x, op::Order, order);
        e = detail::append_unary(x, op::Slice, stride);
        f = detail::append_unary(x, op::Order, quarter);
    }
    return finish(a, b, c, d, e, f);
}
static_assert(shared_builders<true>() == shared_builders<false>());

template <bool Member>
constexpr Graph views() {
    Input x{"x", Signal{4, 6, 8, 16}};
    Output a{"a"};
    Output b{"b"};
    Output c{"c"};
    Output d{"d"};
    Output e{"e"};
    Output f{"f"};
    Output g{"g"};
    if constexpr (Member) {
        a = x.sort(1);
        b = x.slice(1, 2, 3, 1);
        c = x.unfold(2, 3);
        d = x.unfold2d(0, 2, 1, 3);
        e = x.unsqueeze(1).squeeze(1);
        f = x.realize();
        g = x.sort(0) + x.topk(1, 2).max(1, keepdims) + x.quantile(2, 0.25).unsqueeze(2) + x.median(2).unsqueeze(2);
    } else {
        OpAttr two{};
        two.kind = AttrKind::View;
        two.view.kind = ViewKind::Unfold2d;
        two.view.axis = 0;
        two.view.window = 2;
        two.view.axis2 = 1;
        two.view.window2 = 3;
        OpAttr in{};
        OpAttr out{};
        in.kind = out.kind = AttrKind::View;
        in.view.kind = ViewKind::Unsqueeze;
        out.view.kind = ViewKind::Squeeze;
        in.view.axis = out.view.axis = 1;
        OpAttr back{};
        back.kind = AttrKind::View;
        back.view.kind = ViewKind::Unsqueeze;
        back.view.axis = 2;
        a = detail::order(x, OrderOp::Sort, 1);
        b = detail::slice(x, 1, 2, 3, 1);
        c = detail::unfold(x, 2, 3, 1);
        d = detail::append_unary(x, op::Unfold2d, two);
        e = detail::append_unary(detail::append_unary(x, op::Reshape, in), op::Reshape, out);
        f = detail::realize(x);
        g = detail::order(x, OrderOp::Sort, 0) + detail::reduce(detail::order(x, OrderOp::TopK, 1, 2), ReduceOp::Max, Axes{1}, true) +
            detail::append_unary(detail::order(x, OrderOp::Quantile, 2, 0, 0.25), op::Reshape, back) +
            detail::append_unary(detail::order(x, OrderOp::Quantile, 2, 0, 0.5), op::Reshape, back);
    }
    return finish(a, b, c, d, e, f, g);
}
static_assert(views<true>() == views<false>());

// The member transpose names element axes: the analysis appends the last once the value's rank is one more — on a
// signal and on a tensor. A full-length order is refused (Plan/archive/14-dsl/03 §3.4).
template <bool Member>
constexpr Graph permuted() {
    Input x{"x", Signal{4, 6, 8, 16}};
    Input w{"w", {6, 3}};
    Output a{"a"};
    Output b{"b"};
    if constexpr (Member) {
        a = x.transpose({1, 0, 2});
        b = w.transpose({1, 0});
    } else {
        a = detail::transpose(x, Axes{1, 0, 2, 3});
        b = detail::transpose(w, Axes{1, 0, 2});
    }
    return finish(a, b);
}
static_assert(analysed(permuted<true>()) == analysed(permuted<false>()) && permuted<true>() != permuted<false>());
constexpr Graph full_length_order() {
    Input x{"x", Signal{4, 6, 16}};
    Output y{"y"};
    y = x.transpose({1, 0, 2});
    return y;
}
static_assert([] {
    Graph g = full_length_order();
    const Diagnostic d = infer(g);
    return !d.ok && d.message.view() == "heinzel: a transpose order names the element axes, each once";
}());

// The member reshape names element axes: on a signal and on a tensor, the analysis appends the last extent and the
// graph is the one built with the full target.
template <bool Member>
constexpr Graph regrouped() {
    Input x{"x", Signal{6, 16}};
    Input w{"w", {6, 3}};
    Output a{"a"};
    Output b{"b"};
    if constexpr (Member) {
        a = x.reshape({2, 3});
        b = w.reshape({18});
    } else {
        a = detail::reshape(x, Shape{2, 3, 16});
        b = detail::reshape(w, Shape{18, 1});
    }
    return finish(a, b);
}
static_assert(analysed(regrouped<true>()) == analysed(regrouped<false>()) && regrouped<true>() != regrouped<false>());

// `x.max(0)` is a reduction (an integer is an axis); the elementwise maximum is the free `max(x, 0.0)`.
constexpr Graph maxima() {
    Input x{"x", {4, 6}};
    Output a{"a"};
    Output b{"b"};
    a = x.max(0);
    b = max(x, 0.0);
    return finish(a, b);
}
static_assert(analysed(maxima()).values[analysed(maxima()).outputs[0].value].shape == Shape{6, 1} &&
              analysed(maxima()).values[analysed(maxima()).outputs[1].value].shape == Shape{4, 6, 1});

constexpr Index n{};

// The members on an `Output` and a `Variable` (Plan/28-dsl-proposals/02 A2) read the value as `Expr(y)` reads it, so
// each builds the graph `Expr(y)` and the member build, braced lists too: an assigned Output, one read before its
// `[n] =`, a Variable read after its definition and one read before it.
template <bool Member>
constexpr Graph forwarded() {
    Input x{"x", Signal{4, 6, 16}};
    Variable v;
    Variable early;
    Output z{"z"};
    Output r{"r"};
    Output a{"a"};
    Output b{"b"};
    Output c{"c"};
    Output d{"d"};
    Output e{"e"};
    Output f{"f"};
    Output g{"g"};
    Output h{"h"};
    Output i{"i"};
    Output j{"j"};
    z = x * 2.0;
    v[n] = x[n] + 0.5 * v[n - 1];
    if constexpr (Member) {
        i = r.sum(0);
        j = early.max(1, keepdims);
        a = z.max(1, keepdims);
        b = z.reshape({24});
        c = z.transpose({1, 0});
        d = z.sum({0, 1});
        e = v.sum(0);
        f = v.reshape({24});
        g = v.transpose({1, 0});
        h = v.sum({0, 1});
    } else {
        i = Expr(r).sum(0);
        j = Expr(early).max(1, keepdims);
        a = Expr(z).max(1, keepdims);
        b = Expr(z).reshape({24});
        c = Expr(z).transpose({1, 0});
        d = Expr(z).sum({0, 1});
        e = Expr(v).sum(0);
        f = Expr(v).reshape({24});
        g = Expr(v).transpose({1, 0});
        h = Expr(v).sum({0, 1});
    }
    r[n] = x[n] - 0.25 * r[n - 1];
    early[n] = x[n] * 3.0;
    return finish(z, r, a, b, c, d, e, f, g, h, i, j);
}
static_assert(forwarded<true>() == forwarded<false>());

} // namespace

namespace {

int g_wrong = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        ++g_wrong;
        std::printf("  FAILED: %s\n", what);
    }
}

template <class Build>
bool refused(Build build, std::string_view text) {
    try {
        build();
    } catch (const heinzel::error& e) {
        return e.message() == text;
    }
    return false;
}

void refusals() {
    const auto x = [] { return Input{"x", Signal{4, 6, 16}}; };
    constexpr std::string_view front = "heinzel: axes are counted from the front";
    expect(refused([&] { return x().sum(-1); }, front), "sum(-1)");
    expect(refused([&] { return x().mean(Axes{0, -1}, keepdims); }, front), "mean over {0, -1}");
    expect(refused([&] { return x().transpose({-1, 0}); }, front), "a transpose naming -1");
    expect(refused([&] { return x().slice(-1, 0, 1); }, front), "slice(-1, …)");
    expect(refused([&] { return x().unfold(-1, 3); }, front), "unfold(-1, …)");
    expect(refused([&] { return x().unfold2d(0, 2, -1, 2); }, front), "unfold2d's second axis -1");
    expect(refused([&] { return x().squeeze(-1); }, front), "squeeze(-1)");
    expect(refused([&] { return x().unsqueeze(-1); }, front), "unsqueeze(-1)");
    expect(refused([&] { return x().median(-1); }, front), "median(-1)");
    expect(refused([&] { return x().max(-1); }, front), "max(-1)");
    expect(refused([&] { return x().max(Axes{-1}); }, front), "max over {-1}");
    expect(refused([&] { return x().min(Axes{0, -1}, keepdims); }, front), "min over {0, -1}");
    expect(refused([&] { return x().prod(Axes{-1}); }, front), "prod over {-1}");
    expect(refused([&] { return x().sort(-1); }, front), "sort(-1)");
    expect(refused([&] { return x().topk(-1, 2); }, front), "topk(-1, …)");
    expect(refused([&] { return x().quantile(-1, 0.5); }, front), "quantile(-1, …)");
    expect(refused([&] { return x().unfold2d(-1, 2, 1, 2); }, front), "unfold2d's first axis -1");
    expect(refused([&] { return x().reshape({1, 1, 1, 1, 1, 24}); }, "heinzel: a reshape target has at most kMaxRank - 1 axes"),
           "a reshape target of kMaxRank axes");
    expect([&] {
        try {
            (void)x().reshape({1, 1, 1, 1, 24});
            return true;
        } catch (const heinzel::error&) {
            return false;
        }
    }(), "a reshape target of kMaxRank - 1 axes is built");

    // An Output read by a member before it is assigned is marked read, as `Expr(y)` marks it, and `=` refuses it.
    const auto read_then_assigned = [](bool member) {
        Input w{"w", Signal{4, 16}};
        Output y{"y"};
        Expr early = member ? y.sum(0) : Expr(y).sum(0);
        y = w * 2.0;
        return early;
    };
    constexpr std::string_view read = "heinzel: an Output was read before it was assigned; use [n] = for a recurrence";
    expect(refused([&] { return read_then_assigned(true); }, read), "an Output read by a member, then assigned");
    expect(refused([&] { return read_then_assigned(false); }, read), "an Output read as Expr(y), then assigned");
}

} // namespace

int main() {
    refusals();
    if (g_wrong != 0) {
        std::printf("members_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("members_test: every member builds its old spelling's node\n");
    return 0;
}
