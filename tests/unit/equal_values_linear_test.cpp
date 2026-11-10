// The rule for a value written twice (`rewrite/equal_values.hpp`) stays linear in nodes that differ only in their
// attribute: an FIR of 400 lags of one input, each a node of one operation over one value, merged in a step budget
// (this target's `-fconstexpr-steps`) its old hash, which left the attribute out and so probed every lag before each,
// overran. And the lags stay apart, the taps one value each: as many nodes after the pass as before.

#include <cstdio>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/rewrite/equal_values.hpp"
#include "heinzel/sema/analyze.hpp"

using namespace heinzel;

namespace {

constexpr int kTaps = 400;

constexpr Graph fir() {
    constexpr Index n{};
    Input x{"x", Signal{Length{}}};
    Expr s = x;
    for (int k = 1; k <= kTaps; ++k) {
        s = s + x[n - k];
    }
    Output y{"y"};
    y = s;
    Graph g = finish(y);
    if (!infer(g).ok) {
        fail("equal_values_linear: the FIR did not resolve");
    }
    return g;
}

constexpr bool kept_apart() {
    Graph g = fir();
    const std::size_t before = g.nodes.size();
    rewrite::compute_equal_values_once(g);
    return g.nodes.size() == before;
}

} // namespace

int main() {
    static_assert(kept_apart(), "every lag of x a value of its own, merged in this target's step budget");
    std::printf("equal_values_linear: ok\n");
    return 0;
}
