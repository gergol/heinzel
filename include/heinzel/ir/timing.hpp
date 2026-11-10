#pragma once

// When a node reads its operands: what the rule table's timing column means for one node.
//
// The loop tree's planner and lowering ask here which edges constrain an order and how much past a value
// keeps; the reference walk asks which sample to fetch an operand from. None of them knows which operation
// reads the past: this asks the table.

#include <cstddef>
#include <cstdint>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel {

// How many samples back this node reads its operands from, when that is a NUMBER. Zero for almost
// everything, and zero for a signalled lag too — its distance is a signal, so it has no answer here
// and `signalled_tap()` below is the question to ask instead.
constexpr std::int32_t samples_back(const Node& n) {
    if (rules_of(n.op).time != TimeRule::ReadsEarlierSamples) {
        return 0;
    }
    return n.attr.delay.samples;
}

// Where one of a node's operands is read from: an earlier sample (or, for a link, the element before), or this one.
// Per operand, because `_dline` reads its signal from the past and its position from the present.
//
// Asked by the planner to decide which edges constrain the order within a sample. A past read does
// not, because the operand was finished before this sample began, and that is what makes feedback legal.
constexpr bool operand_comes_from_the_past(const Node& n, std::size_t k) {
    switch (rules_of(n.op).time) {
    case TimeRule::SameSample:
        return false;
    case TimeRule::ReadsEarlierSamples:
        // Every operand, from the same earlier sample. A delay of zero samples is not one of these:
        // it reads the present.
        return samples_back(n) > 0;
    case TimeRule::ReadsAtSignalledDepth:
        // The signal is read from the past; the position that says how far back is read NOW, and it
        // therefore has to be finished before this operation can run.
        return k == 0;
    case TimeRule::ReadsTheElementBefore:
        // Operand 1 is read at this sample, but the element before, which was settled before this one needs it: a
        // cycle closes through it as through a lag, so it constrains no order within the sample. Operand 0 does.
        return k == 1;
    case TimeRule::Unclassified:
        fail("heinzel: an operation with no timing rule was asked where it reads its operands from");
    }
    return false;
}

// Does this node read anything from an earlier sample at all?
constexpr bool reads_the_past(const Node& n) {
    for (std::size_t k = 0; k < kMaxNodeInputs; ++k) {
        if (n.inputs[k] != kNoValue && operand_comes_from_the_past(n, k)) {
            return true;
        }
    }
    return false;
}

// ---- the tap a signalled lag asks for ------------------------------------------------------------
//
// A distance that arrives as a signal lands between two samples, so a read at that distance is two
// reads and a blend. This turns the position into the nearer tap and how far toward the further one
// the answer lies. It is about when an operand is read, so it lives here and not in
// `runtime/compute.hpp`; the blend itself is `lerp2` (`runtime/table_gather_element.hpp`).
struct SignalledTap {
    std::int32_t whole{0};  // samples back to the nearer of the two taps
    double fraction{0.0};   // how far from the nearer tap toward the further one, in [0, 1)

    constexpr bool operator==(const SignalledTap&) const = default;
};

// The position, in samples in the past, clamped to `[0, max_delay]`. Clamped rather than trusted,
// because the position is data: nothing bounds it in advance, and a read off the front of the buffer
// would answer a plausible number rather than complain. The clamp is written `!(p > 0)` rather than
// `p < 0` so that a NaN lands at zero instead of reaching the cast, which has no defined answer for it.
constexpr SignalledTap signalled_tap(std::int32_t max_delay, double position) {
    const double ceiling = static_cast<double>(max_delay);
    double p = position;
    if (!(p > 0.0)) {
        p = 0.0;
    } else if (p > ceiling) {
        p = ceiling;
    }
    // `p` is now finite and at least zero, so truncation and flooring are the same thing and no
    // rounding core is needed here.
    const auto whole = static_cast<std::int32_t>(p);
    return SignalledTap{whole, p - static_cast<double>(whole)};
}

constexpr SignalledTap signalled_tap(const Node& n, double position) {
    return signalled_tap(n.attr.signalled_delay.max_delay, position);
}

// ---- the operands of one node, fetched ---------------------------------------------------------
//
// Which samples a node's operands come from, as the reference walk (`tests/reference/interpret.hpp`)
// fetches them. The loop tree uses `samples_back()` and `signalled_tap()` directly, so the two cannot disagree
// about which sample is read (an F32 line at `Normal` and `Fast` splits a float distance in float as it does).
//
// `at(k, back)` is the caller's reader: operand `k` of this node, `back` samples ago, and zero
// before the start of the block.
struct Operands {
    double a{0.0};
    double b{0.0};
    double c{0.0};

    constexpr bool operator==(const Operands&) const = default;
};

template <class Fetch>
constexpr Operands operands_of(const Node& n, Fetch at) {
    if (rules_of(n.op).time == TimeRule::ReadsAtSignalledDepth) {
        // The position is read at this sample and says how far back the signal is read. A distance
        // that lands between two samples is two reads and a blend, so what goes to the meaning is
        // the nearer tap, the further one, and the fraction between them.
        const SignalledTap tap = signalled_tap(n, at(1, 0));
        return Operands{at(0, tap.whole), at(0, tap.whole + 1), tap.fraction};
    }
    const std::int32_t back = samples_back(n);
    return Operands{at(0, back), at(1, back), at(2, back)};
}

} // namespace heinzel
