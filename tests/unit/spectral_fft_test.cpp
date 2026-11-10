// What the analysis pass makes of an `_fft`, and what it refuses — the spectral boundary (milestone
// M5, S4a).
//
// `_fft` is an INTRINSIC BOUNDARY: the SHAPE it imposes on the transformed (innermost) axis and its
// static-length precondition are modelled here, its DFT arithmetic is a backend deferred to M13. So
// this file is about the pass alone — a good transform is accepted with its half-spectrum shape, and
// the refusals below reject something the pass otherwise takes. That a RUN of an `_fft` refuses rather
// than inventing a number is `meaning_test.cpp`'s (no per-sample meaning is written) and the walk's
// `Boundary` arm; that the loop tree refuses it, the one operation the both-ways corpus analyses without
// running, is `both_ways_test.cpp`'s.
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"

#include "filters.hpp"
#include "hand_built_graph.hpp"
#include "interpret.hpp"

#include <string_view>

using namespace heinzel;

// ---- a forward real transform, accepted with its half-spectrum shape --------------------------------
//
// A real block of N numbers on the innermost axis becomes `2*(N/2+1)` planar re/im numbers — `N + 2`
// for the even N a power of two is. Here N = 8 -> 10, the outer axes passing through unchanged.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::Fft;
    k.fft.n = 8;
    k.fft.dir = FftDir::Forward;
    const auto out = add_node(g, op::Fft, x, kNoValue, kNoValue, k);
    return infer(g).ok && g.values[static_cast<std::size_t>(out)].shape == Shape{4, 3, 10};
}(), "a forward real FFT sends an innermost N to its half-spectrum 2*(N/2+1) and keeps the other axes");

// The inverse recovers the real length from the half-spectrum: `2*(N/2+1)` planar numbers back to N.
// It reads the length from the attribute, because the map is not injective — a real 4 and a real 5 both
// give 6 planar numbers — which is the whole reason `n` is carried.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 3, 10}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::Fft;
    k.fft.n = 8;
    k.fft.dir = FftDir::Inverse;
    const auto out = add_node(g, op::Fft, x, kNoValue, kNoValue, k);
    return infer(g).ok && g.values[static_cast<std::size_t>(out)].shape == Shape{4, 3, 8};
}(), "an inverse real FFT recovers the declared length N from its 2*(N/2+1) half-spectrum");

// ---- the refusals, each SHOWN REJECTING -----------------------------------------------------------

// An ODD length is not a power of two, so it is not a real FFT length. One predicate refuses all three
// ways a length can be wrong, and the message names the length so each rejection points at its number.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::Fft;
    k.fft.n = 5;
    add_node(g, op::Fft, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"_fft has length 5; a real FFT length is a power of two of at least two"};
}(), "SHOWN REJECTING: an odd transform length is refused by name");

// An EVEN NON-POWER-OF-TWO length (6) is refused the same way — evenness alone is not enough.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::Fft;
    k.fft.n = 6;
    add_node(g, op::Fft, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"_fft has length 6; a real FFT length is a power of two of at least two"};
}(), "SHOWN REJECTING: an even non-power-of-two length is refused");

// A length BELOW TWO is not a transform at all.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::Fft;
    k.fft.n = 1;
    add_node(g, op::Fft, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"_fft has length 1; a real FFT length is a power of two of at least two"};
}(), "SHOWN REJECTING: a length below two is refused");

// A SYMBOLIC transform length has no static number for the shape or the backend, which the attribute
// check refuses first — the old tree's "static n, v1".
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{Dim{0}}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::Fft;
    k.fft.n = 8;
    add_node(g, op::Fft, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"_fft needs every extent known now, but was given [?0]"};
}(), "SHOWN REJECTING: a symbolic transform length is refused, the transform's first refusal");

