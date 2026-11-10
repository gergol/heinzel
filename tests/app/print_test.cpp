// `print<g>()` (`app/heinzel/print.hpp`, Plan/28-dsl-proposals/04 C3) prints what `Executor<g, M, Checked>` runs. Its
// run-time compile is the one an `Executor` makes while this file compiles: its three listings equal the graph listing
// pinned below and the schedule and program prints of `try_compile` evaluated here at compile time, on two machines,
// checked and not. A refused graph prints its refusal, the analysis's, the loop tree's and `Executor`'s own, which
// `try_compile` does not make (a symbol off time, state that does not run along the stream); a graph function that
// stops prints the error it threw.

#include <cstdio>
#include <expected>
#include <string>
#include <string_view>

#include "heinzel/dsl.hpp"
#include "heinzel/print.hpp"

using namespace heinzel;

namespace {

int g_wrong = 0;

void check(bool ok, const char* what) {
    std::printf("  %s: %s\n", what, ok ? "yes" : "NO");
    g_wrong += ok ? 0 : 1;
}

constexpr Index n{};

// Tour 15's one-pole and soft clip.
constexpr Graph smooth() {
    Input x{"x", Signal{256}};
    Output y{"y"};
    Variable low;
    low[n] = low[n - 1] + 0.1 * (x[n] - low[n - 1]);
    y = tanh(2.0 * low);
    return y;
}

// Its graph as the default machine's planner is given it: the two reads of `low[n - 1]` one, the state in double,
// rounded to float once.
constexpr std::string_view kSmoothGraph = "v2 = input x [256] f32\n"
                                          "n0 v1 = _fdelay v0 back 1 [256] f64\n"
                                          "n1 v3 = v2 - v1 [256] f64\n"
                                          "n2 v5 = 0.1 * v3 [256] f64\n"
                                          "n3 v6 = v1 + v5 [256] f64\n"
                                          "n4 v7 = _recur v6 defines v0 [256] f64\n"
                                          "n5 v11 = _cast v7 [256] f32\n"
                                          "n6 v9 = 2 * v11 [256] f32\n"
                                          "n7 v10 = tanh v9 [256] f32\n"
                                          "output y = v10\n";

// The host keeps a + b above 0, which the proof cannot see: a checked build checks it, as an output `#0`.
constexpr Graph divided() {
    Input x{"x", Signal{8}, ge(-1), le(1)};
    Input a{"a", Signal{8}, ge(0), le(1)};
    Input b{"b", Signal{8}, ge(0), le(1)};
    Output q{"q"};
    q = x / (a + b).promise(gt(0), le(2));
    return q;
}

// Refused by the analysis.
constexpr Graph mismatched() {
    Input a{"a", {4, 3}};
    Input b{"b", {4, 3}};
    Output c{"c"};
    c = einsum("ij,jk->ik", a, b);
    return c;
}

// Refused by the loop tree, which names the operation.
constexpr Graph three_largest() {
    Input x{"x", Signal{6, 64}};
    Output y{"y"};
    y = x.topk(0, 3);
    return y;
}

// Compiled by `try_compile`, refused by `Executor` (`tests/gates/refusals/executor.cpp`, cases 12 and 15): a symbol on
// an element axis, the stream the literal 4; one-sample state against a 16-sample signal, which would step once a run.
constexpr Graph symbol_off_time() {
    Input x{"x", Signal{Dim{0}, 4}};
    Output y{"y"};
    y = x * 2.0;
    return y;
}
constexpr Graph one_sample_recurrence() {
    Input w{"w", Signal{4, 1}};
    Input x{"x", Signal{4, 16}};
    Variable s;
    s[n] = w[n] + 0.5 * s[n - 1];
    Output y{"y"};
    y = x * s;
    return y;
}
static_assert(try_compile(symbol_off_time()).refusal.view().empty());
static_assert(try_compile(one_sample_recurrence()).refusal.view().empty());

// Stops while it runs.
constexpr Graph assigned_twice() {
    Input x{"x", Signal{8}};
    Output y{"y"};
    y = x;
    y = 2.0 * x;
    return y;
}

// The three prints of the compile `Executor<Build, M, Checked>` makes, done while this file compiles, as `print` lays
// them out. The schedule is not kept by `try_compile`; it is the planner's of the graph as planned.
template <auto Build, const Machine& M, bool Checked>
std::string printed_while_compiling() {
    static constexpr CompileResult r = try_compile(Build(), M, Bindings{}, Checked);
    static constexpr std::expected<loops::Schedule, loops::Refusal> s = loops::planner::schedule_of(r.compiled.graph, M);
    const std::string graph = detail::graph_listing(r.compiled.graph);
    const std::string schedule = loops::print(*s, r.compiled.graph);
    const std::string program = loops::print(r.compiled.program);
    return "-- graph\n" + graph + "-- schedule\n" + schedule + "-- program\n" + program;
}

} // namespace

int main() {
    const std::string smooth_here = print<smooth>();
    std::printf("print<smooth>():\n%s", smooth_here.c_str());
    check(smooth_here == printed_while_compiling<smooth, default_machine, detail::kChecked>(),
          "print<smooth>() is the compile-time compile's three prints");
    check(smooth_here.starts_with("-- graph\n" + std::string(kSmoothGraph) + "-- schedule\n"), "its graph as pinned");
    check(print<smooth, zen2>() == printed_while_compiling<smooth, zen2, detail::kChecked>(), "print<smooth, zen2>() too");
    const std::string checked = print<divided, default_machine, true>();
    const std::string unchecked = print<divided, default_machine, false>();
    check(checked == printed_while_compiling<divided, default_machine, true>() &&
              unchecked == printed_while_compiling<divided, default_machine, false>(),
          "checked and not, each its own");
    check(checked.find("output #0 = ") != std::string::npos && unchecked.find("output #0 = ") == std::string::npos,
          "the check an output of the checked one only");

    const std::string refused = print<mismatched>();
    std::printf("print<mismatched>(): %s", refused.c_str());
    check(refused == "einsum's contracted extents disagree: 3 and 4\n" &&
              refused == std::string(try_compile(mismatched()).refusal.view()) + "\n",
          "a refused graph prints the analysis's refusal");
    const std::string not_lowered = print<three_largest>();
    std::printf("print<three_largest>(): %s", not_lowered.c_str());
    check(not_lowered == "heinzel::compile: topk 3 is not one exact rank, which is not lowered yet: [6, 64]\n",
          "and the loop tree's");
    const std::string off_time = print<symbol_off_time>();
    std::printf("print<symbol_off_time>(): %s", off_time.c_str());
    check(off_time == "heinzel::Executor: at most one symbol, on the time axis\n",
          "and Executor's, for a symbol off time");
    const std::string off_stream = print<one_sample_recurrence>();
    std::printf("print<one_sample_recurrence>(): %s", off_stream.c_str());
    check(off_stream == "heinzel::Executor: this graph's state does not run along the stream\n",
          "and for state that does not run along the stream");
    const std::string stopped = print<assigned_twice>();
    std::printf("print<assigned_twice>(): %s", stopped.c_str());
    check(stopped == "heinzel: an Output is assigned once\n", "a graph function that stops prints the error it threw");

    if (g_wrong != 0) {
        std::printf("print_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("print_test: print<g>() shows what Executor<g> runs\n");
    return 0;
}
