#pragma once

// What each operation requires of the attribute it carries, checked once shapes and element types are
// known. A mistake caught here (a state depth that is really a symbol, a transpose that moves time) has a
// sensible shape and type, so the shape sweep cannot see it. A family that makes a shape is asked once more
// to word its refusal (`sema/refusal.hpp`); the checks that make no shape are here. Every check is keyed on
// `AttrKind` (and, for a view, `ViewKind`), never on an operation code; a gate holds this file to that.

#include <cstddef>
#include <cstdint>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/sema/matmul.hpp"
#include "heinzel/sema/order.hpp"
#include "heinzel/sema/overlap_add.hpp"
#include "heinzel/sema/refusal.hpp"
#include "heinzel/sema/spectral.hpp"
#include "heinzel/sema/view_shape.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel {

namespace detail {

// A signalled lag's ceiling is a distance. `signalled_tap` (`ir/timing.hpp`) clamps a position to
// `[0, ceiling]`, so a negative ceiling would read forward in time. Refused here so the clamp can stay
// two comparisons.
constexpr Diagnostic signalled_ceiling_is_a_distance(const Node& n) {
    if (n.attr.signalled_delay.max_delay >= 0) {
        return Diagnostic{};
    }
    return refusal(n.op, " may reach back at most ", n.attr.signalled_delay.max_delay, " samples, which is not a distance");
}

// A constant lag's depth is a state depth, not a size that arrives later. The scan length (how many
// samples a run walks) may be symbolic; the state depth (how far back a lag reaches) sizes the history
// a run keeps, which is decided before any sample is computed.
//
// It needs a check rather than a type: a depth is an `std::int32_t`, and an `Extent` encodes a symbol
// as a value below zero (`ir/vocab.hpp`: `e <= -2` is symbol `-e - 2`). A symbol handed to a depth is
// a negative count, and a read at `n - back` then runs past the end of the block (a graph with
// `delay.samples = encode_symbol(0)` at a block of eight reads element eight). A depth of zero reads
// the present and is allowed. The field is read directly, not through `samples_back()`, which answers
// zero for an operation the table says does not read the past and would let a bad attribute through.
constexpr Diagnostic state_depth_is_a_constant(const Node& n) {
    const std::int32_t depth = n.attr.delay.samples;
    if (depth >= 0) {
        return Diagnostic{};
    }
    // Short: `kMaxMessageLength` is 96.
    return refusal(n.op, " depth ", depth, ": a state depth is a constant >= 0; only a scan length may be symbolic");
}

// A generator reads the shape it declares, since it has no operand. It needs an axis to run along: a rank-0 scalar
// has no position to be. And its element count must be known: it is a loop over the value's elements, and no
// operand's binding could make a symbolic extent concrete later. The element count is the declared shape's numbers
// per sample (its outer axes; the innermost is the streamed axis, time).
constexpr Diagnostic check_generator(const Node& n) {
    if (n.attr.generator.shape.rank < 1) {
        return refusal(n.op, " declares a rank-0 shape; a generator needs at least one axis");
    }
    if (numbers_per_sample(n.attr.generator.shape) == kWidthNotKnown) {
        return refusal(n.op, " declares a symbolic element extent; a generator's shape must be known now");
    }
    return Diagnostic{};
}

// A ramp's numbers are its indices, so it names none in the pool.
constexpr Diagnostic check_ramp_attribute(const Node& n) {
    if (n.attr.generator.first != -1) {
        return refusal(n.op, " names numbers in the pool; a ramp's are its indices");
    }
    return check_generator(n);
}

// A constant's numbers lie in the graph's pool, all of them.
constexpr Diagnostic check_constant_attribute(const Graph& g, const Node& n) {
    if (const Diagnostic why = check_generator(n); !why.ok) {
        return why;
    }
    const std::int64_t first = n.attr.generator.first;
    if (first < 0 || first + numbers_per_sample(n.attr.generator.shape) > static_cast<std::int64_t>(g.constants.size())) {
        return refusal(n.op, " reads numbers outside the graph's pool");
    }
    return Diagnostic{};
}

// A wavetable read. Its resolution `N` is the number of cells the phase is spread across, so it is at least one. The
// table operand (the second) holds those `N` samples, read as a cycle: a read that lands in the last cell
// interpolates toward the first. The table's width is its numbers per sample (the table is an element axis, the
// streamed axis is time), which must be known. The phase may be a symbolic-length signal.
constexpr Diagnostic check_table_attribute(const Graph& g, const Node& n) {
    const std::int32_t size = n.attr.table.size;
    if (size < 1) {
        return refusal(n.op, " has resolution ", size, "; a table read's N is at least one");
    }
    const std::int32_t width = numbers_per_sample(g.values[static_cast<std::size_t>(n.inputs[1])].shape);
    if (width == kWidthNotKnown) {
        return refusal(n.op, " reads a table of unknown size; a wavetable's extent must be known now");
    }
    if (width != size) {
        return refusal(n.op, " of resolution ", size, " needs a table of ", size, " samples, but was given ", width);
    }
    return Diagnostic{};
}

// A reshape conserves its elements. The innermost axis is time; a value's elements are the numbers outside it
// (`numbers_per_sample`). A valid reshape re-cuts the element axes among themselves, so it fails two ways:
//   - it changes `numbers_per_sample` (`{2,3,T}` -> `{6T}`, six numbers per sample becoming one): a
//     reframing over time, which is what `_unfold` and `_ovadd` are for. The reference walk refuses
//     this too (`same_elements_in_order`, `tests/reference/elements.hpp`);
//   - it keeps `numbers_per_sample` but re-cuts the streamed axis (`{8,4}` -> `{8,3}`, eight both
//     ways), which only this check sees.
// It reads the resolved output, since the solver never resolves the attribute's own target shape.
constexpr Diagnostic check_reshape_attribute(const Graph& g, const Node& n) {
    const Shape& from = g.values[static_cast<std::size_t>(n.inputs[0])].shape;
    const Shape& to = g.values[static_cast<std::size_t>(n.output)].shape;
    const std::int32_t from_width = numbers_per_sample(from);
    const std::int32_t to_width = numbers_per_sample(to);
    if (from_width == kWidthNotKnown || to_width == kWidthNotKnown) {
        return Diagnostic{}; // a symbolic width: nothing to compare
    }
    if (from_width != to_width) {
        return refusal(n.op, " changes how many numbers a sample has, from ", from_width, " to ", to_width);
    }
    if (from.rank >= 1 && to.rank >= 1 && !same_extent(extent_at(to, to.rank - 1), extent_at(from, from.rank - 1))) {
        // The extents are not written: the streamed axis may be symbolic here.
        return refusal(n.op, " re-cuts the streamed axis; that is time and stays whole");
    }
    return Diagnostic{};
}

// The refusals a view attribute owes: its family's shape function, asked to word them (`view_shape.hpp`), or for a
// reshape, `check_reshape_attribute`. A squeeze, an unsqueeze and a view still marked were refused by `check_time`.
constexpr Diagnostic check_view_attribute(const Graph& g, const Node& n) {
    const ViewAttr& v = n.attr.view;
    if (v.kind == ViewKind::Reshape) {
        return check_reshape_attribute(g, n);
    }
    const Shape& in = g.values[static_cast<std::size_t>(n.inputs[0])].shape;
    Diagnostic why{};
    if (v.kind == ViewKind::Concat) {
        (void)concat_shape(in, g.values[static_cast<std::size_t>(n.inputs[1])].shape, n.op, v, &why);
    } else {
        (void)viewed_shape(in, n.op, v, &why);
    }
    return why;
}

// The refusal one node's attribute owes, by its kind, written into `why`; false when there is one. A family that
// makes a shape words it when asked. A node whose kind has no check costs nothing here, not even a `Diagnostic`.
constexpr bool attribute_holds(const Graph& g, const Node& n, Diagnostic& why) {
    switch (n.attr.kind) {
    case AttrKind::SignalledDelay:
        why = signalled_ceiling_is_a_distance(n);
        break;
    case AttrKind::Delay:
        why = state_depth_is_a_constant(n);
        break;
    case AttrKind::View:
        why = check_view_attribute(g, n);
        break;
    case AttrKind::Fft:
        (void)fft_shape(g.values[static_cast<std::size_t>(n.inputs[0])].shape, n.op, n.attr.fft, &why);
        break;
    case AttrKind::OverlapAdd:
        (void)overlap_add_shape(g.values[static_cast<std::size_t>(n.inputs[0])].shape, n.op, n.attr.overlap, &why);
        break;
    case AttrKind::Order:
        (void)order_shape(g.values[static_cast<std::size_t>(n.inputs[0])].shape, n.op, n.attr.order, &why);
        break;
    case AttrKind::Table:
        why = check_table_attribute(g, n);
        break;
    case AttrKind::Iota:
        why = check_ramp_attribute(n);
        break;
    case AttrKind::Constant:
        why = check_constant_attribute(g, n);
        break;
    case AttrKind::MatMul:
        why = check_matmul_attribute(g, n);
        break;
    default:
        break;
    }
    return why.ok;
}

// In node order: the first refusal wins.
constexpr Diagnostic check_attributes(const Graph& g) {
    Diagnostic why{};
    for (const Node& n : g.nodes) {
        if (!attribute_holds(g, n, why)) {
            break;
        }
    }
    return why;
}

} // namespace detail

} // namespace heinzel
