// What the analysis pass requires of a VIEW operation's attribute — the structural set's refusals.
//
// Split from `inference_test.cpp` under operator amendment 06: that file works out every value's
// shape and element type, and a view's attribute is a different question, asked of the same pass but
// about the permutation or the window the view carries rather than about a shape relation. Milestone
// M5 adds a view per step (`_xpose`, `_slice`, and `_flip`/`_pad`/`_concat` to come), each with its
// own attribute refusals shown rejecting — so this is the concern that file "turned out to hold", and
// it lives here rather than growing `inference_test.cpp` a view at a time.
//
// The refusals here are the pass's; `rearrange_test.cpp` shows the RUN refusing the same mistakes for
// a hand-built node the pass never saw.
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"

#include "hand_built_graph.hpp"

#include <string_view>

using namespace heinzel;

// ---- the transpose's order, and the two mistakes it refuses (milestone M5, the structural set) -----
//
// A transpose carries a permutation of the value's axes. The pass checks two things about it, in two
// conditions because they are two different mistakes, each with its own message. First a good one is
// accepted so the refusals below are shown to reject something the pass otherwise takes.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{2, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Transpose;
    k.view.order = Axes{1, 0, 2}; // swap the two outer axes, keep the streamed one
    const auto out = add_node(g, op::Transpose, x, kNoValue, kNoValue, k);
    return infer(g).ok && g.values[static_cast<std::size_t>(out)].shape == Shape{3, 2, 8};
}(), "a permutation that keeps the streamed axis is accepted, and the extents follow it");

// An order that is not a permutation of the rank — an axis named twice, another left out — cannot
// make a shape at all, so it is refused by name rather than read.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{2, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Transpose;
    k.view.order = Axes{0, 0, 2}; // axis 0 twice, axis 1 never: not a permutation
    add_node(g, op::Transpose, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"_xpose over rank 3: an order must name every axis of it exactly once"};
}(), "SHOWN REJECTING: an order that is not a permutation is refused, naming the operation and its rank");

// And a permutation that would move the STREAMED axis — a perfectly good permutation and a thing a
// run here cannot do — is a separate refusal with its own message, so the two mistakes never share one
// complaint.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{2, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Transpose;
    k.view.order = Axes{2, 1, 0}; // a permutation, but it sends the innermost axis to the front
    add_node(g, op::Transpose, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"_xpose moves the streamed axis to 0; the innermost axis is time and stays where it is"};
}(), "SHOWN REJECTING: a permutation that moves the streamed axis is refused by name, not run along another");

// ---- the slice's window, and the four mistakes it refuses (milestone M5, the structural set) --------
//
// A slice takes `length` elements of one axis from `offset` at stride `step`. A good one is accepted
// first — its axis shortens to `length`, the others follow — so the refusals below reject something the
// pass otherwise takes.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Slice;
    k.view.axis = 0;
    k.view.offset = 0;
    k.view.length = 2;
    k.view.step = 2; // source indices 0 and 2
    const auto out = add_node(g, op::Slice, x, kNoValue, kNoValue, k);
    return infer(g).ok && g.values[static_cast<std::size_t>(out)].shape == Shape{2, 3, 8};
}(), "a slice within its axis is accepted, and only that axis shortens");

// A stride below one is a reversal, which is `_flip` — the negative-stride exclusion, named where the
// graph would reach for it.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Slice;
    k.view.axis = 0;
    k.view.offset = 0;
    k.view.length = 2;
    k.view.step = 0;
    add_node(g, op::Slice, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"slice steps by 0; a stride below one is a reversal, which is _flip"};
}(), "SHOWN REJECTING: a negative or zero stride is refused, naming _flip as the workaround");

// A length below one is an empty axis, and an offset below zero reads before the source — the attribute
// contradicting itself, before a shape is consulted.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Slice;
    k.view.axis = 0;
    k.view.offset = 0;
    k.view.length = 0;
    k.view.step = 1;
    add_node(g, op::Slice, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"slice takes 0 from 0: a slice keeps at least one, starting at zero or more"};
}(), "SHOWN REJECTING: a length below one is refused");

