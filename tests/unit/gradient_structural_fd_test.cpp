// The structural adjoints (M7 S2), each analytic gradient checked against finite differences — the
// tree can RUN every one of these graphs, so the check is numbers against numbers.
//
// A structural view moves elements without arithmetic; its adjoint moves the cotangent by the inverse
// rearrangement. A finite-difference check over a SCALAR input is only a real test of that inverse if
// the element the forward moves and the element the adjoint moves it back to can be told apart — so
// every graph here places a DISTINCT per-element coefficient BEFORE the view (an `_iota` ramp times the
// input) and a DISTINCT per-element weight AFTER it (a second `_iota` ramp), then folds to one number
// per sample. `dloss/dx` is then `Σ coeff(e)·weight(image-of-e)`, a sum whose value depends on the exact
// pairing the view induces: a wrong inverse permutation, a slice adjoint padded at the wrong offset, or
// a concat split in the wrong place all pair the ramps differently and disagree with the forward
// difference. The per-element MAPPING of the forward views themselves is `rearrange_test.cpp`'s job;
// this checks that the adjoint inverts that mapping.
//
// The graphs are LINEAR in the input, so the forward difference is exact to rounding at any step — the
// tolerance is a rounding margin, not a truncation one. Each sample is independent (no delay), so the
// per-sample forward difference is the per-sample gradient.
#include <array>
#include <bit>
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

// A per-element ramp of a given shape whose element e holds `e + 1` — distinct along every axis, and
// clear of zero so no element's contribution silently drops. `_iota` makes the elements differ (a
// scalar `Const` is one number for all of them); the `+ 1` lifts it off zero.
constexpr std::int32_t ramp(Graph& g, const Shape& shape) {
    OpAttr a{};
    a.kind = AttrKind::Iota;
    a.generator.shape = shape;
    a.generator.type = ElemType::F64;
    const auto counted = add_node(g, op::Iota, kNoValue, kNoValue, kNoValue, a);
    return add_node(g, op::Add, counted, const_scalar(g, 1.0));
}

// x scaled by the coefficient ramp of `shape` — the distinct per-element input the view rearranges.
constexpr std::int32_t coeff_times_x(Graph& g, std::int32_t x, const Shape& shape) {
    return add_node(g, op::Mul, x, ramp(g, shape));
}

// Weight the (rearranged) wide value by a ramp of its own shape and fold every element axis to one
// number per sample.
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
    const auto grads = autodiff::gradients(b.g, b.loss, {x});
    b.grad = grads[0];
}

constexpr OpAttr view(ViewKind kind) {
    OpAttr a{};
    a.kind = AttrKind::View;
    a.view.kind = kind;
    return a;
}

// transpose the two outer axes of a {2,3,block} value → {3,2,block}: the adjoint is the transpose by
// the inverse permutation. The coefficient at [i,j] must pair with the weight at [j,i].
constexpr Built build_transpose() {
    Built b{};
    Graph& g = b.g;
    const auto x = input_x(g);
    const auto pre = coeff_times_x(g, x, Shape{2, 3, kBlock});
    OpAttr t = view(ViewKind::Transpose);
    t.view.order = Axes{1, 0, 2};
    const auto turned = add_node(g, op::Transpose, pre, kNoValue, kNoValue, t);
    b.loss = weight_and_fold(g, turned, Shape{3, 2, kBlock});
    finish(b, x);
    return b;
}

// reverse a {4,block} axis → the flip is its own adjoint. Coefficient at i pairs with weight at 3−i.
constexpr Built build_flip() {
    Built b{};
    Graph& g = b.g;
    const auto x = input_x(g);
    const auto pre = coeff_times_x(g, x, Shape{4, kBlock});
    OpAttr f = view(ViewKind::Flip);
    f.view.axis = 0;
    const auto flipped = add_node(g, op::Flip, pre, kNoValue, kNoValue, f);
    b.loss = weight_and_fold(g, flipped, Shape{4, kBlock});
    finish(b, x);
    return b;
}

// re-cut {6,block} into {2,3,block} → the adjoint reshapes the cotangent back to {6,block}.
constexpr Built build_reshape() {
    Built b{};
    Graph& g = b.g;
    const auto x = input_x(g);
    const auto pre = coeff_times_x(g, x, Shape{6, kBlock});
    OpAttr r = view(ViewKind::Reshape);
    r.view.shape = Shape{2, 3, kBlock};
    const auto cut = add_node(g, op::Reshape, pre, kNoValue, kNoValue, r);
    b.loss = weight_and_fold(g, cut, Shape{2, 3, kBlock});
    finish(b, x);
    return b;
}

// insert an extent-1 axis into {3,block} → {3,1,block}; the adjoint squeezes it back out.
constexpr Built build_unsqueeze() {
    Built b{};
    Graph& g = b.g;
    const auto x = input_x(g);
    const auto pre = coeff_times_x(g, x, Shape{3, kBlock});
    OpAttr u = view(ViewKind::Unsqueeze);
    u.view.axis = 1;
    const auto grown = add_node(g, op::Reshape, pre, kNoValue, kNoValue, u);
    b.loss = weight_and_fold(g, grown, Shape{3, 1, kBlock});
    finish(b, x);
    return b;
}

