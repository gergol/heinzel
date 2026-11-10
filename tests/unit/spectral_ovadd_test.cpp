// What the analysis pass makes of an OVERLAP-ADD — its collapsed shape and its refusals, and the run-side
// scatter it reads (milestone M5, S4c).
//
// `_ovadd` is the ADJOINT of `_unfold`: it takes `[.., F, K]` — F frames of K taps — back to one axis
// `[.., out_len]` with `out_len = (F-1)*hop + window`, each output position the SUM of the overlapping
// frames' taps. Its two innermost element axes collapse into one, which no view expresses, so it
// is a full operation with `ShapeRule::OverlapAddAxes` rather than a view. This file checks the collapsed
// shape, the refusals its attribute owes SHOWN REJECTING deliberately broken inputs, the M5.7 pass-through
// of a symbolic axis elsewhere, and the run-side windowed scatter its reader computes.
//
// Named with the S4 spectral set (`spectral_fft`) because `_ovadd` exists to serve the
// spectral path — reassembling overlapping frames after an inverse transform — though the operation itself
// is a pure reindex-and-sum and runs to numbers here (unlike `_fft`). The M5.3/M5.4 numeric twin — a
// SYMBOLIC frame count executed bit-identically to its literal twin — is `symbolic_test.cpp`'s graph six.
#include "heinzel/sema/analyze.hpp"

#include "elements.hpp"
#include "hand_built_graph.hpp"
#include "overlap_add_element.hpp"

#include <string_view>

using namespace heinzel;

// A node carrying an overlap-add of one operand: `window` taps a frame, `hop` apart. Its own helper so each
// case below states only what it varies.
namespace {
constexpr OpAttr overlap_attr(std::int32_t window, std::int32_t hop) {
    OpAttr k{};
    k.kind = AttrKind::OverlapAdd;
    k.overlap.window = window;
    k.overlap.hop = hop;
    return k;
}
} // namespace

// ---- the collapsed shape (milestone M5, S4c) --------------------------------------------------------
//
// A good overlap-add of `{3, 4, 8}` — three frames (F), four taps (K, the innermost element axis), a
// streamed axis of 8 — with window 4 and hop 2: the two innermost element axes collapse into one of
// `out_len = (3-1)*2 + 4 = 8`, the streamed axis kept. Output `{8, 8}`.
static_assert([] {
    Graph g{};
    const auto in = add_value(g, ElemType::F32, Shape{3, 4, 8}, ValueCategory::Input);
    const auto out = add_node(g, op::OverlapAdd, in, kNoValue, kNoValue, overlap_attr(4, 2));
    return infer(g).ok && g.values[static_cast<std::size_t>(out)].shape == Shape{8, 8};
}(), "an overlap-add collapses [.., F, K] into [.., out_len] and keeps the streamed axis");

// With an OUTER axis kept: `{2, 3, 4, 8}` — an outer axis of 2, then F=3, K=4, streamed 8 — collapses only
// the two innermost element axes, to `{2, 8, 8}`.
static_assert([] {
    Graph g{};
    const auto in = add_value(g, ElemType::F32, Shape{2, 3, 4, 8}, ValueCategory::Input);
    const auto out = add_node(g, op::OverlapAdd, in, kNoValue, kNoValue, overlap_attr(4, 2));
    return infer(g).ok && g.values[static_cast<std::size_t>(out)].shape == Shape{2, 8, 8};
}(), "an overlap-add keeps every outer axis, collapsing only the frame and window axes");

// A hop equal to the window is the non-overlapping case: `out_len = (F-1)*window + window = F*window`, the
// frames laid end to end. `{4, 4, 8}` window 4 hop 4 -> out_len 16.
static_assert([] {
    Graph g{};
    const auto in = add_value(g, ElemType::F32, Shape{4, 4, 8}, ValueCategory::Input);
    const auto out = add_node(g, op::OverlapAdd, in, kNoValue, kNoValue, overlap_attr(4, 4));
    return infer(g).ok && g.values[static_cast<std::size_t>(out)].shape == Shape{16, 8};
}(), "a hop equal to the window lays the frames end to end");

// ---- the refusals, SHOWN REJECTING deliberately broken inputs ---------------------------------------

// A window below one is not a frame — the attribute contradicting itself before any shape is consulted.
static_assert([] {
    Graph g{};
    const auto in = add_value(g, ElemType::F32, Shape{3, 4, 8}, ValueCategory::Input);
    add_node(g, op::OverlapAdd, in, kNoValue, kNoValue, overlap_attr(0, 2));
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"_ovadd windows 0 every 2: a window and a hop are each at least one"};
}(), "SHOWN REJECTING: a window below one is refused");

// An overlap-add needs a frame axis, a window axis and the streamed axis — a value of rank below three has
// no frame axis to place.
static_assert([] {
    Graph g{};
    const auto in = add_value(g, ElemType::F32, Shape{4, 8}, ValueCategory::Input);
    add_node(g, op::OverlapAdd, in, kNoValue, kNoValue, overlap_attr(4, 2));
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() == std::string_view{
                                   "_ovadd folds a value of rank 2; it needs a frame axis, a window axis "
                                   "and the streamed axis"};
}(), "SHOWN REJECTING: a value with too few axes to fold is refused");