// The check reads the WHOLE shape, so a symbolic OUTER axis is refused too — stricter than a
// hypothetical innermost-only reading, and deliberately so (`docs/limitations.md`): a graph that wants
// a batched transform over a bound-later axis binds it first.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{Dim{0}, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::Fft;
    k.fft.n = 8;
    add_node(g, op::Fft, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"_fft needs every extent known now, but was given [?0, 8]"};
}(), "SHOWN REJECTING: a symbolic outer axis is refused too — the check reads the whole shape");

// A symbolic axis that reaches a view first is refused by the view, node by node: a pad of it has no literal to grow,
// and it comes before the transform. Only a hand-built graph can do this.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{Dim{0}, 8}, ValueCategory::Input);
    OpAttr pad{};
    pad.kind = AttrKind::View;
    pad.view.kind = ViewKind::Pad;
    pad.view.axis = 0;
    pad.view.pad_before = 1;
    pad.view.pad_after = 1;
    const auto padded = add_node(g, op::Pad, x, kNoValue, kNoValue, pad);
    OpAttr k{};
    k.kind = AttrKind::Fft;
    k.fft.n = 8;
    add_node(g, op::Fft, padded, kNoValue, kNoValue, k);
    return infer(g).message.view() == std::string_view{"_padc pads axis 0, whose size is not known yet; bind it or declare its extent"};
}(), "SHOWN REJECTING: a symbolic axis padded before a transform is the pad's refusal");

// The axis the transform acts on has to match the length it declares. A FORWARD transform reads N real
// numbers; here the length says 4 but the innermost axis is 8.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::Fft;
    k.fft.n = 4;
    k.fft.dir = FftDir::Forward;
    add_node(g, op::Fft, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"_fft forward reads 4 numbers on its innermost axis, but was given 8"};
}(), "SHOWN REJECTING: a forward transform whose innermost axis is not N is refused");

// An INVERSE transform reads the `2*(N/2+1)` planar numbers a forward one produced; here the length
// says 8 (so 10 planar) but the innermost axis is 8.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::Fft;
    k.fft.n = 8;
    k.fft.dir = FftDir::Inverse;
    add_node(g, op::Fft, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"_fft inverse reads 10 numbers on its innermost axis, but was given 8"};
}(), "SHOWN REJECTING: an inverse transform whose innermost axis is not 2*(N/2+1) is refused");

// A rank-0 value has no axis to transform.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::Fft;
    k.fft.n = 8;
    add_node(g, op::Fft, x, kNoValue, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() == std::string_view{"_fft transforms a rank-0 value, which has no axis to transform"};
}(), "SHOWN REJECTING: an FFT of a rank-0 value is refused");

// The pass ACCEPTS and shapes a well-formed `_fft`, but RUNNING one refuses until the backend arrives.
// Observed from a run rather than asserted, because under constant evaluation a `fail()` IS the compile
// error — the same reason the run-side refusals in `rearrange_concat_test.cpp` live here. The
// interpreted walk reaches the `Boundary` arm and objects rather than inventing a spectral number; the
// loop tree refuses the graph when it plans it (`both_ways_test.cpp` pins that).
static bool running_an_fft_is_refused_until_the_backend_arrives() {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{8}, ValueCategory::Input);
    g.inputs.push_back(NamedValue{Name{"x"}, x});
    OpAttr k{};
    k.kind = AttrKind::Fft;
    k.fft.n = 8;
    k.fft.dir = FftDir::Forward;
    const auto out = add_node(g, op::Fft, x, kNoValue, kNoValue, k);
    g.outputs.push_back(NamedValue{Name{"y"}, out});
    if (!infer(g).ok) {
        return false; // the graph is well-formed; only running it is refused
    }
    try {
        (void)walk<8>(g);
        return false;
    } catch (const error& e) {
        return e.message() ==
               std::string_view{"heinzel::run: an intrinsic boundary (_fft); no spectral backend computes it yet"};
    }
}

int main() { return running_an_fft_is_refused_until_the_backend_arrives() ? 0 : 1; }
