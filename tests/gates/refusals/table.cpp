// A TABLE A READ CANNOT COUNT, AND LOOKUP'S BOUNDS OR CELLS SHORT OF WHAT IT NEEDS (Plan/28-dsl-proposals/05 D3). Each
// case is its own target (`-DCASE=<k>`, tests/CMakeLists.txt), required to fail with one error, the refusal's text in
// the compiler's note:
//   1. `wavetable` of a computed table: its cells are known only to the analysis, and the read is built with them;
//   2. `lookup(table, x, 3.0, -1.0)`: lo above hi;
//   3. `lookup(table, x, -inf, 1.0)`: a bound that is no number a double holds, so that every x would read a NaN;
//   4. `lookup` of one cell: N points from lo to hi lie (hi - lo)/(N - 1) apart.
// The spellings that compile are in `unit/tables_test.cpp`, and the other bounds `lookup` refuses.
#include <limits>

#include "heinzel/frontend/graph_builder.hpp"

using namespace heinzel;

constexpr float kCells[] = {0.0f, 0.5f, 1.0f, 0.5f};
constexpr float kOneCell[] = {0.5f};

constexpr Graph refused() {
    Input p{"p", Signal{8}};
    Expr table = constant(kCells);
    Output y{"y"};
#if CASE == 1
    Expr louder = table * 2.0;
    y = wavetable(louder, p);
#elif CASE == 2
    y = lookup(table, p, 3.0, -1.0);
#elif CASE == 3
    y = lookup(table, p, -std::numeric_limits<double>::infinity(), 1.0);
#elif CASE == 4
    y = lookup(constant(kOneCell), p, 0.0, 1.0);
#endif
    return y;
}

constexpr Graph kRefused = refused();
int main() { return static_cast<int>(kRefused.nodes.size()); }
