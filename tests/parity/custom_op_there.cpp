// The `custom_op` example, built on the OLD tree at c4012b8, run, and its samples printed in the same
// format as `custom_op_here.cpp`.
//
// The old tree's headers are extracted from the shared object database at configure time, not read out
// of /home/user/ctg3's working directory — same reason as the other `_there` programs: the reference
// is pinned to the commit `main` names rather than to whatever happens to be checked out.
//
// The helper `log10(x) = log(x) * (1/ln 10)` is composed here exactly as `custom_op_here.cpp` composes
// it, and the post-multiply by 20 is in the same order with the same constants, so the two programs
// differ in nothing but the natural-log each tree evaluates `_log` with (libm's here).
#include <array>
#include <cstddef>
#include <cstdio>

#include "heinzel/backends/scalar_backend.hpp"
#include "heinzel/frontend/builder.hpp"
#include "heinzel/runtime/compile.hpp"
#include "heinzel/runtime/executor.hpp"

using namespace heinzel;

namespace {

constexpr std::size_t kBlock = 64;

constexpr TargetInfo kTarget{
    .logical_cores = 1,
    .l1_cache_bytes = 16 * 1024,
    .simd_width_bytes = 16,
};

// The composed helper, exactly as `custom_op_here.cpp` and as the old example's own `myops::log10`:
// a function over Expr, not a new op. Its own namespace both mirrors the example and keeps it clear of
// this tree's BUILT-IN `log10` (op `Log10`) — the ambiguity is the point, since the example is about
// adding a spelling the library did not already have.
namespace myops {
constexpr Expr log10(const Expr& x) { return log(x) * 0.43429448190325176; }
} // namespace myops

// The exact tier, as the other `_there` programs use: without it this tree is free to reassociate the
// arithmetic, and the two sides would then differ over whether the compiler rearranged the multiplies
// rather than over the log core. The log tier itself inherits the graph default.
constexpr Graph build() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input x{g, "x", {kBlock}};
    Output y{g, "y", {kBlock}};
    y = myops::log10(x) * 20.0;
    return g;
}

inline constexpr auto kSchedule = compile<build(), kTarget>();

} // namespace

int main() {
    std::array<float, kBlock> x{};
    std::array<float, kBlock> y{};
    for (std::size_t n = 0; n < kBlock; ++n) {
        x[n] = 0.31f * static_cast<float>((n * 5) % 23) + 0.5f;
    }

    Executor<kSchedule, scalar_backend> decibels;
    decibels.input("x") = x.data();
    decibels.output("y") = y.data();
    decibels.run();

    for (std::size_t n = 0; n < kBlock; ++n) {
        std::printf("%.9e\n", static_cast<double>(y[n]));
    }
    return 0;
}
