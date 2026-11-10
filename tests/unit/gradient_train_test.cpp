// M7 S5 (M7.2 — gradients compose) — an optimizer step expressed INSIDE the graph, run as a loop to
// numpy's converged loss. `gradients()` hands back ordinary value ids; the update `w ← w − lr·dw` is
// three more `add_node`s (a scalar `lr`, a `Mul`, a `Sub`) on those ids in the SAME graph — the feature
// amendment A4-1 names, demonstrated by a test rather than asserted by a comment: a design able only to
// return a finished graph could not append the step.
//
// THE MODEL is the S3 MLP layer — loss = mean((relu(x@w + b) − t)^2) — differentiated wrt {w, b}. The
// numpy golden `gen_grad_golden.py` runs the SAME loop (lr 0.05, 100 steps) to `loss_mlp_trained`, so
// the endpoint is an INDEPENDENT oracle, not a recomputation. The relu is `max(zb, 0)`, whose adjoint
// mask is `zb > 0` STRICT (`rules_elementwise.hpp`: a Max tie goes to the second operand, the constant
// zero) — exactly numpy's `(zt > 0)`, so the two trajectories take the same branch at every step and
// the endpoints agree to rounding, not merely approximately.
//
// THE DRIVER holds the learnable state in two host buffers and runs the graph as the training loop's
// body: fill {w, b} from the buffers, step every node once (`walk_step` — `run()`'s per-sample
// semantics, the golden's wide-input driver), read the in-graph `w_new` / `b_new` back into the
// buffers. After 100 update steps the buffers hold params₁₀₀; one final forward reads the loss at
// those params — numpy's `loss_mlp_trained`. The graph is delay-free, so one sample (`kBlock == 1`) is
// the whole computation, and `x` / `t` are filled once (they do not change across steps).
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>

#include "heinzel/autodiff/gradients.hpp"
#include "heinzel/sema/analyze.hpp"

#include "../goldens/grad_golden.hpp"
#include "hand_built_graph.hpp"
#include "interpret.hpp"

using namespace heinzel;
namespace gg = heinzel_golden::grad;