// The innermost element axis IS the window — a value whose innermost axis disagrees with the declared
// window is a malformed frame set, refused with both numbers named.
static_assert([] {
    Graph g{};
    const auto in = add_value(g, ElemType::F32, Shape{3, 5, 8}, ValueCategory::Input);
    add_node(g, op::OverlapAdd, in, kNoValue, kNoValue, overlap_attr(4, 2));
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"_ovadd declares window 4 but its innermost axis is 5"};
}(), "SHOWN REJECTING: a window axis unlike the declared window is refused");

// A symbolic window axis was never bound, so there is no size to fold — refused with the escape named.
static_assert([] {
    Graph g{};
    const auto in = add_value(g, ElemType::F32, Shape{3, Dim{0}, 8}, ValueCategory::Input);
    add_node(g, op::OverlapAdd, in, kNoValue, kNoValue, overlap_attr(4, 2));
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"_ovadd window axis size is not known yet; bind it before the overlap-add"};
}(), "SHOWN REJECTING: an unbound window axis is refused");

// A symbolic frame count that never resolved has no literal to derive `out_len` from — refused rather than
// carried into a wrong length, the `_unfold` precedent, with the two ways to give it a size named. (A frame
// count that DOES resolve — the M5.4 case — executes; that is `symbolic_test.cpp`'s graph six.)
static_assert([] {
    Graph g{};
    const auto in = add_value(g, ElemType::F32, Shape{Dim{0}, 4, 8}, ValueCategory::Input);
    add_node(g, op::OverlapAdd, in, kNoValue, kNoValue, overlap_attr(4, 2));
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"_ovadd frame count is not known yet; bind it or derive it from an _unfold"};
}(), "SHOWN REJECTING: an unresolved symbolic frame count is refused");

// M5.7 for `_ovadd`: a symbolic axis ELSEWHERE passes straight through. Here the OUTER axis is an unbound
// symbol while F and K are literal, so the shape resolves with the outer axis still symbolic and the two
// innermost collapsed to a literal `out_len`.
static_assert([] {
    Graph g{};
    const auto in = add_value(g, ElemType::F32, Shape{Dim{0}, 3, 4, 8}, ValueCategory::Input);
    const auto out = add_node(g, op::OverlapAdd, in, kNoValue, kNoValue, overlap_attr(4, 2));
    const Diagnostic d = infer(g);
    const Shape& s = g.values[static_cast<std::size_t>(out)].shape;
    return d.ok && is_symbol(extent_at(s, 0)) && extent_at(s, 1) == literal_extent(8) &&
           extent_at(s, 2) == literal_extent(8);
}(), "M5.7: an overlap-add passes a symbolic axis elsewhere through, collapsing only its frame and window axes");

// ---- the run side: which source elements one output element sums --------------------------------------
//
// The windowed scatter, checked directly on the reference walk's reader. Operand `[F=3, K=4]`,
// output `[out_len=8]`, window 4, hop 2. Output position 0 is covered by frame 0 alone (tap 0 -> source
// element 0). Output position 4 is covered by TWO frames — frame 1 at tap 2 (source `1*4+2 = 6`) and frame
// 2 at tap 0 (source `2*4+0 = 8`) — the overlap an overlap-add sums.
static_assert([] {
    constexpr std::int32_t kT = 8; // the streamed axis
    Node n{};
    n.op = op::OverlapAdd;
    n.attr.kind = AttrKind::OverlapAdd;
    n.attr.overlap.window = 4;
    n.attr.overlap.hop = 2;
    const Elements from = elements_of(Shape{3, 4, kT}); // [F=3, K=4]
    const Elements to = elements_of(Shape{8, kT});      // [out_len=8]

    const OverlapSpan at0 = overlap_span(n, to, from, 0);
    const bool one = at0.count == 1 && at0.first_frame == 0 &&
                     overlap_read(n, to, from, at0, 0, 0) == 0;

    const OverlapSpan at4 = overlap_span(n, to, from, 4);
    const bool two = at4.count == 2 && at4.first_frame == 1 &&
                     overlap_read(n, to, from, at4, 0, 4) == 6 &&
                     overlap_read(n, to, from, at4, 1, 4) == 8;
    return one && two;
}(), "an overlap-add sums the frames that overlap each output position, at the taps that land there");

// A gap: with a hop LARGER than the window, some output positions no frame covers. `{2, 2, 8}` window 2 hop
// 3 gives `out_len = (2-1)*3 + 2 = 5`; position 2 falls in the gap between frame 0 (0..1) and frame 1
// (3..4), so its span is empty — a sum of nothing, which is the zero an overlap-add reconstructs there.
static_assert([] {
    constexpr std::int32_t kT = 8;
    Node n{};
    n.op = op::OverlapAdd;
    n.attr.kind = AttrKind::OverlapAdd;
    n.attr.overlap.window = 2;
    n.attr.overlap.hop = 3;
    const Elements from = elements_of(Shape{2, 2, kT}); // [F=2, K=2]
    const Elements to = elements_of(Shape{5, kT});      // [out_len=5]
    return overlap_span(n, to, from, 2).count == 0;
}(), "an output position no frame covers sums nothing — the zero an overlap-add reconstructs in a gap");

int main() { return 0; }
