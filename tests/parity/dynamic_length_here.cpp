// The `dynamic_length` example, built in THIS tree's grown frontend (MD S4), run at two block
// lengths through ONE compiled graph, and SELF-CHECKED against a hand reference — the port whose
// parity with the old tree MD.4 asks for.
//
// The old tree's `examples/dynamic_length.cpp` makes one point: an elementwise block effect
// `y = x*gain + bias` over a stereo block whose length `N` is a RUNTIME value, fixed once per
// `prepare()` (the audio-plugin `prepareToPlay()` shape). The graph structure is compiled once at C++
// compile time; only the extent `N` is resolved later, and the SAME compiled graph serves more than
// one length by (re-)preparing between blocks. That is exactly the runtime-resolved streamed extent
// MD built: a `Length` mints the symbol at the frontend (MD S1), and `Executor`'s `prepare` validates the
// bound size against the recorded constraints (MD S2), sizes its storage for N and runs the
// N-independent program for N samples.
//
// TWO CONVENTION TRANSLATIONS make this a faithful port rather than a different graph:
//
//   * AXIS ORDER. The old tree writes the stereo block as `Shape{N, 2}` — N frames outer, 2 channels
//     inner. This tree keeps the STREAMED axis innermost, so the same value is `Shape{2, N}`: width 2
//     (channels) outer, streamed extent N innermost and symbolic. A plain convention translation, not
//     a semantics change — N is the streamed axis the analysis already accepts as a symbolic scan
//     length, and 2 is an ordinary static width.
//
//   * THE KNOBS. The old example makes `gain`/`bias` runtime scalar Params (`ex.param("gain") = ...`).
//     This tree has no runtime Param — `constant()` is compile-time, and the two blocks use DIFFERENT
//     gains — so a knob is spelled as a named INPUT fed a constant across the block, the way the
//     conditionals and tutorial-01 ports established. `gain`/`bias` are `Shape{1, N}`: streamed extent
//     N (matched to x, so the same knob is held every sample) and width 1 (broadcast over the 2
//     channels). No broadcast on the streamed axis, no new op — an ordinary width broadcast.
//
// PARITY MODEL (MD.4, in its own words). The old file is VALUE-SELF-CHECKING: it compares its output
// to a hand reference `x*gain+bias` in its own precision and exits 0 on PASS, 1 on FAIL. This program
// does the same in this tree's precision. So parity is `same PASS + same arena story`: both self-checks
// pass (both recover the block effect), and both print the same arena story — one graph compiled once,
// run at N=256 and N=64 via (re-)prepare, allocation-free. The two trees' arena-size integers are not
// compared: the arena models differ (as `compare.cmake` explains a cross-tree artifact hash cannot
// exist), so the "story" is the narrative both print, not any slot count.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

// The declared length's id (the caller owns it) and the old example's own constraints: a whole
// multiple of 8 — clean vectorization — and a representative 256 the scheduler tunes for. No minimum
// in the old example, so `min` stays 0.
constexpr std::int32_t kLengthId = kStreamLength; // the symbol a Length declares
constexpr Length kFrames{.multiple_of = 8, .min = 0, .expect = 256};

// `x` is a stereo block [2 channels, N samples], N the runtime-resolved streamed (innermost) axis.
// `gain`/`bias` are the two knobs — stream-constant, so [1, N] broadcasting over the 2 channels. The
// same `Length` declares all three inputs, so they share one length (recorded once, deduped by id).
// `y = x*gain + bias` is a computed value, so `finish` may name it, and nothing binds N, so the
// graph stays symbolic and is compiled once for both lengths.
constexpr Graph build() {
    Input x{"x", Signal{2, kFrames}, ElemType::F64};
    Input gain{"gain", Signal{1, kFrames}, ElemType::F64};
    Input bias{"bias", Signal{1, kFrames}, ElemType::F64};
    return finish(Output{"y"} = x * gain + bias);
}

static_assert(!is_static(build()), "the runtime-length graph is not static — N is still a symbol");
static_assert(build().dims.size() == 1 && build().dims[0].id == kLengthId,
              "one length shared across x, gain and bias is recorded once");

// Process one block of `Frames` stereo frames at the given knobs, self-checking against a hand
// reference exactly as the old example does — `max_err = max |y - (x*gain+bias)| < 1e-6`. x and y are
// 2 numbers per sample (sample-major `data[n*2 + channel]`); gain/bias are one number per sample, the
// same across the block. The same block also runs at `High`, checked bit for bit against the walk.
template <std::int32_t Frames>
bool process_block(Executor<build>& ex, double gain, double bias) {
    const std::size_t n = static_cast<std::size_t>(Frames);
    std::vector<double> x(n * 2);
    std::vector<double> g(n);
    std::vector<double> b(n);
    std::vector<double> y(n * 2);
    for (std::size_t i = 0; i < n * 2; ++i) {
        x[i] = 0.01 * static_cast<double>(i % 257) - 1.0; // the old example's own feed
    }
    for (std::size_t i = 0; i < n; ++i) {
        g[i] = gain;
        b[i] = bias;
    }

    // The prepare()-time gate: the bound length must satisfy the declared constraints before a run.
    ex.input("x") = x.data();
    ex.input("gain") = g.data();
    ex.input("bias") = b.data();
    ex.output("y") = y.data();
    ex.prepare(Frames);
    ex.run(Frames);
    testing::executed<build, Frames>({{"x", x.data(), 2}, {"gain", g.data()}, {"bias", b.data()}});

    double max_err = 0.0;
    for (std::size_t i = 0; i < n * 2; ++i) {
        max_err = std::fmax(max_err, std::fabs(y[i] - (x[i] * gain + bias)));
    }
    return max_err < 1e-6;
}

} // namespace

int main() {
    // Two different block lengths through the SAME compiled program: `prepare` is the supported way to
    // change a length without recompiling. The STORY line below is what the parity harness compares.
    Executor<build> ex;
    testing::held(ex);
    const bool ok256 = process_block<256>(ex, 1.5, -0.25);
    const bool ok64 = process_block<64>(ex, 0.5, 0.1);
    std::printf("STORY dynamic_length compiled-once ran-lengths 256 64\n");

    const bool ok = ok256 && ok64;
    std::printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