// The streamed axis is time and cannot be sliced — a slice of it is a selection of samples this tree's
// per-sample model cannot make, exactly as a transpose cannot move it.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Slice;
    k.view.axis = 2; // the streamed axis of a rank-3 value
    k.view.offset = 0;
    k.view.length = 2;
    k.view.step = 1;
    add_node(g, op::Slice, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"slice slices axis 2; that is the streamed axis, which is time and stays whole"};
}(), "SHOWN REJECTING: slicing the streamed axis is refused by name");

// And an axis the value does not have at all is a different mistake with its own message.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Slice;
    k.view.axis = 5; // a rank-3 value has no axis 5
    k.view.offset = 0;
    k.view.length = 2;
    k.view.step = 1;
    add_node(g, op::Slice, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"slice slices axis 5, which this value of rank 3 does not have"};
}(), "SHOWN REJECTING: slicing an axis the value does not have is refused by name");

// A window that reaches past the source axis reads outside it: offset 3, stride 1, so the second
// element asks for index 4 of an axis of extent 4.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Slice;
    k.view.axis = 0;
    k.view.offset = 3;
    k.view.length = 2;
    k.view.step = 1;
    add_node(g, op::Slice, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() == std::string_view{"slice reads index 4 of an axis of extent 4"};
}(), "SHOWN REJECTING: a window that reaches past the source axis is refused");

// A symbolic sliced axis with no binding has no size to fit the window against — this tree binds up
// front, so an unbound one was never bound, and slicing it is refused rather than run.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{Dim{0}, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Slice;
    k.view.axis = 0;
    k.view.offset = 0;
    k.view.length = 2;
    k.view.step = 1;
    add_node(g, op::Slice, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"slice slices axis 0, whose size is not known yet; bind it before slicing"};
}(), "SHOWN REJECTING: an unbound symbolic sliced axis is refused");

// And a symbolic axis BOUND too small is caught by the same window check, because the sweep resolves
// it to that size first — so the refusal names `slice` and the size that broke it.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{Dim{0}, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Slice;
    k.view.axis = 0;
    k.view.offset = 0;
    k.view.length = 2;
    k.view.step = 2; // last index 2, which does not fit an axis bound to 2
    add_node(g, op::Slice, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g, Bindings{}.bind(Dim{0}, 2));
    return !d.ok &&
           d.message.view() == std::string_view{"slice reads index 2 of an axis of extent 2"};
}(), "SHOWN REJECTING: a symbolic sliced axis bound too small is refused, naming slice");

// ---- the unfold's window, and the mistakes it refuses (milestone M5, the structural set) ------------
//
// An unfold windows one OUTER axis: extent L becomes N_out=(L-K)/H+1 frames, and a window axis of K
// elements appears at `axis + 1`, so the rank grows by one. A good one is accepted first — its axis
// becomes the frame count, a window axis follows, the rest are untouched — so the refusals below reject
// something the pass otherwise takes.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{6, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Unfold;
    k.view.axis = 0;
    k.view.window = 3;
    k.view.hop = 1; // (6-3)/1+1 = 4 frames of window 3
    const auto out = add_node(g, op::Unfold, x, kNoValue, kNoValue, k);
    return infer(g).ok && g.values[static_cast<std::size_t>(out)].shape == Shape{4, 3, 3, 8};
}(), "an unfold within its axis is accepted: the axis becomes the frame count and a window axis follows");

// A window or hop below one is not a frame or a stride — the attribute contradicting itself before a
// shape is consulted.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{6, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Unfold;
    k.view.axis = 0;
    k.view.window = 0;
    k.view.hop = 1;
    add_node(g, op::Unfold, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"unfold windows 0 every 1: a window and a hop are each at least one"};
}(), "SHOWN REJECTING: a window below one is refused");

