#pragma once

// Reading a framed-stream input from its feed. `unfold` along the streamed axis turns `N` samples into
// `W = (N-window)/hop + 1` frames: `W` becomes the run's block length and the window tap an element axis,
// innermost among the value's elements (`[.., N]` -> `[.., window, W]`, `unfold_shape` in
// `sema/framing.hpp`). Frame `f` reads source sample `f*hop + k`, across `[0, N)` while the block runs over
// `[0, W)`, so the frames are gathered from the caller's feed rather than from the walk's block.

#include <cstddef>
#include <cstdint>

#include "heinzel/ir/ir.hpp"
#include "heinzel/support/fail.hpp"

#include "elements.hpp"

namespace heinzel {

// True iff this node windows the streamed axis of its source: a `ViewKind::Unfold` whose `axis` is the
// source's innermost (`rank-1`). A window of an outer axis is an ordinary within-sample rearrangement
// (`rearrange_element.hpp`). Keyed on the attribute, not the operation.
constexpr bool frames_the_stream(const Graph& g, const Node& node) {
    if (node.attr.kind != AttrKind::View || node.attr.view.kind != ViewKind::Unfold) {
        return false;
    }
    const std::int32_t src = node.inputs[0];
    if (src < 0 || static_cast<std::size_t>(src) >= g.values.size()) {
        return false;
    }
    return node.attr.view.axis == g.values[static_cast<std::size_t>(src)].shape.rank - 1;
}

// Which feed supplies `value`, by its position in `g.inputs`; `-1` if `value` is not a named input.
constexpr std::int32_t feed_index_of(const Graph& g, std::int32_t value) {
    for (std::size_t i = 0; i < g.inputs.size(); ++i) {
        if (g.inputs[i].value == value) {
            return static_cast<std::int32_t>(i);
        }
    }
    return -1;
}

// A framed input is well formed for a block of `block` frames: its source is a graph input, the run's
// block length is the value's frame count `W`, and nothing reads the source unframed. A computed source,
// or one also read unframed, would need the source to stream over `N` while the block streams over `W`:
// a second block length, which nothing supports, so the walk refuses both by name. Checked once before
// the block.
//
// `W` is a literal when the source's length is a literal, and a symbol when it is a declared dimension.
// A literal `W` must equal the block. A symbolic one has nothing to compare: the caller chose the block
// for its `N`, and the framing step refuses a block the feed cannot serve.
constexpr void framed_inputs_are_well_formed(const Graph& g, std::size_t block) {
    for (const Node& node : g.nodes) {
        if (!frames_the_stream(g, node)) {
            continue;
        }
        const std::int32_t src = node.inputs[0];
        if (feed_index_of(g, src) < 0) {
            fail("heinzel::run: a framed stream's source is not a graph input (a second block "
                 "length; reserved)");
        }
        const Shape& fs = g.values[static_cast<std::size_t>(node.output)].shape;
        const Extent frames = extent_at(fs, fs.rank - 1);
        if (is_literal(frames) && literal_of(frames) != static_cast<std::int32_t>(block)) {
            fail("heinzel::run: the block length must be the framed value's frame count");
        }
        for (const Node& other : g.nodes) {
            for (std::int32_t s = 0; s < kMaxNodeInputs; ++s) {
                if (other.inputs[s] == src && !(frames_the_stream(g, other) && other.inputs[0] == src)) {
                    fail("heinzel::run: a framed input is also read unframed (a second block "
                         "length; reserved)");
                }
            }
        }
    }
}

// True iff `value` is a graph input consumed by a framing node: its frames are materialised from its
// feed by that node, so a run must not also bind it as an ordinary per-sample input.
constexpr bool feeds_a_framed_stream(const Graph& g, std::int32_t value) {
    for (const Node& node : g.nodes) {
        if (frames_the_stream(g, node) && node.inputs[0] == value) {
            return true;
        }
    }
    return false;
}

// How many input samples a feed for a framed stream must hold: the last frame `W-1` reads window tap
// `window-1` at source sample `(W-1)*hop + (window-1)`, so the feed spans `(W-1)*hop + window`
// samples. The minimum — the source's declared `N` may be up to `hop-1` longer, since `W` is a floor.
constexpr std::int32_t framed_feed_samples(std::int32_t window, std::int32_t hop, std::int32_t frames) {
    if (window < 1 || hop < 1 || frames < 1) {
        fail("heinzel::framed_feed_samples: a framed stream has a window, hop and frame count of at "
             "least one");
    }
    return (frames - 1) * hop + window;
}

// Which flat number of a framed input's FEED one output element `e` at frame `f` reads. The framed
// value's element axes are the source's own element axes with a window axis of `window` INNERMOST
// (`unfold_shape`), so an element splits as `e = se*window + k`: `k = e % window` is the window
// tap and `se = e / window` the source element. Frame `f` reads source sample `f*hop + k`, and the
// feed is sample-major over the source's `w_src` numbers per sample (`run_inputs.hpp`), so the number
// is `(f*hop + k) * w_src + se`.
constexpr std::int32_t framed_feed_index(std::int32_t window, std::int32_t hop, std::int32_t w_src,
                                         std::int32_t e, std::int32_t f) {
    if (window < 1 || hop < 1 || w_src < 1 || e < 0 || f < 0) {
        fail("heinzel::framed_feed_index: a framed read outside the value's own elements");
    }
    const std::int32_t k = e % window;
    const std::int32_t se = e / window;
    return (f * hop + k) * w_src + se;
}

} // namespace heinzel
