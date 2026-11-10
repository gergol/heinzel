#pragma once

// How the walk is handed more than one input, and inputs wider than one number per sample. Beside the
// scalar entry point (one input, one number per sample), the wide one takes one feed per named input, in
// `Graph::inputs` order. A feed is a view over the caller's storage, sample-major: `data[n * width + e]`
// is element e at sample n. A run checks the count and each width before it writes a number, so a feed of
// the wrong shape is refused rather than half-read.

#include <cstdint>

#include "heinzel/ir/ir.hpp"
#include "heinzel/support/fail.hpp"
#include "heinzel/support/fixed_vector.hpp"

#include "elements.hpp"

namespace heinzel {

// One input's samples, as a view over the caller's storage: usable in a constant expression, because it
// points at an object alive in the same evaluation. `data` addresses `width * samples` numbers laid out
// sample-major; `width` is how many numbers one sample of this input is, and a run checks it against
// `elements_of(input.shape).count` before reading. Sample-major because a real frame
// arrives interleaved (element fastest within a sample), and the width-one case is then bit for bit the
// scalar entry's `block[n]`.
//
// `samples` is how many samples the feed holds. Zero means exactly the block, as the scalar entry's
// `std::array<double, BlockSize>` is, and every per-sample input leaves it zero. Only a framed input
// states it: its frame `f` reads window tap `k` at source sample `f*hop + k`, up to `N-1` while the
// block loops `[0, W)`, so its feed must declare its `N` and a run checks the frames do not read past it
// (`framed_stream.hpp`). Left zero, a framed feed is refused rather than read off its end.
struct InputFeed {
    const double* data{nullptr};
    std::int32_t width{0};
    std::int32_t samples{0}; // 0 = exactly the block; stated only for a wider-than-block framed feed

    constexpr bool operator==(const InputFeed&) const = default;
};

// The feeds for a whole run, one per named input, in `Graph::inputs` order. Bounded by `kMaxIO`, the
// same ceiling the graph's own input list has.
using Inputs = fixed_vector<InputFeed, kMaxIO>;

// The feeds are this graph's: one per named input, each as wide as the input's own shape says.
// Refused before any number is written, so a feed of the wrong shape is a clear failure rather than
// an input that arrives half-filled.
constexpr void feeds_are_this_graph_s(const Graph& g, const Inputs& feeds) {
    if (feeds.size() != g.inputs.size()) {
        fail("heinzel::feeds_are_this_graph_s: one feed per named input");
    }
    for (std::size_t i = 0; i < g.inputs.size(); ++i) {
        const std::int32_t id = g.inputs[i].value;
        const std::int32_t width = elements_of(g.values[static_cast<std::size_t>(id)].shape).count;
        if (feeds[i].width != width) {
            fail("heinzel::feeds_are_this_graph_s: a feed's width is not this input's element count");
        }
    }
}

} // namespace heinzel
