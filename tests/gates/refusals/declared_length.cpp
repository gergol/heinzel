// A DECLARED LENGTH IS A SIGNAL (Plan/archive/14-dsl/10 §2): a `Variable` or an `Output` declares a `Signal`, never a tensor
// and never a signal of no length. Each case is its own target (`-DCASE=<k>`, tests/CMakeLists.txt), required to
// fail with one error: 1. `Variable v{{4}}`;   2. `Output y{"y", {64}}`;   3. `Variable v{Signal{}}`.
#include "heinzel/frontend/graph_builder.hpp"

using namespace heinzel;

constexpr Graph refused() {
    constexpr Index n{};
    Input x{"x", Signal{64}};
#if CASE == 1
    Variable v{{4}};
    v[n] = x[n] + 0.5 * v[n - 1];
    Output y{"y"};
    y = v * 1.0;
#elif CASE == 2
    Output y{"y", {64}};
    y[n] = x[n] + 0.5 * y[n - 1];
#elif CASE == 3
    Variable v{Signal{}};
    v[n] = x[n] + 0.5 * v[n - 1];
    Output y{"y"};
    y = v * 1.0;
#endif
    return y;
}

int main() { return static_cast<int>(refused().nodes.size()); }
