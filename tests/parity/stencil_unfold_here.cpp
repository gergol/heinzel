// The `stencil_indexing_2d` example's UNFOLD idiom, built in THIS tree's language, run on `Executor`, and
// printed.
//
// The old tree's `examples/stencil_indexing_2d.cpp` shows two idioms for a 2-D stencil. The one MB grants
// is idiom 2 — the windowed VIEW: `sum(unfold(x, axis 0, K=3, S=1) * taps, tap_axis)`, a vertical 1-D
// Laplacian (taps {1,-2,1}) over the interior rows of an H×W image. It is the 2-D element-axis surface
// (MB.S3) plus the existing `_unfold` (M5, closed), no new op. The image is a tensor, `x{H,W}`, recorded
// with a length-1 stream, `x{H,W,1}`, as the shapes below are. `_unfold` on the ROW axis (axis 0, an
// outer axis) turns extent H into (H-2) frames and appends a window axis of 3 at axis 1 — `{H-2,3,W,1}` —
// then a per-tap weight and a SUM over the tap axis is the stencil, dropping that axis to `{H-2,W,1}`.
//
// Idiom 1 (the per-axis subscript `x[i-1,j] + x[i,j-1] - 2*x[i,j]`) is NOT ported and is recorded reserved
// in docs/decisions.md: it needs a multi-axis subscript frontend surface this tree does not have, and each
// lagged tap is a SHIFT of an ELEMENT (non-streamed) axis — which brushes the reserved innermost-axis
// `_delay` question the operator kept separate. So only the unfold idiom is ported here.
//
// THE NUMERIC DIFFERENCE, measured not fitted. The stencil is a weighted SUM over the 3 taps, and the two
// trees round that sum differently (old: ascending-tap `fma_rn` in f32; here: the `_reduce` of the
// products). So any gap is a reduction-order rounding difference; the parity ALLOWED is its measured max.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "heinzel/frontend/graph_builder.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t H = 8;
constexpr std::int32_t W = 12;
constexpr std::int32_t K = 3;
constexpr std::int32_t kFrames = H - K + 1; // (H-window)/hop + 1 = H-2 interior rows
constexpr std::int32_t kIn = H * W;
constexpr std::int32_t kOut = kFrames * W;

// x(h,w) through float so its bits match the old tree's f32 input: 0.13f*(p%23) - 1, p the flat index.
constexpr double x_value(std::int32_t h, std::int32_t w) {
    const std::int32_t p = h * W + w;
    return static_cast<double>(0.13f * static_cast<float>(p % 23) - 1.0f);
}

// y[f,w] = sum_tap x[f+tap, w] * k[tap], k = {1,-2,1}. unfold the row axis into (frame, tap), weight by
// taps{1,3,1,1} (broadcast over frame, w and the stream), sum the tap axis away.
constexpr Graph build() {
    Input x{"x", {H, W}};
    Input taps{"taps", {1, K, 1}};
    Expr windowed = x.unfold(/*axis*/ 0, /*window*/ K, /*hop*/ 1); // {H-2, K, W, 1}
    return finish(Output{"y"} = (windowed * taps).sum(1));
}

} // namespace

int main() {
    std::array<double, kIn> x_buf{};
    for (std::int32_t h = 0; h < H; ++h) {
        for (std::int32_t w = 0; w < W; ++w) {
            x_buf[static_cast<std::size_t>(h * W + w)] = x_value(h, w);
        }
    }
    std::array<double, K> taps_buf{static_cast<double>(1.0f), static_cast<double>(-2.0f),
                                   static_cast<double>(1.0f)};

    const testing::Outputs y = testing::executed<build>({{"x", x_buf.data(), kIn}, {"taps", taps_buf.data(), K}});

    // y over (frame, w) row-major — element f*W + w. The same order stencil_unfold_there
    // prints its interior-rows buffer.
    for (std::int32_t e = 0; e < kOut; ++e) {
        std::printf("%.9e\n", y[0][static_cast<std::size_t>(e)]);
    }
    return 0;
}
