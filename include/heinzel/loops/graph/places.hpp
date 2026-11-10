#pragma once

// Where each value is made: which stage of a schedule, which statement of it once lowered, and which buffer holds it.
// Also which stages read it, so that whether anything outside some stages reads it, in which case it needs storage, is
// two comparisons. The lowering, `loops/band_layout.hpp` and the band and split passes read it; it decides nothing and
// names no operation.

#include <cstdint>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/schedule.hpp"
#include "heinzel/support/fixed_vector.hpp"

namespace heinzel::loops::graph {

// Per value: the stage producing it, its statement there (known once lowered), the buffer it is stored in (if any),
// and the first and the last stage that read it. A reader in no stage reads it at -1 and the caller at `kMaxStages`,
// past every stage. A view with no statement is not a reader itself, but its readers are the value's; a variable's
// readers are its definition's. Nothing reads it where the first stage is past the last.
struct ValuePlaces {
    std::int32_t stage[kMaxValues];
    std::int32_t stmt[kMaxValues];
    std::int32_t buffer[kMaxValues];
    std::int32_t first_reader[kMaxValues];
    std::int32_t last_reader[kMaxValues];
};

namespace detail {

constexpr void read_from(ValuePlaces& at, std::size_t v, std::int32_t first, std::int32_t last) {
    at.first_reader[v] = first < at.first_reader[v] ? first : at.first_reader[v];
    at.last_reader[v] = last > at.last_reader[v] ? last : at.last_reader[v];
}

} // namespace detail

// The readers are gathered in one pass from the last node to the first. What reads a view with no statement reads it
// at the same sample, so the frontend builds it after the view, and a view's readers are all in when the view is
// reached (`graph_facts_test` holds the result to the definition).
constexpr ValuePlaces where_values_are_made(const Graph& g, const Facts& facts, const fixed_vector<Schedule::Stage, kMaxStages>& stages) {
    ValuePlaces at{};
    for (std::size_t v = 0; v < kMaxValues; ++v) {
        at.stage[v] = at.stmt[v] = at.buffer[v] = -1;
        at.first_reader[v] = static_cast<std::int32_t>(kMaxStages) + 1; // (read by nothing)
        at.last_reader[v] = -2;
    }
    for (std::size_t s = 0; s < stages.size(); ++s) {
        for (const std::int32_t ni : stages[s].nodes) {
            at.stage[static_cast<std::size_t>(g.nodes[static_cast<std::size_t>(ni)].output)] = static_cast<std::int32_t>(s);
        }
    }
    for (const NamedValue& out : g.outputs) {
        detail::read_from(at, static_cast<std::size_t>(out.value), static_cast<std::int32_t>(kMaxStages), static_cast<std::int32_t>(kMaxStages));
    }
    for (std::size_t i = g.nodes.size(); i-- > 0;) {
        const heinzel::Node& n = g.nodes[i];
        const std::size_t made = static_cast<std::size_t>(n.output);
        for (std::size_t k = 0; k < rules_of(n.op).arity; ++k) {
            const std::size_t v = static_cast<std::size_t>(facts.read_as[static_cast<std::size_t>(n.inputs[k])]);
            if (facts.statement[i]) {
                detail::read_from(at, v, at.stage[made], at.stage[made]);
            } else if (at.first_reader[made] <= at.last_reader[made]) {
                detail::read_from(at, v, at.first_reader[made], at.last_reader[made]);
            }
        }
    }
    return at;
}

// Does anything outside stages [lo, hi) read value `v`: a stage before or after them, a node in no stage, or the
// caller? A value needs storage when something outside its own stage reads it.
constexpr bool read_outside(const ValuePlaces& at, std::int32_t v, std::int32_t lo, std::int32_t hi) {
    const std::size_t i = static_cast<std::size_t>(v);
    return at.first_reader[i] <= at.last_reader[i] && (at.first_reader[i] < lo || at.last_reader[i] >= hi);
}
constexpr bool leaves_its_stage(const ValuePlaces& at, std::int32_t v) {
    const std::int32_t home = at.stage[static_cast<std::size_t>(v)];
    return graph::read_outside(at, v, home, home + 1);
}

} // namespace heinzel::loops::graph
