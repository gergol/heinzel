#pragma once

// The tests' failing forms of the planner and the lowering (`Plan/archive/13-loop-tree/07` §2): an analysed graph's program for
// a machine, planned and lowered once; the planner's schedule, for a test that changes one decision; a schedule edited
// or written by hand, lowered — each the program, held to what the runner accepts (`well_formed.hpp`), or `fail` with
// the refusal or the rule it breaks, so a refused graph stops the build at the line that asked. And the refusal itself,
// as a value, for the tests that pin it — at compile time or at run time alike.

#include <expected>
#include <memory>
#include <string_view>

#include "heinzel/ir/ir.hpp"
#include "heinzel/loops/lower.hpp"
#include "heinzel/loops/planner/plan.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/schedule.hpp"
#include "heinzel/schedule/machine.hpp"
#include "heinzel/support/fail.hpp"

#include "well_formed.hpp"

namespace planned {

namespace loops = heinzel::loops;

// A program the tests keep, held as the one member of a struct so that the runner's template argument names a
// SUBOBJECT. Named as a whole `constexpr` variable, clang's code generation builds its half-megabyte initializer over
// and over (`nest_odd_shapes`: 69 s against 28 s, `Plan/archive/12` M4). Spelled `constexpr planned::Held kXHeld{…};
// constexpr const loops::Program& kX = kXHeld.program;`, so every use names `kX` as before.
struct Held {
    loops::Program program;
};

// The program the planner makes of `g` for `m`.
constexpr loops::Program program_of(const heinzel::Graph& g, const heinzel::Machine& m) {
    const std::expected<loops::Planned, loops::Refusal> r = loops::plan_and_lower(g, m);
    if (!r) {
        heinzel::fail(r.error().view());
    }
    if (const std::expected<void, loops::Refusal> w = loops::well_formed(r->program); !w) {
        heinzel::fail(w.error().view());
    }
    return r->program;
}

// The planner's schedule of `g` for `m`: what a test edits before `program_of(g, schedule)`.
constexpr loops::Schedule schedule_of(const heinzel::Graph& g, const heinzel::Machine& m) {
    const std::expected<loops::Schedule, loops::Refusal> d = loops::planner::schedule_of(g, m);
    if (!d) {
        heinzel::fail(d.error().view());
    }
    return *d;
}

// A schedule lowered.
constexpr loops::Program program_of(const heinzel::Graph& g, const loops::Schedule& d) {
    loops::Program p{};
    const std::expected<void, loops::Refusal> r = loops::lower_into(g, d, p);
    if (const std::expected<void, loops::Refusal> w = r ? loops::well_formed(p) : r; !w) {
        heinzel::fail(w.error().view());
    }
    return p;
}

// Why the planner or the lowering refuses `g` for `m`, or the runner's rule its program breaks ("" where it lowers
// to a program the runner accepts). Each `refusal_of` holds what it makes on the heap, so a run-time caller
// spends no half megabyte of stack on it.
constexpr loops::Refusal refusal_of(const heinzel::Graph& g, const heinzel::Machine& m) {
    const std::unique_ptr<const std::expected<loops::Planned, loops::Refusal>> r(new auto(loops::plan_and_lower(g, m)));
    const std::expected<void, loops::Refusal> w = *r ? loops::well_formed((*r)->program) : std::unexpected(r->error());
    return w ? loops::Refusal{} : w.error();
}

// ...and a schedule's.
constexpr loops::Refusal refusal_of(const heinzel::Graph& g, const loops::Schedule& d) {
    const std::unique_ptr<loops::Program> p = std::make_unique<loops::Program>();
    const std::expected<void, loops::Refusal> r = loops::lower_into(g, d, *p);
    const std::expected<void, loops::Refusal> w = r ? loops::well_formed(*p) : r;
    return w ? loops::Refusal{} : w.error();
}

} // namespace planned
