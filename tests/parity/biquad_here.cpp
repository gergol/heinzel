// The biquad, built in THIS tree's language, run on `Executor`, and its samples printed.
//
// The companion program `biquad_there.cpp` builds the same filter on the old tree at c4012b8 and
// prints the same thing. A test runs both and compares. This is milestone M1's fourth condition,
// and it is the only check that reaches outside this tree.
//
// The comparison has to be numeric rather than a comparison of compiled artifacts. The old plan's
// gate was byte-identical artifacts, which works when both sides are values of the same type — and
// across two trees they are not, deliberately: a rule table with shape and element-type columns is
// a different thing from the one the old tree has. Asking for a hash across trees is asking for
// something that cannot exist.
#include <array>
#include <cstddef>
#include <cstdio>

#include "heinzel/frontend/graph_builder.hpp"

#include "executed.hpp"

using namespace heinzel;
constexpr Index n{};

namespace {

constexpr std::size_t kBlock = 64; // one block at 48 kHz

// Poles at radius 0.9, angle 45 degrees; unity feed-forward. The same numbers the old tree's own
// embedded example uses, so the two sides are the same filter and not merely similar ones.
constexpr float kFeedback1 = 2.0f * 0.9f * 0.70710678f; // 2 r cos(theta)
constexpr float kFeedback2 = -(0.9f * 0.9f);            // -r squared

// y[n] = x[n] + c1*y[n-1] + c2*y[n-2]
constexpr Graph build() {
    Input x{"x", Signal{kBlock}};
    Variable y;
    y[n] = x + static_cast<double>(kFeedback1) * y[n - 1] + static_cast<double>(kFeedback2) * y[n - 2];
    return finish(Output{"y"} = y);
}

} // namespace

int main() {
    std::array<double, kBlock> impulse{};
    impulse[0] = 1.0;

    const testing::Outputs y = testing::executed<build>({{"x", impulse.data()}});
    for (std::size_t n = 0; n < kBlock; ++n) {
        std::printf("%.9e\n", y[0][n]);
    }
    return 0;
}
