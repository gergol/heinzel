// What the analysis pass requires of a 2-D VIEW operation's attribute — `_unfld2d`'s refusals and its
// derived frame counts.
//
// Split from `view_inference_test.cpp` under operator amendment 06: that file holds the structural
// set's attribute refusals, and the 2-D unfold turned out to be a concern of its own — a second
// windowed axis doubles every refusal (a window/hop per axis, two distinct ordered axes, two frame
// fits) and the derived path is two `relate()` calls rather than one (S3f-2). It is the same
// concern-split-with-its-feature `rearrange2d_test.cpp` made of the run refusals; the refusals here
// are the pass's, `rearrange2d_test.cpp` shows the RUN refusing the same mistakes for a hand-built node.
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"

#include "hand_built_graph.hpp"

#include <string_view>

using namespace heinzel;

// ---- the 2-D unfold, and the mistakes it refuses (milestone M5, S3f) --------------------------------
//
// `_unfld2d` windows TWO outer axes at once: each of extent L_i becomes N_i=(L_i-K_i)/H_i+1 frames with
// a window axis of K_i beside it, INTERLEAVED, so the rank grows by two. A good one is accepted first —
// axis 0 of extent 6 windowed 2 at hop 2 gives 3 frames, axis 1 of extent 5 windowed 3 at hop 1 gives 3
// — so the shape is [3, 2, 3, 3, 8] and the refusals below reject something the pass otherwise takes.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{6, 5, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Unfold2d;
    k.view.axis = 0;
    k.view.window = 2;
    k.view.hop = 2;
    k.view.axis2 = 1;
    k.view.window2 = 3;
    k.view.hop2 = 1;
    const auto out = add_node(g, op::Unfold2d, x, kNoValue, kNoValue, k);
    return infer(g).ok && g.values[static_cast<std::size_t>(out)].shape == Shape{3, 2, 3, 3, 8};
}(), "a 2-D unfold within both its axes is accepted: each becomes a frame count with a window axis beside it");

// A window or hop below one, on either axis, is the attribute contradicting itself before a shape.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{6, 5, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Unfold2d;
    k.view.axis = 0;
    k.view.window = 2;
    k.view.hop = 2;
    k.view.axis2 = 1;
    k.view.window2 = 0; // the second window is not a frame
    k.view.hop2 = 1;
    add_node(g, op::Unfold2d, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"unfold2d windows 2/0: a window and a hop are each at least one on both axes"};
}(), "SHOWN REJECTING: a window below one on the second axis is refused");

// The two axes must be DISTINCT and ORDERED: a second axis that is not further out than the first frames
// the same axis twice, which is not a 2-D window.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 5, 6, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Unfold2d;
    k.view.axis = 2;
    k.view.window = 2;
    k.view.hop = 1;
    k.view.axis2 = 1; // further IN than the first, not out
    k.view.window2 = 2;
    k.view.hop2 = 1;
    add_node(g, op::Unfold2d, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"unfold2d unfolds axes 2 and 1: each an outer axis, and the second further out than the first"};
}(), "SHOWN REJECTING: a second axis not further out than the first is refused");

// The streamed axis is time and cannot be the second windowed axis, for the reason it cannot be the
// single unfold's: a window over time is a selection of samples this tree's per-sample model cannot make.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 5, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Unfold2d;
    k.view.axis = 0;
    k.view.window = 2;
    k.view.hop = 1;
    k.view.axis2 = 2; // the streamed axis of a rank-3 value
    k.view.window2 = 2;
    k.view.hop2 = 1;
    add_node(g, op::Unfold2d, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"unfold2d unfolds axes 0 and 2: each an outer axis, and the second further out than the first"};
}(), "SHOWN REJECTING: unfolding the streamed axis as the second window is refused");

// A window wider than its axis does not fit — the same per-axis fit the single unfold makes, here on the
// second axis, whose extent of 5 cannot hold a window of 6.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{6, 5, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Unfold2d;
    k.view.axis = 0;
    k.view.window = 2;
    k.view.hop = 2;
    k.view.axis2 = 1;
    k.view.window2 = 6; // wider than the extent of 5
    k.view.hop2 = 1;
    add_node(g, op::Unfold2d, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() == std::string_view{"unfold2d windows 6 of an axis of extent 5"};
}(), "SHOWN REJECTING: a second window wider than its axis is refused");

// A symbolic windowed axis with NO declared frame count has no size to fit the window against and no
// derived extent to stand in for it, so it is refused — the 1-D refusal, per axis. The message now names
// both escapes, as the single unfold's does since S3e-2: bind the axis, or declare its frame count.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{Dim{0}, 5, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Unfold2d;
    k.view.axis = 0;
    k.view.window = 2;
    k.view.hop = 1;
    k.view.axis2 = 1;
    k.view.window2 = 3;
    k.view.hop2 = 1;
    add_node(g, op::Unfold2d, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"unfold2d unfolds axis 0, whose size is not known yet; bind it or declare its frame count"};
}(), "SHOWN REJECTING: an unbound symbolic 2-D axis with no declared frame count is refused");

