// The contraction adjoint (M7 S3), each analytic gradient checked against finite differences — the
// tree RUNS every one of these graphs, so the check is numbers against numbers.
//
// A contraction is `reduce_sum(A ⊙ B, axes=S)` over the operands' joined shape; its adjoint routes the
// summed result's cotangent back to each operand as the joined-shape product of the spread cotangent
// with the OTHER operand, unbroadcast to that operand's shape (`rules_contract.hpp`). A finite-
// difference check is a real test of that routing only if the elements the sum pairs and the elements
// the adjoint pairs back can be told apart — so every graph places a DISTINCT per-element coefficient
// ramp on the x-carrying operand and a DISTINCT constant ramp on the other, and weights the result by
// a third ramp before folding to one number per sample. A wrong summed axis, a mis-shaped `dP`, or an
// unbroadcast to the wrong operand extent pairs the ramps differently and disagrees with the forward
// difference.
//
// EACH GRAPH IS LINEAR IN x: x flows through exactly ONE operand, the other is a constant ramp, so the
// product is linear and the forward difference is exact to rounding at any step — the tolerance is a
// rounding margin, not a truncation one. The three graphs exercise the two operand paths (dA, dB) and
// the broadcasting case where an operand is extent-1 along the summed axis (the general `dP` path, in
// which the ones-expand and the unbroadcast-sum together carry the gradient). The both-operands-vary
// case is the MLP numpy golden's job (`gradient_golden_test.cpp`); this isolates the pairing.
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

struct Built {
    Graph g{};
    std::int32_t loss{kNoValue};
    std::int32_t grad{kNoValue};
};

constexpr std::int32_t input_x(Graph& g) {
    const auto x = add_value(g, ElemType::F64, Shape{kBlock}, ValueCategory::Input);
    g.inputs.push_back(NamedValue{Name{"x"}, x});
    return x;
}

constexpr std::int32_t const_scalar(Graph& g, double v) {
    const auto id = add_value(g, ElemType::F64, Shape{}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(id)].constant = v;
    return id;
}

// A per-element ramp of a given shape whose element e holds `e + 1` — distinct along every axis and
// clear of zero, so no element's contribution silently drops. Constant: it does not depend on x.
constexpr std::int32_t ramp(Graph& g, const Shape& shape) {
    OpAttr a{};
    a.kind = AttrKind::Iota;
    a.generator.shape = shape;
    a.generator.type = ElemType::F64;
    const auto counted = add_node(g, op::Iota, kNoValue, kNoValue, kNoValue, a);
    return add_node(g, op::Add, counted, const_scalar(g, 1.0));
}

// x scaled by a distinct per-element ramp of `shape` — the x-carrying operand, linear in x.
constexpr std::int32_t coeff_times_x(Graph& g, std::int32_t x, const Shape& shape) {
    return add_node(g, op::Mul, x, ramp(g, shape));
}

// A contraction summing the named axes of the two operands' joined shape.
constexpr std::int32_t contract(Graph& g, std::int32_t a, std::int32_t b, const Axes& axes) {
    OpAttr s{};
    s.kind = AttrKind::Contract;
    s.contract.axes = axes;
    return add_node(g, op::Contract, a, b, kNoValue, s);
}

// Weight the contracted result by a ramp of its own shape and fold every element axis to one number
// per sample (all axes but the innermost, which is time).
constexpr std::int32_t weight_and_fold(Graph& g, std::int32_t wide, const Shape& shape) {
    const auto weighted = add_node(g, op::Mul, wide, ramp(g, shape));
    OpAttr collapse{};
    collapse.kind = AttrKind::Reduce;
    collapse.reduce.op = ReduceOp::Sum;
    collapse.reduce.axes = Axes{};
    for (std::int32_t k = 0; k + 1 < shape.rank; ++k) {
        collapse.reduce.axes.axes[collapse.reduce.axes.rank++] = k;
    }
    return add_node(g, op::Reduce, weighted, kNoValue, kNoValue, collapse);
}

