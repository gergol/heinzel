// The oscillator benchmark's AFFINE arm, built on the OLD tree at c4012b8, run, and its samples
// printed in the same order as `oscillator_affine_here.cpp`.
//
// The old tree's headers are extracted from the shared-object database at configure time (pinned to
// the commit `main` names) — same as the other `*_there.cpp`; nothing here opens /home/user/ctg3.
//
// This is `benchmarks/oscillator_bench.cpp`'s `g_affine_sine` graph: `dsp::affine_oscillator(g, {N},
// inc, amp, wave::sine)` = `wave::sine(frac(iota(N)·inc))·amp`. The bench's `inc` is a scalar `param`;
// here it is a `constant` of the same value so both sides are input-free generators of the same N
// samples. `set_fast_math(false)` pins the deterministic tier, so any difference is the affine path's
// f32 (this executor) vs f64 (the here walk) rounding plus the two trees' sinpi cores — not
// reassociation.
#include <array>
#include <cstddef>
#include <cstdio>

#include "heinzel/backends/scalar_backend.hpp"
#include "heinzel/dsp/oscillator.hpp"
#include "heinzel/frontend/builder.hpp"
#include "heinzel/runtime/compile.hpp"
#include "heinzel/runtime/executor.hpp"

using namespace heinzel;

namespace {

constexpr std::size_t N = 96; // matches oscillator_affine_here.cpp (sized to kMaxPlanes=768)

constexpr float kFs = 48000.0f;
constexpr float kFreq = 1234.0f; // the corpus pitch: ~2.5 frac wraps in 96 samples, non-power-of-two inc
constexpr float kAmp = 0.5f;
constexpr float kIncF = kFreq / kFs;

constexpr TargetInfo kTarget{
    .logical_cores = 1,
    .l1_cache_bytes = 32 * 1024,
    .simd_width_bytes = 32,
};

constexpr Graph build() {
    GraphBuilder g;
    g.set_fast_math(false);
    auto y = g.output("y", {static_cast<int>(N)});
    y = dsp::affine_oscillator(g, Shape{static_cast<int>(N)}, g.constant(kIncF, ElemType::F32),
                               g.constant(kAmp, ElemType::F32), dsp::wave::sine);
    return g;
}

inline constexpr auto kSched = compile<build(), kTarget>();

} // namespace

int main() {
    std::array<float, N> y{};

    Executor<kSched, scalar_backend> ex;
    ex.output("y") = y.data();
    ex.run();

    for (std::size_t n = 0; n < N; ++n) {
        std::printf("%.9e\n", static_cast<double>(y[n]));
    }
    return 0;
}
