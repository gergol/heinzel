// The `dynamic_length` example, built on the OLD tree at c4012b8, run at two block lengths, and
// SELF-CHECKED against a hand reference — the reference half of MD.4's parity.
//
// This is the old tree's `examples/dynamic_length.cpp` verbatim in substance: an elementwise block
// effect `y = x*gain + bias` over a stereo block `Shape{N, 2}` whose length `N` is a runtime dimension
// declared with constraints (a multiple of 8; a representative 256) and resolved at `prepare()`. The
// graph is compiled once; two different block lengths run through the SAME executor by re-preparing.
//
// The old tree's headers are extracted from the shared object database at configure time, not read out
// of a working directory — the reference is pinned to the commit `main` names. Only the `include/` tree
// is archived, so the example's body is reconstructed here (it cannot `#include examples/...`); nothing
// about the graph, the feed, the knobs, or the self-check is changed.
//
// PARITY MODEL (MD.4). This file is value-self-checking: `process_block` returns whether its output
// matched `x*gain+bias` within 1e-6, in the old tree's own f32 precision, and `main` exits 0 on PASS.
// The parity harness (`dynamic_length_selfcheck.cmake`) requires this and the `_here` program both exit
// 0 (both recover the block effect) and both print the same `STORY` line (same arena story). The
// `arena_elems` integer is printed for the record but NOT compared across trees — the two arena models
// differ, and a cross-tree arena-size equality is as meaningless as the cross-tree artifact hash
// `compare.cmake` explains cannot exist.
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

#include "heinzel/backends/scalar_backend.hpp"
#include "heinzel/frontend/builder.hpp"
#include "heinzel/runtime/compile.hpp"
#include "heinzel/runtime/executor.hpp"

using namespace heinzel;

namespace {
constexpr TargetInfo kTarget{
    .logical_cores = 1,
    .l1_cache_bytes = 32 * 1024,
    .simd_width_bytes = 32,
};

// y = x * gain + bias, over a stereo block of [N, 2]. N is a DYNAMIC dimension, resolved at prepare().
// gain/bias are runtime scalar params (knobs), unrelated to the shape.
constexpr Graph build() {
    GraphBuilder g;
    auto N = g.dim("N", {.multiple_of = 8, .expect = 256});
    Input x{g, "x", {N, 2}};
    Param gain{g, "gain"};
    Param bias{g, "bias"};
    Output y{g, "y", {N, 2}};
    y = x * gain + bias;
    return g;
}

inline constexpr auto kArtifact = compile<build(), kTarget>();

// Process one block of `frames` stereo frames at the given length, comparing to a hand reference.
bool process_block(Executor<kArtifact, scalar_backend>& ex, std::int32_t frames, float gain, float bias) {
    const std::size_t n = static_cast<std::size_t>(frames) * 2;
    std::vector<float> x(n);
    std::vector<float> y(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = 0.01f * static_cast<float>(i % 257) - 1.0f;
    }
    ex.input("x") = x.data();
    ex.output("y") = y.data();
    ex.param("gain") = gain;
    ex.param("bias") = bias;
    ex.dim("N") = frames; // set the runtime length...
    ex.prepare();          // ...then validate + (re)allocate
    ex.run();              // allocation-free

    float max_err = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        max_err = std::fmax(max_err, std::fabs(y[i] - (x[i] * gain + bias)));
    }
    return max_err < 1e-6f;
}
} // namespace

int main() {
    Executor<kArtifact, scalar_backend> ex;

    // Two different block lengths through the SAME compiled graph + executor: re-prepare() reallocates
    // for the new length.
    const bool ok256 = process_block(ex, 256, 1.5f, -0.25f);
    const bool ok64 = process_block(ex, 64, 0.5f, 0.1f);

    std::printf("arena: arena_elems=%d\n", kArtifact.arena_elems);
    std::printf("STORY dynamic_length compiled-once ran-lengths 256 64\n");
    const bool ok = ok256 && ok64;
    std::printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
