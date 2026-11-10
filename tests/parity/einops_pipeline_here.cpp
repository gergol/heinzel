// The `einops_pipeline` example, built in THIS tree's language, run on `Executor`, and its samples printed.
//
// The old tree's `examples/einops_pipeline.cpp` takes a packed spectrogram-like buffer `[B, C*F, T]`,
// SPLITS the flattened channel/frequency axis back apart with `rearrange("b (c f) t -> b c f t", x,
// {{"f", F}})`, and REDUCES over the frequency and time axes to a per-channel energy `[B, C]` with the
// einops `mean`. It is whole-tensor and index-free, with no notion of a sample stream — milestone MB's
// rearrange example (MB.S5). A value with no time of its own is a tensor, `x{B, C*F, T}`: the named axes
// are all ELEMENT axes, and the length-1 stream it is recorded with is never named. The split `(c f)` is a `_reshape` of the packed element axis into two, and the
// `mean` over f and t is a `_reduce` over those two element axes — the same nodes MB.S5a's notation
// lowers to (`rearrange_notation_test.cpp`) and M4/M5 already measured.
//
// THE NUMERIC DIFFERENCE, measured not fitted (the `image_elementwise` / `einsum_contractions`
// discipline). A `mean` is a SUM then a divide, and the two trees round that sum differently: the old
// tree's scalar backend accumulates the reduction in f32, while this tree's `_reduce` sums in double and
// rounds once. So any gap is a reduction-order rounding difference, not a disagreement about the
// operation; the parity test's ALLOWED is the measured maximum of it. Inputs are carried through float so
// their bits match the old tree's f32.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/notation/rearrange.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

// The pipeline has no time of its own. The old example's own extents.
constexpr std::int32_t B = 2;
constexpr std::int32_t C = 3;
constexpr std::int32_t F = 4;
constexpr std::int32_t T = 5;
constexpr std::int32_t kIn = B * (C * F) * T;

// y = mean_{f,t} of the split of the tensor x{B, C*F, T}: rearrange "b (c f) t -> b c f t" then reduce
// "b c f t -> b c" with a mean. Output is {B, C} (recorded {B, C, 1}: a stream of one sample).
constexpr Graph build() {
    Input x{"x", {B, C * F, T}};
    Expr split = rearrange("b (c f) t -> b c f t", x, {{"f", F}});
    return finish(Output{"y"} = reduce("b c f t -> b c", split, ReduceOp::Mean));
}

} // namespace

int main() {
    // x[e] in row-major element order over (b, c*F+f, t) — the same flat order the old tree's buffer uses,
    // carried through float so the bits match.
    std::array<double, kIn> x_buf{};
    for (std::int32_t e = 0; e < kIn; ++e) {
        x_buf[static_cast<std::size_t>(e)] = static_cast<double>(0.1f * static_cast<float>(e) + 0.5f);
    }

    const testing::Outputs y = testing::executed<build>({{"x", x_buf.data(), kIn}});

    // Print y over (b, c) in row-major order — element b*C + c. The same order einops_pipeline_there
    // prints its {B,C} buffer.
    for (const double v : y[0]) {
        std::printf("%.9e\n", v);
    }
    return 0;
}
