// The same biquad, built on the OLD tree at c4012b8, run, and its samples printed in the same
// format as `biquad_here.cpp`.
//
// The old tree's headers are extracted from the shared object database at configure time, not read
// out of /home/user/ctg3's working directory. Two reasons, and the second is the important one:
// extraction reads the commit that `main` names rather than whatever happens to be checked out, so
// the oracle is pinned by construction; and nothing here ever opens that directory, which is the
// cheapest way to keep it read-only in fact rather than in intention.
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

constexpr float kFeedback1 = 2.0f * 0.9f * 0.70710678f;
constexpr float kFeedback2 = -(0.9f * 0.9f);

constexpr TargetInfo kTarget{
    .logical_cores = 1,
    .l1_cache_bytes = 16 * 1024,
    .simd_width_bytes = 16,
};

constexpr Graph build() {
    GraphBuilder g;
    // The exact tier: no reordering of arithmetic. Without it the old tree defaults to allowing
    // reassociation, and the two sides then differ in the last bit — which is not a disagreement
    // about the filter, only about whether the compiler was free to rearrange it. Pinning the tier
    // on BOTH sides is what makes the comparison about the graph.
    g.set_fast_math(false);
    Input x{g, "x", {kBlock}};
    Output y{g, "y", {kBlock}};
    Index n;
    y[n] = x[n] + kFeedback1 * y[n - 1] + kFeedback2 * y[n - 2];
    return g;
}

inline constexpr auto kSchedule = compile<build(), kTarget>();

} // namespace

int main() {
    std::array<float, kBlock> x{};
    std::array<float, kBlock> y{};
    x[0] = 1.0f;

    Executor<kSchedule, scalar_backend> filter;
    filter.input("x") = x.data();
    filter.output("y") = y.data();
    filter.run();

    for (std::size_t n = 0; n < kBlock; ++n) {
        std::printf("%.9e\n", static_cast<double>(y[n]));
    }
    return 0;
}