// MF.2 (representation A, Decision 44 — the multi-rate / frame-stream model): the streamed axis CAN now
// be unfolded, and doing so frames the stream. Unlike an outer-axis unfold, the window tap becomes a
// static element axis placed OUTER and the frame count `W=(N-K)/H+1` becomes the NEW innermost/streamed
// extent — the block the graph runs at. So the streamed axis 2 of extent 8, windowed 2 every 2, frames
// to `[4, 3, 2, 4]`: the outer axes untouched, the window (2) as an element axis, and W=(8-2)/2+1=4 the
// new stream. The reversed window/frame order versus the outer-axis case above is the layout A fixes.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Unfold;
    k.view.axis = 2; // the streamed axis of a rank-3 value — now a frame-stream, not a refusal
    k.view.window = 2;
    k.view.hop = 2; // (8-2)/2+1 = 4 frames of window 2
    const auto out = add_node(g, op::Unfold, x, kNoValue, kNoValue, k);
    return infer(g).ok && g.values[static_cast<std::size_t>(out)].shape == Shape{4, 3, 2, 4};
}(), "MF.2: unfolding the streamed axis frames it — window becomes an element axis, W the new stream");

// The window still has to fit the streamed axis: a window wider than the block is refused BY NAME,
// exactly as it is for an outer axis — the lift admits framing the stream, it does not admit a window
// that does not fit.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Unfold;
    k.view.axis = 2; // the streamed axis
    k.view.window = 9; // wider than the block of 8
    k.view.hop = 1;
    add_node(g, op::Unfold, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"unfold windows 9 of an axis of extent 8"};
}(), "SHOWN REJECTING: a window wider than the streamed block is refused by name");

// The other stream guards survive framing: after the streamed axis is framed to `[4, 3, 2, 4]`, its
// NEW innermost axis `W` is the stream, and a transpose that moves it is refused by name exactly as it
// is for an unframed stream. The frame-stream lift admits ONE view of the innermost axis (windowing it);
// it does not weaken the refusal of moving or reordering whatever axis streams.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 3, 8}, ValueCategory::Input);
    OpAttr uf{};
    uf.kind = AttrKind::View;
    uf.view.kind = ViewKind::Unfold;
    uf.view.axis = 2;
    uf.view.window = 2;
    uf.view.hop = 2; // frames the stream to [4, 3, 2, 4], W=4 innermost
    const auto framed = add_node(g, op::Unfold, x, kNoValue, kNoValue, uf);
    OpAttr tp{};
    tp.kind = AttrKind::View;
    tp.view.kind = ViewKind::Transpose;
    tp.view.order = Axes{3, 1, 2, 0}; // sends the new streamed axis (W, index 3) to the front
    add_node(g, op::Transpose, framed, kNoValue, kNoValue, tp);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"_xpose moves the streamed axis to 0; the innermost axis is time and stays where it is"};
}(), "SHOWN REJECTING: after framing, moving the new streamed axis W is still refused by name");

// An axis the value does not have at all is a different mistake with its own message.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Unfold;
    k.view.axis = 5; // a rank-3 value has no axis 5
    k.view.window = 2;
    k.view.hop = 1;
    add_node(g, op::Unfold, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"unfold unfolds axis 5, which this value of rank 3 does not have"};
}(), "SHOWN REJECTING: unfolding an axis the value does not have is refused by name");

// Adding the window axis must not exceed the rank ceiling: a rank-6 value unfolded would be rank 7,
// past `kMaxRank`.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{2, 2, 2, 2, 2, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Unfold;
    k.view.axis = 0;
    k.view.window = 2;
    k.view.hop = 1;
    add_node(g, op::Unfold, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"unfold unfolds a value of rank 6; a window axis would exceed the rank ceiling"};
}(), "SHOWN REJECTING: an unfold that would push the rank past kMaxRank is refused");

