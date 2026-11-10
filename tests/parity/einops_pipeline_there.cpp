// The `einops_pipeline` example built on the OLD tree at c4012b8, run, and its outputs printed in the same
// (b, c) row-major order as `einops_pipeline_here.cpp`.
//
// The old tree's headers are extracted from the shared-object database at configure time, not read out of
// /home/user/ctg3's working directory — the reference is pinned to the commit `main` names, the same as the
// other `_there` programs. This is the old example's own `build_pipeline` graph verbatim: split the packed
// `[B, C*F, T]` with `rearrange("b (c f) t -> b c f t", ..., {{"f", F}})`, then `reduce("b c f t -> b c",
// ..., Mean)`. `set_fast_math(false)` is pinned so the reduction takes the deterministic left-fold the
// example's exact mode uses, rather than a reassociated running sum.
#include <array>
#include <cstddef>
#include <cstdio>

#include "heinzel/backends/scalar_backend.hpp"
#include "heinzel/frontend/builder.hpp"
#include "heinzel/runtime/compile.hpp"
#include "heinzel/runtime/executor.hpp"

using namespace heinzel;

namespace {

constexpr TargetInfo kTarget{.logical_cores = 1, .l1_cache_bytes = 32 * 1024, .simd_width_bytes = 32};
constexpr int B = 2;
constexpr int C = 3;
constexpr int F = 4;
constexpr int T = 5;

constexpr Graph build_pipeline() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input x{g, "x", {B, C * F, T}};
    Output y{g, "y", {B, C}};
    auto split = rearrange("b (c f) t -> b c f t", x, {{"f", F}});
    y = reduce("b c f t -> b c", split, ReduceOp::Mean);
    return g;
}

inline constexpr auto kSched = compile<build_pipeline(), kTarget>();

} // namespace

int main() {
    std::array<float, B * C * F * T> x{};
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] = 0.1f * static_cast<float>(i) + 0.5f;
    }
    std::array<float, B * C> y{};

    Executor<kSched, scalar_backend> engine;
    engine.input("x") = x.data();
    engine.output("y") = y.data();
    engine.run();

    for (int e = 0; e < B * C; ++e) {
        std::printf("%.9e\n", static_cast<double>(y[e]));
    }
    return 0;
}
