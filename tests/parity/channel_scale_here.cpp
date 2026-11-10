// The `channel_scale_3d` example, built in THIS tree's language, run on `Executor`, and its samples printed.
//
// The old tree's `examples/channel_scale_3d.cpp` makes one point: an N-D elementwise value scaled and
// biased per channel — `y = x·scale + bias` with `x{C,H,W}`, `scale{C,1,1}`, `bias{C,1,1}` — where the
// per-channel vector BROADCASTS over the H and W axes (the canonical N-D broadcast). It is the first of
// milestone MB's nine batch examples, and it was the capability MB.S2 was written to ADD — until the
// tick's finding F-MB-BROADCAST-ALREADY-EXISTS established that operand broadcast is already built and
// tested in BOTH layers (`sema/analyze.hpp` `broadcast()`/`combine_extent`, numpy's rule; and
// `tests/reference/elements.hpp` `element_read_by`, the zero-stride-on-extent-1 read). So this is a PORT on the
// existing surface, not new capability: no batch-axis code is written.
//
// THE MAPPING, which is MB.S2's one decision. This tree keeps the streamed axis innermost, so the old
// tensor's innermost axis W becomes the streamed axis and the value is streamed W times; each sample is
// the `{C,H}` plane. `scale`/`bias` are `{C,1}` per sample — extent 1 on H so they broadcast over H, the
// same numbers every sample so they are constant across the stream (their W axis is literal-equal to
// x's). This is exactly the old tree's `{C,1,1}` broadcast with W taken as the stream: no sample-at-a-
// time re-ordering can change an elementwise result, so the numbers are the SAME numbers.
//
// The companion `channel_scale_there.cpp` builds the old example's own `{C,H,W}·{C,1,1}` graph on the
// old tree at c4012b8 and prints the same values in the same order. The two agree BIT FOR BIT and
// ALLOWED is 0 — but the reason is sharper than "one multiply then one add". The old tree contracts
// `a*b+c` into a single-rounded fused multiply-add at op-eval (see `image_elementwise` for a case where
// that DOES bite), so `x*scale+bias` there is `fma(x,scale,bias)` while here it is a separately-rounded
// multiply then add. Those two agree only when the intermediate product `x*scale` does not round — and
// here it never does, because the scales are `{2, -1, 0.5}`, all powers of two, so `x*scale` is exact and
// fma and multiply-then-add cannot differ. The bias is carried through float so its bits match, and the
// result is the exact IEEE f32 value on both sides. Change a scale off a power of two and this becomes
// the `image_elementwise` case, with an accounted FMA-contraction ULP rather than an exact match.
#include <array>
#include <cstddef>
#include <cstdio>

#include "heinzel/frontend/graph_builder.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

// The old example's own extents. W is the streamed axis here; each sample is the C*H plane.
constexpr std::int32_t C = 3;
constexpr std::int32_t H = 4;
constexpr std::int32_t W = 5;
constexpr std::size_t kBlock = static_cast<std::size_t>(W);
constexpr std::int32_t kPlane = C * H; // numbers per sample of x and y
constexpr std::int32_t kChan = C;      // numbers per sample of scale and bias ({C,1})

// The per-channel constants, exactly the old example's — carried THROUGH FLOAT so their bits match the
// old tree's f32 inputs. 0.1/-0.2/0.3 are not f32-exact, so feeding the double literals would round the
// bias one ULP away from the old side and turn a bit-exact port into a one-ULP disagreement.
constexpr std::array<double, C> kScale{static_cast<double>(2.0f), static_cast<double>(-1.0f),
                                       static_cast<double>(0.5f)};
constexpr std::array<double, C> kBias{static_cast<double>(0.1f), static_cast<double>(-0.2f),
                                      static_cast<double>(0.3f)};

// x(c,h,w), computed through float so its bits match the old tree's f32 input exactly. The old
// example's own `0.03f*i - 1` with `i` the row-major flat index.
constexpr double x_value(std::int32_t c, std::int32_t h, std::int32_t w) {
    const std::int32_t i = (c * H + h) * W + w;
    return static_cast<double>(0.03f * static_cast<float>(i) - 1.0f);
}

// x{C,H,W}, scale{C,1,W}, bias{C,1,W}; y = x*scale + bias. scale/bias broadcast over H (extent 1) and
// are constant across the streamed W. Built in the frontend — an elementwise graph needs no new op.
constexpr Graph build() {
    Input x{"x", Signal{C, H, W}};
    Input scale{"scale", Signal{C, 1, W}};
    Input bias{"bias", Signal{C, 1, W}};
    return finish(Output{"y"} = x * scale + bias);
}

} // namespace

int main() {
    // The feeds, sample-major (`data[n*width + e]`). x is the C*H plane per sample; scale/bias are the
    // C-vector per sample, the same every sample.
    std::array<double, kBlock * static_cast<std::size_t>(kPlane)> x_buf{};
    std::array<double, kBlock * static_cast<std::size_t>(kChan)> scale_buf{};
    std::array<double, kBlock * static_cast<std::size_t>(kChan)> bias_buf{};
    for (std::int32_t w = 0; w < W; ++w) {
        for (std::int32_t c = 0; c < C; ++c) {
            for (std::int32_t h = 0; h < H; ++h) {
                x_buf[static_cast<std::size_t>(w * kPlane + (c * H + h))] = x_value(c, h, w);
            }
            scale_buf[static_cast<std::size_t>(w * kChan + c)] = kScale[static_cast<std::size_t>(c)];
            bias_buf[static_cast<std::size_t>(w * kChan + c)] = kBias[static_cast<std::size_t>(c)];
        }
    }

    const testing::Outputs y = testing::executed<build>(
        {{"x", x_buf.data(), kPlane}, {"scale", scale_buf.data(), kChan}, {"bias", bias_buf.data(), kChan}});

    // Print y over (c, h, w) in row-major order — the same order channel_scale_there prints its flat
    // buffer, so the two sequences line up element for element. Element (c,h) is c*H+h; sample is w.
    for (std::int32_t c = 0; c < C; ++c) {
        for (std::int32_t h = 0; h < H; ++h) {
            for (std::int32_t w = 0; w < W; ++w) {
                std::printf("%.9e\n", y[0][static_cast<std::size_t>(w * kPlane + c * H + h)]);
            }
        }
    }
    return 0;
}
