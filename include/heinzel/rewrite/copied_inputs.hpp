#pragma once

// The rewrite layer's rule for a delay line over an input (`x[n - d.at_most(25.0) * ms]`, Plan/28-dsl-proposals/05 D4):
// it reads a copy of the input, which a stage writes into a buffer that keeps the input's past before its block, as a
// computed value's buffer does, carried from run to run. A caller's feed keeps no past, and a read whose distance
// changes reaches, in a run's first samples, both before the run and into it. Read from the feed's kept tail instead,
// those samples run apart as a fixed lag of an input runs them (`loops/run_stage.hpp`), it measured the same to 9 %
// slower over a position that is an input, and 1.5 to 2.3 times slower in a chorus, whose oscillator joined the line's
// stage (`Plan/probes/28-dsl-proposals/dline-input`). The copy changes no number. Compiling makes it first, in place,
// before the rules of `rewritten.hpp`, which read the graph's feedback and would see a line with no plan
// (`compile.hpp`). It names no operation: a delay line is the rule table's `ReadsAtSignalledDepth`, and the copy its one
// row that hands an operand on at the same sample, unchanged.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel::rewrite {
namespace detail {

constexpr bool hands_on_unchanged(const OpRules& r) {
    return r.arity == 1 && r.attr == AttrKind::None && r.shape == ShapeRule::Passthrough && r.time == TimeRule::SameSample;
}

// The copy: the one row that hands its one operand on at the same sample, its type, shape and numbers unchanged.
constexpr OpCode copy_op() {
    std::int32_t found = -1;
    for (std::size_t i = 0; i < kOpCount; ++i) {
        if (detail::hands_on_unchanged(kOpTable[i].rules)) {
            found = found < 0 ? static_cast<std::int32_t>(i) : kOpCount;
        }
    }
    if (found < 0 || found == kOpCount) {
        fail("heinzel::rewrite: the rule table has no one row that hands an operand on unchanged");
    }
    return OpCode{static_cast<std::uint8_t>(found)};
}

// A delay line's signal, where it is an input of time (a Param has none to read back along).
constexpr bool reads_an_input(const Graph& g, const Node& n) {
    if (rules_of(n.op).time != TimeRule::ReadsAtSignalledDepth) {
        return false;
    }
    const Value& signal = g.values[static_cast<std::size_t>(n.inputs[0])];
    return signal.category == ValueCategory::Input && signal.shape.rank >= 1;
}

} // namespace detail

// Every delay line of analysed graph `g` whose signal is an input reads a copy of it instead, one copy an input, made
// just before its first such line. A copy past `kMaxValues` or `kMaxNodes` is refused in that capacity's words
// (`push_value`, `push_node`), as a graph past one is where the frontend finishes it; a graph with no such line is left
// as it is.
constexpr void copy_inputs_of_delay_lines(Graph& g) {
    bool any = false;
    for (const Node& n : g.nodes) {
        any = any || detail::reads_an_input(g, n);
    }
    if (!any) {
        return;
    }
    const OpCode copy = detail::copy_op();
    std::vector<std::int32_t> copy_of(g.values.size(), kNoValue);
    std::vector<Node> nodes;
    for (const Node& n : g.nodes) {
        Node line = n;
        if (detail::reads_an_input(g, n)) {
            const auto signal = static_cast<std::size_t>(n.inputs[0]);
            if (copy_of[signal] == kNoValue) {
                Value held = g.values[signal];
                held.category = ValueCategory::Internal;
                copy_of[signal] = push_value(g, held);
                Node made{.op = copy, .output = copy_of[signal]};
                made.inputs[0] = n.inputs[0];
                nodes.push_back(made);
            }
            line.inputs[0] = copy_of[signal];
        }
        nodes.push_back(line);
    }
    g.nodes.clear();
    for (const Node& n : nodes) {
        push_node(g, n);
    }
}

} // namespace heinzel::rewrite