namespace {

constexpr std::size_t kBlock = 1; // one sample: the whole matrix, one streamed time step
constexpr double kLearningRate = 0.05;
constexpr int kSteps = 100;

struct Train {
    Graph g{};
    std::int32_t x{kNoValue};
    std::int32_t w{kNoValue};
    std::int32_t bias{kNoValue};
    std::int32_t t{kNoValue};
    std::int32_t loss{kNoValue};
    std::int32_t w_new{kNoValue};
    std::int32_t b_new{kNoValue}; // the in-graph SGD update, w − lr·dW and b − lr·dB
};

constexpr std::int32_t unsqueeze(Graph& g, std::int32_t v, std::int32_t axis) {
    OpAttr a{};
    a.kind = AttrKind::View;
    a.view.kind = ViewKind::Unsqueeze;
    a.view.axis = axis;
    return add_node(g, op::Reshape, v, kNoValue, kNoValue, a);
}

constexpr std::int32_t contract_k(Graph& g, std::int32_t a, std::int32_t b) {
    OpAttr s{};
    s.kind = AttrKind::Contract;
    s.contract.axes = Axes{1}; // the K axis of the joined [B,K,N,1]
    return add_node(g, op::Contract, a, b, kNoValue, s);
}

// The MLP layer, its gradients wrt {w, b}, and the SGD step built onto those gradient ids. Built at
// compile time — an ill-shaped update node (a scalar that would not broadcast onto dW, say) fails the
// augmented-graph `infer` here rather than at run time.
constexpr Train build_train() {
    Train b{};
    Graph& g = b.g;
    b.x = add_value(g, ElemType::F64, Shape{gg::B, gg::K, 1}, ValueCategory::Input);
    b.w = add_value(g, ElemType::F64, Shape{gg::K, gg::N, 1}, ValueCategory::Input);
    b.bias = add_value(g, ElemType::F64, Shape{gg::N, 1}, ValueCategory::Input);
    b.t = add_value(g, ElemType::F64, Shape{gg::B, gg::N, 1}, ValueCategory::Input);
    g.inputs.push_back(NamedValue{Name{"x"}, b.x});
    g.inputs.push_back(NamedValue{Name{"w"}, b.w});
    g.inputs.push_back(NamedValue{Name{"b"}, b.bias});
    g.inputs.push_back(NamedValue{Name{"t"}, b.t});

    const auto xr = unsqueeze(g, b.x, 2);
    const auto wr = unsqueeze(g, b.w, 0);
    const auto z = contract_k(g, xr, wr);                     // [B,N,1]
    const auto zb = add_node(g, op::Add, z, b.bias);          // [B,N,1]
    const auto zero = add_value(g, ElemType::F64, Shape{}, ValueCategory::Const);
    const auto y = add_node(g, op::Max, zb, zero);            // relu
    const auto diff = add_node(g, op::Sub, y, b.t);
    const auto sq = add_node(g, op::Mul, diff, diff);

    OpAttr mean{};
    mean.kind = AttrKind::Reduce;
    mean.reduce.op = ReduceOp::Mean;
    mean.reduce.axes = Axes{0, 1};
    b.loss = add_node(g, op::Reduce, sq, kNoValue, kNoValue, mean); // one number per sample
    g.outputs.push_back(NamedValue{Name{"loss"}, b.loss});

    // gradients() returns ordinary graph values; the optimizer step is more nodes on them.
    const auto grads = autodiff::gradients(g, b.loss, {b.w, b.bias});
    const auto dW = grads[0];
    const auto dB = grads[1];

    const auto lr = add_value(g, ElemType::F64, Shape{}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(lr)].constant = kLearningRate;
    b.w_new = add_node(g, op::Sub, b.w, add_node(g, op::Mul, lr, dW)); // w − lr·dW  [K,N,1]
    b.b_new = add_node(g, op::Sub, b.bias, add_node(g, op::Mul, lr, dB)); // b − lr·dB  [N,1]
    g.outputs.push_back(NamedValue{Name{"w_new"}, b.w_new});
    g.outputs.push_back(NamedValue{Name{"b_new"}, b.b_new});

    // Resolve the appended step (M6.5 idempotence is what makes this second infer sound).
    if (const Diagnostic d = infer(g); !d.ok) {
        fail("gradient_train: the graph with the in-graph SGD step did not infer");
    }
    return b;
}

constexpr Train kTrain = build_train();

// Fill every Const value's planes (the relu's zero, the autodiff-emitted ones/constants, the lr).
void fill_consts(const Graph& g, Signals<kBlock>& out) {
    for (std::size_t i = 0; i < g.values.size(); ++i) {
        if (g.values[i].category != ValueCategory::Const) {
            continue;
        }
        for (std::int32_t e = 0; e < elements_of(g.values[i].shape).count; ++e) {
            for (std::size_t n = 0; n < kBlock; ++n) {
                detail::write(out, static_cast<std::int32_t>(i), e, n, g.values[i].constant);
            }
        }
    }
}

// Write a host buffer into one wide Input's planes (row-major, matching numpy's C-order).
template <std::size_t K>
void write_input(Signals<kBlock>& out, std::int32_t value, const double (&buf)[K]) {
    for (std::int32_t e = 0; e < static_cast<std::int32_t>(K); ++e) {
        for (std::size_t n = 0; n < kBlock; ++n) {
            detail::write(out, value, e, n, buf[static_cast<std::size_t>(e)]);
        }
    }
}

template <std::size_t K>
void read_output(const Signals<kBlock>& out, std::int32_t value, double (&buf)[K]) {
    for (std::int32_t e = 0; e < static_cast<std::int32_t>(K); ++e) {
        buf[static_cast<std::size_t>(e)] = sample_at(out, value, e, 0);
    }
}

// Fill one wide Input from a float golden buffer (the fixed inputs x and t).
template <std::size_t K>
void fill_golden(Signals<kBlock>& out, std::int32_t value, const std::array<float, K>& buf) {
    for (std::int32_t e = 0; e < static_cast<std::int32_t>(K); ++e) {
        for (std::size_t n = 0; n < kBlock; ++n) {
            detail::write(out, value, e, n, static_cast<double>(buf[static_cast<std::size_t>(e)]));
        }
    }
}

// One forward+backward+update pass over the whole graph — the training loop's body.
void step_graph(const Train& b, Signals<kBlock>& out) {
    for (std::size_t n = 0; n < kBlock; ++n) {
        for (const Node& node : b.g.nodes) {
            detail::walk_step(b.g, node, n, out);
        }
    }
}

// Run the loop: state in host buffers, the graph as the update rule. Returns the loss before any step
// and the loss at params₁₀₀ (numpy's loss_mlp_trained — read by one final forward, since the graph's
// own loss is at the params BEFORE that step's update).
void run_training(const Train& b, double& loss0, double& loss_trained) {
    Signals<kBlock> sig{};
    lay_out(b.g, sig);
    fill_consts(b.g, sig);
    fill_golden(sig, b.x, gg::X); // x and t are fixed across the whole loop
    fill_golden(sig, b.t, gg::T);

    double wbuf[gg::K * gg::N];
    double bbuf[gg::N];
    for (std::size_t e = 0; e < gg::W.size(); ++e) {
        wbuf[e] = static_cast<double>(gg::W[e]);
    }
    for (std::size_t e = 0; e < gg::BIAS.size(); ++e) {
        bbuf[e] = static_cast<double>(gg::BIAS[e]);
    }

    write_input(sig, b.w, wbuf);
    write_input(sig, b.bias, bbuf);
    step_graph(b, sig);
    loss0 = sample_at(sig, b.loss, 0, 0); // the loss before training descends

    for (int s = 0; s < kSteps; ++s) {
        write_input(sig, b.w, wbuf);
        write_input(sig, b.bias, bbuf);
        step_graph(b, sig);
        read_output(sig, b.w_new, wbuf); // w ← w − lr·dW, computed in the graph
        read_output(sig, b.b_new, bbuf);
    }

    write_input(sig, b.w, wbuf);
    write_input(sig, b.bias, bbuf);
    step_graph(b, sig);
    loss_trained = sample_at(sig, b.loss, 0, 0);
}

} // namespace

