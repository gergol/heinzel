// MG S1 — multi-output capture: a frontend graph may declare more than one named output.
//
// The single-output `finish` splits a two-output graph into two graphs (the tutorial-02 port does
// exactly that). `gradients` cannot: a loss and its gradients are several named readings of ONE
// forward computation, and splitting them would recompute the forward graph per output. So MG S1 added
// a multi-output `finish`, now `finish(Output…)` (Plan/archive/14-dsl/02 §3), and this file proves it: a
// two-output streaming graph built through the frontend, run through the whole interpret path, both
// outputs read back and asserted bit-for-bit against the hand computation; a value feeding both
// outputs is emitted ONCE. An output that is a bare input is the identity — one `_realize` node, read
// back bit for bit (Plan/archive/10 §2.2 item 6); a bare literal is refused. Nothing else is added: it only
// marks more than one already-computed value as an output.
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"
#include "heinzel/support/fail.hpp"

#include "interpret.hpp"

using namespace heinzel;

namespace {

constexpr std::size_t N = 8;

// A probe that includes a negative, a zero, and non-integers, so `x*x` and `x*2+1` exercise real
// rounding rather than only exact integers.
constexpr std::array<double, N> kProbe{-1.5, -0.5, 0.0, 0.25, 0.75, 1.0, 2.5, 3.0};

bool same_bits(double a, double b) {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

// Build, over one F64 input `x` of length N, the two-output graph
//     a = x*x + 1     b = x*x * 2
// where the shared subexpression `s = x*x` must appear as ONE node, not two.
Graph build_two_outputs() {
    Input x{"x", Signal{N}, f64};
    Expr s = x * x; // the shared computation
    Output a{"a"};
    Output b{"b"};
    a = s + 1.0;
    b = s * 2.0;
    return finish(a, b);
}

// The two named outputs are present, in order, over one shared computation; the graph runs and each
// output reads back bit-for-bit its hand value. Returns 0, or a nonzero code naming what was wrong.
int check_two_outputs_run_and_share() {
    Graph g = build_two_outputs();

    if (g.outputs.size() != 2) {
        return 1;
    }
    if (g.outputs[0].name.view() != std::string_view{"a"} ||
        g.outputs[1].name.view() != std::string_view{"b"}) {
        return 2;
    }
    // The shared `x*x` is emitted once: the node list is Mul(x,x), Add(s,1), Mul(s,2) — three ops.
    if (g.nodes.size() != 3) {
        return 3;
    }
    if (!infer(g).ok) {
        return 4;
    }

    std::array<double, N> x{};
    for (std::size_t i = 0; i < N; ++i) {
        x[i] = kProbe[i];
    }
    Signals<N> sig{};
    run(g, x, sig);

    for (std::size_t n = 0; n < N; ++n) {
        const double s = kProbe[n] * kProbe[n];
        if (!same_bits(sample_at(sig, g.outputs[0].value, 0, n), s + 1.0)) {
            return 5;
        }
        if (!same_bits(sample_at(sig, g.outputs[1].value, 0, n), s * 2.0)) {
            return 6;
        }
    }
    return 0;
}

// A bare input as the output: its identity, one `_realize` node, and the numbers it was fed.
int check_an_input_is_its_identity() {
    Input x{"x", Signal{N}, f64};
    Output y{"y"};
    y = x;
    Graph g = y;
    if (g.nodes.size() != 1 || g.nodes[0].op != op::Realize || !infer(g).ok) {
        return 7;
    }
    Signals<N> sig{};
    run(g, kProbe, sig);
    for (std::size_t n = 0; n < N; ++n) {
        if (!same_bits(sample_at(sig, g.outputs[0].value, 0, n), kProbe[n])) {
            return 7;
        }
    }
    return 0;
}

// A bare literal is no output: refused, by name.
bool rejects_a_literal() {
    try {
        (void)finish(Output{"y"} = constant(1.0));
    } catch (const heinzel::error& e) {
        return e.message() == "heinzel: an output is computed or an input";
    }
    return false;
}

} // namespace

int main() {
    if (const int rc = check_two_outputs_run_and_share(); rc != 0) {
        return rc;
    }
    if (const int rc = check_an_input_is_its_identity(); rc != 0) {
        return rc;
    }
    return rejects_a_literal() ? 0 : 8;
}
