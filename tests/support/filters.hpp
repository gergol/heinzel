#pragma once

// The filters the tests run, written once.
//
// Several test files need a biquad, each at a block length it chooses — sixty-four where the
// question is whether a bound symbol reproduces its literal twin, eight where a graph only has to
// exist. Written out in each, the copies drift, and then tests that look like they run the same
// filter do not. One spelling everywhere, test graphs included.
//
// `tests/parity/biquad_here.cpp` deliberately keeps its own copy and is NOT built on this. It is
// the cross-tree mirror: its coefficients are computed from a pole radius and angle so that they
// are visibly the same numbers the old tree's own embedded example uses. Making it share a builder
// with the unit tests would hide the thing it exists to show.

#include <array>
#include <cstddef>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"

// The wide filter below is hand-built rather than written through the frontend: a literal wider than
// one number per sample has no spelling in the language yet, and it is the only way this tree can
// make a wide value out of a narrow input.
#include "hand_built_graph.hpp"
#include "interpret.hpp"

namespace heinzel {

// y[n] = x[n] + 1.27279221*y[n-1] - 0.81*y[n-2]. Poles at radius 0.9, angle 45 degrees.
//
// The block length arrives as an `Extent`, so the same filter can be built with it literal or
// declared as a symbol — which is the whole of what oracle O7 compares.
constexpr Graph biquad(Extent block, const Bindings& sizes = Bindings{},
                       numerics::Precision level = numerics::Precision::Normal) {
    constexpr Index n{};
    Input x{"x", Signal{block}};
    Output y{"y"};
    // Each feedback product is named before combining. The two products are the operands of
    // `operator+`, whose evaluation order C++ leaves unsequenced, and each operand's evaluation is
    // what inserts its constant/mul nodes into the arena. Written inline, gcc's constant evaluator
    // and its generated code pick different operand orders, so the arena permutes between compile
    // time and run time (Q-2 — construction order depending on evaluation order; the numbers are
    // bit-identical either way). Naming fixes the order deterministically. See
    // docs/decisions.md (finding Q-2/GCC-ARENA-ORDER).
    Expr fb1 = 1.27279221 * y[n - 1];
    Expr fb2 = -0.81 * y[n - 2];
    y[n] = x[n] + fb1 + fb2;
    Graph g = y;
    set_default_precision(g, level);
    const Diagnostic d = infer(g, sizes);
    if (!d.ok) {
        fail("the biquad graph did not resolve");
    }
    return g;
}

// A feed-forward comb into a one-pole: y[n] = x[n] - 0.5*x[n-2] + 0.8*y[n-1]. A different
// arrangement from the biquad — a subtraction, and a delay of the INPUT rather than of the output —
// so a defect in how one arrangement resolves does not pass twice.
constexpr Graph comb(Extent block, const Bindings& sizes = Bindings{}) {
    constexpr Index n{};
    Input x{"x", Signal{block}};
    Output y{"y"};
    // Same Q-2 discipline as biquad: name each product before combining so the arena order does not
    // depend on operator+/operator- operand evaluation order. See docs/decisions.md (finding Q-2/GCC-ARENA-ORDER).
    Expr ff = 0.5 * x[n - 2];
    Expr fb = 0.8 * y[n - 1];
    y[n] = x[n] - ff + fb;
    Graph g = y;
    const Diagnostic d = infer(g, sizes);
    if (!d.ok) {
        fail("the comb graph did not resolve");
    }
    return g;
}

// A graph `channels` numbers per sample, driven by a named input of ONE number per sample.
//
// Here rather than in one test because two now run it, and for the reason `biquad` is here: the block
// arrives as a parameter because the tests that want it want different lengths. The width comes from
// a LITERAL, which is the only way this tree can make a wide value out of a narrow input — a literal
// is one number and repeats over the elements of the value it stands for.
//
// `channels == 1` gives the same graph one number wide, which is the twin every element of the wide
// one is compared against.
constexpr Graph wide_filter(std::int32_t channels, std::int32_t block) {
    Graph g{};
    const Shape wide = channels == 1 ? Shape{block} : Shape{channels, block};
    const auto x = add_value(g, ElemType::F64, Shape{block}, ValueCategory::Input);
    const auto gain = add_value(g, ElemType::F64, wide, ValueCategory::Const);
    g.values[static_cast<std::size_t>(gain)].constant = 0.5;
    const auto scaled = add_node(g, op::Mul, x, gain);

    OpAttr lag{};
    lag.kind = AttrKind::Delay;
    lag.delay.samples = 1;
    const auto delayed = add_node(g, op::FixedDelay, scaled, kNoValue, kNoValue, lag);
    const auto sum = add_node(g, op::Add, scaled, delayed);

    g.inputs.push_back(NamedValue{"x", x});
    g.outputs.push_back(NamedValue{"y", sum});
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the wide filter did not resolve");
    }
    return g;
}

// One impulse, then silence.
template <std::size_t BlockSize>
constexpr std::array<double, BlockSize> impulse() {
    std::array<double, BlockSize> block{};
    block[0] = 1.0;
    return block;
}

// The graph's output, walked in the written order.
template <std::size_t BlockSize>
constexpr std::array<double, BlockSize> walk(const Graph& g) {
    Signals<BlockSize> signals{};
    run(g, impulse<BlockSize>(), signals);
    std::array<double, BlockSize> out{};
    for (std::size_t n = 0; n < BlockSize; ++n) {
        out[n] = sample_at(signals, g.outputs[0].value, 0, n);
    }
    return out;
}

// A filter that produced silence would satisfy every comparison a test makes about it, so the
// samples have to be a real signal before agreeing about them means anything.
template <std::size_t BlockSize>
constexpr bool is_a_real_signal(const std::array<double, BlockSize>& s) {
    bool moved = false;
    for (const double x : s) {
        if (x != 0.0) {
            moved = true;
        }
        if (x != x) { // not a number
            return false;
        }
    }
    return moved && s[0] != 0.0;
}

} // namespace heinzel
