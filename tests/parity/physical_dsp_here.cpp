// The `physical_dsp` example, built in THIS tree's language, run on `Executor`, and its samples printed.
//
// The old tree's `examples/physical_dsp.cpp` makes one point: a DSP delay written as a physical
// DURATION rather than a magic sample count, folded to an exact integer lag at COMPILE TIME using the
// graph's sample rate, so the graph is byte-identical to the hand-written lag at zero runtime cost.
// A feedback comb  y[n] = x[n] + g·y[n−D]  whose delay D is `3 ms`; at 2 kHz that is exactly 6
// samples, and a units layer folds it.
//
// In this tree the fold is `heinzel::units::to_samples`, over the named durations added alongside this
// example (M8.3, S5, physical_dsp c1). It runs at compile time and yields the same integer 6,
// which feeds the already-closed fixed-delay spelling `y[n - D]` — so the comb here is the exact
// recurrence the old example builds, and nothing new-semantic was added to reach it. The companion
// `physical_dsp_there.cpp` builds the old example's own comb on the old tree at c4012b8 (using the
// old `units/` layer and `g.samples`), runs it, and prints; the two are output-compared.
//
// The impulse response of  y[n] = x[n] + 0.5·y[n−6]  is 0.5^k at n = 6k and zero elsewhere. Every
// value is an exact power of two, representable identically in this tree's f64 walk and the old
// tree's f32 executor, so the two runs agree BIT FOR BIT — the test's ALLOWED is 0.0 and no cause is
// owed (see the registration's note).
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/units/named_units.hpp"

#include "executed.hpp"

using namespace heinzel;
constexpr Index n{};

namespace {

constexpr std::size_t N = 64; // the old example's own length

// The delay, written physically and folded at compile time. 3 ms at 2 kHz is exactly 6 samples —
// the whole point of the example. `to_samples` lands on an integer sample count; a sub-sample duration
// would stop the build (named_units_test.cpp).
constexpr double kSampleRate = 2000.0;
constexpr std::int32_t kDelay = units::to_samples(3.0 * units::ms, kSampleRate);
static_assert(kDelay == 6, "3 ms at 2 kHz must fold to 6 samples");

// The feedback comb, written on its output: y reads its own value 6 samples ago. `y[n - kDelay]`
// is the already-closed fixed-delay op with a constant depth — the fold produced that constant.
constexpr Graph build_comb() {
    Input x{"x", Signal{N}};
    Variable y;
    y[n] = x + 0.5 * y[n - kDelay];
    return finish(Output{"y"} = y);
}

} // namespace

int main() {
    // An impulse: the comb's response is the printed signal the comparison reads.
    std::array<double, N> x{};
    x[0] = 1.0;

    const testing::Outputs y = testing::executed<build_comb>({{"x", x.data()}});
    for (std::size_t n = 0; n < N; ++n) {
        std::printf("%.9e\n", y[0][n]);
    }
    return 0;
}
