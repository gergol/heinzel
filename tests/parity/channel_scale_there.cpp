// The `channel_scale_3d` example, built on the OLD tree at c4012b8, run, and its samples printed in the
// same format as `channel_scale_here.cpp`.
//
// The old tree's headers are extracted from the shared object database at configure time, not read out
// of /home/user/ctg3's working directory — same reason as the other `_there` programs: the reference is
// pinned to the commit `main` names rather than to whatever happens to be checked out.
//
// This is the old example's own graph verbatim: `x{C,H,W}`, `scale{C,1,1}`, `bias{C,1,1}`, and
// `y = x*scale + bias`, the per-channel vector broadcasting over H and W. `set_fast_math(false)` is
// pinned (the example itself does not) so this side cannot contract `x*scale+bias` into a single-rounded
// FMA — with it off both trees do one f32 multiply then one f32 add, which is why the comparison is
// exact. The values are filled from the same `0.03f*i - 1` and the same per-channel constants
// `channel_scale_here.cpp` uses, and printed in the same row-major (c,h,w) order.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>

#include "heinzel/backends/scalar_backend.hpp"
#include "heinzel/frontend/builder.hpp"
#include "heinzel/runtime/compile.hpp"
#include "heinzel/runtime/executor.hpp"

using namespace heinzel;

namespace {

constexpr int C = 3;
constexpr int H = 4;
constexpr int W = 5;
constexpr TargetInfo kTarget{.logical_cores = 1, .l1_cache_bytes = 32 * 1024, .simd_width_bytes = 32};

constexpr Graph build_channel_scale() {
    GraphBuilder g;
    g.set_fast_math(false); // no FMA contraction: one multiply then one add, to match the here side
    Input x{g, "x", {C, H, W}};
    Input scale{g, "scale", {C, 1, 1}}; // broadcasts over H, W
    Input bias{g, "bias", {C, 1, 1}};
    Output y{g, "y", {C, H, W}};
    y = x * scale + bias;
    return g;
}

inline constexpr auto kSched = compile<build_channel_scale(), kTarget>();

} // namespace

int main() {
    constexpr int N = C * H * W;
    std::array<float, N> x{};
    std::array<float, N> y{};
    for (int i = 0; i < N; ++i) {
        x[i] = 0.03f * static_cast<float>(i) - 1.0f;
    }
    std::array<float, C> scale{2.0f, -1.0f, 0.5f};
    std::array<float, C> bias{0.1f, -0.2f, 0.3f};

    Executor<kSched, scalar_backend> engine;
    engine.input("x") = x.data();
    engine.input("scale") = scale.data();
    engine.input("bias") = bias.data();
    engine.output("y") = y.data();
    engine.run();

    // y is the flat {C,H,W} row-major buffer, so y[(c*H+h)*W + w] is (c,h,w). Printed in the same
    // nested order channel_scale_here.cpp prints its (element c*H+h, sample w).
    for (int c = 0; c < C; ++c) {
        for (int h = 0; h < H; ++h) {
            for (int w = 0; w < W; ++w) {
                std::printf("%.9e\n", static_cast<double>(y[(c * H + h) * W + w]));
            }
        }
    }
    return 0;
}
