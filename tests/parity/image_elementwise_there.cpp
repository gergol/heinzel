// The `image_elementwise_2d` example, built on the OLD tree at c4012b8, run, and its samples printed in
// the same format as `image_elementwise_here.cpp`.
//
// The old tree's headers are extracted from the shared object database at configure time, not read out
// of /home/user/ctg3's working directory — same reason as the other `_there` programs: the reference is
// pinned to the commit `main` names rather than to whatever happens to be checked out.
//
// This is the old example's own graph verbatim: `x{H,W}`, `bias{W}` broadcast over rows, a `gain`
// Param, and `y = t/(1+t*t)`, `t = gain*x + bias`. `set_fast_math(false)` is pinned (the example does
// not) so this side cannot contract `gain*x+bias` into a once-rounded FMA; with `-ffp-contract=off` on
// both sides too, both do add/mul/div correctly rounded to f32 in the same order, which is why the
// comparison is exact. Values are filled from the same `0.07f*i-2` / `0.1f*w-0.5` / `1.3f` the here side
// uses and printed in the same (h,w) row-major order.
#include <array>
#include <cstddef>
#include <cstdio>

#include "heinzel/backends/scalar_backend.hpp"
#include "heinzel/frontend/builder.hpp"
#include "heinzel/runtime/compile.hpp"
#include "heinzel/runtime/executor.hpp"

using namespace heinzel;

namespace {

constexpr int H = 8;
constexpr int W = 12;
constexpr TargetInfo kTarget{.logical_cores = 1, .l1_cache_bytes = 32 * 1024, .simd_width_bytes = 32};

constexpr Graph build_image() {
    GraphBuilder g;
    g.set_fast_math(false); // no FMA contraction: gain*x then +bias, to match the here side
    Input x{g, "x", {H, W}};
    Input bias{g, "bias", {W}}; // broadcasts over rows
    Param gain{g, "gain"};
    Output y{g, "y", {H, W}};
    auto t = gain * x + bias;
    y = t / (1.0f + t * t);
    return g;
}

inline constexpr auto kSched = compile<build_image(), kTarget>();

} // namespace

int main() {
    std::array<float, H * W> x{};
    std::array<float, H * W> y{};
    std::array<float, W> bias{};
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] = 0.07f * static_cast<float>(i) - 2.0f;
    }
    for (int w = 0; w < W; ++w) {
        bias[w] = 0.1f * static_cast<float>(w) - 0.5f;
    }
    constexpr float gain = 1.3f;

    Executor<kSched, scalar_backend> engine;
    engine.input("x") = x.data();
    engine.input("bias") = bias.data();
    engine.param("gain") = gain;
    engine.output("y") = y.data();
    engine.run();

    // y is the flat {H,W} row-major buffer, so y[h*W+w] is pixel (h,w) — the same order the here side
    // prints its (element h*W+w, sample 0).
    for (int h = 0; h < H; ++h) {
        for (int w = 0; w < W; ++w) {
            std::printf("%.9e\n", static_cast<double>(y[h * W + w]));
        }
    }
    return 0;
}
