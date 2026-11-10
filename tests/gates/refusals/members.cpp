// A FLOATING-POINT AXIS DOES NOT COMPILE (Plan/archive/14-dsl/03 §5): `x.max(0.0)` reads as the elementwise maximum and
// would otherwise convert to axis 0 and fold. Each case is its own target (`-DCASE=<k>`, tests/CMakeLists.txt),
// required to fail on the deleted overload with one error:
//   1. `x.max(0.0)`;   2. `x.max(0.0, keepdims)`;   3. `x.slice(0.5, 0, 1)`;   4-15. each other member's first
//   axis a floating-point one, one case a member: sum, mean, min, prod, unfold, unfold2d, squeeze, unsqueeze, sort,
//   topk, quantile, median;   16. `z.max(0.0)` on an `Output z`, which forwards the members (Plan/28-dsl-proposals/02
//   A2), refused in the same words.
#include "heinzel/frontend/graph_builder.hpp"

using namespace heinzel;

constexpr Graph refused() {
    Input x{"x", {4, 6}};
    Output y{"y"};
#if CASE == 1
    y = x.max(0.0);
#elif CASE == 2
    y = x.max(0.0, keepdims);
#elif CASE == 3
    y = x.slice(0.5, 0, 1);
#elif CASE == 4
    y = x.sum(0.0);
#elif CASE == 5
    y = x.mean(0.0);
#elif CASE == 6
    y = x.min(0.0);
#elif CASE == 7
    y = x.prod(0.0);
#elif CASE == 8
    y = x.unfold(0.0, 3);
#elif CASE == 9
    y = x.unfold2d(0.0, 2, 1, 2);
#elif CASE == 10
    y = x.squeeze(0.0);
#elif CASE == 11
    y = x.unsqueeze(0.0);
#elif CASE == 12
    y = x.sort(0.0);
#elif CASE == 13
    y = x.topk(0.0, 2);
#elif CASE == 14
    y = x.quantile(0.0, 0.5);
#elif CASE == 15
    y = x.median(0.0);
#elif CASE == 16
    Output z{"z"};
    z = x * 2.0;
    y = z.max(0.0);
#endif
    return y;
}

int main() { return static_cast<int>(refused().nodes.size()); }
