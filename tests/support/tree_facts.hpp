#pragma once

// What the nest tests assert of a program, in the tree's words (`Plan/archive/13-loop-tree/07` §2's migration rule): a
// stage's statements, its walking nodes outermost first (all, or over one axis), what a node is, how many nodes of a
// kind a stage holds, and — at run time — its subtree as `loops::print` prints it.

#include <cstddef>
#include <cstdint>
#include <string>

#include "heinzel/loops/print.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/tree.hpp"
#include "heinzel/support/fail.hpp"
#include "heinzel/support/fixed_vector.hpp"

namespace tree_facts {

namespace loops = heinzel::loops;

// Statement `k` of stage `s` — one past its statements is no statement of it, not the next stage's first.
constexpr const loops::Stmt& stmt(const loops::Program& p, std::size_t s, std::int32_t k) {
    if (s >= p.stages.size() || k < 0 || k >= p.stages[s].count) {
        heinzel::fail("tree_facts::stmt: the stage has no such statement");
    }
    return p.stmts[static_cast<std::size_t>(p.stages[s].first + k)];
}

// One past the last node of stage `s`'s subtree: nodes are numbered depth first, so the subtree is every node from
// the Stage node on that it encloses.
constexpr std::int32_t subtree_end(const loops::Program& p, std::size_t s) {
    const std::int32_t root = p.stages[s].root;
    std::int32_t end = root + 1;
    while (static_cast<std::size_t>(end) < p.nodes.size() && loops::encloses(p, root, end)) {
        ++end;
    }
    return end;
}

// The walking nodes (`Loop`, `Unroll`, `Vector`, `Turns`) of stage `s`, in the order they stand in the tree (pre-order: the
// outermost first, a jammed chain's copies each where it stands).
constexpr heinzel::fixed_vector<loops::Node, 64> walks(const loops::Program& p, std::size_t s) {
    heinzel::fixed_vector<loops::Node, 64> out{};
    for (std::int32_t n = p.stages[s].root + 1; n < subtree_end(p, s); ++n) {
        const loops::Node& node = loops::node_at(p, n);
        if (node.kind == loops::NodeKind::Loop || node.kind == loops::NodeKind::Unroll || node.kind == loops::NodeKind::Vector ||
            node.kind == loops::NodeKind::Turns) {
            out.push_back(node);
        }
    }
    return out;
}

// Walking node `k` of stage `s` — past the last there is none, not a default node that a negated fact would pass.
constexpr loops::Node walk(const loops::Program& p, std::size_t s, std::size_t k) {
    const auto all = walks(p, s);
    if (k >= all.size()) {
        heinzel::fail("tree_facts::walk: the stage has fewer walking nodes");
    }
    return all[k];
}

// The walking nodes of stage `s` over `axis`, in tree order.
constexpr heinzel::fixed_vector<loops::Node, 64> walks_over(const loops::Program& p, std::size_t s, std::int32_t axis) {
    heinzel::fixed_vector<loops::Node, 64> out{};
    for (const loops::Node& w : walks(p, s)) {
        if (w.axis == axis) {
            out.push_back(w);
        }
    }
    return out;
}

// A node of `kind` over `axis` in steps of `step`.
constexpr bool is(const loops::Node& n, loops::NodeKind kind, std::int32_t axis, std::int32_t step) {
    return n.kind == kind && n.axis == axis && n.step == step;
}

// A walking node that steps one point at a time, in order: a Loop of step 1 that is no strip.
constexpr bool serial(const loops::Node& n) { return n.kind == loops::NodeKind::Loop && n.step == 1 && n.tail == loops::Tail::Clip; }

// How many nodes of kind `k` stage `s` holds.
constexpr std::int32_t count(const loops::Program& p, std::size_t s, loops::NodeKind k) {
    std::int32_t c = 0;
    for (std::int32_t n = p.stages[s].root + 1; n < subtree_end(p, s); ++n) {
        c += loops::node_at(p, n).kind == k ? 1 : 0;
    }
    return c;
}

// Stage `s`'s subtree as `loops::print` prints it, its Stage node at depth 0.
inline std::string listing(const loops::Program& p, std::size_t s) {
    std::string out;
    const std::int32_t root = p.stages[s].root;
    for (std::int32_t n = root; n < subtree_end(p, s); ++n) {
        std::size_t depth = 0;
        for (std::int32_t at = n; at != root; at = loops::node_at(p, at).parent) {
            ++depth;
        }
        loops::detail::put_node(out, p, static_cast<std::size_t>(n), depth);
    }
    return out;
}

} // namespace tree_facts
