// A CAPACITY REACHED IS REFUSED BY NAME, WITH THE OVERRIDE THAT RAISES IT (docs/limitations.md).
//
// Each case is its own target (`-DCASE=<k>`, tests/CMakeLists.txt), built with a small override of the capacity it
// reaches and required to fail with that refusal's own words. The graph is compiled in a constant expression through
// `compile()`, whose note carries the whole text at C++26 and C++23 alike (an `Executor` prints a fixed one on C++23):
//   1. values;   2. nodes;   3. inputs;   4. outputs;   5. a Dim's id;   6. statements;   7. tree nodes;
//   8. buffers;   9. loop nests;   10. a stage's statements;   11. a cycle the planner keeps in one stage;
//   12. a constant's numbers;   13. the stated ranges;   14. the outputs with a debug build's checks of promises (the
//   graph compiled checked, so that it has its check in every build);   15. and 16. the values and the nodes with the
//   copy of an input a delay line reads, made while compiling: the graph itself is at the capacity, asserted, so that
//   one already past it fails twice, and its test asks for one error.
#include "heinzel/compile.hpp"
#include "heinzel/frontend/graph_builder.hpp"

using namespace heinzel;

constexpr Index n{};

// Cases 15 and 16: five values and two nodes, and the copy of x its delay line reads one more of each.
constexpr Graph line_over_an_input() {
    constexpr Index at{48'000};
    Input x{"x", Signal{64}};
    Input d{"d", Signal{64}};
    Output y{"y"};
    y = x[at - d.at_most(1.0) * ms];
    return y;
}

constexpr Graph reached() {
#if CASE == 1 || CASE == 2 || CASE == 6 || CASE == 7 || CASE == 8 || CASE == 10
    Input x{"x", Signal{64}};
    Input w{"w", Signal{64}};
    Output y{"y"};
    y = x * 0.5 + w + 2.0;
    return y;
#elif CASE == 3
    Input a{"a", Signal{64}};
    Input b{"b", Signal{64}};
    Input c{"c", Signal{64}};
    Output y{"y"};
    y = a + b + c;
    return y;
#elif CASE == 4
    Input x{"x", Signal{64}};
    Output a{"a"};
    Output b{"b"};
    Output c{"c"};
    a = x * 1.0;
    b = x * 2.0;
    c = x * 3.0;
    return finish(a, b, c);
#elif CASE == 5
    Input x{"x", Signal{Dim{1}}};
    Output y{"y"};
    y = x * 0.5;
    return y;
#elif CASE == 9
    Input x{"x", Signal{4, 4, 64}};
    Output y{"y"};
    y = x.sum(0).sum(0); // two loop nests
    return y;
#elif CASE == 11
    Input x{"x", Signal{64}};
    Output y{"y"};
    y[n] = x[n] + 0.5 * y[n - 1];
    return y;
#elif CASE == 12
    Input x{"x", {3}};
    Output y{"y"};
    y = x * constant({0.25, 0.5, 0.25});
    return y;
#elif CASE == 13
    Input x{"x", Signal{64}, ge(-1), le(1)};
    Param gain{"gain", ge(0), le(1)};
    Output y{"y"};
    y = x * gain;
    return y;
#elif CASE == 14
    Input x{"x", Signal{64}};
    Output y{"y"};
    y = (x * 2.0).promise(ge(-1), le(1));
    return y;
#elif CASE == 15 || CASE == 16
    return line_over_an_input();
#endif
}

#if CASE == 15
static_assert(reached().values.size() == kMaxValues);
#elif CASE == 16
static_assert(reached().nodes.size() == kMaxNodes);
#endif

constexpr Compiled kCompiled = compile(reached(), default_machine, Bindings{}, true);

int main() { return static_cast<int>(kCompiled.program.stmts.size()); }