constexpr void finish(Built& b, std::int32_t x) {
    b.g.outputs.push_back(NamedValue{Name{"loss"}, b.loss});
    b.grad = autodiff::gradients(b.g, b.loss, {x})[0];
}

// x through operand A of a matmul-shaped contraction: A=[M,K,1,B] (x·ramp), Bc=[1,K,N,B] (const ramp),
// contract the K axis → [M,N,B]. Tests the dA path — dY·Bᵀ, summed over the contracted axis.
constexpr Built build_lhs() {
    Built b{};
    Graph& g = b.g;
    const auto x = input_x(g);
    const auto a = coeff_times_x(g, x, Shape{2, 3, 1, kBlock});
    const auto bc = ramp(g, Shape{1, 3, 2, kBlock});
    const auto y = contract(g, a, bc, Axes{1});
    b.loss = weight_and_fold(g, y, Shape{2, 2, kBlock});
    finish(b, x);
    return b;
}

// x through operand B: Ac=[M,K,1,B] (const ramp), B=[1,K,N,B] (x·ramp), contract K → [M,N,B]. Tests
// the dB path — Aᵀ·dY.
constexpr Built build_rhs() {
    Built b{};
    Graph& g = b.g;
    const auto x = input_x(g);
    const auto ac = ramp(g, Shape{2, 3, 1, kBlock});
    const auto bb = coeff_times_x(g, x, Shape{1, 3, 2, kBlock});
    const auto y = contract(g, ac, bb, Axes{1});
    b.loss = weight_and_fold(g, y, Shape{2, 2, kBlock});
    finish(b, x);
    return b;
}

// x through an operand that is EXTENT-1 along the summed axis: A=[2,1,B] (x·ramp), Bc=[2,3,B] (const
// ramp), contract axis 1 → [2,B]. The joined shape is [2,3,B], so A broadcasts across the sum:
// `Y[i] = A[i]·Σ_j Bc[i,j]`, and `dA[i] = Σ_j dY[i]·Bc[i,j]`. Tests that `dP`'s ones-expand to the full
// summed extent and the unbroadcast-sum back to extent 1 together carry the gradient.
constexpr Built build_broadcast() {
    Built b{};
    Graph& g = b.g;
    const auto x = input_x(g);
    const auto a = coeff_times_x(g, x, Shape{2, 1, kBlock});
    const auto bc = ramp(g, Shape{2, 3, kBlock});
    const auto y = contract(g, a, bc, Axes{1});
    b.loss = weight_and_fold(g, y, Shape{2, kBlock});
    finish(b, x);
    return b;
}

// Built at compile time: an adjoint rule that produced an ill-shaped node would fail the augmented
// graph's infer here, not at run time.
constexpr Built kLhs = build_lhs();
constexpr Built kRhs = build_rhs();
constexpr Built kBroadcast = build_broadcast();

std::array<double, kBlock> sample_input() {
    std::array<double, kBlock> x{};
    for (std::size_t n = 0; n < kBlock; ++n) {
        x[n] = 0.31 * static_cast<double>((n * 5) % 23) - 2.0;
    }
    return x;
}

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
        const double tol = 1e-5 * (1.0 + std::fabs(analytic));
        if (std::fabs(fd - analytic) > tol) {
            std::printf("%s sample %zu: analytic %.9e  fd %.9e\n", name, n, analytic, fd);
            ++failures;
        }
    }
    return failures;
}

} // namespace

int main() {
    int failures = 0;
    failures += fd_check("contract-lhs", kLhs);
    failures += fd_check("contract-rhs", kRhs);
    failures += fd_check("contract-broadcast", kBroadcast);
    if (failures != 0) {
        std::printf("gradient contract FD check: %d samples disagreed\n", failures);
        return 1;
    }
    return 0;
}
