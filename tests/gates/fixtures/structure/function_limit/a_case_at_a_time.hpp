#pragma once

// A DELIBERATE OFFENDER, and nothing includes it.
//
// The other half of showing the structure gate rejecting: this one trips the FUNCTION limit and only
// that one. The file is well under 600 lines, and exactly one function in it is over 60 — genuinely
// over, not over a limit lowered to suit it.
//
// What it is a caricature of is real: a dispatcher that grew a case per thing it had to handle, each
// case perfectly reasonable on the day it was added. That is `sema/analyze.hpp`'s `infer()` at 346
// lines and `schedule/plan.hpp`'s `plan_schedule()` at 224, which is where this tree actually was
// before amendment 06.

#include <cstdint>

namespace heinzel::fixture {

// Short, so the gate has something to NOT complain about in the same file.
constexpr std::int32_t untouched() {
    return 0;
}

// This one grew.
constexpr std::int32_t a_dispatcher_that_grew_a_case_at_a_time(std::int32_t which, std::int32_t x) {
    std::int32_t answer = 0;
    if (which == 1) {
        answer = x + 1;
    }
    if (which == 2) {
        answer = x + 2;
    }
    if (which == 3) {
        answer = x + 3;
    }
    if (which == 4) {
        answer = x + 4;
    }
    if (which == 5) {
        answer = x + 5;
    }
    if (which == 6) {
        answer = x + 6;
    }
    if (which == 7) {
        answer = x + 7;
    }
    if (which == 8) {
        answer = x + 8;
    }
    if (which == 9) {
        answer = x + 9;
    }
    if (which == 10) {
        answer = x + 10;
    }
    if (which == 11) {
        answer = x + 11;
    }
    if (which == 12) {
        answer = x + 12;
    }
    if (which == 13) {
        answer = x + 13;
    }
    if (which == 14) {
        answer = x + 14;
    }
    if (which == 15) {
        answer = x + 15;
    }
    if (which == 16) {
        answer = x + 16;
    }
    if (which == 17) {
        answer = x + 17;
    }
    if (which == 18) {
        answer = x + 18;
    }
    if (which == 19) {
        answer = x + 19;
    }
    if (which == 20) {
        answer = x + 20;
    }
    if (which == 21) {
        answer = x + 21;
    }
    if (which == 22) {
        answer = x + 22;
    }
    if (which == 23) {
        answer = x + 23;
    }
    return answer;
}

} // namespace heinzel::fixture
