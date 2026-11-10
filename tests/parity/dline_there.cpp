// The same tap delay line, built on the OLD tree at c4012b8, run, and its samples printed in the
// same format as `dline_here.cpp`.
//
// The old tree's headers are extracted from the shared object database at configure time, not read
// out of /home/user/ctg3's working directory — same reason as `biquad_there.cpp`: the reference is
// pinned to the commit `main` names rather than to whatever happens to be checked out, and nothing
// here ever opens that directory.
#include <array>
#include <cstddef>
#include <cstdio>

#include "heinzel/backends/scalar_backend.hpp"
#include "heinzel/frontend/builder.hpp"
#include "heinzel/runtime/compile.hpp"
#include "heinzel/runtime/executor.hpp"

using namespace heinzel;

namespace {

constexpr std::size_t kBlock = 64;
constexpr int kMaxDelay = 8;

constexpr TargetInfo kTarget{
    .logical_cores = 1,
    .l1_cache_bytes = 16 * 1024,
    .simd_width_bytes = 16,
};

// The same graph as `dline_here.cpp`, spelled in this tree's language: a comparison of the signal
// against zero, scaled and offset into a read position, and the line read at it. The position lands
// on 3.25 or 3.75 and is exact either way, so both trees choose the same pair of samples and what
// the comparison measures is the interpolation.
constexpr Graph build() {
    GraphBuilder g;
    // The exact tier, as the biquad does: without it this tree is free to reassociate, and the two
    // sides would then differ over whether the compiler was allowed to rearrange the arithmetic
    // rather than over the delay line.
    g.set_fast_math(false);
    Input x{g, "x", {kBlock}};
    Output y{g, "y", {kBlock}};
    Expr negative = (x < 0.0f);
    Expr position = 3.25f + negative * 0.5f;
    y = frac_delay(x, position, kMaxDelay);
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

    Executor<kSchedule, scalar_backend> line;
    line.input("x") = x.data();
    line.output("y") = y.data();
    line.run();

    for (std::size_t n = 0; n < kBlock; ++n) {
        std::printf("%.9e\n", static_cast<double>(y[n]));
    }
    return 0;
}
