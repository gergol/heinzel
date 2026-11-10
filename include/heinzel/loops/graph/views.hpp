#pragma once

// Views are not computed. A reader of a sliding window reads its source, at an address still affine in the reader's
// own axes: frame `f` and tap `k` of a window over axis `a` are source element `f * hop + k` of that axis, every other
// axis itself, so a windowed value gets no loop nest and no storage. The other affine views (a transpose, a slice, a
// flip, new extents over the same elements) are the same kind of address; one that anything but a view reads becomes
// a statement, a Load at that address (`Facts::statement`). Keyed on the view's kind, it names no operation.

#include <cstdint>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"

namespace heinzel::loops::graph {

// Per axis of a value: which axis of `source` one step along it moves (-1: none — an axis of extent one the source
// does not have), and by how many elements — a reversal steps by minus one. Per axis of the SOURCE, where element
// zero of the value stands along it. A value that is no view reads itself, axis for axis, from the start.
struct Viewed {
    std::int32_t source{kNoValue};
    std::int32_t axis[kMaxRank]{};
    std::int32_t step[kMaxRank]{};
    std::int32_t offset[kMaxRank]{};

    constexpr bool operator==(const Viewed&) const = default;
};

// A sliding window: its readers read through it, and it never has a statement of its own.
constexpr bool folds_into_its_readers(const heinzel::Node& n) {
    return rules_of(n.op).attr == AttrKind::View && (n.attr.view.kind == ViewKind::Unfold || n.attr.view.kind == ViewKind::Unfold2d);
}

// How many taps each of its frames holds: what computing a producer per tap instead of once multiplies.
constexpr std::int32_t taps_of(const heinzel::Node& window) {
    return window.attr.view.window * (window.attr.view.kind == ViewKind::Unfold2d ? window.attr.view.window2 : 1);
}

// The other views with an affine address: the same elements under another order of axes, a strided part of one
// axis, one axis reversed, or another set of extents over the same elements in order.
constexpr bool keeps_elements_in_order(const heinzel::Node& n) {
    const ViewKind kind = n.attr.view.kind;
    return rules_of(n.op).attr == AttrKind::View && (kind == ViewKind::Reshape || kind == ViewKind::Squeeze || kind == ViewKind::Unsqueeze);
}
constexpr bool is_an_affine_view(const heinzel::Node& n) {
    const ViewKind kind = n.attr.view.kind;
    return rules_of(n.op).attr == AttrKind::View &&
           (kind == ViewKind::Transpose || kind == ViewKind::Slice || kind == ViewKind::Flip || graph::keeps_elements_in_order(n));
}

// ...of which only a window over an ELEMENT axis of its source, by a hop of one or more, has an address: the
// streamed axis is time, and a window over it reaches into samples of other runs.
constexpr bool windows_an_element_axis(const Graph& g, const heinzel::Node& view) {
    const ViewAttr& how = view.attr.view;
    const std::int32_t rank = g.values[static_cast<std::size_t>(view.inputs[0])].shape.rank;
    if (how.kind == ViewKind::Unfold2d && (how.hop2 < 1 || how.axis2 <= how.axis || how.axis2 + 1 >= rank)) {
        return false; // the second window, over an element axis after the first
    }
    return how.hop >= 1 && how.axis >= 0 && how.axis + 1 < rank;
}

constexpr Viewed read_as_itself(std::int32_t v) {
    Viewed self{.source = v};
    for (std::size_t k = 0; k < kMaxRank; ++k) {
        self.axis[k] = static_cast<std::int32_t>(k);
        self.step[k] = 1;
    }
    return self;
}

// New extents over the same elements in order. The source is made, so it lies in memory in its own order with its
// last element axis innermost: every axis of the view walks THAT axis, by as many elements as the view's axes
// inside it hold. (The streamed axis is itself on both sides.)
constexpr Viewed in_other_extents(const Graph& g, const heinzel::Node& view, const Viewed& under) {
    const Shape& to = g.values[static_cast<std::size_t>(view.output)].shape;
    const Shape& from = g.values[static_cast<std::size_t>(under.source)].shape;
    Viewed through{.source = under.source};
    std::int32_t inside = 1;
    for (std::int32_t k = to.rank - 2; k >= 0; --k) {
        const Extent e = extent_at(to, k);
        through.axis[k] = from.rank >= 2 ? from.rank - 2 : -1;
        through.step[k] = inside;
        inside *= is_literal(e) ? e.encoded : 1;
    }
    if (to.rank >= 1) {
        through.axis[to.rank - 1] = from.rank - 1;
        through.step[to.rank - 1] = 1;
    }
    return through;
}

// `view`, applied on top of what its operand is read through (`under`). The view has an address (`why_no_address` is
// asked of every view before anything is read through one).
constexpr Viewed through_view(const Graph& g, const heinzel::Node& view, const Viewed& under) {
    const ViewAttr& how = view.attr.view;
    const std::int32_t rank = g.values[static_cast<std::size_t>(view.output)].shape.rank;
    if (graph::keeps_elements_in_order(view)) {
        return graph::in_other_extents(g, view, under);
    }
    Viewed through = under;
    const bool windowed = graph::folds_into_its_readers(view);
    const bool twice = how.kind == ViewKind::Unfold2d;
    for (std::int32_t k = 0; k < rank; ++k) {
        // A window's frame and tap both walk its axis — the second window's stand one further on, past the first
        // one's tap; a transpose reads the axis its order names.
        const std::int32_t from = how.kind == ViewKind::Transpose ? how.order.axes[k]
                                  : !windowed || k <= how.axis      ? k
                                  : !twice || k <= how.axis2 + 1    ? k - 1
                                                                    : k - 2;
        through.axis[k] = under.axis[from];
        through.step[k] = under.step[from];
    }
    const std::int32_t at = how.kind == ViewKind::Transpose ? 0 : how.axis; // (a transpose's `axis` says nothing)
    const std::int32_t along = under.axis[at];
    const std::int32_t by = under.step[at];
    if (windowed) {
        through.step[how.axis] = by * how.hop;
        if (twice) {
            through.step[how.axis2 + 1] = under.step[how.axis2] * how.hop2;
        }
    } else if (how.kind == ViewKind::Slice) {
        through.step[how.axis] = by * how.step;
        through.offset[along] += by * how.offset;
    } else if (how.kind == ViewKind::Flip) {
        through.step[how.axis] = -by;
        through.offset[along] += by * (extent_at(g.values[static_cast<std::size_t>(view.output)].shape, how.axis).encoded - 1);
    }
    return through;
}
} // namespace heinzel::loops::graph