// drop an extent-1 axis from {3,1,block} → {3,block}; the adjoint unsqueezes it back in.
constexpr Built build_squeeze() {
    Built b{};
    Graph& g = b.g;
    const auto x = input_x(g);
    const auto pre = coeff_times_x(g, x, Shape{3, 1, kBlock});
    OpAttr sq = view(ViewKind::Squeeze);
    sq.view.axis = 1;
    const auto dropped = add_node(g, op::Reshape, pre, kNoValue, kNoValue, sq);
    b.loss = weight_and_fold(g, dropped, Shape{3, kBlock});
    finish(b, x);
    return b;
}

// take three elements of a {5,block} axis at offset one, step one → {3,block}. The adjoint pads the
// cotangent back to five with a zero fill, so the two dropped elements get a zero gradient.
constexpr Built build_slice() {
    Built b{};
    Graph& g = b.g;
    const auto x = input_x(g);
    const auto pre = coeff_times_x(g, x, Shape{5, kBlock});
    OpAttr s = view(ViewKind::Slice);
    s.view.axis = 0;
    s.view.offset = 1;
    s.view.length = 3;
    s.view.step = 1;
    const auto taken = add_node(g, op::Slice, pre, kNoValue, kNoValue, s);
    b.loss = weight_and_fold(g, taken, Shape{3, kBlock});
    finish(b, x);
    return b;
}

// pad a {3,block} axis by one element on each side (a non-zero fill, which carries no gradient) → the
// adjoint slices the body back out at offset one. Coefficient m pairs with weight m+1.
constexpr Built build_pad() {
    Built b{};
    Graph& g = b.g;
    const auto x = input_x(g);
    const auto pre = coeff_times_x(g, x, Shape{3, kBlock});
    OpAttr p = view(ViewKind::Pad);
    p.view.axis = 0;
    p.view.pad_before = 1;
    p.view.pad_after = 1;
    p.view.pad_bits = std::bit_cast<std::uint64_t>(2.0);
    const auto padded = add_node(g, op::Pad, pre, kNoValue, kNoValue, p);
    b.loss = weight_and_fold(g, padded, Shape{5, kBlock});
    finish(b, x);
    return b;
}

// join two {2,block} values derived from x along their outer axis → {4,block}; the adjoint is the two
// halves of the cotangent, sliced at the split. Both operands come from the same input, so its
// cotangent is the SUM of the two contributions.
constexpr Built build_concat() {
    Built b{};
    Graph& g = b.g;
    const auto x = input_x(g);
    const auto lo = coeff_times_x(g, x, Shape{2, kBlock});
    const auto hi = add_node(g, op::Mul, x, ramp(g, Shape{2, kBlock}));
    OpAttr j = view(ViewKind::Concat);
    j.view.axis = 0;
    const auto joined = add_node(g, op::Concat, lo, hi, kNoValue, j);
    b.loss = weight_and_fold(g, joined, Shape{4, kBlock});
    finish(b, x);
    return b;
}

// pack two SAME-shaped {2,block} values derived from x → {4,block}, the equal-operand join (`_concat`); the
// adjoint is again the two halves, each operand's own extent giving its slice length.
constexpr Built build_pack() {
    Built b{};
    Graph& g = b.g;
    const auto x = input_x(g);
    const auto lo = coeff_times_x(g, x, Shape{2, kBlock});
    const auto hi = add_node(g, op::Mul, x, ramp(g, Shape{2, kBlock}));
    OpAttr j = view(ViewKind::Concat);
    j.view.axis = 0;
    const auto packed = add_node(g, op::Concat, lo, hi, kNoValue, j);
    b.loss = weight_and_fold(g, packed, Shape{4, kBlock});
    finish(b, x);
    return b;
}

// Built at compile time: an adjoint rule that produced an ill-shaped node would fail the build here,
// not at run time (gradients() infers the augmented graph and would have refused).
constexpr Built kTranspose = build_transpose();
constexpr Built kFlip = build_flip();
constexpr Built kReshape = build_reshape();
constexpr Built kUnsqueeze = build_unsqueeze();
constexpr Built kSqueeze = build_squeeze();
constexpr Built kSlice = build_slice();
constexpr Built kPad = build_pad();
constexpr Built kConcat = build_concat();
constexpr Built kPack = build_pack();

std::array<double, kBlock> sample_input() {
    std::array<double, kBlock> x{};
    for (std::size_t n = 0; n < kBlock; ++n) {
        x[n] = 0.31 * static_cast<double>((n * 5) % 23) - 2.0; // the parity oracle's spread of magnitudes
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
    failures += fd_check("transpose", kTranspose);
    failures += fd_check("flip", kFlip);
    failures += fd_check("reshape", kReshape);
    failures += fd_check("unsqueeze", kUnsqueeze);
    failures += fd_check("squeeze", kSqueeze);
    failures += fd_check("slice", kSlice);
    failures += fd_check("pad", kPad);
    failures += fd_check("concat", kConcat);
    failures += fd_check("pack", kPack);
    if (failures != 0) {
        std::printf("gradient structural FD check: %d samples disagreed\n", failures);
        return 1;
    }
    return 0;
}
