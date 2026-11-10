#pragma once

// What a stage costs a sample, the planner's one model of it (Plan/23 04): the most of its chain (a state's recurrence,
// in latencies) and its issue (its operations' slots over what the machine issues a cycle), taken together where they
// come close (`busy`), its loads and its stores, and the window bound (one sample's own path, `depth`, overlaps the next
// samples' only as far as the out-of-order window holds their slots); a stage in lanes along time runs `lanes` samples
// at once, voices in lanes take `bodies` lane bodies a sample; and at least the bytes it moves through memory past the
// last-level cache. Validated against measured programs (`Plan/probes/audio-iir/cost_corpus*.cpp`). An operation's cost
// is its rule-table class's (`Latency`), never its name's. And what a stage that runs once a run costs, against
// checking whether its Params changed (`once`, `check`, `kept`; `docs/knob-math.md`).

#include <bit>
#include <cstdint>

#include "heinzel/ir/op_rules.hpp"
#include "heinzel/schedule/machine.hpp"

namespace heinzel::cost {

// What operation `op` costs computed in `type`, on machine `m`.
constexpr OpCost of(OpCode op, ElemType type, const Machine& m) {
    const bool wide = type == ElemType::F64;
    switch (rules_of(op).latency) {
    case Latency::Add:
        return m.add;
    case Latency::MultiplyAdd:
        return m.multiply_add;
    case Latency::Divide:
        return wide ? m.divide_double : m.divide;
    case Latency::Root:
        return wide ? m.root_double : m.root;
    case Latency::Load:
        return {m.forwarded, 0};
    case Latency::Core:
        // A stand-in (Decision 78, D2): a transcendental with a writing but no float core (tan, pow, exp2:
        // `has_a_writing_over_the_value_type`, not `has_a_core_in_the_values_own_type`) runs its double core one
        // number at a time, many times `m.core` in a lane of floats, yet is priced as a float core is. Its plan is
        // correct, only not chosen by its real cost.
        return wide ? m.core_double : m.core;
    default:
        return {0, 0};
    }
}

// A fold of `bodies` lane bodies of `lanes` lanes each into one number, as a chain waits on it: the bodies added, then
// log2(lanes) steps across the lane, a shuffle and an add each; `add` is its add's latency.
constexpr double lanes_folded(double add, std::int64_t lanes, std::int64_t bodies, const Machine& m) {
    const double steps = static_cast<double>(std::bit_width(static_cast<std::uint64_t>(lanes)) - 1);
    return static_cast<double>(bodies - 1) * add + steps * (m.shuffle.latency + add);
}

// A stage's counts, a sample of one lane body; and the cycles a sample they cost. `bytes`: what its loads and stores
// move a sample, all its voices, through memory past the last-level cache (`Machine::memory_bytes`).
struct Stage {
    double chain{0};
    double depth{0};
    double slots{0};
    double loads{0};
    double stores{0};
    double bodies{1};
    double lanes{1};
    double bytes{0};
};

// A chain and an issue bound together: their p-norm, p = `m.contention`. Where one dwarfs the other, about it; where
// they meet, more than either (two of 6 cycles cost zen2 6.5), since the chain's operations wait behind the work issued
// beside them. (1 + r^p)^(1/p) by Newton's method from its tangent at r = 0, within a part in 1e15 after five steps.
constexpr double busy(double chain, double issue, const Machine& m) {
    const double hi = chain > issue ? chain : issue;
    const double lo = chain > issue ? issue : chain;
    const std::int32_t p = m.contention;
    if (p <= 0 || hi <= 0) {
        return hi;
    }
    const auto power = [p](double x, std::int32_t less) {
        double out = 1;
        for (std::int32_t k = less; k < p; ++k) {
            out *= x;
        }
        return out;
    };
    const double sum = 1 + power(lo / hi, 0);
    double x = 1 + (sum - 1) / p;
    for (int step = 0; step < 5; ++step) {
        x -= (power(x, 0) - sum) / (p * power(x, 1));
    }
    return hi * x;
}

constexpr double cycles(const Stage& s, const Machine& m) {
    const double slots = s.slots * s.bodies;
    const double window = s.lanes > 1 ? 0 : s.depth * slots / m.window;
    double most = busy(s.chain, slots / m.fp_per_cycle, m);
    for (const double bound : {s.loads * s.bodies / m.loads_per_cycle, s.stores * s.bodies / m.stores_per_cycle, window}) {
        most = bound > most ? bound : most;
    }
    const double memory = m.memory_bytes > 0 ? s.bytes / m.memory_bytes : 0;
    return most / s.lanes > memory ? most / s.lanes : memory;
}

// A stage that runs once a run, not once a sample: work of no axis from Params and constants alone, such as a filter's
// coefficients worked out from its knobs, ahead of every loop. It costs what a sample of a loop that
// carries nothing would: its path and its issue together (`busy`), the path from the loads it starts with, its Params,
// which the run has just copied into its frame (forwarded; a loop's loads run ahead of its samples, so `depth` leaves
// them out). What the work around it hides the model does not see: on zen2 this price is 0.8 to 6.4 times what
// skipping the stage saved, yet against `check` it decides as measured (`knob_price_test`).
constexpr double once(const Stage& s, const Machine& m) {
    return busy(m.forwarded + s.depth, s.slots / m.fp_per_cycle, m);
}

// What it costs a run to check whether such a stage's Params changed: two loads a Param it watches (its bits, and the
// bits the stage last ran with) and one branch, a cycle: `Machine` has no figure for a branch, and a cycle is how the
// price's probe scored the decision. Measured at rest on zen2 (`docs/knob-math.md`): on `Executor`, which copies the
// Params into its frame, well under it (24 Params in one stage: 3.6 cycles a run against 25); through `loops::run`, the
// Params read through the caller's pointers, 0.7 to 1.7 times it.
constexpr double check(std::int32_t watched, const Machine& m) {
    return 2.0 * static_cast<double>(watched) / m.loads_per_cycle + 1;
}

// Is such a stage kept: run only in a run where a Param it watches changed, its results kept in between?
// Where it costs more than the check, with no margin: on the probe's corpus the two decide as measured in 83 of 84, and
// the one miss is within its row's noise.
constexpr bool kept(const Stage& s, std::int32_t watched, const Machine& m) {
    return once(s, m) > check(watched, m);
}

} // namespace heinzel::cost
