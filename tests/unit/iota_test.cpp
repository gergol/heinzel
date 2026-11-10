// What the analysis pass and the runners make of an INDEX RAMP — its declared shape and type, its
// refusals, and the ramp a hand-filled walk reads (milestone M5, S6).
//
// `_iota` is `out[e] = e`, the element's own index, generated with no operand: the tree's first arity-0
// operation. Its shape and type are DECLARED in `GeneratorAttr` (there is nothing flowing in to derive either
// from), and its element meaning is `ElementKind::Indexed` — `compute()` is handed operand numbers and no
// index, so a generator has no sample-at-a-time answer, exactly as a fold or a rearrangement does not. This
// file checks the declared shape and type, the attribute refusals SHOWN REJECTING deliberately broken
// inputs, and the ramp a hand-filled walk reads by hand. The loop tree's ramps are `nest_views_test.cpp`'s.

#include <array>
#include <string_view>

#include "heinzel/ir/ir.hpp"
#include "heinzel/sema/analyze.hpp"

#include "hand_built_graph.hpp"
#include "interpret.hpp"
#include "iota_element.hpp"

using namespace heinzel;

namespace {
constexpr OpAttr iota_attr(Shape shape, ElemType type) {
    OpAttr a{};
    a.kind = AttrKind::Iota;
    a.generator.shape = shape;
    a.generator.type = type;
    return a;
}
} // namespace

// ---- the declared shape and type (milestone M5, S6) -------------------------------------------------
//
// A generator states its own result: no operand, so the shape and the element type come straight off the
// attribute. A ramp declared `{4, 8}` (four elements over a streamed axis of eight) keeps that shape and
// the declared `F64`.
static_assert([] {
    Graph g{};
    const auto out = add_node(g, op::Iota, kNoValue, kNoValue, kNoValue, iota_attr(Shape{4, 8}, ElemType::F64));
    return infer(g).ok && g.values[static_cast<std::size_t>(out)].shape == Shape{4, 8} &&
           g.values[static_cast<std::size_t>(out)].type == ElemType::F64;
}(), "an index ramp has its declared shape and type");

// ---- the refusals, SHOWN REJECTING deliberately broken inputs ---------------------------------------

// A rank-0 scalar has no axis to ramp along — the old tree's own builder check.
static_assert([] {
    Graph g{};
    add_node(g, op::Iota, kNoValue, kNoValue, kNoValue, iota_attr(Shape{}, ElemType::F64));
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() == std::string_view{"iota declares a rank-0 shape; a generator needs at least one axis"};
}(), "SHOWN REJECTING: a rank-0 ramp is refused");

// A symbolic element extent has no count to loop to, and a generator has no operand whose binding could
// ever make it concrete — so it is refused up front rather than run into a wrong length. (The precondition
// column, gone since Decision 60, could not carry this: it gated OPERANDS, and a generator has none.)
static_assert([] {
    Graph g{};
    add_node(g, op::Iota, kNoValue, kNoValue, kNoValue, iota_attr(Shape{Dim{0}, 8}, ElemType::F64));
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"iota declares a symbolic element extent; a generator's shape must be known now"};
}(), "SHOWN REJECTING: a symbolic-extent ramp is refused");

// ---- the run side: the ramp a hand-filled walk reads ------------------------------------------------
//
// A ramp of four elements over an eight-sample block, read by a hand-filled walk at sample zero. Its value
// at element e is e, the same at every sample and depending on no input at all — the four indices differ, so
// a walk that wrote the wrong index, or the same one to every element, would move the answer.
namespace {
constexpr std::array<double, 4> ramp() {
    Graph g{};
    const auto out = add_node(g, op::Iota, kNoValue, kNoValue, kNoValue, iota_attr(Shape{4, 8}, ElemType::F64));
    if (!infer(g).ok) {
        fail("the iota graph did not resolve");
    }
    Signals<8> signals{};
    lay_out(g, signals);
    detail::walk_indexed_step(g, g.nodes[0], 0, signals);
    std::array<double, 4> result{};
    for (std::int32_t e = 0; e < 4; ++e) {
        result[static_cast<std::size_t>(e)] = sample_at(signals, out, e, 0);
    }
    return result;
}
} // namespace

static_assert(ramp() == std::array<double, 4>{0.0, 1.0, 2.0, 3.0},
              "an index ramp's element e is e, generated from no operand");

int main() { return 0; }
