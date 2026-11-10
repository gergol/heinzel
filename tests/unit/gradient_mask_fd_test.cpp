// The mask-routed adjoints (M7 S1c), each analytic gradient checked against finite differences — the
// tree can RUN all of these, so the check is numbers against numbers.
//
// These are the operations whose adjoint routes a cotangent to ONE operand chosen per element rather
// than scaling it by arithmetic: elementwise `min` / `max` / `select`, and reduce `max` / `min` /
// `prod`; and `abs`, whose slope flips at 0 as theirs does at a crossing. Each build below is a one-input graph (`run` drives one input of one number per sample) whose
// loss is a small expression in the input; `gradients()` appends the adjoint nodes, and the same `run`
// that computes the loss computes the gradient beside it. The finite-difference check perturbs the
// input and compares `(loss(x+ε) − loss(x))/ε` to that gradient, per sample (each sample is
// independent — the graphs carry no delay).
//
// The samples are chosen to sit clear of every kink and tie: at an exact tie the forward's tie-break
// and the subgradient are a matter of convention, and a finite difference straddling a kink is neither
// side's slope. So the min/max thresholds and the reduce channels are laid out so no sample lands on a
// crossing — which is what lets FD be the oracle here.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>

#include "heinzel/autodiff/gradients.hpp"
#include "heinzel/sema/analyze.hpp"

#include "hand_built_graph.hpp"
#include "interpret.hpp"

using namespace heinzel;

