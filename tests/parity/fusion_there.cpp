// The fusion benchmark's producer-into-conv graph, built on the OLD tree at c4012b8, run, and its
// frames printed in the same order as `fusion_here.cpp`.
//
// The old tree's headers are extracted from the shared-object database at configure time (pinned to the
// commit `main` names, not to whatever is checked out) — same reason as the other `*_there.cpp`.
//
// This is `benchmarks/fusion_bench.cpp`'s `fused()` graph VERBATIM: p = x*x, then a 1-D convolution
// spelled as a windowed view `transpose(unfold(p, K, 1))` weighted by `reshape(h, {K,1})` and summed
// over the tap axis — y[i] = Σ_k p[i+k]·h[k], (N-K+1) outputs. `set_fast_math(false)` pins the
// deterministic reduce fold, so the only freedom left between the two trees is the ORDER in which the
// K products are summed (this side: the old tree's tap-axis fold; the here side: `_reduce` over the
// window axis) — which is exactly what the parity ALLOWED accounts for.
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
// N,K match fusion_here.cpp (a correctness corpus, not the bench's timing sweep). See the here side.
constexpr int N = 48;
constexpr int K = 5;

// p = x*x, then conv1d: y[i] = Σ_k p[i+k]·h[k] — the bench's fused() body, fast-math pinned off.
constexpr Graph build() {
    GraphBuilder g;
    g.set_fast_math(false);
    auto x = g.input("x", {N});
    auto h = g.input("h", {K});
    auto y = g.output("y", {N - K + 1});
    auto p = x * x;
    auto wt = transpose(unfold(p, K, 1), Perm{1, 0}); // [K, N_out]
    auto hc = reshape(h, Shape{K, 1});
    y = sum(wt * hc, Axes{0});
    return g.graph;
}

inline constexpr auto kSched = compile<build(), kTarget>();

} // namespace

int main() {
    std::array<float, N> x{};
    for (int i = 0; i < N; ++i) {
        x[static_cast<std::size_t>(i)] = 0.001f * static_cast<float>(i % 1000) - 0.6f;
    }
    std::array<float, K> h{};
    for (int k = 0; k < K; ++k) {
        h[static_cast<std::size_t>(k)] = 0.001f * static_cast<float>(k % 1000) - 0.3f;
    }
    std::array<float, N - K + 1> y{};

    Executor<kSched, scalar_backend> ex;
    ex.input("x") = x.data();
    ex.input("h") = h.data();
    ex.output("y") = y.data();
    ex.run();

    for (int f = 0; f < N - K + 1; ++f) {
        std::printf("%.9e\n", static_cast<double>(y[static_cast<std::size_t>(f)]));
    }
    return 0;
}
