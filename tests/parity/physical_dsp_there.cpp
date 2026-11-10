// The `physical_dsp` example, built on the OLD tree at c4012b8, run, and its samples printed in the
// same format as `physical_dsp_here.cpp` (M8.3, S5). The old tree's headers are pinned from the
// object database at configure time; nothing here opens /home/user/ctg3.
//
// This is the old example's own comb, verbatim: a feedback comb whose delay is written as a duration
// and folded to an integer sample count by the old `units/` layer — `g.set_sample_rate` fixes the
// rate, `g.samples(3*ms)` folds 3 ms at 2 kHz to 6 samples at compile time. `set_fast_math(false)`
// pins the arithmetic tier exactly as `biquad_there.cpp` and `tutorial_04_there.cpp` do. The impulse
// response is 0.5^k at n = 6k — exact powers of two, so the f32 executor here and the f64 walk on
// the here side land on the same numbers bit for bit (ALLOWED 0.0).
#include <array>
#include <cstddef>
#include <cstdio>

#include "heinzel/backends/scalar_backend.hpp"
#include "heinzel/frontend/builder.hpp"
#include "heinzel/runtime/compile.hpp"
#include "heinzel/runtime/executor.hpp"
#include "heinzel/units/dimension.hpp"

using namespace heinzel;

namespace {

constexpr std::size_t N = 64; // the old example's own length

constexpr TargetInfo kTarget{
    .logical_cores = 1,
    .l1_cache_bytes = 16 * 1024,
    .simd_width_bytes = 16,
};

// The old example's build_comb: the delay is a physical duration, folded to 6 samples at 2 kHz by
// `g.samples`, with the old layer's dimensional check.
constexpr Graph build_comb() {
    GraphBuilder g;
    g.set_fast_math(false);
    g.set_sample_rate(2000.0 * units::hertz);
    Input x{g, "x", {N}};
    Output y{g, "y", {N}};
    Index n;
    y[n] = x[n] + 0.5f * y[n - g.samples(3.0 * units::ms)];
    return g;
}

inline constexpr auto kComb = compile<build_comb(), kTarget>();

} // namespace

int main() {
    std::array<float, N> x{};
    std::array<float, N> y{};
    x[0] = 1.0f; // an impulse

    Executor<kComb, scalar_backend> comb;
    comb.input("x") = x.data();
    comb.output("y") = y.data();
    comb.run();

    for (std::size_t n = 0; n < N; ++n) {
        std::printf("%.9e\n", static_cast<double>(y[n]));
    }
    return 0;
}
