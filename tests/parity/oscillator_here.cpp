// The `oscillator` example, built in THIS tree's language, run on `Executor` over a whole buffer, and its
// samples printed (M8.3, S5). Compared against `oscillator_there.cpp`, the old tree's own oscillator.
//
// The old example (examples/oscillator.cpp) makes one point: an audio oscillator is a RECURRENCE —
// phase[n] = frac(phase[n-1] + inc) — with a waveform applied to the wrapped phase, so one graph
// covers fixed pitch (constant-filled inc, as here) and FM. The old example renders it two ways,
// block-by-block with streaming and once over the whole buffer, and checks the two agree; this tree
// has no set_streaming/reset block driving (established at ch04), so the ported form is the
// whole-buffer run — which is exactly the old example's OWN `whole` reference — and the cross-tree
// comparison stands in for the old example's internal streamed==whole check plus its analytic check.
//
// The oscillator is `heinzel::dsp::oscillator`, the opt-in layer added alongside this example: the
// phase accumulator over the recurrence frontend, `sinpi(2*phase)` for the sine, scaled by amp. It
// composes the `frac`/`sinpi` spellings (oscillator c1) — no new op. Single precision throughout, the
// old executor's own precision, so the two trees differ only by their transcendental cores, not by a
// storage-width mismatch (the parity registration names that as the allowance's cause).
#include <array>
#include <cstddef>
#include <cstdio>

#include "heinzel/dsp/oscillator.hpp"
#include "heinzel/frontend/graph_builder.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

constexpr std::size_t N = 512; // the old example's own length (64-sample block x 8 blocks)

constexpr float kFs = 48000.0f;
constexpr float kFreq = 440.0f;
constexpr float kAmp = 0.5f;

// The oscillator graph: inc is a per-sample input (constant-filled here for a fixed pitch), amp a
// literal. The old example's `amp` is a Param fixed at 0.5 and never varied, so a constant is the
// same graph.
constexpr Graph build_osc() {
    Input inc{"inc", Signal{N}};
    Expr y = dsp::oscillator(inc, constant(static_cast<double>(kAmp)));
    return finish(Output{"y"} = y);
}

} // namespace

int main() {
    // A fixed pitch: inc filled with the per-sample increment in cycles. Computed in float and widened
    // so its value equals the old side's float `kFreq / kFs` exactly (the walk takes a double block).
    const float kInc = kFreq / kFs;
    std::array<double, N> inc{};
    inc.fill(static_cast<double>(kInc));

    const testing::Outputs y = testing::executed<build_osc>({{"inc", inc.data()}});
    for (std::size_t n = 0; n < N; ++n) {
        std::printf("%.9e\n", y[0][n]);
    }
    return 0;
}
