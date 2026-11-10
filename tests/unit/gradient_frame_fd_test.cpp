// The framing-pair adjoints (M7 S4) — unfold and overlap-add, each other's adjoint — checked against
// finite differences. The tree RUNS every one of these graphs (`_unfold` and `_ovadd` both produce
// numbers, unlike `_fft`), so the check is numbers against numbers.
//
// An unfold reads source position `f*hop + k` into frame f, tap k; its adjoint scatters each frame's
// cotangent back to that position and sums the overlaps — an overlap-add of the same window and hop
// (`rules_structural.hpp`, `adjoint_frame`). An overlap-add's adjoint reads each summed output position
// back into the frame it came from — an unfold (`adjoint_scatter`). A finite-difference check is a real
// test of that scatter/gather only if the elements a window pairs and the elements the adjoint pairs
// back can be told apart, so every graph places a DISTINCT per-element coefficient ramp on the framed
// value and weights the result by a second ramp before folding to one number per sample. A wrong axis,
// window or hop pairs the ramps differently and disagrees with the forward difference.
//
// EACH GRAPH IS LINEAR IN x: x flows through one framed value (x·ramp), and unfold, overlap-add, the
// weighting `_mul` and the fold `_reduce` are all linear, so the forward difference is exact to rounding
// at any step — the tolerance is a rounding margin, not a truncation one. The three graphs are: an
// exact-tiling unfold (`(F-1)·hop+window == L`, no dropped tail); a hopped unfold whose floor-division
// DROPS a tail (`out_len < L`), which the adjoint must zero-pad back to L — a graph that would not even
// infer if the pad were missing or mis-sized; and an overlapping overlap-add, whose adjoint unfolds a
// shared output cotangent back into every frame that wrote it.
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

// A per-element ramp whose element e holds `e + 1` — distinct along every axis and clear of zero, so no
// element's contribution silently drops. Constant: it does not depend on x.
constexpr std::int32_t ramp(Graph& g, const Shape& shape) {
    OpAttr a{};
    a.kind = AttrKind::Iota;
    a.generator.shape = shape;
    a.generator.type = ElemType::F64;
    const auto counted = add_node(g, op::Iota, kNoValue, kNoValue, kNoValue, a);
    return add_node(g, op::Add, counted, const_scalar(g, 1.0));
}

// x scaled by a distinct per-element ramp of `shape` — the framed value, linear in x.
constexpr std::int32_t coeff_times_x(Graph& g, std::int32_t x, const Shape& shape) {
    return add_node(g, op::Mul, x, ramp(g, shape));
}

constexpr std::int32_t unfold(Graph& g, std::int32_t v, std::int32_t axis, std::int32_t window,
                              std::int32_t hop) {
    OpAttr a{};
    a.kind = AttrKind::View;
    a.view.kind = ViewKind::Unfold;
    a.view.axis = axis;
    a.view.window = window;
    a.view.hop = hop;
    return add_node(g, op::Unfold, v, kNoValue, kNoValue, a);
}

constexpr std::int32_t ovadd(Graph& g, std::int32_t v, std::int32_t window, std::int32_t hop) {
    OpAttr a{};
    a.kind = AttrKind::OverlapAdd;
    a.overlap.window = window;
    a.overlap.hop = hop;
    return add_node(g, op::OverlapAdd, v, kNoValue, kNoValue, a);
}

// Weight a wide result by a ramp of its own shape and fold every element axis to one number per sample
// (all axes but the innermost, which is time).
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

// x through an exact-tiling unfold: xv=[L=8,B] (x·ramp), window 3 hop 1 → [F=6,3,B], and
// (6-1)·1+3 == 8, so the adjoint overlap-add reconstructs all 8 source positions with no pad. Tests the
// scatter of each frame's cotangent back to `f·hop+k`.
constexpr Built build_unfold_exact() {
    Built b{};
    Graph& g = b.g;
    const auto x = input_x(g);
    const auto xv = coeff_times_x(g, x, Shape{8, kBlock});
    const auto framed = unfold(g, xv, 0, 3, 1);
    b.loss = weight_and_fold(g, framed, Shape{6, 3, kBlock});
    finish(b, x);
    return b;
}

// x through a hopped unfold that DROPS a tail: xv=[L=8,B], window 3 hop 2 → F=(8-3)/2+1=3 frames,
// out_len=(3-1)·2+3=7 < 8, so source position 7 is read by no frame. Its cotangent is zero, which the
// adjoint supplies by zero-padding the overlap-add from 7 back to 8 — the graph would not infer if that
// pad were missing (a [7,B] contribution to an [8,B] operand) or mis-sized. FD naturally excludes the
// unread position, so a spurious non-zero pad would disagree.
constexpr Built build_unfold_tail() {
    Built b{};
    Graph& g = b.g;
    const auto x = input_x(g);
    const auto xv = coeff_times_x(g, x, Shape{8, kBlock});
    const auto framed = unfold(g, xv, 0, 3, 2);
    b.loss = weight_and_fold(g, framed, Shape{3, 3, kBlock});
    finish(b, x);
    return b;
}

// x through an overlapping overlap-add: xv=[F=3,K=4,B] (x·ramp), hop 2 → [out_len=(3-1)·2+4=8, B].
// Windows overlap (hop 2 < window 4), so an output position is the SUM of taps from several frames; the
// adjoint unfolds the shared output cotangent back into each of those frames. Tests `adjoint_scatter`.
constexpr Built build_overlap_add() {
    Built b{};
    Graph& g = b.g;
    const auto x = input_x(g);
    const auto xv = coeff_times_x(g, x, Shape{3, 4, kBlock});
    const auto joined = ovadd(g, xv, 4, 2);
    b.loss = weight_and_fold(g, joined, Shape{8, kBlock});
    finish(b, x);
    return b;
}

// Built at compile time: an adjoint rule that produced an ill-shaped node would fail the augmented
// graph's infer here, not at run time.
constexpr Built kUnfoldExact = build_unfold_exact();
constexpr Built kUnfoldTail = build_unfold_tail();
constexpr Built kOverlapAdd = build_overlap_add();

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
    failures += fd_check("unfold-exact", kUnfoldExact);
    failures += fd_check("unfold-tail", kUnfoldTail);
    failures += fd_check("overlap-add", kOverlapAdd);
    if (failures != 0) {
        std::printf("gradient framing FD check: %d samples disagreed\n", failures);
        return 1;
    }
    return 0;
}
