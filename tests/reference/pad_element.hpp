#pragma once

// Which element of its operand one element of a padded result is, or that it is a margin reading the
// constant fill: the `ElementKind::Guarded` meaning (`_padc`). It reads the `View` attribute and names no
// operation; the indexing is `padded_element` in `elements.hpp`. It answers a `PadRead` rather than an
// index, and the fill value is the attribute's IEEE-754 bit pattern, read out by `pad_fill`.

#include <bit>

#include "heinzel/ir/ir.hpp"
#include "heinzel/support/fail.hpp"

#include "elements.hpp"

namespace heinzel {

constexpr PadRead padded_read(const Node& n, const Elements& to, const Elements& from, std::int32_t e) {
    if (n.attr.kind != AttrKind::View || n.attr.view.kind != ViewKind::Pad) {
        fail("heinzel::padded_read: this is a guarded pad and carries no pad attribute saying how");
    }
    return padded_element(to, from, n.attr.view.axis, n.attr.view.pad_before, n.attr.view.pad_after, e);
}

// The constant a pad's margin reads, decoded from the IEEE-754 bit pattern the attribute stores it as. A
// margin is the one place a rearrangement-shaped operation produces a number not already stored, so the
// walk rounds it to the result's type, as it does a fold's result.
constexpr double pad_fill(const Node& n) { return std::bit_cast<double>(n.attr.view.pad_bits); }

} // namespace heinzel