// ---- the DERIVED 2-D frame count: symbolic axes with declared N_out symbols (S3f-2) -----------------
//
// The 1-D derived count applied to two axes: each symbolic windowed axis declares its own frame symbol
// (`frames` for the first, `frames2` for the second), the pass records `N_i = (L_i - window_i)/hop_i + 1`
// with a `relate()` call per symbolic axis, and bound, both frame axes resolve to the literal counts
// their literal twin has — checked on EVERY value, not only the output. `Dim{0}`/`Dim{2}` are the framed
// axes; `Dim{1}`/`Dim{3}` the declared counts. Axis 0 of six windowed 3 hop 1 → 4 frames; axis 1 of five
// windowed 2 hop 1 → 4 frames, so the interleaved shape is [4, 3, 4, 2, 8].
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{Dim{0}, Dim{2}, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Unfold2d;
    k.view.axis = 0;
    k.view.window = 3;
    k.view.hop = 1;
    k.view.frames = 1;
    k.view.axis2 = 1;
    k.view.window2 = 2;
    k.view.hop2 = 1;
    k.view.frames2 = 3;
    const auto out = add_node(g, op::Unfold2d, x, kNoValue, kNoValue, k);
    return infer(g, Bindings{}.bind(Dim{0}, 6).bind(Dim{2}, 5)).ok &&
           g.values[static_cast<std::size_t>(out)].shape == Shape{4, 3, 4, 2, 8};
}(), "a 2-D unfold whose two frame counts are symbols resolves to its literal twin's shape once bound");

// A 2-D unfold may MIX a literal axis with a symbolic-with-declared-count one — a new possibility to two
// axes, and one the 1-D case never had. Here the first axis is a literal six (→ 4 frames) and the second
// is a symbol bound later with a declared count (→ 4 frames), and the shape resolves the same way.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{6, Dim{2}, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Unfold2d;
    k.view.axis = 0;
    k.view.window = 3;
    k.view.hop = 1; // literal first axis, no frame symbol
    k.view.axis2 = 1;
    k.view.window2 = 2;
    k.view.hop2 = 1;
    k.view.frames2 = 3; // symbolic second axis, declared count
    const auto out = add_node(g, op::Unfold2d, x, kNoValue, kNoValue, k);
    return infer(g, Bindings{}.bind(Dim{2}, 5)).ok &&
           g.values[static_cast<std::size_t>(out)].shape == Shape{4, 3, 4, 2, 8};
}(), "a 2-D unfold mixing a literal axis with a symbolic declared-count one resolves correctly");

// Unbound, the two-symbol graph is well-formed — its frame counts are symbols that have not resolved
// rather than mistakes — and each frame axis comes out a symbol.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{Dim{0}, Dim{2}, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Unfold2d;
    k.view.axis = 0;
    k.view.window = 3;
    k.view.hop = 1;
    k.view.frames = 1;
    k.view.axis2 = 1;
    k.view.window2 = 2;
    k.view.hop2 = 1;
    k.view.frames2 = 3;
    const auto out = add_node(g, op::Unfold2d, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    const Shape& s = g.values[static_cast<std::size_t>(out)].shape;
    return d.ok && is_symbol(extent_at(s, 0)) && is_symbol(extent_at(s, 2));
}(), "an unbound 2-D unfold with two declared frame counts is a valid graph carrying symbols");

// SHOWN REJECTING: ONE axis declares a frame count and the OTHER is symbolic with none. The whole shape
// is handed back and `check_attributes` refuses the node by name at the undeclared axis — and because
// the shape was not produced, NO relation was recorded for the declared axis either
// (`record_derived_extent` relates only when the rank grew). The refusal names the second axis, the one still owing.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{Dim{0}, Dim{2}, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Unfold2d;
    k.view.axis = 0;
    k.view.window = 3;
    k.view.hop = 1;
    k.view.frames = 1; // first axis declares a count
    k.view.axis2 = 1;
    k.view.window2 = 2;
    k.view.hop2 = 1; // second axis symbolic, NO declared count
    add_node(g, op::Unfold2d, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"unfold2d unfolds axis 1, whose size is not known yet; bind it or declare its frame count"};
}(), "SHOWN REJECTING: a 2-D unfold declaring one frame count but not the other is refused, naming the axis");

// SHOWN REJECTING: two 2-D unfolds name ONE frame symbol for one source axis with different windows,
// deriving it two incompatible ways — the same `agreed`-false seam the 1-D twin exercises, reached
// through `record_derived_extent`'s second `relate()` call. Refused while still symbolic.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{Dim{0}, Dim{2}, 8}, ValueCategory::Input);
    OpAttr a{};
    a.kind = AttrKind::View;
    a.view.kind = ViewKind::Unfold2d;
    a.view.axis = 0;
    a.view.window = 3;
    a.view.hop = 1;
    a.view.frames = 1;
    a.view.axis2 = 1;
    a.view.window2 = 2; // N_out2 = (L2 - 1)/1 for symbol 3
    a.view.hop2 = 1;
    a.view.frames2 = 3;
    add_node(g, op::Unfold2d, x, kNoValue, kNoValue, a);
    OpAttr b = a;
    b.view.window2 = 4; // N_out2 = (L2 - 3)/1 for the SAME symbol 3: cannot both hold
    add_node(g, op::Unfold2d, x, kNoValue, kNoValue, b);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"one axis is derived two different ways: at unfold2d and at unfold2d"};
}(), "SHOWN REJECTING: one 2-D frame symbol derived two incompatible ways is refused");


int main() { return 0; }
