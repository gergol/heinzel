#pragma once

// A DELIBERATE OFFENDER for the frontend_decides_nothing_semantic gate. Nothing includes this file; it exists only
// to be what a `_would_object` test points the gate at, so a gate that has quietly stopped objecting is caught. It
// keeps operations made before a graph exists, to replay them later — the machinery a leaf that is its own graph
// made unnecessary — under a name of the tree's own style, which a plain `pending` would not catch.
namespace heinzel::fixture {

inline constexpr int kMaxPending = 8; // the offence

} // namespace heinzel::fixture
