// A value with TWO element axes and no streamed-axis dependence — a spatial image — run both ways.
//
// Milestone MB, step S3. The batch/broadcast axis-model's second capability after operand broadcast
// (which S2 found already built): a spatial value whose axes are BOTH element axes and whose streamed
// axis is DEGENERATE — length one, no time. The old tree's `examples/image_elementwise_2d.cpp` is
// exactly this: an H×W image, whole-tensor, index-free, with a per-column bias broadcast over rows and
// no notion of a sample stream at all. This tree keeps the streamed axis innermost, so a value with no
// time is a value whose innermost axis is length one — `Shape{H, W, 1}`, `H` and `W` the element axes,
// the block a single sample.
//
// WHAT THIS CHECKS, and why it is here rather than assumed. MB.S3's audit flagged that the port rests
// on an UNVERIFIED claim: that the streaming model runs a length-one stream over a rank-2 element value
// on every runner (the walk, and the loop tree through `Executor`). That is the claim under test. The
// broadcast is over an ELEMENT axis (the bias over rows) rather than the whole value. If both runners
// agree with a hand reference here, the surface carries a 2-D image; if one of them refused a
// length-one stream or mis-indexed a rank-2 element, it would be a plausible wrong number caught here.
//
// The elements are made to DIFFER (wide inputs, MW's feeds) so a mis-index shows in the numbers rather
// than cancelling.
#include "heinzel/executor.hpp"
#include "heinzel/sema/analyze.hpp"

#include <array>
#include <cstddef>

#include "executed.hpp"
#include "hand_built_graph.hpp"
#include "interpret.hpp"
#include "run_inputs.hpp"

using namespace heinzel;

namespace {

// A single sample: the image has no time. H and W are the element axes.
constexpr std::size_t kBlock = 1;
constexpr std::int32_t H = 3;
constexpr std::int32_t W = 4;
constexpr std::int32_t kPixels = H * W;
constexpr double kGain = 1.3;

// y = t / (1 + t*t), t = gain*x + bias — the old example's smooth saturator, kept to add/mul/div so
// it is exact against the reference. `x{H,W,1}`, `bias{1,W,1}` broadcasting over rows, `gain` a scalar
// constant broadcasting over the whole value.
constexpr Graph build_image() {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{H, W, 1}, ValueCategory::Input);
    const auto bias = add_value(g, ElemType::F64, Shape{1, W, 1}, ValueCategory::Input);
    const auto gain = add_value(g, ElemType::F64, Shape{1}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(gain)].constant = kGain;

    const auto scaled = add_node(g, op::Mul, gain, x);
    const auto t = add_node(g, op::Add, scaled, bias);
    const auto tt = add_node(g, op::Mul, t, t);
    const auto one = add_value(g, ElemType::F64, Shape{1}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(one)].constant = 1.0;
    const auto denom = add_node(g, op::Add, one, tt);
    const auto y = add_node(g, op::Div, t, denom);

    g.inputs.push_back(NamedValue{Name{"x"}, x});
    g.inputs.push_back(NamedValue{Name{"bias"}, bias});
    g.outputs.push_back(NamedValue{Name{"y"}, y});
    return g;
}

constexpr Graph kImage = [] {
    Graph g = build_image();
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the 2-D image graph did not resolve");
    }
    return g;
}();

// It really is a rank-2 element value: two element axes, twelve numbers, one sample.
static_assert(numbers_per_sample(kImage.values[static_cast<std::size_t>(kImage.outputs[0].value)].shape) == kPixels,
              "the image is H*W numbers per sample");

// The pixel values, distinct so a mis-index is visible; the per-column bias, distinct per column.
constexpr double x_at(std::int32_t h, std::int32_t w) { return 0.07 * static_cast<double>(h * W + w) - 2.0; }
constexpr double bias_at(std::int32_t w) { return 0.1 * static_cast<double>(w) - 0.5; }

constexpr std::array<double, kPixels> x_feed_data() {
    std::array<double, kPixels> d{};
    for (std::int32_t h = 0; h < H; ++h) {
        for (std::int32_t w = 0; w < W; ++w) {
            d[static_cast<std::size_t>(h * W + w)] = x_at(h, w);
        }
    }
    return d;
}
constexpr std::array<double, W> bias_feed_data() {
    std::array<double, W> d{};
    for (std::int32_t w = 0; w < W; ++w) {
        d[static_cast<std::size_t>(w)] = bias_at(w);
    }
    return d;
}

// The reference: the same arithmetic, per pixel, in double.
constexpr double reference(std::int32_t h, std::int32_t w) {
    const double t = kGain * x_at(h, w) + bias_at(w);
    return t / (1.0 + t * t);
}

// ---- the two runners, each reading every pixel of the one sample ----------------------------------

constexpr std::array<double, kPixels> walked() {
    const auto xs = x_feed_data();
    const auto bs = bias_feed_data();
    Inputs feeds{};
    feeds.push_back(InputFeed{xs.data(), kPixels});
    feeds.push_back(InputFeed{bs.data(), W});
    Signals<kBlock> out{};
    run(kImage, feeds, out);
    std::array<double, kPixels> got{};
    for (std::int32_t e = 0; e < kPixels; ++e) {
        got[static_cast<std::size_t>(e)] = sample_at(out, kImage.outputs[0].value, e, 0);
    }
    return got;
}

constexpr Graph image() { return kImage; }

std::array<double, kPixels> executed() {
    const auto xs = x_feed_data();
    const auto bs = bias_feed_data();
    std::array<double, kPixels> got{};
    Executor<image> ex;
    testing::held(ex);
    ex.input("x") = xs.data();
    ex.input("bias") = bs.data();
    ex.output("y") = got.data();
    ex.run();
    return got;
}

// Every pixel, on both runners, is the hand reference — and the reference is a real image (its pixels
// differ), so agreeing about it means the rank-2 element loop and the row-broadcast both landed right.
bool every_pixel_matches_the_reference() {
    const std::array<double, kPixels> walk = walked();
    const std::array<double, kPixels> loop = executed();
    for (std::int32_t h = 0; h < H; ++h) {
        for (std::int32_t w = 0; w < W; ++w) {
            const std::size_t e = static_cast<std::size_t>(h * W + w);
            const double want = reference(h, w);
            if (!(walk[e] == want && loop[e] == want)) {
                return false;
            }
        }
    }
    return true;
}

// The walk meets the reference while compiling too.
constexpr bool the_walk_matches_the_reference() {
    const std::array<double, kPixels> walk = walked();
    for (std::int32_t h = 0; h < H; ++h) {
        for (std::int32_t w = 0; w < W; ++w) {
            if (!(walk[static_cast<std::size_t>(h * W + w)] == reference(h, w))) {
                return false;
            }
        }
    }
    return true;
}
static_assert(the_walk_matches_the_reference(), "the walk disagrees with the hand reference about a pixel");

// The image is not flat: two pixels that share a column but differ by row must differ, or a mis-index
// that dropped the row axis would pass the check above by accident.
static_assert(reference(0, 1) != reference(2, 1), "the reference image is degenerate along rows");

} // namespace

int main() { return every_pixel_matches_the_reference() ? 0 : 1; }
