#pragma once

// A planted offender for `no_per_op_branches.cmake`: a rewrite that finds the operation it emits by its name in the
// rule table, where it should ask a rule column for the role it needs.
constexpr int emitted() { return heinzel::op_named("_fma"); }
