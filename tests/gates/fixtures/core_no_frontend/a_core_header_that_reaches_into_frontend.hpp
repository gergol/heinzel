#pragma once

// A DELIBERATE OFFENDER for the core_no_frontend gate (M8.1 / O12; the boundary `02-Target-
// Architecture.md` §2.4 adds and the old tree left RED). Nothing includes this file; it exists only to
// be the one thing the `_would_object` test points the gate at, so a gate that has quietly stopped
// objecting is caught. A core header reaching into `heinzel/frontend/` is the edge O12 forbids: the
// frontend builds ON the core (it includes ir/sema), so a core header naming the frontend would mean
// the core was shaped by the surface and could not compile with the frontend deleted.
#include "heinzel/frontend/graph_builder.hpp"

namespace heinzel::fixture {
inline constexpr int core_no_frontend_offender = 0;
}
