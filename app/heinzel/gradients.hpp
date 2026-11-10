#pragma once

// heinzel/gradients.hpp: `gradients(loss, {Grad{"dw", w}, ...})`, a graph carrying a loss and its
// gradients. It needs both the frontend and the autodiff transform, which the core keeps apart (no core
// header includes autodiff or reaches into the frontend; gates `autodiff_is_a_leaf` and `core_no_frontend`),
// so it lives in `app/`, above both. Add `app/` to the include path beside `include/`.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>

#include "heinzel/autodiff/gradients.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/ir/ir.hpp"
#include "heinzel/support/fail.hpp"
#include "heinzel/support/fixed_vector.hpp"

namespace heinzel {

// "Name this target's gradient": the input to differentiate the loss with respect to, and the name
// the gradient is exposed under.
struct Grad {
    std::string_view name;
    Expr wrt;
};

// A graph carrying `loss` and its gradient with respect to each named target, in that order:
//
//     Output loss{"loss"};  loss = mean_square(error);
//     return gradients(loss, {Grad{"dtaps", taps}}, Precision::High);
//
// The forward graph is finished once, `autodiff::gradients` appends the adjoint nodes, and the loss and
// each gradient are named as outputs; no operation and no numeric meaning is added. One reverse sweep is
// taken per target. The transform walks back only over the primal nodes present when it is entered, so a
// later target's sweep skips the adjoint nodes an earlier one appended (they carry no cotangent from the
// loss). One sweep for all targets would be faster; the numbers would be the same.
//
// The level is the graph's default precision, set before the first sweep: the gradients are built by analysing
// the graph, so it cannot be set after (a default changed after one analysis is refused by the next).
constexpr Graph gradients(const Output& loss, std::initializer_list<Grad> grads, numerics::Precision level = numerics::Precision::Normal) {
    if (grads.size() == 0) {
        fail("heinzel::gradients: no target to differentiate with respect to");
    }
    if (grads.size() + 1 > kMaxIO) { // the loss and its gradients
        fail("heinzel::gradients: more outputs than kMaxIO; rebuild with -DHEINZEL_MAX_IO=N");
    }
    // Every target in the loss's arena, so `finish` emits them and their positions are the graph's value ids.
    Expr root = loss;
    fixed_vector<Expr, kMaxIO> wrts{};
    for (const Grad& gr : grads) {
        Expr w = gr.wrt;
        if (w.arena() == nullptr) {
            fail("heinzel::gradients: a differentiation target must be an Input"); // (a default `Expr{}`)
        }
        bring_together(root, w);
        wrts.push_back(w);
    }
    Graph g = finish(loss);
    if (g.over != Capacity::None) {
        return g; // the forward graph over a capacity, which `infer` refuses first
    }
    set_default_precision(g, level);
    const std::int32_t loss_id = g.outputs[0].value;
    std::size_t k = 0;
    for (const Grad& gr : grads) {
        Expr& w = wrts[k++];
        w.catch_up();
        g.outputs.push_back(NamedValue{detail::output_name(gr.name), autodiff::gradients(g, loss_id, {w.position()})[0]});
    }
    if (detail::names_repeat(g)) {
        fail("heinzel: two ports share a name");
    }
    return g;
}

} // namespace heinzel
