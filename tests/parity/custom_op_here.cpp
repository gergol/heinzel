// The `custom_op` example, built in THIS tree's language, run on `Executor`, and its samples printed.
//
// The old tree's `examples/custom_op.cpp` makes one point: Heinzel has NO custom-op registration
// seam — you extend the vocabulary by writing an ordinary `constexpr` helper that composes built-in
// ops. That point is TRUE OF THIS TREE BY CONSTRUCTION, and more directly than in the old one: a leaf
// allocates its own graph (frontend/graph_builder.hpp), so `log10` below is just a function over
// `Expr`, fused into whatever consumes it exactly as a built-in would be. The only frontend growth
// this example needs is the `log` spelling, which names the already-checked M4 code `op::Log` and adds
// no operation (operator decision 2026-09-04 11:58, Option A).
//
// The companion `custom_op_there.cpp` composes the SAME helper on the old tree at c4012b8 and prints
// the same format. A test runs both and compares. The comparison is numeric, not a comparison of
// compiled artifacts (the two trees' IR types differ deliberately). Both sides spell
// `log(x) * (1/ln10) * 20`, in that order, with the same f32 constants; the pair measures bit-identical
// (tests/CMakeLists.txt, BECAUSE), the old side's log being libm's.
#include <array>
#include <cstddef>
#include <cstdio>

#include "heinzel/frontend/graph_builder.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

constexpr std::size_t kBlock = 64;

// The example's thesis, verbatim: a new "operation" is a composed helper, not a registered op.
// log10(x) = log(x) * (1/ln 10). `log` is found by ADL on Expr. Kept in its own namespace to mirror
// the old example's `myops::log10` and `custom_op_there.cpp`.
namespace myops {
constexpr Expr log10(Expr x) { return log(x) * 0.43429448190325176f; } // the old side's f32 literal
} // namespace myops

// amplitude -> decibels: y = 20 * log10(x), the old example's graph. An amplitude is above 0, which `log` needs.
constexpr Graph build() {
    Input x{"x", Signal{kBlock}, gt(0)};
    return finish(Output{"y"} = myops::log10(x) * 20.0);
}

} // namespace

int main() {
    // A strictly positive signal so `log` is in domain, computed through float so its bits match the
    // old tree's f32 input exactly (the reduce/delay parity do the same). It visits many magnitudes
    // and few of its values are exactly representable, which is what makes a transcendental core
    // difference show up in the numbers rather than cancel.
    std::array<double, kBlock> x{};
    for (std::size_t n = 0; n < kBlock; ++n) {
        x[n] = static_cast<double>(0.31f * static_cast<float>((n * 5) % 23) + 0.5f);
    }

    const testing::Outputs y = testing::executed<build>({{"x", x.data()}});
    for (std::size_t n = 0; n < kBlock; ++n) {
        std::printf("%.9e\n", y[0][n]);
    }
    return 0;
}
