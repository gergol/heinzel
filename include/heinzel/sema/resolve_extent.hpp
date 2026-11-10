#pragma once

// Is the size a runtime-resolved dimension is bound to allowed? A graph whose signals have a `Length`
// declares each such dimension with the constraints its size must satisfy (`DimDecl`, `ir/ir.hpp`), and
// `Executor` checks the frame count against them at `prepare` and `run`. The size is user input (the block
// length an audio host supplies), so a violation comes back as a `Diagnostic` naming the size and the rule
// it broke, for the caller to report.

#include <cstdint>
#include <string_view>

#include "heinzel/ir/ir.hpp"
#include "heinzel/sema/solver.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel {

// Is one size allowed for one dimension's constraints? A size must be a real block (at least one
// sample), at least the declared minimum, and a whole multiple of the declared factor. The checks run
// from the most basic outward, so the message names the first thing wrong. A size is 64 bits: a run's
// length may be longer than 2^31 samples.
constexpr Diagnostic check_dimension_size(const DimConstraints& c, std::int64_t size) {
    if (size < 1) {
        MessageWriter m;
        m << std::string_view{"heinzel::resolve_extent: a resolved size is at least one sample, not "} << size;
        return m.problem();
    }
    if (size < c.min) {
        MessageWriter m;
        m << std::string_view{"heinzel::resolve_extent: size "} << size << std::string_view{" is below the minimum "}
          << c.min;
        return m.problem();
    }
    // `multiple_of <= 1` is "no constraint", and the guard also keeps the modulo off a zero or negative
    // divisor a malformed constraint could carry in.
    if (c.multiple_of > 1 && size % c.multiple_of != 0) {
        MessageWriter m;
        m << std::string_view{"heinzel::resolve_extent: size "} << size
          << std::string_view{" is not a multiple of "} << c.multiple_of;
        return m.problem();
    }
    return Diagnostic{};
}

// Every runtime-resolved dimension a graph declares must be bound, to a size its constraints allow.
// Returns the first violation. An unbound dimension is refused too, rather than defaulting to something
// plausible downstream.
constexpr Diagnostic resolve_dimensions(const Graph& g, const Bindings& sizes) {
    for (const DimDecl& d : g.dims) {
        bool bound = false;
        std::int32_t size = 0;
        for (const SymbolBinding& b : sizes.at) {
            if (b.symbol == d.id) {
                bound = true;
                size = b.size;
                break;
            }
        }
        if (!bound) {
            MessageWriter m;
            m << std::string_view{"heinzel::resolve_extent: dimension "} << d.id
              << std::string_view{" was left unbound at prepare()"};
            return m.problem();
        }
        const Diagnostic one = check_dimension_size(d.constraints, size);
        if (!one.ok) {
            return one;
        }
    }
    return Diagnostic{};
}

} // namespace heinzel
