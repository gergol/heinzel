// Tutorial chapter 08 — "autoscheduling and deployment", built on the OLD tree at c4012b8, run, and
// its samples printed in the same format as `tutorial_08_here.cpp` (M8.3, S5c). The old tree's
// headers are pinned from the object database at configure time; nothing here opens /home/user/ctg3.
//
// The chapter's numeric content is the row RMS-normalize the old `08_autoscheduling_and_deployment.cpp`
// pins under its two machine models and its pinned executor: y = x * rsqrt(mean(x*x, Axes{1}, true)
// + 1e-6f). `set_fast_math(false)` pins the arithmetic; what remains between the two trees is the mean
// reduction accumulated in f32 here versus in double in `tutorial_08_here.cpp`, the test's
// named allowance. This program pins ONE machine model (the schedule the old chapter would inspect is
// not part of the numeric compare — the two-machine-models demonstration is re-expressed on the here
// side in this tree's own spelling).
#include <array>
#include <cstddef>
#include <cstdio>

#include "heinzel/backends/scalar_backend.hpp"
#include "heinzel/frontend/builder.hpp"
#include "heinzel/runtime/compile.hpp"
#include "heinzel/runtime/executor.hpp"

using namespace heinzel;

namespace {

constexpr int R = 6;
constexpr int C = 8;

constexpr TargetInfo kTarget{
    .logical_cores = 1,
    .l1_cache_bytes = 16 * 1024,
    .simd_width_bytes = 16,
};

// Row RMS-normalize, exactly the old chapter's `build_norm` graph.
constexpr Graph build_norm() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input x{g, {.name = "x", .shape = {R, C}}};
    (x * rsqrt(mean(x * x, Axes{1}, true) + 1e-6f)).as_output("y"); // row RMS-normalize
    return g;
}

inline constexpr auto kNorm = compile<build_norm(), kTarget>();

} // namespace

int main() {
    std::array<float, R * C> x{};
    std::array<float, R * C> y{};
    for (int i = 0; i < R * C; ++i) {
        x[static_cast<std::size_t>(i)] = 0.37f * static_cast<float>(i) - 2.0f;
    }
    Executor<kNorm, scalar_backend> ex;
    ex.input("x") = x.data();
    ex.output("y") = y.data();
    ex.run();
    for (int i = 0; i < R * C; ++i) {
        std::printf("%.9e\n", static_cast<double>(y[static_cast<std::size_t>(i)]));
    }
    return 0;
}
