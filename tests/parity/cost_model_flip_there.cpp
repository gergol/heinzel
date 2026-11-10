// The cost_model_flip benchmark's separable blur, built on the OLD tree at c4012b8, run, and its
// samples printed in the same order as `cost_model_flip_here.cpp`.
//
// This is `benchmarks/cost_model_flip_bench.cpp`'s `g_sep_blur` — a Kp-tap horizontal reduce feeding a
// Kc-tap vertical windowed reduce:
//     bx[i,j] = Σ_p x[i, j+p]·wh[p]        (unfold the innermost/W axis, weight, sum the window)
//     y[i,j]  = Σ_c bx[i+c, j]·wv[c]       (unfold_rows the row axis, weight, sum the window)
// The bench's POINT is that the cost model's roofline flips this graph's SCHEDULE (fuse vs materialize
// blur_x) between a compute-bound and a memory-rich target while the numbers stay bit-identical — a
// SCHEDULING property, checked by the schedule gates in tests/. As an S6 CORRECTNESS corpus the ported
// content is the numeric blur OUTPUT, so this program compiles the graph for ONE target and runs it;
// the flip is not re-demonstrated here (it is numerically neutral by construction).
//
// The bench feeds a constant 0.5 image with UNIFORM weights, which makes every blur output a constant
// 0.5 — vacuous for a parity comparison. This corpus instead feeds a varied image and non-uniform
// weights so bx and y are genuine weighted sums and a reduction-order difference between the trees is
// actually observable. The GRAPH is `g_sep_blur` as written.
//
// The old tree's headers come from the shared-object database at configure time (pinned to `main`),
// same as the other `*_there.cpp`. Exact tier (`set_fast_math(false)`) pins the deterministic fold.
#include <array>
#include <cstddef>
#include <cstdio>

#include "heinzel/backends/scalar_backend.hpp"
#include "heinzel/frontend/builder.hpp"
#include "heinzel/runtime/compile.hpp"
#include "heinzel/runtime/executor.hpp"

using namespace heinzel;

namespace {

constexpr int H = 6; // small correctness corpus (under kMaxPlanes), not the bench's 1024x4096 sweep
constexpr int W = 8;
constexpr int Kp = 3;
constexpr int Kc = 3;
constexpr int OH = H - Kc + 1;
constexpr int OW = W - Kp + 1;

constexpr TargetInfo kTarget{
    .logical_cores = 1,
    .l1_cache_bytes = 16 * 1024,
    .simd_width_bytes = 16,
};

// `g_sep_blur` (H, W, Kp, Kc) verbatim: horizontal reduce (unfold innermost/W) then vertical windowed
// reduce (unfold_rows the row axis). Both weighted sums fold over the appended window axis (Axes{2}).
constexpr Graph build() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input x{g, "x", {H, W}};
    Input wh{g, "wh", {Kp}};
    Input wv{g, "wv", {Kc}};
    Output y{g, "y", {OH, OW}};
    Expr bx = sum(unfold(x, Kp, 1) * wh, Axes{2});      // [H, W-Kp+1]
    y = sum(unfold_rows(bx, Kc, 1) * wv, Axes{2});      // [H-Kc+1, W-Kp+1]
    return g;
}

inline constexpr auto kBlur = compile<build(), kTarget>();

} // namespace

int main() {
    std::array<float, H * W> x{};
    for (int p = 0; p < H * W; ++p) {
        x[static_cast<std::size_t>(p)] = 0.13f * static_cast<float>(p % 23) - 1.0f; // varied, non-constant
    }
    std::array<float, Kp> wh{};
    for (int p = 0; p < Kp; ++p) {
        wh[static_cast<std::size_t>(p)] = 0.2f + 0.1f * static_cast<float>(p); // non-uniform taps
    }
    std::array<float, Kc> wv{};
    for (int c = 0; c < Kc; ++c) {
        wv[static_cast<std::size_t>(c)] = 0.3f - 0.1f * static_cast<float>(c); // non-uniform taps
    }
    std::array<float, OH * OW> y{};

    Executor<kBlur, scalar_backend> ex;
    ex.input("x") = x.data();
    ex.input("wh") = wh.data();
    ex.input("wv") = wv.data();
    ex.output("y") = y.data();
    ex.run();

    for (int e = 0; e < OH * OW; ++e) {
        std::printf("%.9e\n", static_cast<double>(y[static_cast<std::size_t>(e)]));
    }
    return 0;
}
