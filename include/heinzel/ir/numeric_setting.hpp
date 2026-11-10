#pragma once

// The numeric setting: how much accuracy an operation's core may trade for speed, and how much freedom
// of order it has. A value's precision is resolved at compile time (`numerics::Precision`, `ir/ir.hpp`)
// and mapped to this by `numeric_setting_of`; the runtime cores read `fn`. Nothing here knows an
// operation or a shape.

#include <cstdint>

namespace heinzel {

// ---- which core an operation runs, and how much freedom of order it has --------------------------

// Which implementation of a transcendental runs. `Inherit` means "no preference expressed here" — it
// is never a tier a core may run at; a core reached with `Inherit` means resolution never happened.
enum class FnAccuracy : std::uint8_t { Inherit, Exact, Approx };

// Whether the optimiser may reassociate or contract arithmetic. `Reproducible` is the promise that
// the same graph gives the same bits; `Free` allows reassociation.
enum class Ordering : std::uint8_t { Inherit, Reproducible, Free };

// What a resolved precision maps to: one answer per axis. Produced by `numeric_setting_of` and
// consumed by the runtime cores (which read only `fn`).
struct NumericSetting {
    FnAccuracy fn{FnAccuracy::Inherit};
    Ordering ordering{Ordering::Inherit};

    constexpr bool operator==(const NumericSetting&) const = default;
};

} // namespace heinzel
