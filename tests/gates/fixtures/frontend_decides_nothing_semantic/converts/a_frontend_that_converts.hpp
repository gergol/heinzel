#pragma once

// A DELIBERATE OFFENDER for the frontend_decides_nothing_semantic gate: a frontend inserting a conversion on the
// user's behalf, which is the analysis pass's decision. Nothing includes this file.
namespace heinzel::fixture {

inline constexpr auto kConversion = "op::Cast"; // the offence: the gate reads code, and this names the op

} // namespace heinzel::fixture
