// The `conditionals` example, built in THIS tree's language, run on `Executor`, and its samples printed.
//
// The old `examples/conditionals.cpp` shows the two disciplined ways to spell a conditional without a
// data-dependent branch in the schedule: (1) a per-lane predicate-and-select — a noise gate
// `where(|x| > thresh, x, 0)` — and (2) a whole-subgraph choice made at the call boundary by an
// ordinary host `if`, between a soft limiter `x/(1+|x|)` and a hard limiter `min(max(x,-1),1)`. All
// three are pure elementwise graphs; this program builds each and runs it.
//
// Two things this port establishes for the rest of M8.3:
//   * a runtime scalar "Param" (the gate's `thresh`) is, in this tree, an ORDINARY named input fed a
//     constant-valued stream on the MW multi-input surface — no new op, no new value category, and
//     not the reserved batch/broadcast-over-time axis model (the graph never sees it as anything but
//     one number per sample). It is fed beside `x`.
//   * the exact choose/compare/clamp spellings (`max`, `min`, `abs`, `>`, `where`) each name an M4
//     code that already exists, so the whole example lowers to already-checked ops.
//
// The companion `conditionals_there.cpp` builds the same three graphs on the old tree at c4012b8 and
// prints the same three sections in the same order. The comparison is numeric.
#include <array>
#include <cstddef>
#include <cstdio>

#include "heinzel/frontend/graph_builder.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

constexpr std::size_t kBlock = 64;
constexpr double kThresh = 0.5; // the gate's runtime knob, held at one value for the comparison

// (1) per-lane gate: y = where(|x| > thresh, x, 0). `thresh` is a named input, not a literal — the
//     point (1) makes is that it is a runtime knob.
constexpr Graph build_gate() {
    Input x{"x", Signal{kBlock}};
    Input thresh{"thresh", Signal{kBlock}};
    return finish(Output{"y"} = where(abs(x) > thresh, x, 0.0));
}

// (2a) soft limiter: y = x / (1 + |x|).
constexpr Graph build_soft() {
    Input x{"x", Signal{kBlock}};
    return finish(Output{"y"} = x / (1.0 + abs(x)));
}

// (2b) hard limiter: y = clip(x, -1, 1) = min(max(x, -1), 1).
constexpr Graph build_hard() {
    Input x{"x", Signal{kBlock}};
    return finish(Output{"y"} = min(max(x, -1.0), 1.0));
}

void print_block(const testing::Outputs& y) {
    for (std::size_t n = 0; n < kBlock; ++n) {
        std::printf("%.9e\n", y[0][n]);
    }
}

} // namespace

int main() {
    // The old example's own signal, through float so its bits match the old tree's f32 input. It sweeps
    // from -2.5 to just under 2.5, so it crosses the gate threshold and runs off both ends of the
    // limiters' [-1, 1] range.
    std::array<double, kBlock> x{};
    for (std::size_t n = 0; n < kBlock; ++n) {
        x[n] = static_cast<double>(0.08f * static_cast<float>(n) - 2.5f);
    }

    // The gate takes two inputs: `x` streamed, `thresh` fed the same value every sample (a knob).
    std::array<double, kBlock> thresh{};
    thresh.fill(kThresh);
    print_block(testing::executed<build_gate>({{"x", x.data()}, {"thresh", thresh.data()}}));
    print_block(testing::executed<build_soft>({{"x", x.data()}}));
    print_block(testing::executed<build_hard>({{"x", x.data()}}));
    return 0;
}
