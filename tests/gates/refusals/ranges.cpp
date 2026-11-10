// A RANGE IS A LIST OF COMPARATORS, LAST (Plan/26-value-ranges/02): what does not say one does not compile. Each case is
// its own target (`-DCASE=<k>`, tests/CMakeLists.txt), required to fail with one error of its own:
//   1. a promise of no comparator;   2. a promise of a bare number;   3. a Param bounded by a bare number;
//   4. a type after an input's range;   5. a promise on `y[n]`, which names no value, refused by name;
//   6. a range that leaves no value, refused while compiling, its text in the compiler's note;   7. `v[n]`'s, as 5.
// The refusals with a text of their own are shown at run time in `unit/ranges_recorded_test.cpp`.
#include "heinzel/frontend/graph_builder.hpp"

using namespace heinzel;

constexpr Index n{};

constexpr Graph refused() {
    Input x{"x", Signal{8}};
    Output y{"y"};
#if CASE == 1
    y = (x * 2.0).promise();
#elif CASE == 2
    y = (x * 2.0).promise(0.5);
#elif CASE == 3
    Param p{"p", 0.5};
    y = x * p;
#elif CASE == 4
    Input w{"w", Signal{8}, ge(0), f64};
    y = x * w;
#elif CASE == 5
    y[n] = x[n] + 0.5 * y[n - 1];
    y[n].promise(ge(-2), le(2));
#elif CASE == 6
    Param p{"p", gt(2), lt(1)};
    y = x * p;
#elif CASE == 7
    Variable v;
    v[n] = x[n] + 0.5 * v[n - 1];
    v[n].promise(ge(-2), le(2));
    y = v;
#endif
    return y;
}

#if CASE == 6
constexpr Graph kRefused = refused();
int main() { return static_cast<int>(kRefused.ranges.size()); }
#else
int main() { return static_cast<int>(refused().ranges.size()); }
#endif
