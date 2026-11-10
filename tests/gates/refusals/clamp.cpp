// CLAMP'S TWO NUMBERS THE WRONG WAY ROUND (Plan/28-dsl-proposals/02 A1): `clamp(x, 1.0, -1.0)` is refused while the
// graph compiles, its text in the compiler's note, with one error (tests/CMakeLists.txt). A bound that is an
// expression is not known while the graph is built and is not checked; the graphs of the spellings that compile are
// in `unit/comparisons_test.cpp`.
#include "heinzel/frontend/graph_builder.hpp"

using namespace heinzel;

constexpr Graph refused() {
    Input x{"x", Signal{8}};
    Output y{"y"};
    y = clamp(x, 1.0, -1.0);
    return y;
}

constexpr Graph kRefused = refused();
int main() { return static_cast<int>(kRefused.nodes.size()); }
