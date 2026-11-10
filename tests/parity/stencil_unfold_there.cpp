// The `stencil_indexing_2d` example's UNFOLD idiom built on the OLD tree at c4012b8, run, and its interior
// rows printed in the same (frame, w) row-major order as `stencil_unfold_here.cpp`.
//
// The old tree's headers are extracted from the shared-object database at configure time. This is the old
// example's `build_unfolded` verbatim: a 3-tap vertical Laplacian via `sum(unfold(x, 0, 3, 1) * taps,
// Axes{2})` over the "valid" interior region `[H-2, W]`. `set_fast_math(false)` pins the deterministic
// reduce fold the example's own reference (ascending-tap `fma_rn`) assumes.
#include <array>
#include <cstddef>
#include <cstdio>
#include <span>

#include "heinzel/backends/scalar_backend.hpp"
#include "heinzel/frontend/builder.hpp"
#include "heinzel/runtime/compile.hpp"
#include "heinzel/runtime/executor.hpp"

using namespace heinzel;

namespace {

constexpr TargetInfo kTarget{.logical_cores = 1, .l1_cache_bytes = 32 * 1024, .simd_width_bytes = 32};
constexpr int H = 8;
constexpr int W = 12;

constexpr Graph build_unfolded() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input x{g, "x", {H, W}};
    constexpr std::array<float, 3> k{1.0f, -2.0f, 1.0f};
    auto taps = g.constant(std::span<const float>(k), {3});
    sum(unfold(x, /*axis*/ 0, /*K*/ 3, /*S*/ 1) * taps, Axes{2}).as_output("y");
    return g;
}

inline constexpr auto kSched = compile<build_unfolded(), kTarget>();

} // namespace

int main() {
    std::array<float, H * W> x{};
    for (int p = 0; p < H * W; ++p) {
        x[static_cast<std::size_t>(p)] = 0.13f * static_cast<float>(p % 23) - 1.0f;
    }
    std::array<float, (H - 2) * W> y{};
    Executor<kSched, scalar_backend> ex;
    ex.input("x") = x.data();
    ex.output("y") = y.data();
    ex.run();
    for (int e = 0; e < (H - 2) * W; ++e) {
        std::printf("%.9e\n", static_cast<double>(y[e]));
    }
    return 0;
}
