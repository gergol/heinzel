#pragma once

// Fuses a contraction's multiply and add — or a sum's add and the product it folds — where the schedule asks for it and
// the result's precision grants it. The loop a prefetch runs along is checked against the register point in validation,
// once normalization has found it.

#include <cstdint>
#include <expected>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/lower_stmts.hpp"
#include "heinzel/loops/program.hpp"

namespace heinzel::loops::detail {

// The stage's contraction multiplies and adds in one operation — a freedom its result's precision grants.
constexpr Done contract(const Graph& g, Formed& f) {
    for (Stmt& st : f.stmts) {
        if (st.kind != StmtKind::Fold || st.node < 0 || !graph::is_a_contraction(g.nodes[static_cast<std::size_t>(st.node)])) {
            continue;
        }
        if (!numerics::grants(st.precision).reorder) {
            return std::unexpected("heinzel::compile: the schedule fuses a multiply-add the result's precision does not grant");
        }
        st.fused = true;
        return {};
    }
    return {};
}

// The stage's sum folds the product it alone reads in one multiply-add a point: the fold reads the product's two
// operands, and the product's statement, read by nothing, is left to the compiler to drop.
constexpr Done fuse_the_product(Formed& f) {
    for (Stmt& st : f.stmts) {
        if (st.kind != StmtKind::Fold || st.collapse != AttrKind::Reduce || st.in[0].source != Source::Local) {
            continue;
        }
        const Stmt& product = f.stmts[static_cast<std::size_t>(st.in[0].local)];
        std::int32_t readers = 0;
        for (const Stmt& other : f.stmts) {
            for (const Access& in : other.in) {
                readers += in.source == Source::Local && in.local == st.in[0].local ? 1 : 0;
            }
        }
        if (product.kind != StmtKind::Map || rules_of(product.op).algebra != Algebra::Product || product.store.source != Source::None || readers != 1 ||
            (st.fold != ReduceOp::Sum && st.fold != ReduceOp::Mean)) {
            break;
        }
        if (!numerics::grants(st.precision).reorder) {
            return std::unexpected("heinzel::compile: the schedule fuses a multiply-add the result's precision does not grant");
        }
        st.in[0] = product.in[0], st.in[1] = product.in[1];
        st.fused = true;
        return {};
    }
    return std::unexpected("heinzel::compile: the schedule fuses a multiply-add into a stage whose sum folds no product of its own");
}

} // namespace heinzel::loops::detail
