// Tutorial chapter 01 — "getting started", built on the OLD tree at c4012b8, run, and its 256
// samples printed in the same format as `tutorial_01_here.cpp`.
//
// The old tree's headers are extracted from the shared object database at configure time, not read
// out of /home/user/ctg3's working directory — the reference is pinned to the commit `main` names,
// and nothing here opens that directory. Same discipline as every other `_there` program.
//
// The graph is the chapter's own y = a*x + 1 with a REAL `Param a` set to 2.0. With `set_fast_math`
// off, the two sides are MEASURED bit-exact (largest difference 0.000e+00 over 256 samples): the old
// tree's fma contraction of a*x+1 (opt/optimize.hpp `contract_fma`) does not change the result on the
// scalar backend here, so — unlike the biquad — there is no accounted difference and the test's
// allowance is 0.0. See the test's BECAUSE in tests/CMakeLists.txt.
#include <array>
#include <cstddef>
#include <cstdio>

#include "heinzel/backends/scalar_backend.hpp"
#include "heinzel/frontend/builder.hpp"
#include "heinzel/runtime/compile.hpp"
#include "heinzel/runtime/executor.hpp"

using namespace heinzel;

namespace {

constexpr std::size_t kBlock = 256;

constexpr TargetInfo kTarget{
    .logical_cores = 1,
    .l1_cache_bytes = 16 * 1024,
    .simd_width_bytes = 16,
};

// y = a*x + 1, with `a` a runtime Param. `set_fast_math(false)` pins the arithmetic so the two sides
// cannot differ over reassociation — what remains is the tree's own fma contraction, named in the
// test's allowance.
constexpr Graph build() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input x{g, "x", {kBlock}};
    Param a{g, "a"};
    Output y{g, "y", {kBlock}};
    y = a * x + 1.0;
    return g;
}

inline constexpr auto kSchedule = compile<build(), kTarget>();

} // namespace

int main() {
    std::array<float, kBlock> x{};
    std::array<float, kBlock> y{};
    for (std::size_t i = 0; i < kBlock; ++i) {
        x[i] = 0.25f * static_cast<float>(i) - 8.0f;
    }

    Executor<kSchedule, scalar_backend> ex;
    ex.input("x") = x.data();
    ex.param("a") = 2.0f;
    ex.output("y") = y.data();
    ex.run();

    for (std::size_t n = 0; n < kBlock; ++n) {
        std::printf("%.9e\n", static_cast<double>(y[n]));
    }
    return 0;
}
