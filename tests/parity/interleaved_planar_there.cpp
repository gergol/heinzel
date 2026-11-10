// The `interleaved_planar_audio` example's PLANAR gain built on the OLD tree at c4012b8, run, and printed
// TWICE — so the here side's direct gain and its transpose-round-trip gain each compare against this same
// reference (line i and line i + C*T).
//
// The old tree's headers are extracted from the shared-object database at configure time, not read out of
// /home/user/ctg3's working directory. This builds the old example's gain on a PLANAR `[C, T]` input (not
// the interleaved-physical `input_layout` — that run-surface memory-layout feature is the S5 reserved
// inventory item, recorded in docs/decisions.md, not ported), which is the same arithmetic `y[c,t] =
// gain[c]·x[c,t]` and the faithful reference for the ported subset. `set_fast_math(false)` is pinned for
// determinism though a lone multiply of exact operands contracts nothing.
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

constexpr TargetInfo kTarget{.logical_cores = 1, .l1_cache_bytes = 16 * 1024, .simd_width_bytes = 16};
constexpr std::int32_t C = 2;
constexpr std::int32_t T = 16;

constexpr Graph build_gain() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input x{g, "x", {C, T}};
    Output y{g, "y", {C, T}};
    const float gains[C] = {0.5f, 2.0f};
    auto gvec = g.constant(std::span<const float>(gains, C), {C, 1}); // {C,1} broadcasts over t
    y = x * gvec;
    return g;
}

inline constexpr auto kGain = compile<build_gain(), kTarget>();

void run_and_print() {
    std::array<float, C * T> x{};
    for (int c = 0; c < C; ++c) {
        for (int t = 0; t < T; ++t) {
            x[static_cast<std::size_t>(c * T + t)] = (c == 0) ? static_cast<float>(t) : 100.0f + static_cast<float>(t);
        }
    }
    std::array<float, C * T> y{};
    Executor<kGain, scalar_backend> ex;
    ex.input("x") = x.data();
    ex.output("y") = y.data();
    ex.run();
    for (int e = 0; e < C * T; ++e) {
        std::printf("%.9e\n", static_cast<double>(y[e]));
    }
}

} // namespace

int main() {
    run_and_print(); // reference for the here side's direct gain
    run_and_print(); // reference for the here side's transpose-round-trip gain
    return 0;
}
