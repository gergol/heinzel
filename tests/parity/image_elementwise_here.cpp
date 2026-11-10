// The `image_elementwise_2d` example, built in THIS tree's language, run on `Executor`, and its samples
// printed.
//
// The old tree's `examples/image_elementwise_2d.cpp` is an H×W image put through a smooth saturating
// nonlinearity `y = t/(1+t*t)`, `t = gain*x + bias`, with a per-column `bias{W}` broadcast over rows
// and `gain` a runtime scalar. It is whole-tensor and index-free, with NO notion of a sample stream —
// milestone MB's second batch example, the genuine 2-D spatial surface (unit-tested three ways in
// `image_surface_test.cpp`). A value with no time is a tensor: `x{H,W}`, `bias{1,W}` broadcasting over rows,
// the block a single sample. `gain` is a runtime scalar knob — here an ordinary named input fed a constant-valued
// stream, the Param-as-input surface `conditionals_here.cpp` established (Decision 20).
//
// The companion `image_elementwise_there.cpp` builds the old example's own `{H,W}` graph on the old
// tree at c4012b8 and prints the same values in the same (h,w) row-major order. The arithmetic is
// add/mul/div only on exact f32 operands, so the two would agree bit for bit — EXCEPT the old tree's
// op-eval contracts `a*b+c` into a single-rounded fused multiply-add (`fmaf`), which `-ffp-contract=off`
// does not suppress because it is explicit, not a compiler contraction; it does so twice, in `t` and in
// the denominator. This tree evaluates a separately-rounded multiply then add — it has no auto-fusion
// (op::Fma is spelled explicitly or not at all). So this output is exactly the non-contracted
// evaluation and the there output exactly the twice-contracted one, and the whole difference is those
// two intermediate-product roundings: one f32 ULP at the output's magnitude on 22 of 96 pixels, which
// the parity test's ALLOWED accounts for and which goes to zero the day this tree contracts a*b+c too.
#include <array>
#include <cstddef>
#include <cstdio>

#include "heinzel/frontend/graph_builder.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

// The image has no time: H and W are the element axes.
constexpr std::int32_t H = 8;
constexpr std::int32_t W = 12;
constexpr std::int32_t kPixels = H * W;

// x(h,w) and the per-column bias, carried through float so their bits match the old tree's f32 inputs.
constexpr double x_at(std::int32_t h, std::int32_t w) {
    return static_cast<double>(0.07f * static_cast<float>(h * W + w) - 2.0f);
}
constexpr double bias_at(std::int32_t w) { return static_cast<double>(0.1f * static_cast<float>(w) - 0.5f); }
constexpr double kGain = static_cast<double>(1.3f);

// y = t / (1 + t*t), t = gain*x + bias. `x{H,W}`, `bias{1,W}` (broadcasts over rows), `gain{}` a
// per-sample scalar broadcasting over the whole value. Built in the frontend — elementwise, no new op.
constexpr Graph build() {
    Input x{"x", {H, W}};
    Input bias{"bias", {1, W}};
    Input gain{"gain", {}};
    Expr t = gain * x + bias;
    return finish(Output{"y"} = t / (1.0 + t * t));
}

} // namespace

int main() {
    // One sample, so a feed is just the element values.
    std::array<double, kPixels> x_buf{};
    std::array<double, W> bias_buf{};
    std::array<double, 1> gain_buf{kGain};
    for (std::int32_t h = 0; h < H; ++h) {
        for (std::int32_t w = 0; w < W; ++w) {
            x_buf[static_cast<std::size_t>(h * W + w)] = x_at(h, w);
        }
    }
    for (std::int32_t w = 0; w < W; ++w) {
        bias_buf[static_cast<std::size_t>(w)] = bias_at(w);
    }

    const testing::Outputs y = testing::executed<build>(
        {{"x", x_buf.data(), kPixels}, {"bias", bias_buf.data(), W}, {"gain", gain_buf.data()}});

    // Print y over (h, w) in row-major order — the same order image_elementwise_there prints its flat
    // buffer. Pixel (h,w) is element h*W+w.
    for (const double v : y[0]) {
        std::printf("%.9e\n", v);
    }
    return 0;
}