namespace {

constexpr std::size_t kBlock = 64;
constexpr std::int32_t kChannels = 3;

struct Built {
    Graph g{};
    std::int32_t loss{kNoValue};
    std::int32_t grad{kNoValue};
};

// A scalar constant standing for every element of its value — one number that broadcasts against
// anything, the same repeat a `Const` gets when it is read.
constexpr std::int32_t const_scalar(Graph& g, double v) {
    const auto id = add_value(g, ElemType::F64, Shape{}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(id)].constant = v;
    return id;
}

// A per-channel index ramp {kChannels, kBlock}: value `c` at channel c, the same at every sample (the
// streamed axis is innermost, so `_iota`'s per-sample plane is the channel axis). This is how a
// test gets DISTINCT values along a reduced axis — a scalar `Const` is one number for all channels,
// which would make a reduce-max a degenerate all-ways tie.
constexpr OpAttr channel_ramp() {
    OpAttr a{};
    a.kind = AttrKind::Iota;
    a.generator.shape = Shape{kChannels, kBlock};
    a.generator.type = ElemType::F64;
    return a;
}

constexpr OpAttr reduce_over_channels(ReduceOp op) {
    OpAttr a{};
    a.kind = AttrKind::Reduce;
    a.reduce.op = op;
    a.reduce.axes = Axes{0};
    return a;
}

constexpr std::int32_t input_x(Graph& g) {
    const auto x = add_value(g, ElemType::F64, Shape{kBlock}, ValueCategory::Input);
    g.inputs.push_back(NamedValue{Name{"x"}, x});
    return x;
}

constexpr void finish(Built& b, std::int32_t x) {
    b.g.outputs.push_back(NamedValue{Name{"loss"}, b.loss});
    const auto grads = autodiff::gradients(b.g, b.loss, {x});
    b.grad = grads[0];
}

// loss = max(1.3·x, 0.7): the gradient is 1.3 where 1.3·x > 0.7 and 0 below (a tie goes to the
// second operand, 0.7, whose gradient is zero — the forward tie-break the adjoint mask follows).
constexpr Built build_max() {
    Built b{};
    Graph& g = b.g;
    const auto x = input_x(g);
    const auto s = add_node(g, op::Mul, x, const_scalar(g, 1.3));
    b.loss = add_node(g, op::Max, s, const_scalar(g, 0.7));
    finish(b, x);
    return b;
}

// loss = min(1.3·x, 0.7): the gradient is 1.3 where 1.3·x < 0.7 and 0 above.
constexpr Built build_min() {
    Built b{};
    Graph& g = b.g;
    const auto x = input_x(g);
    const auto s = add_node(g, op::Mul, x, const_scalar(g, 1.3));
    b.loss = add_node(g, op::Min, s, const_scalar(g, 0.7));
    finish(b, x);
    return b;
}

// loss = select(x < 0.9, 1.3·x, −0.4·x): the gradient is 1.3 where x < 0.9 and −0.4 above. The
// condition is a `_lt` node (zero-derivative — no cotangent flows to it); each branch keeps only the
// elements it produced.
constexpr Built build_select() {
    Built b{};
    Graph& g = b.g;
    const auto x = input_x(g);
    const auto cond = add_node(g, op::Lt, x, const_scalar(g, 0.9));
    const auto then_br = add_node(g, op::Mul, x, const_scalar(g, 1.3));
    const auto else_br = add_node(g, op::Mul, x, const_scalar(g, -0.4));
    b.loss = add_node(g, op::Select, cond, then_br, else_br);
    finish(b, x);
    return b;
}

// loss = abs(1.3·x): the gradient is 1.3·sign(x), so -1.3 below 0, 1.3 above and 0 at 0 (as `sign(0)`), which no
// finite difference sees and `at_zero` pins instead.
constexpr Built build_abs() {
    Built b{};
    Graph& g = b.g;
    const auto x = input_x(g);
    const auto s = add_node(g, op::Mul, x, const_scalar(g, 1.3));
    b.loss = add_node(g, op::Abs, s);
    finish(b, x);
    return b;
}

// A reduce over three channels of `1.5·x + c + bias`. The `1.5·x` term is common to every channel, so
// the argmax/argmin never moves off channel 2 / channel 0 — a clean single winner, no ties. `bias`
// lifts the channels clear of zero, which `prod` needs (its gradient is Inf/NaN at a zero element).
constexpr Built build_reduce(ReduceOp op, double bias) {
    Built b{};
    Graph& g = b.g;
    const auto x = input_x(g);
    const auto s = add_node(g, op::Mul, x, const_scalar(g, 1.5));
    const auto ramp = add_node(g, op::Iota, kNoValue, kNoValue, kNoValue, channel_ramp());
    const auto shifted = add_node(g, op::Add, s, ramp);
    const auto per_ch = add_node(g, op::Add, shifted, const_scalar(g, bias));
    b.loss = add_node(g, op::Reduce, per_ch, kNoValue, kNoValue, reduce_over_channels(op));
    finish(b, x);
    return b;
}

// Built at compile time: an adjoint rule that produced an ill-shaped node would fail the build here
// (gradients() infers the augmented graph and would refuse it), not at run time.
constexpr Built kMax = build_max();
constexpr Built kMin = build_min();
constexpr Built kSelect = build_select();
constexpr Built kAbs = build_abs();
constexpr Built kReduceMax = build_reduce(ReduceOp::Max, 0.0);
constexpr Built kReduceMin = build_reduce(ReduceOp::Min, 0.0);
constexpr Built kReduceProd = build_reduce(ReduceOp::Prod, 5.0);

// The input spread — the parity oracle's, a range of magnitudes both signs, none landing on any of the
// kinks or channel crossings the graphs above are built around.
std::array<double, kBlock> sample_input() {
    std::array<double, kBlock> x{};
    for (std::size_t n = 0; n < kBlock; ++n) {
        x[n] = 0.31 * static_cast<double>((n * 5) % 23) - 2.0;
    }
    return x;
}

// The adjoint gradient against a forward finite difference, per sample.
int fd_check(const char* name, const Built& b) {
    const std::array<double, kBlock> x = sample_input();
    Signals<kBlock> base{};
    run(b.g, x, base);

    const double eps = 1e-6;
    std::array<double, kBlock> bumped_in{};
    for (std::size_t n = 0; n < kBlock; ++n) {
        bumped_in[n] = x[n] + eps;
    }
    Signals<kBlock> bumped{};
    run(b.g, bumped_in, bumped);

    int failures = 0;
    for (std::size_t n = 0; n < kBlock; ++n) {
        const double analytic = sample_at(base, b.grad, 0, n);
        const double fd = (sample_at(bumped, b.loss, 0, n) - sample_at(base, b.loss, 0, n)) / eps;
        const double tol = 1e-4 * (1.0 + std::fabs(analytic));
        if (std::fabs(fd - analytic) > tol) {
            std::printf("%s sample %zu: analytic %.9e  fd %.9e\n", name, n, analytic, fd);
            ++failures;
        }
    }
    return failures;
}

// abs's gradient at +0 and -0: 0, not a side's slope (Plan/29, Gerald 2026-10-20: PyTorch's sign(0)).
int at_zero() {
    std::array<double, kBlock> x{};
    for (std::size_t n = 0; n < kBlock; ++n) {
        x[n] = n % 2 == 0 ? 0.0 : -0.0;
    }
    Signals<kBlock> out{};
    run(kAbs.g, x, out);
    int failures = 0;
    for (std::size_t n = 0; n < kBlock; ++n) {
        const double grad = sample_at(out, kAbs.grad, 0, n);
        if (grad != 0.0) {
            std::printf("abs at %g: gradient %.9e, not 0\n", x[n], grad);
            ++failures;
        }
    }
    return failures;
}

} // namespace

int main() {
    int failures = 0;
    failures += fd_check("max", kMax);
    failures += fd_check("min", kMin);
    failures += fd_check("select", kSelect);
    failures += fd_check("abs", kAbs);
    failures += at_zero();
    failures += fd_check("reduce-max", kReduceMax);
    failures += fd_check("reduce-min", kReduceMin);
    failures += fd_check("reduce-prod", kReduceProd);
    if (failures != 0) {
        std::printf("gradient mask FD check: %d samples disagreed\n", failures);
        return 1;
    }
    return 0;
}
