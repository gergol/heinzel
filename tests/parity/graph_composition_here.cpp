// The `graph_composition` example, built in THIS tree's language, run on `Executor`, and its samples printed.
//
// The old `examples/graph_composition.cpp` composes reusable subgraph fragments into a channel strip:
// in-gain -> two one-pole low-passes in series -> a tanh saturator -> out-gain. Its point is that a
// fragment is an ordinary helper (builder + input handle -> output handle) and that `instantiate`
// namespaces a fragment's parameters so the SAME fragment reused twice keeps its knobs distinct
// (lpf.p1.a vs lpf.p2.a).
//
// Here a fragment is a `constexpr` function over `Expr`: `subgraph("name", fragment)` makes a piece of it,
// and applied, `piece(x)` prefixes the inputs it declares, composing exactly the same dotted parameter
// names (in_gain.gain, lpf.p1.a, lpf.p2.a, sat.drive, out_gain.gain). Each knob is an ordinary named
// input fed a constant-valued stream (Decision 20); the two one-pole recurrences are `Variable`s, the
// same machinery the biquad parity uses, here composed twice in series. The only new frontend spelling
// is `tanh`, which names the already-checked M4 op.
//
// The companion `graph_composition_there.cpp` builds the same strip on the old tree at c4012b8 and
// prints the same samples. Two differences are accounted for (tests/CMakeLists.txt, BECAUSE): the old
// tree fuses each one-pole's multiply-add, and its tanh is libm's.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/frontend/subgraph.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

constexpr std::size_t kBlock = 32;
constexpr Index n{};

// The five knob values, in the order the old example sets them — its f32 literals, so both sides are
// fed the same numbers (0.3, 0.8 and 0.7 are not f32 values; the run does not round a feed).
constexpr double kInGain = 1.5f;
constexpr double kA1 = 0.3f;
constexpr double kA2 = 0.5f;
constexpr double kDrive = 0.8f;
constexpr double kOutGain = 0.7f;

// ---- the reusable fragments — ordinary constexpr functions over Expr -----------------------------

// A gain stage: multiply by a runtime scalar knob (a named input its subgraph prefixes).
constexpr Expr gain_stage(Expr x) {
    Input gain{"gain", Signal{kBlock}};
    return x * gain;
}

// A one-pole low-pass (a recurrence): y[n] = (1-a)*x[n] + a*y[n-1].
constexpr Expr onepole_stage(Expr x) {
    Input a{"a", Signal{kBlock}};
    Variable y;
    y[n] = (1.0 - a) * x + a * y[n - 1];
    return Expr(y);
}

// Two one-pole sections in series; the same fragment applied as "p1" and as "p2".
constexpr Expr twopole_stage(Expr x) {
    auto p1 = subgraph("p1", onepole_stage);
    auto p2 = subgraph("p2", onepole_stage);
    Expr first = p1(x);
    return p2(first);
}

// A tanh soft-clip saturator with a drive knob.
constexpr Expr saturate_stage(Expr x) {
    Input drive{"drive", Signal{kBlock}};
    return tanh(x * drive);
}

// The composed graph: in_gain -> twopole(p1,p2) -> sat -> out_gain. `gain_stage` reused twice.
constexpr Graph build_strip() {
    auto in_gain = subgraph("in_gain", gain_stage);
    auto lpf = subgraph("lpf", twopole_stage);
    auto sat = subgraph("sat", saturate_stage);
    auto out_gain = subgraph("out_gain", gain_stage);
    Input x{"x", Signal{kBlock}};
    Expr s = in_gain(x);
    s = lpf(s);
    s = sat(s);
    Output y{"y"};
    y = out_gain(s);
    return y;
}

} // namespace

int main() {
    // The old example's own signal, through float so its bits match the old tree's f32 input.
    std::array<double, kBlock> x{};
    for (std::size_t n = 0; n < kBlock; ++n) {
        x[n] = static_cast<double>(std::sin(0.4f * static_cast<float>(n)));
    }

    // Each knob is a constant-valued stream, at the value the old example assigns it; x is the signal.
    std::array<double, kBlock> in_gain{};
    std::array<double, kBlock> a1{};
    std::array<double, kBlock> a2{};
    std::array<double, kBlock> drive{};
    std::array<double, kBlock> out_gain{};
    in_gain.fill(kInGain);
    a1.fill(kA1);
    a2.fill(kA2);
    drive.fill(kDrive);
    out_gain.fill(kOutGain);

    const testing::Outputs y = testing::executed<build_strip>({{"x", x.data()},
                                                              {"in_gain.gain", in_gain.data()},
                                                              {"lpf.p1.a", a1.data()},
                                                              {"lpf.p2.a", a2.data()},
                                                              {"sat.drive", drive.data()},
                                                              {"out_gain.gain", out_gain.data()}});
    for (std::size_t n = 0; n < kBlock; ++n) {
        std::printf("%.9e\n", y[0][n]);
    }
    return 0;
}
