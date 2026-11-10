// A GRAPH IN WHICH AN OPERATION MAY LEAVE ITS DOMAIN DOES NOT COMPILE (Plan/26-value-ranges/05).
//
// Each case is its own target (`-DCASE=<k>`, tests/CMakeLists.txt), an `Executor` of a graph the proof of ranges
// refuses, required to fail with one error whose message is the finding (C++26; on C++23 the fixed text an `Executor`
// gives every refusal):
//   1. a division by a range that holds 0;   2. `sqrt` of a range that reaches below 0;   3. `log` of [0, 1];
//   4. `asin` of [-2, 2];   5. a promise the proof contradicts;   6. a past read, which reaches silence (`1/x[n-1]`);
//   7. `log10` of [0, 1], refused as `log` is, in its words: log10 is log times a constant;   8. `pow` of a base that
//   may be negative, in [-1, 1].
// The graphs the proof proves, which compile, are in `unit/ranges_walk_test.cpp`.
#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"

using namespace heinzel;

constexpr Index n{};

constexpr Graph refused() {
    Input x{"x", Signal{8}, ge(-1), le(1)};
    Output y{"y"};
#if CASE == 1
    y = 1.0 / x;
#elif CASE == 2
    y = sqrt(x);
#elif CASE == 3
    Input u{"u", Signal{8}, ge(0), le(1)};
    y = x * log(u);
#elif CASE == 4
    y = apply(op::Asin, x * 2.0);
#elif CASE == 5
    y = (x - 2.0).promise(gt(0), le(2));
#elif CASE == 6
    Input c{"c", Signal{8}, ge(1), le(2)};
    y = x / c[n - 1];
#elif CASE == 7
    Input u{"u", Signal{8}, ge(0), le(1)};
    y = x * log10(u);
#elif CASE == 8
    y = pow(x, 2.0);
#endif
    return y;
}

int main() {
    Executor<refused> ex;
    return static_cast<int>(sizeof ex);
}
