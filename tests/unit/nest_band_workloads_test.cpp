// Bands over the other workloads (`tests/support/band_cases.hpp`, the cases and the run, `nest_band_test` the rest):
// each the planner's schedule of a machine that bands nothing with a band added by hand over all its stages, run twice
// beside the same graph unbanded and held to it BIT FOR BIT (a band changes no arithmetic order), every run canaried
// (`canaried.hpp`).
//
//   harris    two stages (dx/dy and products; box sums and response), three band buffers
//   chain     three vertical blurs: leads 4, 2, 0; two band buffers
//   conv      p = x*x stored, then a 7-tap fold: the band axis carries the lanes, tails in every chunk
//   feedback  the oscillator and a one-pole bank with a map after: a band over time, the state carried
//             between chunks and from one run to the next, at lengths 1, 7, 64 and 1000

#include "band_cases.hpp"

#include <cstdio>
#include <vector>

using namespace heinzel;

namespace {

using namespace band_cases;
namespace loops = heinzel::loops;

template <std::int32_t... Steps>
void check_harris_and_chain() {
    const std::vector<float> x = noise(std::size_t(12) * (kW - 33), 4);
    const std::vector<float> wx = noise(9, 5);
    const std::vector<float> wy = noise(9, 6);
    const loops::Feeds feeds = feeds_of(kHarris, {{"x", &x}, {"wx", &wx}, {"wy", &wy}});
    (same<kPlain<kHarris>, kBanded<kHarris, 0, Steps>>("harris", feeds), ...);
    const std::vector<float> image = noise(std::size_t(kH) * kW, 7);
    const std::vector<float> w = noise(3, 8);
    const loops::Feeds chain_feeds = feeds_of(kChain, {{"x", &image}, {"w", &w}});
    (same<kPlain<kChain>, kBanded<kChain, 0, Steps>>("three blurs", chain_feeds), ...);
}

template <std::int32_t... Steps>
void check_conv() {
    const std::vector<float> x = noise(1000, 9);
    const std::vector<float> h = noise(7, 10);
    const loops::Feeds feeds = feeds_of(kConv, {{"x", &x}, {"h", &h}});
    (same<kPlain<kConv, kStoring>, kBanded<kConv, 0, Steps, kStoring>>("conv", feeds), ...);
}

template <std::int32_t... Steps>
void check_feedback(std::int64_t length) {
    loops::Sizes sizes{};
    sizes[0] = length;
    const std::vector<float> inc(static_cast<std::size_t>(length), 0.0137f);
    const std::vector<float> x = noise(static_cast<std::size_t>(3 * length), 11);
    const loops::Feeds osc_feeds = feeds_of(kOsc, {{"inc", &inc}});
    (same<kPlain<kOsc>, kBanded<kOsc, 0, Steps>>("oscillator", osc_feeds, sizes), ...);
    const loops::Feeds pole_feeds = feeds_of(kOnepole, {{"x", &x}});
    (same<kPlain<kOnepole>, kBanded<kOnepole, 1, Steps>>("one-pole bank", pole_feeds, sizes), ...);
}

} // namespace

int main() {
    check_harris_and_chain<1, 3, 4, 7, 8>();
    check_conv<64, 50, 1>();
    for (const std::int64_t length : {1, 7, 64, 1000}) {
        check_feedback<1, 16, 100>(length);
    }
    if (g_wrong != 0) {
        std::printf("%d results are wrong\n", g_wrong);
        return 1;
    }
    std::printf("nest_band_workloads: every banded run bit for bit the unbanded one, nothing written outside a buffer\n");
    return 0;
}
