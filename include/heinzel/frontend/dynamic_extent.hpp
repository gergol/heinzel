#pragma once

// Recording a runtime-resolved stream length on a graph: a block length known only at `prepare()`.
// A `Length` in a `Signal` (`declarations.hpp`) makes the stream's length a symbol and carries the
// constraints its size must satisfy, as a `DimDecl` on the leaf; finishing a graph records each one here.
// The analysis never reads the constraints: `sema/resolve_extent.hpp` checks a bound size against them
// at `prepare()`.

#include "heinzel/ir/ir.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel::detail {

// Record a declared dimension on a graph, deduplicated by id. Declaring the same id twice is how one length is shared
// across inputs and is fine; declaring it twice with DIFFERENT constraints is a contradiction — the two declarations
// disagree about what the one size must be — and is refused here rather than left for the resolver to trip over with
// less to say.
constexpr void record_dim(Graph& g, const DimDecl& d) {
    if (d.id == kNoDimension) {
        return; // no dimension declared at this site
    }
    for (const DimDecl& seen : g.dims) {
        if (seen.id == d.id) {
            if (!(seen.constraints == d.constraints)) {
                fail("heinzel: a length declared twice with different constraints");
            }
            return; // already recorded, and it agrees
        }
    }
    g.dims.push_back(d);
}

} // namespace heinzel::detail
