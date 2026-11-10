// The channel-strip benchmark's saturating gain chain, built in THIS tree's language, run on `Executor`, and
// printed.
//
// `benchmarks/channel_strip_bench.cpp`'s `g_strip()` is a realistic saturator chain, pure elementwise:
//
//     s = kGin·x                        (input gain)
//     s = tanh(kD1·s + kB1)             (three cascaded tanh drive stages, each with a
//     s = tanh(kD2·s + kB2)              pre-gain "drive" and a DC "bias")
//     s = tanh(kD3·s + kB3)
//     s = kGout·s                       (output gain)
//     y = min(max(s, -1), 1)            (hard clamp to [-1, 1])
//
// As an S6 CORRECTNESS corpus its numeric content is exactly that chain: three tanh saturations with
// gain staging and a clamp. The transcendental is the crux of the bench (it is what a hand/library
// chain calls scalar std::tanh for, and what the old tree vectorizes); here it is the M4 `op::Tanh`,
// the same core `graph_composition`'s single saturator uses — this port compounds it over THREE stages.
// No new op: the gains and biases are `op::Mul`/`op::Add`, the clamp is `op::Min`/`op::Max` (all M4).
//
// The strip is pure pointwise, so x carries no time of its own: the length-N signal is laid along the
// block axis and run in one pass, like `conditionals`. The strip's constants are written here as the
// old tree's f32 literals cast to double, so every gain and bias is bit-identical to the there side. Two
// differences are accounted for: the old tree fuses each saturator's d*s+b, and its tanh is libm's.
// See the CMake `BECAUSE`.
//
// The input is a fast sine sweep 0.6·sin(0.09·i): over the 64-sample corpus it spans nearly a full
// period, both signs, so kGin pushes it well into the tanh saturation on both rails — the bench's own
// 0.01 tone would only creep across the near-linear part of the curve in so few samples, making the
// tanh-core comparison vacuous. The GRAPH is `g_strip()` verbatim; only the test signal is chosen to
// exercise the saturator densely in a small corpus.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>

#include "heinzel/frontend/graph_builder.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

constexpr std::size_t kBlock = 64; // the strip has no time of its own; the signal is one block

// The strip coefficients, the old bench's f32 literals cast to double so the arithmetic constants match
// the there side bit-for-bit.
constexpr double kGin = static_cast<double>(2.0f);
constexpr double kGout = static_cast<double>(0.7f);
constexpr double kD1 = static_cast<double>(1.5f);
constexpr double kD2 = static_cast<double>(1.3f);
constexpr double kD3 = static_cast<double>(1.1f);
constexpr double kB1 = static_cast<double>(0.10f);
constexpr double kB2 = static_cast<double>(-0.05f);
constexpr double kB3 = static_cast<double>(0.0f);

// One tanh saturation stage: sat(s) = tanh(d·s + b) — the bench's `saturate`.
constexpr Expr saturate(Expr s, double d, double b) { return tanh(d * s + b); }

// The strip: in-gain -> three tanh drive stages -> out-gain -> hard clamp. `g_strip()` verbatim.
constexpr Graph build() {
    Input x{"x", Signal{kBlock}};
    Expr s = kGin * x;
    s = saturate(s, kD1, kB1);
    s = saturate(s, kD2, kB2);
    s = saturate(s, kD3, kB3);
    s = kGout * s;
    s = max(min(s, 1.0), -1.0); // hard clamp to [-1, 1], the bench's `max(min(s,1),-1)` spelling
    return finish(Output{"y"} = s);
}

} // namespace

int main() {
    // A fast sine sweep, through float so its bits match the old tree's f32 input.
    std::array<double, kBlock> x{};
    for (std::size_t n = 0; n < kBlock; ++n) {
        x[n] = static_cast<double>(0.6f * std::sin(0.09f * static_cast<float>(n)));
    }

    const testing::Outputs y = testing::executed<build>({{"x", x.data()}});
    for (std::size_t n = 0; n < kBlock; ++n) {
        std::printf("%.9e\n", y[0][n]);
    }
    return 0;
}
