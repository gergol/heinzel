#pragma once

// The chain the compile-time guard measures on, beside `solver_cascade.hpp`.

#include <cstddef>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"

namespace heinzel {

// A chain of second-order filter sections, each reading the one before it. Chosen because it is the
// shape real work has: every section merges two arenas, every section adds a value defined by its
// own past, and the delay taps are read before the definitions exist — so this exercises the
// forwarding, the repeated merging, and the sweeps of the analysis pass all at once.
template <int Sections>
constexpr Graph cascade() {
    constexpr Index n{};
    Expr signal = Input{"x", Signal{64}};

    for (int s = 0; s < Sections; ++s) {
        Variable stage;
        const double feedback1 = 1.2 + 0.001 * s;
        const double feedback2 = -0.81;
        stage[n] = signal - feedback1 * stage[n - 1] - feedback2 * stage[n - 2];
        signal = stage;
    }

    Graph g = finish(Output{"y"} = signal);
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the cascade did not resolve");
    }
    return g;
}

} // namespace heinzel
