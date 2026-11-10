// The tap delay line, written in THIS tree's language, run on `Executor`, and its samples printed.
//
// Milestone M4.5's delay-line family. The companion `dline_there.cpp` builds the same graph on the
// old tree at c4012b8; a test runs both and compares, and the comparison names the cause of any
// difference rather than only its size.
//
// Built by hand until a delay that changes had a spelling (Plan/28-dsl-proposals/05 D4): its distance
// is a signal in samples, so the rate is 1 kHz, where a millisecond is one sample, and
// `x[n - position.at_most(8.0) * ms]` reads at the position times one, exactly, a line of 8 samples. It
// runs on `Executor` at High, its numbers the walk's bit for bit (`executed`), over a copy of the input
// that keeps its past (`rewrite/copied_inputs.hpp`).
#include <array>
#include <cstddef>
#include <cstdio>

#include "heinzel/frontend/declarations.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

constexpr std::size_t kBlock = 64;
constexpr Index n{1'000};

// The furthest back the line may be asked to read: 8 samples. Chosen well above every position this
// graph produces, so that what is compared is the gather and the blend rather than the clamp — the
// clamp has its own checks in `tests/unit/timing_test.cpp`, and folding two questions into one test
// is how a failure stops naming its cause.
constexpr double kMostMs = 8.0;

// THE POSITION IS EXACT ON BOTH SIDES, AND THAT IS THE POINT OF SPELLING IT THIS WAY.
//
// A read position is used twice: its whole part chooses which two samples are read, and its
// fraction blends them. The blend degrades gracefully — a position wrong in its last bit moves the
// answer by a last bit. The CHOICE does not: a position that lands a hair either side of an integer
// reads a different pair of samples, and the two trees then disagree by a whole signal step for a
// reason that has nothing to do with the delay line.
//
// So the position here is built out of numbers that are exact in single precision and cannot be
// perturbed by anything either tree is free to do to them. A comparison answers exactly one or
// exactly zero; a half is exact; and 3.25 plus either of them is exact. The result is 3.25 or 3.75,
// never an integer, and identical bit for bit on both sides however the arithmetic is arranged —
// which leaves the interpolation as the only thing the comparison can be measuring. A millisecond
// at 1 kHz is one sample, and the product with one is exact too.
constexpr Graph build() {
    Input x{"x", Signal{kBlock}};
    Expr negative = x < 0.0;
    Expr position = 3.25 + negative * 0.5;
    Output y{"y"};
    y = x[n - position.at_most(kMostMs) * ms];
    return y;
}

} // namespace

int main() {
    // The old tree's own delay-line oracle signal (`tests/unit/pipeline_test.cpp` at c4012b8), used
    // here for the same reason it was chosen there: it visits many magnitudes and few of its values
    // are exactly representable, so an interpolation that is arranged differently shows up in the
    // numbers instead of cancelling.
    std::array<double, kBlock> x{};
    for (std::size_t s = 0; s < kBlock; ++s) {
        x[s] = static_cast<double>(0.31f * static_cast<float>((s * 5) % 23) - 2.0f);
    }

    const testing::Outputs y = testing::executed<build>({{"x", x.data()}});
    for (const double sample : y[0]) {
        std::printf("%.9e\n", sample);
    }
    return 0;
}
