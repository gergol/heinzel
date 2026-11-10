// What the analysis pass requires of the JOIN's attribute and its two operands — `_concat`'s refusals
// and its summed shape (milestone M5, S3i).
//
// Split from `view_inference_test.cpp` under operator amendment 06: that file holds the structural set's
// attribute refusals and is at its 600-line limit. `_concat` is a concern of its own — the FIRST view
// over TWO operands, so its refusals are the only view checks that read a SECOND operand's shape and
// compare the two (an unequal rank, a disagreeing non-joined axis, a symbolic joined axis). It is the
// same concern-split `view_inference2d_test.cpp` / `view_inference_pad_test.cpp` made; the refusals here
// are the pass's, `rearrange_concat_test.cpp` shows the RUN refusing a hand-built node's.
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"

#include "hand_built_graph.hpp"

#include <string_view>

using namespace heinzel;

// ---- the join, a splice of two operands along one outer axis (milestone M5, S3i) -------------------
//
// `_concat` joins TWO operands along one outer axis; that axis's extent becomes the LITERAL SUM of the
// two operands', every other axis being equal (no broadcast — numpy semantics). A good one is accepted
// with the summed shape, and the refusals below then reject something the pass otherwise takes.
static_assert([] {
    Graph g{};
    const auto a = add_value(g, ElemType::F32, Shape{2, 3, 8}, ValueCategory::Input);
    const auto b = add_value(g, ElemType::F32, Shape{3, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Concat;
    k.view.axis = 0;
    const auto out = add_node(g, op::Concat, a, b, kNoValue, k);
    return infer(g).ok && g.values[static_cast<std::size_t>(out)].shape == Shape{5, 3, 8};
}(), "a join of two operands along an outer axis sums that axis and keeps the others");

// The streamed axis is time and cannot be joined — splicing two signals is not something the per-sample
// model can make, the same refusal `_slice`/`_unfold`/`_flip`/`_padc` make of their axes.
static_assert([] {
    Graph g{};
    const auto a = add_value(g, ElemType::F32, Shape{2, 3, 8}, ValueCategory::Input);
    const auto b = add_value(g, ElemType::F32, Shape{2, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Concat;
    k.view.axis = 2; // the streamed axis of a rank-3 value
    add_node(g, op::Concat, a, b, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"_concat joins axis 2; that is the streamed axis, which is time and stays whole"};
}(), "SHOWN REJECTING: joining the streamed axis is refused by name");

// An axis the value does not have is no axis at all.
static_assert([] {
    Graph g{};
    const auto a = add_value(g, ElemType::F32, Shape{2, 3, 8}, ValueCategory::Input);
    const auto b = add_value(g, ElemType::F32, Shape{2, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Concat;
    k.view.axis = 5; // rank is 3
    add_node(g, op::Concat, a, b, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() == std::string_view{"_concat joins axis 5, which this value of rank 3 does not have"};
}(), "SHOWN REJECTING: joining an axis the value does not have is refused");

// A join keeps every axis of both operands, so the two must have the SAME rank — one of rank 3 and one of
// rank 2 cannot line up their axes at all.
static_assert([] {
    Graph g{};
    const auto a = add_value(g, ElemType::F32, Shape{2, 3, 8}, ValueCategory::Input);
    const auto b = add_value(g, ElemType::F32, Shape{3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Concat;
    k.view.axis = 0;
    add_node(g, op::Concat, a, b, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"_concat joins operands of rank 3 and 2; a join needs both of the same rank"};
}(), "SHOWN REJECTING: operands of unequal rank are refused");

// No broadcast across a join: every NON-joined axis must be equal between the two operands. Here axis 0 is
// joined and axis 1 disagrees (3 against 4).
static_assert([] {
    Graph g{};
    const auto a = add_value(g, ElemType::F32, Shape{2, 3, 8}, ValueCategory::Input);
    const auto b = add_value(g, ElemType::F32, Shape{3, 4, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Concat;
    k.view.axis = 0;
    add_node(g, op::Concat, a, b, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"_concat joins axis 0 but its operands disagree on axis 1"};
}(), "SHOWN REJECTING: a non-joined axis the operands disagree on is refused");

// A SYMBOLIC joined axis has no literal sum to write — its output extent would be a sum of two, not a
// single number known here — so `_concat` is literal-only along the joined axis (the `_padc` precedent),
// refused by name; the derived form is a later step.
static_assert([] {
    Graph g{};
    const auto a = add_value(g, ElemType::F32, Shape{Dim{0}, 3, 8}, ValueCategory::Input);
    const auto b = add_value(g, ElemType::F32, Shape{2, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Concat;
    k.view.axis = 0; // the symbolic axis itself is the join
    add_node(g, op::Concat, a, b, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{
                   "_concat joins axis 0, whose size is a sum of two operand extents, not known yet; bind both"};
}(), "SHOWN REJECTING: a symbolic joined axis is refused, naming the way to give it a size");

// M5.7 for `_concat`: a symbolic axis ELSEWHERE passes through, so long as the two operands AGREE on it.
// Here axis 1 is the same unbound symbol in both, and axis 0 (a literal 2 and 3) joins to a literal 5.
static_assert([] {
    Graph g{};
    const auto a = add_value(g, ElemType::F32, Shape{2, Dim{0}, 8}, ValueCategory::Input);
    const auto b = add_value(g, ElemType::F32, Shape{3, Dim{0}, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Concat;
    k.view.axis = 0;
    const auto out = add_node(g, op::Concat, a, b, kNoValue, k);
    const Diagnostic d = infer(g);
    const Shape& s = g.values[static_cast<std::size_t>(out)].shape;
    return d.ok && extent_at(s, 0) == literal_extent(5) && is_symbol(extent_at(s, 1)) &&
           extent_at(s, 2) == literal_extent(8);
}(), "M5.7: a join passes a graph with a symbolic axis elsewhere through, summing only the literal axis it joins");

int main() { return 0; }
