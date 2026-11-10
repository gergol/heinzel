// A reduction over an axis, built on the OLD tree at c4012b8, run, and its samples printed in the
// same format as `reduce_here.cpp`.
//
// Milestone M4.5's reduction family — the half that was carried into M5 because it needed an
// axis-capable executor to compare against, and now has one.
//
// The old tree's headers are extracted from the shared object database at configure time, not read
// out of /home/user/ctg3's working directory — same reason as `biquad_there.cpp`: the reference is
// pinned to the commit `main` names rather than to whatever happens to be checked out.
//
// THE STREAMED AXIS IS THE INNERMOST ONE IN BOTH TREES, which is what makes `Axes{0}` here and
// `Axes{0}` there the same claim. This tree settled that convention at M5 on its own committed
// evidence; the old tree says it in as many words in `integrations/juce/graph_processor.hpp` ("the
// graph's fixed internal block = the innermost extent of its first input leaf") and in
// `opt/relayout.hpp` ("per-channel instances along the innermost (time) axis"). So the axis this
// reduction collapses is the CHANNEL axis on both sides, and the comparison is a comparison.
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

constexpr std::size_t kBlock = 64;
constexpr std::int32_t kWide = 3;

constexpr TargetInfo kTarget{
    .logical_cores = 1,
    .l1_cache_bytes = 16 * 1024,
    .simd_width_bytes = 16,
};

// A constant TENSOR every entry of which is a half. That is what a wide literal is in the other
// tree — one number standing for every element of the value — and it is spelled out entry by entry
// here because this tree has no other way to say it. Its size is why the block is sixty-four and the
// width three: the old tree's constant pool is `kMaxConstData = 256` floats and 3 x 64 is 192, so
// one such tensor fits in a program and two do not.
constexpr std::array<float, static_cast<std::size_t>(kWide) * kBlock> halves() {
    std::array<float, static_cast<std::size_t>(kWide) * kBlock> a{};
    for (float& v : a) {
        v = 0.5f;
    }
    return a;
}
constexpr auto kHalves = halves();

// The exact tier, as the biquad and the delay line do: without it this tree is free to reassociate
// the sum, and the two sides would then differ over whether the compiler was allowed to rearrange
// the fold rather than over the fold.
constexpr Graph build() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input x{g, "x", {kBlock}};
    Output y{g, "y", {kBlock}};
    Expr spread = x * g.constant(std::span<const float>{kHalves.data(), kHalves.size()},
                                 Shape{kWide, static_cast<std::int32_t>(kBlock)});
    y = sum(spread, Axes{0});
    return g;
}

inline constexpr auto kSchedule = compile<build(), kTarget>();

} // namespace

int main() {
    std::array<float, kBlock> x{};
    std::array<float, kBlock> y{};
    for (std::size_t n = 0; n < kBlock; ++n) {
        x[n] = 0.31f * static_cast<float>((n * 5) % 23) - 2.0f;
    }

    Executor<kSchedule, scalar_backend> folded;
    folded.input("x") = x.data();
    folded.output("y") = y.data();
    folded.run();

    for (std::size_t n = 0; n < kBlock; ++n) {
        std::printf("%.9e\n", static_cast<double>(y[n]));
    }
    return 0;
}