// A symbolic windowed axis with no binding AND no declared frame count has no size to fit the window
// against and no derived extent to stand in for it, so it is refused rather than run. The refusal now
// names both escapes: bind the axis (the literal path), or declare a frame count (the derived path).
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{Dim{0}, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Unfold;
    k.view.axis = 0;
    k.view.window = 2;
    k.view.hop = 1;
    add_node(g, op::Unfold, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"unfold unfolds axis 0, whose size is not known yet; bind it or declare its frame count"};
}(), "SHOWN REJECTING: an unbound symbolic windowed axis with no declared frame count is refused");

// ---- the DERIVED frame count: a symbolic windowed axis with a declared N_out (S3e-2) ----------------
//
// The lift the refusal above names: the author declares a symbol for the frame count and the pass
// records `N_out = (L - window)/hop + 1` from the source. Bound, the frame axis resolves to the literal
// count its literal twin has — checked here on every value, not only the output. `Dim{0}` is the framed
// axis L, `Dim{1}` the declared frame count N_out.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{Dim{0}, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Unfold;
    k.view.axis = 0;
    k.view.window = 3;
    k.view.hop = 1; // (6-3)/1+1 = 4 frames of window 3, once L is bound to 6
    k.view.frames = 1; // the declared N_out symbol
    const auto out = add_node(g, op::Unfold, x, kNoValue, kNoValue, k);
    return infer(g, Bindings{}.bind(Dim{0}, 6)).ok &&
           g.values[static_cast<std::size_t>(out)].shape == Shape{4, 3, 3, 8};
}(), "a symbolic windowed axis with a declared frame count resolves to its literal twin's shape once bound");

// Unbound, the same graph is well-formed — its frame count is a symbol that has not resolved rather
// than a mistake — exactly as an unbound biquad still carries its block length. The frame axis comes
// out a symbol; the input axis it derives from does too.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{Dim{0}, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Unfold;
    k.view.axis = 0;
    k.view.window = 3;
    k.view.hop = 1;
    k.view.frames = 1;
    const auto out = add_node(g, op::Unfold, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return d.ok && is_symbol(extent_at(g.values[static_cast<std::size_t>(out)].shape, 0));
}(), "an unbound symbolic windowed axis with a declared frame count is a valid graph carrying a symbol");

// SHOWN REJECTING: two unfolds naming ONE frame-count symbol for one source axis with different
// windows derive that axis two incompatible ways — the derived-extent algebra names both operations
// rather than carry a size that cannot be both. This is the derived path's twin of M5.3's "two
// consumers of one meaning" disagreement, and it exercises the frame relation flowing through `infer`
// (the `agreed`-false seam `record_derived_extent` reaches), not `relate` in isolation. Refused while
// still symbolic — no binding needed, the two affine relations cannot coexist.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{Dim{0}, 3, 8}, ValueCategory::Input);
    OpAttr a{};
    a.kind = AttrKind::View;
    a.view.kind = ViewKind::Unfold;
    a.view.axis = 0;
    a.view.window = 3; // N_out = (L - 2)/1
    a.view.hop = 1;
    a.view.frames = 1;
    add_node(g, op::Unfold, x, kNoValue, kNoValue, a);
    OpAttr b = a;
    b.view.window = 5; // N_out = (L - 4)/1 for the SAME frame symbol: cannot both hold
    add_node(g, op::Unfold, x, kNoValue, kNoValue, b);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"one axis is derived two different ways: at unfold and at unfold"};
}(), "SHOWN REJECTING: one frame symbol derived two incompatible ways by two unfolds is refused");

// A window wider than the axis does not fit — offset none, the window alone reaches past the extent.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Unfold;
    k.view.axis = 0;
    k.view.window = 5; // wider than the extent of 4
    k.view.hop = 1;
    add_node(g, op::Unfold, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() == std::string_view{"unfold windows 5 of an axis of extent 4"};
}(), "SHOWN REJECTING: a window wider than its axis is refused");

