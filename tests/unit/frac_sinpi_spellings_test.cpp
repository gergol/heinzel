// The `floor`, `frac`, and `sinpi` frontend spellings the `oscillator` example needs (M8.3, S5).
//
// Each is a spelling for an op that already exists and is already checked, added because one example
// needs it (operator decision 2026-09-04 11:58, Reading A / Option A). This file proves the lowering:
// a graph built from the spelling, run through the whole interpret path, lands on exactly the number
// the M4 CORE answers — so the spelling names the right op and reaches the interpreter, not merely
// that it compiles. `floor` and `sinpi` name op::Floor and op::SinPi directly; `frac` is the composed
// `x - floor(x)`, so its check is the composed identity, not a core of its own.
//
// F64 storage throughout and a BITWISE compare: floor is exact, the fractional subtraction is exact
// on these probes (floor(x) and x share an exponent in [0,1) and at the chosen magnitudes), and the
// sinpi core at the graph-default Precise tier is the SAME exact core `apply(op::SinPi, .)` runs, so
// every one of these agrees to the last bit or the spelling is lowering to something else.
#include <array>
#include <cstddef>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"

#include "interpret.hpp"
#include "one_operation.hpp"

using namespace heinzel;
using heinzel::testing::apply;
using heinzel::testing::same_bits;

namespace {

constexpr std::size_t N = 12;

// A probe crossing the interesting boundaries for all three spellings: negatives (floor rounds toward
// −inf, not toward zero), exact integers and negative zero (frac must give +0), a large magnitude
// where the fractional part is still exact, and the phase values 0, 0.25, 0.5, 0.75 where sinpi(2·p)
// is 0, +1, 0, −1 — the oscillator's own waveform points.
constexpr std::array<double, N> kProbe{
    -1.7, -0.5, -0.0, 0.0, 0.25, 0.5, 0.75, 1.0, 1.7, 3.5, 8.0, 100.25,
};

// Run a single-input elementwise F64 graph over the probe and return its N samples.
std::array<double, N> run_spelling(const Graph& g) {
    std::array<double, N> x{};
    for (std::size_t i = 0; i < N; ++i) {
        x[i] = kProbe[i];
    }
    Signals<N> s{};
    run(g, x, s);
    std::array<double, N> y{};
    for (std::size_t n = 0; n < N; ++n) {
        y[n] = sample_at(s, g.outputs[0].value, 0, n);
    }
    return y;
}

// Build `f(x)` over one F64 input of length N, resolved.
template <class Build>
Graph one_input_graph(Build&& f) {
    Input x{"x", Signal{N}, ElemType::F64};
    Graph g = finish(Output{"y"} = f(x));
    if (!infer(g).ok) {
        fail("the spelling graph did not resolve");
    }
    return g;
}

} // namespace

int main() {
    const std::array<double, N> got_floor = run_spelling(one_input_graph([](Expr x) { return floor(x); }));
    const std::array<double, N> got_frac = run_spelling(one_input_graph([](Expr x) { return frac(x); }));
    const std::array<double, N> got_sinpi = run_spelling(one_input_graph([](Expr x) { return sinpi(x); }));

    for (std::size_t i = 0; i < N; ++i) {
        const double x = kProbe[i];

        // floor names op::Floor: the frontend result is bit-for-bit the core's answer.
        if (!same_bits(got_floor[i], apply(op::Floor, x))) {
            return 1;
        }

        // frac is the composed x − floor(x): bit-for-bit the same subtraction, and +0 (never −0) on an
        // exact integer, which the −0.0 and integer probes pin.
        const double frac_ref = x - apply(op::Floor, x);
        if (!same_bits(got_frac[i], frac_ref)) {
            return 2;
        }

        // sinpi names op::SinPi at the graph-default Precise (exact) tier: bit-for-bit the exact core.
        if (!same_bits(got_sinpi[i], apply(op::SinPi, x))) {
            return 3;
        }
    }

    return 0;
}
