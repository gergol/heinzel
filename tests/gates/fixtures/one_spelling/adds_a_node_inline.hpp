#pragma once

// A committed offender for the one-spelling gate, included by nothing. It spells a graph the way the
// gate forbids: a program that adds to a graph inline instead of writing the language or calling the
// shared hand-built helper. The gate is pointed here and observed to OBJECT; a gate that only ever
// passes is indistinguishable from one that never runs.

#include "heinzel/ir/ir.hpp"

namespace heinzel::one_spelling_fixture {

constexpr Graph a_graph_built_the_wrong_way() {
    Graph g{};
    g.values.push_back(Value{ElemType::F32, Shape{4}, ValueCategory::Input, 0.0});
    Node n{};
    n.op = op::Neg;
    n.inputs[0] = 0;
    n.output = 0;
    g.nodes.push_back(n);
    return g;
}

} // namespace heinzel::one_spelling_fixture