// ---- the flip, a reversal of one outer axis (milestone M5, S3g) -------------------------------------
//
// `_flip` reverses one OUTER axis and changes no extent, so a good one is accepted with the SAME shape
// it was given — the refusals below then reject something the pass otherwise takes.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Flip;
    k.view.axis = 0;
    const auto out = add_node(g, op::Flip, x, kNoValue, kNoValue, k);
    return infer(g).ok && g.values[static_cast<std::size_t>(out)].shape == Shape{4, 3, 8};
}(), "a flip of an outer axis is accepted and keeps the shape it was given");

// The streamed axis is time and cannot be reversed — reversing time is not a per-sample operation, the
// same refusal `_slice` and `_unfold` make of their axes.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Flip;
    k.view.axis = 2; // the streamed axis of a rank-3 value
    add_node(g, op::Flip, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"_flip flips axis 2; that is the streamed axis, which is time and stays whole"};
}(), "SHOWN REJECTING: flipping the streamed axis is refused");

// An axis the value does not have is no axis at all.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Flip;
    k.view.axis = 5; // rank is 3
    add_node(g, op::Flip, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() == std::string_view{"_flip flips axis 5, which this value of rank 3 does not have"};
}(), "SHOWN REJECTING: flipping an axis the value does not have is refused");

// M5.7 for `_flip`: a graph that carries a symbolic dimension ELSEWHERE passes through — a flip keeps
// the extent, so it needs no size to reverse and rejects only the axis it cannot handle. Here axis 0 is
// flipped (a bound literal) while axis 1 is a symbol left unbound; the graph is well-formed and its
// flipped axis stays the literal it was, its symbolic axis stays a symbol.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, Dim{0}, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Flip;
    k.view.axis = 0;
    const auto out = add_node(g, op::Flip, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    const Shape& s = g.values[static_cast<std::size_t>(out)].shape;
    return d.ok && extent_at(s, 0) == literal_extent(4) && is_symbol(extent_at(s, 1));
}(), "M5.7: a flip passes a graph with a symbolic axis elsewhere through, rejecting only the axis it cannot handle");

// A flip may reverse a SYMBOLIC axis itself — the extent is preserved, so there is nothing to know about
// its size; the flipped axis stays a symbol and the graph is well-formed.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{Dim{0}, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Flip;
    k.view.axis = 0; // the symbolic axis itself
    const auto out = add_node(g, op::Flip, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return d.ok && is_symbol(extent_at(g.values[static_cast<std::size_t>(out)].shape, 0));
}(), "a flip of a symbolic axis is well-formed: the extent is preserved, no size is needed to reverse it");

// SHOWN REJECTING at bind time: a declared frame count whose source is bound too SMALL for even one
// window works out to zero frames, and the derived-extent algebra names it where the arithmetic
// happens. Observed from a run rather than a static_assert, because under constant evaluation that
// `fail()` is the compile error itself and cannot be caught.
static bool a_frame_count_that_underflows_is_refused_at_bind_time() {
    try {
        Graph g{};
        const auto x = add_value(g, ElemType::F32, Shape{Dim{0}, 3, 8}, ValueCategory::Input);
        OpAttr k{};
        k.kind = AttrKind::View;
        k.view.kind = ViewKind::Unfold;
        k.view.axis = 0;
        k.view.window = 3;
        k.view.hop = 1;
        k.view.frames = 1;
        add_node(g, op::Unfold, x, kNoValue, kNoValue, k);
        (void)infer(g, Bindings{}.bind(Dim{0}, 2)); // L = 2 < window 3: no frame fits
        return false;
    } catch (const error& e) {
        return e.message() ==
               std::string_view{"heinzel::ExtentSolver: a derived extent works out to zero or fewer elements"};
    }
}

int main() { return a_frame_count_that_underflows_is_refused_at_bind_time() ? 0 : 1; }
