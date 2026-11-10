#pragma once

// A link, the `ElementKind::Linked` meaning of `_chain`, and the passes the walk takes over a sample of a graph that
// holds one. `u = _chain(x, V)` lays sections along axis 0: element 0 of `u` is `x` (one section's worth, broadcast)
// and element j is `V`'s element j - 1, both at this sample. `V` is the variable the sections' cycle declares and
// closes, defined later in the node list, so one walk of the nodes settles only section 0 of a sample: the walk goes
// over them again, and pass p settles section p (`docs/sections-in-lanes.md`).

#include <cstddef>
#include <cstdint>

#include "heinzel/ir/ir.hpp"
#include "heinzel/support/fail.hpp"

#include "compute_reference.hpp"
#include "elements.hpp"
#include "signals.hpp"

namespace heinzel {

// Which element of which operand the element `e` of a link's result is. `from_before` picks operand 1 (the section
// before) over operand 0 (the link's input); `index` is that operand's own flat element index.
struct LinkRead {
    std::int32_t index{0};
    bool from_before{false};
    constexpr bool operator==(const LinkRead&) const = default;
};

// `to` is the link's element layout, `x` and `before` its two operands'. Axis 0 is the outermost element axis, so one
// section is `inner` consecutive elements: those below `inner` are section 0, read from `x`; any other is the element
// one section back, `inner` lower, of `before`, which is laid out as the result is.
//
// The refusals are shapes the link's row accepts (it broadcasts as any elementwise operation does) and its meaning says
// nothing about, so the walk gives no number for them: a result with no element axis has no sections to link; a
// `before` laid out otherwise than the result does not hold section j - 1 for every section j; and an `x` with more
// than one element along axis 0 is not one section's worth.
constexpr LinkRead linked_element(const Elements& to, const Elements& x, const Elements& before, std::int32_t e) {
    if (to.rank < 1) {
        fail("heinzel::linked_element: a link's result has no element axis to lay sections along");
    }
    if (before != to) {
        fail("heinzel::linked_element: a link's operand 1 is not laid out as its result");
    }
    const std::int32_t x_axis = x.rank - to.rank; // x's axis lined up with the result's axis 0, if x has one
    if (x_axis >= 0 && x.extent[x_axis] != 1) {
        fail("heinzel::linked_element: a link's operand 0 is more than one section's worth");
    }
    std::int32_t inner = 1;
    for (std::int32_t k = 1; k < to.rank; ++k) {
        inner *= to.extent[k];
    }
    if (e < inner) {
        return LinkRead{element_read_by(to, x, e), false};
    }
    return LinkRead{e - inner, true};
}

// How many times the walk goes over the nodes of one sample. A link's read of the section before is the one read of a
// value made later in the node list, so each such read along a chain of reads within the sample costs a pass more; a
// chain meets each element a link reads from the section before at most once (twice would be a loop within the
// sample), so one pass, and one more for each such element, is enough. One link over k sections of one element takes
// k passes; two links in series k1 + k2 - 1, as the second starts to settle only once the first has (their largest k
// would not be enough). A graph with no link takes one, as before.
constexpr std::size_t passes_of(const Graph& g) {
    std::size_t passes = 1;
    for (const Node& node : g.nodes) {
        if (element_kind_of(node.op) != ElementKind::Linked) {
            continue;
        }
        const Elements result = elements_of(g.values[static_cast<std::size_t>(node.output)].shape);
        if (result.rank > 0 && result.extent[0] > 1) {
            passes += static_cast<std::size_t>(result.count - result.count / result.extent[0]);
        }
    }
    return passes;
}

namespace detail {

// One node at one sample, when the operation is a LINK: each element of the result is one element of one of two
// operands, `linked_element` saying which. No arithmetic and no rounding: the result's type is the wider of its
// operands' (`TypeRule::FromOperands`), so the copy is exact.
template <std::size_t BlockSize, std::size_t Planes>
constexpr void walk_link_step(const Graph& g, const Node& node, std::size_t n, Signals<BlockSize, Planes>& out) {
    const std::int32_t id = node.output;
    Elements operand[kMaxNodeInputs]{};
    operand_elements(g, node, operand);
    const Elements result = elements_of(g.values[static_cast<std::size_t>(id)].shape);

    for (std::int32_t e = 0; e < result.count; ++e) {
        const LinkRead r = linked_element(result, operand[0], operand[1], e);
        const std::int32_t src = r.from_before ? node.inputs[1] : node.inputs[0];
        write(out, id, e, n, sample_at(out, src, r.index, n));
    }
}

} // namespace detail

} // namespace heinzel
