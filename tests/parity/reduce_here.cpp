// A reduction over an axis, built in THIS tree, run on `Executor`, and its samples printed.
//
// A reduction changes axes, so it has no sample-at-a-time meaning (`MeaningRule::AxisChanging`). The
// loop tree and the reference walk give it an element-wise meaning instead, which is what makes this a
// comparison of numbers.
//
// Hand-built rather than written in the frontend's language, for the reason `dline_here.cpp` gives:
// this tree's surface has no spelling for a reduction yet, and `tests/support/hand_built_graph.hpp`
// is the shared way to put an operation under pressure before the surface reaches it.
//
// WHAT THIS COMPARISON CANNOT SEE, stated rather than left to be discovered. Nothing in the
// operation set yet MAKES a wide value whose elements differ: a literal here is ONE number standing
// for every element of its value, an input is one number per sample, and everything else is
// elementwise. So the three channels below carry the same number at every sample, and what the fold
// demonstrates against the old tree is the fan-in and the arithmetic rather than a per-channel
// mapping. `Concat` and `Iota` are the first operations that will make the elements differ, and both
// are later steps of this milestone. The mapping itself is checked where it can be — on
// `folded_element` directly, in `tests/unit/collapse_test.cpp`.
#include <array>
#include <cstddef>
#include <cstdio>

#include "executed.hpp"
#include "hand_built_graph.hpp"

using namespace heinzel;

namespace {

constexpr std::size_t kBlock = 64;
constexpr std::int32_t kWide = 3;

// SIXTY-FOUR AND THREE ARE THE OLD TREE'S CONSTRAINT, not a preference: the wide literal is spelled
// there as a constant TENSOR, one entry per element, and that tree's pool is `kMaxConstData = 256`
// floats. Three by sixty-four is 192, so one such tensor fits and two do not. Recorded where the
// numbers are, because a later reader will otherwise widen one of them and find out the hard way.
constexpr std::int32_t wide_literal(Graph& g, double value) {
    const auto v = add_value(g, ElemType::F32, Shape{kWide, kBlock}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(v)].constant = value;
    return v;
}

// Three channels, each the input scaled by a half, summed back to one number per sample. The axis
// that collapses is axis 0 — the CHANNEL axis, since the streamed axis is the innermost one, which
// is the convention both trees hold (see `reduce_there.cpp` for the old tree's own words on it).
constexpr Graph build() {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{kBlock}, ValueCategory::Input);
    const auto spread = add_node(g, op::Mul, x, wide_literal(g, 0.5));

    OpAttr collapse{};
    collapse.kind = AttrKind::Reduce;
    collapse.reduce.op = ReduceOp::Sum;
    collapse.reduce.axes = Axes{0};
    const auto y = add_node(g, op::Reduce, spread, kNoValue, kNoValue, collapse);

    g.inputs.push_back(NamedValue{Name{"x"}, x});
    g.outputs.push_back(NamedValue{Name{"y"}, y});
    return g;
}

} // namespace

int main() {
    // The same oracle signal the delay-line parity uses, and for the same reason: it visits many
    // magnitudes and few of its values are exactly representable, so arithmetic arranged differently
    // shows up in the numbers instead of cancelling.
    std::array<double, kBlock> x{};
    for (std::size_t n = 0; n < kBlock; ++n) {
        x[n] = static_cast<double>(0.31f * static_cast<float>((n * 5) % 23) - 2.0f);
    }

    const testing::Outputs y = testing::executed<build>({{"x", x.data()}});
    for (std::size_t n = 0; n < kBlock; ++n) {
        std::printf("%.9e\n", y[0][n]);
    }
    return 0;
}
