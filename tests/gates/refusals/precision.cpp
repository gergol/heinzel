// A PRECISION MARK GOES WHERE A VALUE IS MADE (Plan/28-dsl-proposals/02 A4): a named value shares its node with every
// reader, so marking it would mark them too. Each case is its own target (`-DCASE=<k>`, tests/CMakeLists.txt),
// required to fail with one error of its own:
//   1. `s.precision(High)` on a named value: a build error that shows the fix;
//   2. `x[n].precision(High)` after `tanh(x)` read x: refused while the graph compiles, its text in the compiler's note.
// The spellings that compile are in `unit/precision_resolve_test.cpp`.
#include "heinzel/frontend/graph_builder.hpp"

using namespace heinzel;

constexpr Index n{};

constexpr Graph refused() {
    Input x{"x", Signal{8}};
    Output y{"y"};
    Expr s = tanh(x);
#if CASE == 1
    y = s.precision(High);
#elif CASE == 2
    y = s + x[n].precision(High);
#endif
    return y;
}

constexpr Graph kRefused = refused();
int main() { return static_cast<int>(kRefused.nodes.size()); }
