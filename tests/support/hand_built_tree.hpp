#pragma once

// Building a loop tree by hand — the one spelling of it in the test tree, as `hand_built_graph.hpp` is for graphs
// (`gate_one_spelling`): a node appended as its parent's last kid, accesses, statements, stages. For the tests of the
// loop tree by itself (`loops_ir`, `loops_runner`); it includes only `ir/` and `loops/`, so `loops_alone` admits it.

#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "heinzel/ir/ir.hpp"
#include "heinzel/loops/program.hpp"

namespace hand_built_tree {

using heinzel::Dim;
using heinzel::ElemType;
using heinzel::Extent;
using heinzel::OpCode;
using heinzel::ReduceOp;
using heinzel::Shape;
using namespace heinzel::loops;

// What an analysed graph resolves a node's precision to, unless it asks for another.
inline constexpr heinzel::numerics::Precision kResolved = heinzel::numerics::Precision::Normal;

// Appends `n` as the last kid of `parent` and returns its index.
constexpr std::int32_t add(Program& p, Node n, std::int32_t parent) {
    n.parent = parent;
    const auto at = static_cast<std::int32_t>(p.nodes.size());
    p.nodes.push_back(n);
    if (parent >= 0) {
        std::int32_t* link = &p.nodes[static_cast<std::size_t>(parent)].first_kid;
        while (*link >= 0) {
            link = &p.nodes[static_cast<std::size_t>(*link)].next_sibling;
        }
        *link = at;
    }
    return at;
}

// A buffer access walking buffer axis `axes[k]` along domain axis k (-1: broadcast).
constexpr Access read(std::int32_t buffer, std::initializer_list<std::int32_t> axes, std::int32_t back = 0) {
    Access a{.source = Source::Buffer, .buffer = buffer, .back = back};
    std::size_t k = 0;
    for (const std::int32_t axis : axes) {
        a.buffer_axis[k++] = axis;
    }
    return a;
}
constexpr Access local(std::int32_t s) { return Access{.source = Source::Local, .local = s}; }

constexpr Stmt load(Access from, Access to = {}) {
    return Stmt{.kind = StmtKind::Load, .type = ElemType::F32, .computed_in = ElemType::F32, .in = {from}, .store = to};
}
constexpr Stmt map(OpCode op, Access a, Access b, Access to = {}) {
    return Stmt{.kind = StmtKind::Map, .op = op, .node = 3, .type = ElemType::F32, .computed_in = ElemType::F32, .precision = kResolved, .in = {a, b}, .store = to};
}
constexpr Stmt fold(ReduceOp op, Access a, Access to) {
    return Stmt{.kind = StmtKind::Fold, .node = 4, .type = ElemType::F32, .computed_in = ElemType::F32, .precision = kResolved, .in = {a}, .store = to, .fold = op};
}

// A stage over `axes` holding the last `count` statements, and its Stage node, the root of its tree.
constexpr std::int32_t stage(Program& p, std::initializer_list<Extent> axes, std::int32_t count) {
    Stage st{.first = static_cast<std::int32_t>(p.stmts.size()) - count, .count = count};
    for (const Extent e : axes) {
        st.axes.push_back(e);
    }
    st.root = add(p, Node{.kind = NodeKind::Stage}, -1);
    p.stages.push_back(st);
    return st.root;
}

constexpr Node loop(std::int32_t axis, std::int32_t step, Tail tail = Tail::Clip, std::int32_t strip = -1) {
    return Node{.kind = NodeKind::Loop, .axis = axis, .step = step, .tail = tail, .strip = strip};
}
constexpr Node walker(NodeKind kind, std::int32_t axis, std::int32_t step) { return Node{.kind = kind, .axis = axis, .step = step}; }
constexpr Node leaf(NodeKind kind, std::int32_t stmt) { return Node{.kind = kind, .stmt = stmt}; }
constexpr Node registers(Role role, std::int32_t stmt, std::int32_t link) {
    return Node{.kind = NodeKind::Registers, .stmt = stmt, .role = role, .link = link};
}

constexpr Buffer feed(Shape shape) { return Buffer{.type = ElemType::F32, .shape = shape, .home = Home::Feed, .feed = 0, .value = 0}; }
constexpr Buffer made(Shape shape, std::int32_t value) { return Buffer{.type = ElemType::F32, .shape = shape, .value = value}; }

} // namespace hand_built_tree
