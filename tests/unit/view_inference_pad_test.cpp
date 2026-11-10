// What the analysis pass requires of the CONSTANT PAD's attribute — `_padc`'s refusals and its grown
// shape (milestone M5, S3h).
//
// Split from `view_inference_test.cpp` under operator amendment 06: that file holds the structural set's
// attribute refusals and reached its 600-line limit as the pad block was added. `_padc` is a concern of
// its own — the first view whose meaning is a GUARDED read (a margin has no source element), and its
// refusals are distinct (a non-negative amount, a literal-only padded axis). It is the same
// concern-split `view_inference2d_test.cpp` and `rearrange_flip_test.cpp` made for their features; the
// refusals here are the pass's, `rearrange_pad_test.cpp` shows the RUN refusing a hand-built node's.
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"

#include "hand_built_graph.hpp"

#include <string_view>

using namespace heinzel;

// ---- the constant pad, a growth of one outer axis (milestone M5, S3h) -------------------------------
//
// `_padc` grows ONE outer axis to the LITERAL `L + before + after`; the margins read a constant and the
// body the source shifted by `before`. A good one is accepted with the grown shape, and the refusals
// below then reject something the pass otherwise takes. LITERAL-ONLY, the `_slice`/`_unfold` precedent.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Pad;
    k.view.axis = 0;
    k.view.pad_before = 2;
    k.view.pad_after = 1;
    const auto out = add_node(g, op::Pad, x, kNoValue, kNoValue, k);
    return infer(g).ok && g.values[static_cast<std::size_t>(out)].shape == Shape{7, 3, 8};
}(), "a constant pad of an outer axis grows it to L + before + after and keeps the other axes");

// The streamed axis is time and cannot grow — padding it would make an output sample read a
// different-time source, the same refusal `_slice`/`_unfold`/`_flip` make of their axes.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Pad;
    k.view.axis = 2; // the streamed axis of a rank-3 value
    k.view.pad_before = 1;
    add_node(g, op::Pad, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"_padc pads axis 2; that is the streamed axis, which is time and stays whole"};
}(), "SHOWN REJECTING: padding the streamed axis is refused by name");

// An axis the value does not have is no axis at all.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Pad;
    k.view.axis = 5; // rank is 3
    add_node(g, op::Pad, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() == std::string_view{"_padc pads axis 5, which this value of rank 3 does not have"};
}(), "SHOWN REJECTING: padding an axis the value does not have is refused");

// A pad never shrinks, so a negative amount is the attribute contradicting itself before any shape is
// consulted — the same self-contradiction `_slice`'s length and `_unfold`'s window refuse.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Pad;
    k.view.axis = 0;
    k.view.pad_before = -1;
    add_node(g, op::Pad, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() == std::string_view{"_padc pads axis 0 by -1 and 0: a pad amount is at least zero"};
}(), "SHOWN REJECTING: a negative pad amount is refused");

// A SYMBOLIC padded axis has no literal L to grow to `L + before + after` — `_padc` is literal-only
// (S3h), so it is refused by name, the `_slice`/`_unfold` precedent; the derived form is a later step.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{Dim{0}, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Pad;
    k.view.axis = 0; // the symbolic axis itself
    k.view.pad_before = 1;
    add_node(g, op::Pad, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"_padc pads axis 0, whose size is not known yet; bind it or declare its extent"};
}(), "SHOWN REJECTING: a symbolic padded axis is refused, naming the two ways to give it a size");

// M5.7 for `_padc`: a graph carrying a symbolic dimension ELSEWHERE passes through — only the padded
// axis is inspected. Here axis 0 (a literal 4) is padded to 6 while axis 1 stays an unbound symbol.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, Dim{0}, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Pad;
    k.view.axis = 0;
    k.view.pad_before = 1;
    k.view.pad_after = 1;
    const auto out = add_node(g, op::Pad, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    const Shape& s = g.values[static_cast<std::size_t>(out)].shape;
    return d.ok && extent_at(s, 0) == literal_extent(6) && is_symbol(extent_at(s, 1));
}(), "M5.7: a pad passes a graph with a symbolic axis elsewhere through, growing only the literal axis it pads");

int main() { return 0; }
