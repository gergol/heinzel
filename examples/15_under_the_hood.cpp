// Tour 15: under the hood. What `Executor` compiles a graph to, for which machine, and how to look at it.
//
//   - `print<smooth>()` (`heinzel/print.hpp`) shows what `Executor<smooth>` runs, in three listings. The graph as the
//     planner is given it, after the rewrite, one line a node (`n8 v11 = tanh v10 [256] f32`: node 8 makes value 11):
//     here the one-pole's state is kept in double (`f64`) and rounded to float once (`_cast`). The schedule, which puts
//     the nodes into stages (`s1 = n8`) and gives each its loops. And the loop program: its buffers (`b0`, …), its
//     stages, each stage's statements (`s0`, …: a load, a map, a fold, each naming its graph node, `node 8`) and the
//     tree of loops that runs them (`n0`, …), numbered on their own: there `n8` is a node of the tree and `s1` a
//     statement, not the graph's node 8 or the schedule's stage 1. For `smooth`, stage 0 runs the feedback a sample at
//     a time (`loop a0 x1`), its state held in a register across the loop (`registers state`), and stage 1 the `tanh`
//     in vector lanes (`vector a0 x4`). `print` compiles at run time, which puts the compiler into the binary and about
//     280 KB on the heap while it runs; an `Executor` compiles while the program compiles, and puts neither there.
//   - `try_compile(graph())` is the compiler `Executor` runs while compiling, as a function: it returns the program,
//     or the reason a graph is refused, which a `static_assert` states in words and `print` prints.
//   - The second argument of `Executor` and of `print` is the machine to plan for: `zen2` (AVX2, FMA), `m1_pro`
//     (NEON), the `default_machine`, or `host`, measured on the build machine when CMake is configured with
//     `-DHEINZEL_AUTOTUNE=ON` (`heinzel/host_machine.hpp`); `print<smooth, zen2>()` shows what
//     `Executor<smooth, zen2>` runs. A machine changes how the loops are laid out (vector width, tiles, what runs
//     fused), never what they mean: at `High` not a bit. At `Normal` the planner's cost model also chooses per
//     machine how a sum's roundings fall (lanes, fused multiply-adds), whether a loop keeps its state in double and
//     whether a chain of filter sections runs in its parallel form, each within `Normal`'s accuracy. The one-pole
//     below comes out the same on all of them.
//   - The third argument says whether the `Executor` checks its runs (tour 12); a checked and an unchecked one are
//     different types, and may be different programs.
//   - A graph that does not make sense stops the build with one error, its reason: two shapes that disagree, a
//     port's name misspelt (`smooth.input("xx")`), a value that may leave its operation's domain, a graph larger than
//     a capacity (with the `-DHEINZEL_MAX_…=N` that raises it). So does a graph function that stops while it runs, as
//     one assigning an `Output` twice does: the reason, and the line, are in the error's notes.
//
// The machines' results are checked against each other and against a plain C++ loop.

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "heinzel/dsl.hpp"
#include "heinzel/print.hpp"
#if __has_include("heinzel/host_machine.hpp")
#include "heinzel/host_machine.hpp" // in a build directory configured with -DHEINZEL_AUTOTUNE=ON
#define HAVE_HOST_MACHINE 1
#endif

using namespace heinzel;

namespace {

constexpr int kSamples = 256;
constexpr Index n{};

// A one-pole low-pass, then a soft clip.
constexpr Graph smooth() {
    Input x{"x", Signal{kSamples}};
    Output y{"y"};
    Variable low;
    low[n] = low[n - 1] + 0.1 * (x[n] - low[n - 1]);
    y = tanh(2.0 * low);
    return y;
}

// Two tensors whose shapes cannot be multiplied: refused, in words.
constexpr Graph mismatched() {
    Input a{"a", {4, 3}};
    Input b{"b", {4, 3}};
    Output c{"c"};
    c = einsum("ij,jk->ik", a, b);
    return c;
}
static_assert(try_compile(mismatched()).refusal.view() == "einsum's contracted extents disagree: 3 and 4");

template <const Machine& M>
std::vector<float> run_on(const std::vector<float>& x) {
    std::vector<float> y(kSamples);
    Executor<smooth, M> filter;
    filter.input("x") = x.data();
    filter.output("y") = y.data();
    filter.run();
    return y;
}

} // namespace

int main() {
    // Compiled again here, at run time, to be looked at: what `Executor<smooth>` compiled while this file compiled.
    const std::string listing = print<smooth>();
    std::printf("what `Executor<smooth>` runs:\n\n%s\n", listing.c_str());

    std::vector<float> x(kSamples);
    for (int t = 0; t < kSamples; ++t) {
        x[t] = t % 64 < 32 ? 1.0f : -1.0f; // a square wave
    }
    std::vector<std::vector<float>> runs = {run_on<default_machine>(x), run_on<zen2>(x), run_on<m1_pro>(x)};
#ifdef HAVE_HOST_MACHINE
    runs.push_back(run_on<host>(x));
#endif

    double low = 0.0;
    double off = 0.0;
    for (int t = 0; t < kSamples; ++t) {
        low += 0.1 * (x[t] - low);
        for (const std::vector<float>& y : runs) {
            off = std::fmax(off, std::fabs(y[t] - std::tanh(2.0 * low)));
        }
    }
    if (!(off < 1e-6)) {
        std::fprintf(stderr, "15_under_the_hood: %.3g from the plain loop\n", off);
        return 1;
    }
    bool same = true;
    for (const std::vector<float>& y : runs) {
        same = same && y == runs[0];
    }
    std::printf("15_under_the_hood: %zu machines, %s, within %.1e of the plain loop\n", runs.size(),
                same ? "the same numbers bit for bit" : "the same numbers up to a sum's last bits", off);
    return 0;
}
