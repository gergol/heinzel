// The apps benchmark's `cascade4` family, built in THIS tree's language, run on `Executor`, and printed.
//
// `benchmarks/apps_bench.cpp`'s `g_cascade4` is a four-section biquad cascade: four chained
// second-order recurrences where each section reads the CURRENT sample of the one before it and its
// own two past outputs. Only the final section `y4` escapes as an output; `y1`, `y2`, `y3` are
// internal recurrence states (`variable()` on the old tree, `Variable` here). The distinct S6 content
// is a CHAINED recurrence scan — one section's output feeds the next within the same sample step —
// which `biquad`/`dline` (a single recurrence) do not exercise.
//
// The companion `cascade4_there.cpp` builds the same cascade on the old tree at c4012b8 and prints
// the same thing; a test runs both and compares with a named tolerance and its cause.
//
// THE NUMERIC DIFFERENCE. Every coefficient is the old bench's f32 literal carried through `c()` so
// its bits equal the there side's f32 constant; the accumulation is written in the same left-to-right
// order on both sides. So the only accounted difference is that this tree computes the recurrences in
// f64 while the old tree's scalar executor runs them in f32, and that per-sample rounding COMPOUNDS
// through four chained feedback loops over the block — a bounded, deterministic gap (fast-math off on
// both sides), measured and named in the test, not a fitted margin.
#include <array>
#include <cstddef>
#include <cstdio>

#include "heinzel/frontend/graph_builder.hpp"

#include "executed.hpp"

using namespace heinzel;
constexpr Index n{};

namespace {

constexpr std::size_t kBlock = 64; // a correctness corpus, not the bench's 1<<20 sweep

// A coefficient as the old bench's f32 literal widened to double, so the constant's bits equal the
// there side's f32 value — the two sides multiply by the same number, not merely a close one.
constexpr double c(float v) { return static_cast<double>(v); }

// g_cascade4: y1 ← x, y2 ← y1, y3 ← y2, y4 ← y3, each a second-order feedback section.
constexpr Graph build() {
    Input x{"x", Signal{kBlock}};
    Expr x1 = x[n - 1]; // x[n-1]

    Variable y1;
    Variable y2;
    Variable y3;
    Variable y4;
    y1[n] = c(0.3f) * x + c(0.2f) * x1 - c(0.4f) * y1[n - 1] + c(0.1f) * y1[n - 2];
    y2[n] = c(0.5f) * y1 + c(0.1f) * y1[n - 1] + c(0.3f) * y2[n - 1] - c(0.2f) * y2[n - 2];
    y3[n] = c(0.4f) * y2 + c(0.2f) * y2[n - 1] - c(0.1f) * y3[n - 1] + c(0.15f) * y3[n - 2];
    y4[n] = c(0.6f) * y3 + c(0.1f) * y3[n - 1] + c(0.2f) * y4[n - 1] - c(0.1f) * y4[n - 2];
    return finish(Output{"y4"} = y4);
}

} // namespace

int main() {
    // A varied signal (the dline oracle's shape): visits many magnitudes and few of its values are
    // exactly representable, so a compounded rounding shows up in the numbers instead of cancelling.
    std::array<double, kBlock> x{};
    for (std::size_t n = 0; n < kBlock; ++n) {
        x[n] = static_cast<double>(0.31f * static_cast<float>((n * 5) % 23) - 2.0f);
    }

    const testing::Outputs y4 = testing::executed<build>({{"x", x.data()}});
    for (std::size_t n = 0; n < kBlock; ++n) {
        std::printf("%.9e\n", y4[0][n]);
    }
    return 0;
}
