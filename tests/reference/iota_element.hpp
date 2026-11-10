#pragma once

// What one element of an index ramp (`_iota`) is worth: the `ElementKind::Indexed` meaning. It reads no
// operand, attribute or layout: element `e` is worth `e`, the same at every sample. The ramp runs over the
// value's elements, not over time, because the streamed axis is outside the element loop; for two or more
// element axes it is the flat index in this tree's element order (`elements.hpp`).

#include <cstdint>

namespace heinzel {

// The value at flat element index `e`. Rounded to the result's type by the caller, exactly as a fold's
// or a pad-fill's is — an index is the one number this meaning produces, and it was not already stored.
constexpr double iota_value(std::int32_t e) { return static_cast<double>(e); }

} // namespace heinzel
