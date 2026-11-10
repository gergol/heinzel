// The `oscillator` example, built on the OLD tree at c4012b8, run over a whole buffer, and its
// samples printed in the same format as `oscillator_here.cpp` (M8.3, S5). The old tree's headers are
// pinned from the object database at configure time; nothing here opens /home/user/ctg3.
//
// This is the old example's own `whole` reference: one whole-buffer run of `dsp::oscillator` with the
// sine waveform, a constant-filled inc for a fixed 440 Hz pitch and amp 0.5. The old example also
// renders it block-by-block with streaming and checks the two agree; this tree has no streaming, so
// the ported here side is the whole-buffer form and the cross-tree compare stands in for that check.
// `set_fast_math(false)` pins the arithmetic tier exactly as the other `_there` fixtures do.
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

constexpr std::size_t N = 512; // 64-sample block x 8 blocks, the old example's own length

constexpr TargetInfo kTarget{
    .logical_cores = 1,
    .l1_cache_bytes = 16 * 1024,
    .simd_width_bytes = 16,
};

constexpr float kFs = 48000.0f;
constexpr float kFreq = 440.0f;
constexpr float kAmp = 0.5f;

// The old example's build_osc: dsp::oscillator over a per-sample inc input and an amp param, with the
// sine waveform. fast-math off to pin the tier.
constexpr Graph build_osc() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input inc{g, "inc", {N}};
    Param amp{g, "amp"};
    dsp::oscillator(inc, amp, dsp::wave::sine).as_output("y");
    return g;
}

inline constexpr auto kOsc = compile<build_osc(), kTarget>();

} // namespace

int main() {
    const float kInc = kFreq / kFs; // cycles per sample
    std::array<float, N> inc{};
    std::array<float, N> y{};
    inc.fill(kInc);

    Executor<kOsc, scalar_backend> osc;
    osc.param("amp") = kAmp;
    osc.input("inc") = inc.data();
    osc.output("y") = y.data();
    osc.run();

    for (std::size_t n = 0; n < N; ++n) {
        std::printf("%.9e\n", static_cast<double>(y[n]));
    }
    return 0;
}
