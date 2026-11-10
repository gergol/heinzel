// SPELLINGS THAT MUST NOT COMPILE: index notation's build-failure fixtures (Plan/archive/14-dsl/02 §2).
//
// Each case is built as its own target (`-DCASE=<k>`, tests/CMakeLists.txt) that must fail to build for
// its own reason, matched on the compiler's message:
//   1. `n - 2.5` — a lag is a whole number of samples;
//   2. `n + 1` — the future is no lag;
//   3. `x[n] = e` on an Expr — would assign to a temporary and define nothing;
//   4. `y[n - 1] = e` — the same, on a lag;
//   5. `v[n] = e` through a `const Variable&` — the same, on the read-only proxy;
//   6. `Variable w = v;` — a variable is defined once, so it is not copied;
//   7. `Signal{}` — a signal has a length;
//   8. `Output z = y;` — it would copy y's name (`z = y` gives z y's value).
#include "heinzel/frontend/graph_builder.hpp"

using namespace heinzel;

constexpr Index n{};

constexpr int build() {
    Expr x = Input{"x", Signal{8}};
    Variable v;
#if CASE == 1
    Expr late = x[n - 2.5];
#elif CASE == 2
    Expr late = x[n + 1];
#elif CASE == 3
    x[n] = x * 2.0;
#elif CASE == 4
    x[n - 1] = x * 2.0;
#elif CASE == 5
    const Variable& read_only = v;
    read_only[n] = x * 2.0;
#elif CASE == 6
    Variable w = v;
#elif CASE == 7
    Signal none{};
#elif CASE == 8
    Output y{"y"};
    Output z = y;
#endif
    return 0;
}

int main() { return build(); }
