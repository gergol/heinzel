// The oscillator benchmark's AFFINE (loop-invariant pitch) arm, built in THIS tree's language, run on
// `Executor`, and printed (M8.3, S6). The companion `oscillator_affine_there.cpp` builds the same affine
// oscillator on the old tree at c4012b8 and prints the same thing; a test runs both and compares.
//
// The existing `oscillator` parity (S5) already covers the FM / RECURRENCE arm — phase[n] =
// frac(phase[n-1] + inc) via `dsp::oscillator`, the serial phase integration. This is the OTHER arm
// the bench contrasts: when `inc` is a loop-invariant scalar, the phase is not a recurrence but
// frac(n·inc), a pure elementwise ramp the executor can walk SIMD-wide. It is a genuinely distinct
// numeric mechanism, not a re-spelling: the recurrence ACCUMULATES inc across n additions (n roundings)
// while the affine path forms n·inc with a single multiply — so the two phases, equal in exact
// arithmetic, round differently, which is exactly why the bench keeps both.
//
// The oscillator is `dsp::affine_oscillator`, added to the ported dsp layer alongside this test:
// `sinpi(2·frac(iota·inc))·amp`, composing the `iota`, `frac` and `sinpi` (M4) spellings — no new op.
// The N-sample buffer is a tensor of N elements, `iota({N})` (recorded {N,1}: the stream of one sample
// this tree keeps innermost); the graph is a generator (iota is arity-0), run for one stream step,
// printing the N phase samples in order — the same values, same order as the there side's {N}.
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "heinzel/dsp/oscillator.hpp"
#include "heinzel/frontend/graph_builder.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

// A correctness corpus, not the bench's 1<<22-sample sweep. iota is element-indexed
// (in the walk), so a materialized buffer of N samples costs Θ(N) planes; N=96 fits kMaxPlanes=768 (672 planes)
// with headroom (the SHAPE of the cost is linear — amendment 03 — so the corpus is sized to fit, no
// ceiling raised). The corpus pitch is higher than the bench's 440 Hz so that `frac` genuinely WRAPS
// several times within the 96-sample window (at 440 Hz one period is ~109 samples, barely one wrap);
// 1234 Hz gives ~2.5 wraps and a non-power-of-two `inc` so n·inc also carries real f32-vs-f64 rounding,
// not only the sine core.
constexpr std::size_t N = 96;

constexpr float kFs = 48000.0f;
constexpr float kFreq = 1234.0f;
constexpr float kAmp = 0.5f;

// The per-sample increment in cycles, computed in float and widened so its bits equal the there side's
// f32 `kFreq / kFs` — the two sides multiply the ramp by the same number, not merely a close one.
constexpr float kIncF = kFreq / kFs;

constexpr Graph build() {
    Output y{"y"};
    y = dsp::affine_oscillator(Shape{static_cast<std::int32_t>(N)}, constant(static_cast<double>(kIncF)),
                               constant(static_cast<double>(kAmp)));
    return y;
}

} // namespace

int main() {
    const testing::Outputs y = testing::executed<build>(); // a generator: iota has no inputs
    for (const double v : y[0]) {
        std::printf("%.9e\n", v);
    }
    return 0;
}
