// Tutorial chapter 04 — "recurrences and streaming", built on the OLD tree at c4012b8, run, and its
// samples printed in the same format as `tutorial_04_here.cpp` (M8.3, S5). The old tree's headers are
// pinned from the object database at configure time; nothing here opens /home/user/ctg3.
//
// The chapter's compared numeric content is the section-(1) leaky one-pole y[n] = (1-a)*x[n] +
// a*y[n-1], run whole-buffer with zero initial history — the sub-part the old chapter seeds with
// `set_initial` and the block-streaming it drives with `set_streaming` have no counterpart in this
// tree's whole-block runtime and are re-expressed on the here side (see `tutorial_04_here.cpp`).
// `set_fast_math(false)` pins the arithmetic tier on this side exactly as `biquad_there.cpp` does, so
// what remains between the two trees is this tree's fused multiply-add in each recurrence — the
// test's named allowance.
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

constexpr std::size_t N = 48;
constexpr float kA = 0.85f; // the chapter's one-pole coefficient

constexpr TargetInfo kTarget{
    .logical_cores = 1,
    .l1_cache_bytes = 16 * 1024,
    .simd_width_bytes = 16,
};

constexpr Graph build_leaky() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input x{g, "x", {N}};
    Output y{g, "y", {N}};
    Index n;
    y[n] = (1.0f - kA) * x[n] + kA * y[n - 1];
    return g;
}

// Section (3): the internal Variable two-pole chain. `s` is a bare Variable (internal state), only
// `y` is an output — the same shape as the here side's chained Variables.
constexpr Graph build_twopole() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input x{g, "x", {N}};
    Index n;
    Variable s;
    s[n] = 0.7f * x[n] + 0.3f * s[n - 1];
    Variable y;
    y[n] = 0.6f * s[n] + 0.4f * y[n - 1];
    Expr(y).as_output("y");
    return g;
}

inline constexpr auto kLeaky = compile<build_leaky(), kTarget>();
inline constexpr auto kTwopole = compile<build_twopole(), kTarget>();

template <auto Schedule>
void run_and_print(const std::array<float, N>& x) {
    std::array<float, N> y{};
    Executor<Schedule, scalar_backend> filter;
    filter.input("x") = x.data();
    filter.output("y") = y.data();
    filter.run();
    for (std::size_t n = 0; n < N; ++n) {
        std::printf("%.9e\n", static_cast<double>(y[n]));
    }
}

} // namespace

int main() {
    std::array<float, N> x{};
    for (std::size_t i = 0; i < N; ++i) {
        x[i] = std::sin(0.3f * static_cast<float>(i)) + 0.2f;
    }

    run_and_print<kLeaky>(x);   // section (1): the leaky one-pole
    run_and_print<kTwopole>(x); // section (3): the internal Variable two-pole chain
    return 0;
}