int main() {
    std::int32_t planes = 0;
    for (const Value& v : kTrain.g.values) {
        planes += elements_of(v.shape).count;
    }
    std::printf("train: %zu values, %d planes (kMaxValues=%zu, kMaxPlanes=%zu)\n", kTrain.g.values.size(),
                planes, kMaxValues, kMaxPlanes);

    double loss0 = 0.0;
    double loss_trained = 0.0;
    run_training(kTrain, loss0, loss_trained);

    int failures = 0;
    std::printf("initial loss %.9e -> trained loss %.9e (golden %.9e)\n", loss0, loss_trained,
                static_cast<double>(gg::loss_mlp_trained));

    if (!(loss_trained < loss0)) {
        std::printf("train: the in-graph SGD step did not reduce the loss (%.9e >= %.9e)\n", loss_trained,
                    loss0);
        ++failures;
    }
    if (std::fabs(loss_trained - gg::loss_mlp_trained) >
        1e-4 * (1.0 + std::fabs(static_cast<double>(gg::loss_mlp_trained)))) {
        std::printf("train: loss after %d steps %.9e, golden %.9e\n", kSteps, loss_trained,
                    static_cast<double>(gg::loss_mlp_trained));
        ++failures;
    }

    if (failures != 0) {
        std::printf("in-graph SGD training: %d checks disagreed\n", failures);
        return 1;
    }
    return 0;
}
