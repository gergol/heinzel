// A DELAY THAT CHANGES, SPELLED SHORT OF WHAT IT NEEDS (Plan/28-dsl-proposals/05 D4). Each case is its own target
// (`-DCASE=<k>`, tests/CMakeLists.txt), required to fail with one error, the refusal's text in the compiler's message
// or note:
//   1. `x[n - d * ms]`: a distance with no bound, so the line would not know how much to keep;
//   2. `x[n - d.at_most(25.0)]`: a bound with no unit of time;
//   3. `d.at_most(-1.0)`: a bound below 0;
//   4. `x[n - d.at_most(1.0) * ms]` with an `Index` that carries no sample rate.
// The spellings that compile are in `unit/delay_lines_test.cpp`.
#include "heinzel/frontend/declarations.hpp"

using namespace heinzel;

constexpr Index n{48'000};
constexpr Index unrated{};

constexpr Graph refused() {
    Input x{"x", Signal{8}};
    Input d{"d", Signal{8}};
    Output y{"y"};
#if CASE == 1
    y = x[n - d * ms];
#elif CASE == 2
    y = x[n - d.at_most(25.0)];
#elif CASE == 3
    y = x[n - d.at_most(-1.0) * ms];
#elif CASE == 4
    y = x[unrated - d.at_most(1.0) * ms];
#endif
    return y;
}

constexpr Graph kRefused = refused();
int main() { return static_cast<int>(kRefused.nodes.size()); }
