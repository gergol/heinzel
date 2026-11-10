#pragma once

// How much of a series a function-accuracy tier evaluates: the one place a tier becomes a number. Every
// approximate core is a reduction followed by a polynomial, and the polynomial is where a tier is spent:
// fewer terms is cheaper and coarser. Each series declares its lengths beside its coefficients, where the
// bound each delivers is argued from the dropped term; only the choosing lives here, in one switch rather
// than one per core, because a switch per series picks up a wrong arm when a tier is added.

#include "heinzel/ir/vocab.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel {

// How many terms of one series each tier keeps. `exact` is the length the core was written at;
// `approx` is shorter, and its bound is argued where it is declared.
struct SeriesLength {
    int exact{0};
    int approx{0};

    constexpr bool operator==(const SeriesLength&) const = default;
};

// The tier, applied. `Inherit` is refused rather than treated as `Exact`: reaching a core with it
// means the resolution against the graph never happened, and answering the most accurate thing
// would hide that — the graph would compute the right numbers while its setting was being ignored.
constexpr int terms_at(SeriesLength lengths, FnAccuracy fn) {
    switch (fn) {
    case FnAccuracy::Exact:
        return lengths.exact;
    case FnAccuracy::Approx:
        return lengths.approx;
    case FnAccuracy::Inherit:
        break;
    }
    fail("heinzel: a core was asked for FnAccuracy::Inherit, which is not a tier but a request");
}

// A length that is not shorter at a coarser tier is a tier that does not exist. Checked here rather
// than left to each declaration, so a table with a typo in it cannot be committed. Only the order is
// checked: a length against the array it indexes is a fact about that array, so each core asserts it
// beside its own coefficients, with the floor its Horner loop needs (`exponential_minus_one_of_nonnegative`
// takes its first two terms out of the loop and cannot be shortened below three).
constexpr bool is_a_ladder(SeriesLength lengths) {
    return lengths.exact > lengths.approx && lengths.approx > 0;
}

} // namespace heinzel
