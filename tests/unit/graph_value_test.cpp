// The graph as a value.
//
// The property being tested is unusual enough to state: a whole graph must be usable as a template
// argument. That is what lets one graph be handed to the compiler and turned into code, and it
// rules out a large family of otherwise reasonable designs — anything with a pointer, a virtual
// function, or a container that grows.
//
// So the central test here is that `template <Graph G>` accepts one. If any type in the graph
// stopped being a plain fixed-size value, this file would not compile.
#include "heinzel/ir/ir.hpp"

#include <string_view>

using namespace heinzel;

// A graph that exercises every part: a named input, a constant, an operation carrying an attribute,
// and a named output.
constexpr Graph one_delay() {
    Graph g{};
    g.values.push_back(Value{ElemType::F32, Shape{64}, ValueCategory::Input, 0.0});
    g.values.push_back(Value{ElemType::F32, Shape{64}, ValueCategory::Internal, 0.0});

    Node n{};
    n.op = op::FixedDelay;
    n.inputs[0] = 0;
    n.output = 1;
    n.attr.kind = AttrKind::Delay;
    n.attr.delay.samples = 1;
    g.nodes.push_back(n);

    g.inputs.push_back(NamedValue{Name{"x"}, 0});
    g.outputs.push_back(NamedValue{Name{"y"}, 1});
    return g;
}

// ---- the property everything else rests on -------------------------------------------------------
template <Graph G>
constexpr std::size_t node_count() {
    return G.nodes.size();
}

static_assert(node_count<one_delay()>() == 1, "a whole graph is usable as a template argument");

// ---- structure -----------------------------------------------------------------------------------
static_assert(one_delay().values.size() == 2);
static_assert(one_delay().inputs.size() == 1 && one_delay().inputs[0].name.view() == std::string_view{"x"});
static_assert(one_delay().outputs[0].value == 1);
static_assert(one_delay().nodes[0].attr.kind == AttrKind::Delay);
static_assert(one_delay().nodes[0].attr.delay.samples == 1);

// Unused operand slots say so, rather than defaulting to value zero — which would silently be a
// real value.
static_assert(one_delay().nodes[0].inputs[1] == kNoValue && one_delay().nodes[0].inputs[2] == kNoValue);

// ---- equality ------------------------------------------------------------------------------------
// Whole-graph equality is what the check that compilation and execution agree will compare. It has
// to see a real difference and ignore spare capacity.
static_assert(one_delay() == one_delay());

static_assert([] {
    Graph a = one_delay();
    Graph b = one_delay();
    b.nodes[0].attr.delay.samples = 2;
    return !(a == b);
}(), "a different attribute is a different graph");

static_assert([] {
    Graph a = one_delay();
    Graph b = one_delay();
    b.values[0].type = ElemType::F64;
    return !(a == b);
}(), "a different element type is a different graph");

// Two attributes of different kinds do not accidentally compare equal because the fields they use
// happen to be at their defaults.
static_assert([] {
    OpAttr delay{};
    delay.kind = AttrKind::Delay;
    OpAttr view{};
    view.kind = AttrKind::View;
    return !(delay == view);
}(), "the tag is part of what an attribute is");

// ---- symbolic extents ------------------------------------------------------------------------------
static_assert(is_static(one_delay()));

static_assert([] {
    constexpr Dim n{0};
    Graph g = one_delay();
    g.values[0].shape = Shape{n};
    return !is_static(g);
}(), "one symbolic extent anywhere makes the graph not fully known");

int main() { return 0; }
