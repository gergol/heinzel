// The `conditionals` example, built on the OLD tree at c4012b8, run, and its samples printed in the
// same three sections and the same order as `conditionals_here.cpp`.
//
// The old tree's headers are extracted from the shared object database at configure time, not read out
// of /home/user/ctg3's working directory — same reason as the other `_there` programs.
//
// The gate's `thresh` is the old tree's real `Param`, set to the same value `conditionals_here.cpp`
// feeds its `thresh` input every sample; the two therefore compute the same numbers, one via a knob
// and one via a constant-valued input.
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
constexpr float kThresh = 0.5f;

constexpr TargetInfo kTarget{
    .logical_cores = 1,
    .l1_cache_bytes = 16 * 1024,
    .simd_width_bytes = 16,
};

// The exact tier, as the other `_there` programs use, so a difference cannot be the compiler
// reassociating arithmetic rather than the thing under test.
constexpr Graph build_gate() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input x{g, "x", {kBlock}};
    Param thresh{g, "thresh"};
    Output y{g, "y", {kBlock}};
    y = where(abs(x) > thresh, x, 0.0f);
    return g;
}

constexpr Graph build_soft() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input x{g, "x", {kBlock}};
    Output y{g, "y", {kBlock}};
    y = x / (1.0f + abs(x));
    return g;
}

constexpr Graph build_hard() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input x{g, "x", {kBlock}};
    Output y{g, "y", {kBlock}};
    y = min(max(x, -1.0f), 1.0f);
    return g;
}

inline constexpr auto kGate = compile<build_gate(), kTarget>();
inline constexpr auto kSoft = compile<build_soft(), kTarget>();
inline constexpr auto kHard = compile<build_hard(), kTarget>();

void print_block(const std::array<float, kBlock>& y) {
    for (std::size_t n = 0; n < kBlock; ++n) {
        std::printf("%.9e\n", static_cast<double>(y[n]));
    }
}

} // namespace

int main() {
    std::array<float, kBlock> x{};
    std::array<float, kBlock> y{};
    for (std::size_t n = 0; n < kBlock; ++n) {
        x[n] = 0.08f * static_cast<float>(n) - 2.5f;
    }

    {
        Executor<kGate, scalar_backend> gate;
        gate.input("x") = x.data();
        gate.param("thresh") = kThresh;
        gate.output("y") = y.data();
        gate.run();
        print_block(y);
    }
    {
        Executor<kSoft, scalar_backend> soft;
        soft.input("x") = x.data();
        soft.output("y") = y.data();
        soft.run();
        print_block(y);
    }
    {
        Executor<kHard, scalar_backend> hard;
        hard.input("x") = x.data();
        hard.output("y") = y.data();
        hard.run();
        print_block(y);
    }
    return 0;
}
