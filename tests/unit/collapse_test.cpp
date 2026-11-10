// An operation that COLLAPSES axes, run: which of an operand's elements fold into one of a result's,
// and what the fold does to the numbers.
//
// Milestone M5's executor. `compute()` answers for one element out of three numbers and refuses an
// operation the table classifies `MeaningRule::AxisChanging`, because one element of such a result is
// assembled out of many of its operand's. This is the file that checks what it is instead.
//
// The two halves are deliberately checked apart, because they fail in different ways and only one of
// them can be seen through a whole run at this step:
//
//   the INDEXING   which of the operand's elements the j-th contributor to element e is. Checked on
//                  `folded_element` directly, where the elements can be told apart by their indices.
//   the FOLD       what the numbers become. Checked by filling a value's planes with numbers that
//                  DIFFER and running one step of the walk over them — nothing in the operation set
//                  yet makes a wide value whose elements differ, so a whole run cannot show this and
//                  saying so is better than a test that looks stronger than it is.
#include <array>
#include <string_view>

#include "heinzel/ir/ir.hpp"
#include "heinzel/sema/analyze.hpp"

#include "collapse_element.hpp"
#include "compute_reference.hpp"
#include "elements.hpp"
#include "hand_built_graph.hpp"
#include "interpret.hpp"
#include "meaning_coverage.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t kT = 4; // the streamed axis, kept short: nothing here is about time

// A value of shape {2, 3, T} — six numbers per sample, laid out row-major with the innermost width
// axis fastest, which is the layout `elements_of` describes and `folded_element` indexes.
constexpr Elements kSix = elements_of(Shape{2, 3, kT});
static_assert(kSix.rank == 2 && kSix.count == 6 && kSix.full_rank == 3,
              "the width axes, their product, and the value's own rank — which is what names an axis");

// ---- the indexing --------------------------------------------------------------------------------

// Collapsing the INNER width axis: three elements fold into one, and they are consecutive.
constexpr Collapse kInner = collapse_over(Axes{1}, false, kSix);
constexpr Elements kTwo = elements_of(Shape{2, kT});
static_assert(kInner.fan_in == 3, "an axis of extent three folds three elements into one");
static_assert(folded_element(kSix, kInner, kTwo, 0, 0) == 0 && folded_element(kSix, kInner, kTwo, 0, 1) == 1 &&
              folded_element(kSix, kInner, kTwo, 0, 2) == 2,
              "the first result element is fed by the operand's first three");
static_assert(folded_element(kSix, kInner, kTwo, 1, 0) == 3 && folded_element(kSix, kInner, kTwo, 1, 2) == 5,
              "and the second by its last three");

// Collapsing the OUTER width axis: two elements fold into one, and they are three apart. A `stride`
// would have been right for the case above and is wrong here in a different way for each axis, which
// is why the mapping is decomposed rather than strided.
constexpr Collapse kOuter = collapse_over(Axes{0}, false, kSix);
constexpr Elements kThree = elements_of(Shape{3, kT});
static_assert(kOuter.fan_in == 2);
static_assert(folded_element(kSix, kOuter, kThree, 0, 0) == 0 && folded_element(kSix, kOuter, kThree, 0, 1) == 3 &&
              folded_element(kSix, kOuter, kThree, 2, 0) == 2 && folded_element(kSix, kOuter, kThree, 2, 1) == 5,
              "collapsing the outer axis folds elements three apart");

// BOTH width axes at once, which is the case a stride cannot express at all: six elements into one.
constexpr Collapse kBoth = collapse_over(Axes{0, 1}, false, kSix);
constexpr Elements kOne = elements_of(Shape{kT});
static_assert(kBoth.fan_in == 6);
static_assert(folded_element(kSix, kBoth, kOne, 0, 0) == 0 && folded_element(kSix, kBoth, kOne, 0, 3) == 3 &&
              folded_element(kSix, kBoth, kOne, 0, 5) == 5,
              "every element of the operand folds into the one element of the result");

// Keeping the axis leaves it at extent one, and the mapping is unchanged: the kept axis consumes a
// position of the result whose index is always zero. Asserted against the dropping case rather than
// spelled again, because "the same mapping" is the claim.
constexpr Collapse kInnerKept = collapse_over(Axes{1}, true, kSix);
constexpr Elements kTwoByOne = elements_of(Shape{2, 1, kT});
static_assert(kTwoByOne.count == 2, "keeping the axis at extent one does not change how many numbers there are");
static_assert(folded_element(kSix, kInnerKept, kTwoByOne, 1, 2) == folded_element(kSix, kInner, kTwo, 1, 2),
              "keeping a collapsed axis at extent one changes the shape and not the mapping");

// ---- the fold, over numbers that differ ----------------------------------------------------------
//
// One node, one sample, over planes filled by hand — 1 through 6 — because nothing in the operation
// set yet MAKES a wide value whose elements differ, and a fold over six equal numbers would agree
// with a fold that read the same one six times.
constexpr Graph reducing(ReduceOp which, Axes over, bool keep) {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{2, 3, kT}, ValueCategory::Input);
    OpAttr collapse{};
    collapse.kind = AttrKind::Reduce;
    collapse.reduce.op = which;
    collapse.reduce.axes = over;
    collapse.reduce.keep_axes = keep;
    add_node(g, op::Reduce, x, kNoValue, kNoValue, collapse);
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the reducing graph did not resolve");
    }
    return g;
}

template <std::size_t Results>
constexpr std::array<double, Results> folded(ReduceOp which, Axes over, bool keep = false) {
    const Graph g = reducing(which, over, keep);
    Signals<kT> signals{};
    lay_out(g, signals);
    for (std::int32_t e = 0; e < 6; ++e) {
        signals.at[static_cast<std::size_t>(signals.first[0] + e)][0] = 1.0 + static_cast<double>(e);
    }
    detail::walk_folding_step(g, g.nodes[0], 0, signals);

    std::array<double, Results> out{};
    for (std::size_t i = 0; i < Results; ++i) {
        out[i] = sample_at(signals, g.nodes[0].output, static_cast<std::int32_t>(i), 0);
    }
    return out;
}

// 1+2+3 and 4+5+6 — which is also the check that the two result elements read DIFFERENT numbers.
static_assert(folded<2>(ReduceOp::Sum, Axes{1}) == std::array<double, 2>{6.0, 15.0});

// 1+4, 2+5, 3+6 — the same six numbers folded the other way, so a mapping that ignored which axis it
// was told to collapse would have to disagree with one of the two.
static_assert(folded<3>(ReduceOp::Sum, Axes{0}) == std::array<double, 3>{5.0, 7.0, 9.0});

// All six into one.
static_assert(folded<1>(ReduceOp::Sum, Axes{0, 1}) == std::array<double, 1>{21.0});

// Each of the five operations, on the same numbers, so none of them is another under a different
// name. `Mean` divides by the fan-in and is the one that needs a finish of its own.
static_assert(folded<2>(ReduceOp::Prod, Axes{1}) == std::array<double, 2>{6.0, 120.0});
static_assert(folded<2>(ReduceOp::Max, Axes{1}) == std::array<double, 2>{3.0, 6.0});
static_assert(folded<2>(ReduceOp::Min, Axes{1}) == std::array<double, 2>{1.0, 4.0});
static_assert(folded<2>(ReduceOp::Mean, Axes{1}) == std::array<double, 2>{2.0, 5.0});

// Keeping the axis changes the shape and not the numbers.
static_assert(folded<2>(ReduceOp::Sum, Axes{1}, true) == folded<2>(ReduceOp::Sum, Axes{1}, false));

// ---- two operands: the JOIN, and a fold whose contributor is a product -----------------------------
//
// A contraction sums over axes of the operands' JOINED shape rather than of either operand's, which is
// what `ShapeRule::ContractAxes` already does to build its result — broadcast, then collapse. So its
// element meaning is the same two mechanisms in the same order, and what is new is only what one
// contributor is: a product rather than an element.

// The join itself, over layouts written out. It is the same alignment `element_read_by` walks, so an
// operand short of an axis contributes nothing to it and one at extent one is the other's extent.
static_assert(joined_elements(kSix, kSix) == kSix, "a layout joined with itself is itself");
static_assert(joined_elements(kSix, elements_of(Shape{kT})).count == 6 &&
              joined_elements(elements_of(Shape{kT}), kSix).count == 6,
              "a value of one number joins with a wide one to the wide one, either way round");
static_assert(joined_elements(elements_of(Shape{3, kT}), elements_of(Shape{2, 1, kT})) ==
                  elements_of(Shape{2, 3, kT}),
              "an extent of one takes the other operand's, on whichever axis it is");
// A pair that does not join at all — two and three on the same axis — is observed objecting in
// `main`, because under constant evaluation a `fail()` IS the compile error.

// A contraction over planes filled by hand. Both operands are the same value, so the contributor at
// element e is the SQUARE of the number there — which is what tells a sum of products apart from a
// sum: 1+4+9 rather than 1+2+3.
constexpr Graph contracting(Axes over) {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{2, 3, kT}, ValueCategory::Input);
    OpAttr summed{};
    summed.kind = AttrKind::Contract;
    summed.contract.axes = over;
    add_node(g, op::Contract, x, x, kNoValue, summed);
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the contracting graph did not resolve");
    }
    return g;
}

template <std::size_t Results>
constexpr std::array<double, Results> contracted(Axes over) {
    const Graph g = contracting(over);
    Signals<kT> signals{};
    lay_out(g, signals);
    for (std::int32_t e = 0; e < 6; ++e) {
        signals.at[static_cast<std::size_t>(signals.first[0] + e)][0] = 1.0 + static_cast<double>(e);
    }
    detail::walk_folding_step(g, g.nodes[0], 0, signals);
    std::array<double, Results> out{};
    for (std::size_t i = 0; i < Results; ++i) {
        out[i] = sample_at(signals, g.nodes[0].output, static_cast<std::int32_t>(i), 0);
    }
    return out;
}

// 1+4+9 and 16+25+36. Against the reduction's 6 and 15 on the same numbers, which is the check that
// the product is really happening rather than the sum being reached twice.
static_assert(contracted<2>(Axes{1}) == std::array<double, 2>{14.0, 77.0},
              "a contraction over the inner axis is not the sum of the squares there");
static_assert(contracted<3>(Axes{0}) == std::array<double, 3>{17.0, 29.0, 45.0},
              "1+16, 4+25, 9+36 — the same six numbers contracted the other way");
static_assert(contracted<1>(Axes{0, 1}) == std::array<double, 1>{91.0}, "all six squares into one");

// What a contributor IS, asked directly, because through a whole run a wrong answer here and a wrong
// fold are the same symptom.
constexpr Node reduce_node = reducing(ReduceOp::Sum, Axes{1}, false).nodes[0];
constexpr Node contract_node = contracting(Axes{1}).nodes[0];
static_assert(contribution(reduce_node, 3.0, 5.0) == 3.0,
              "a reduction's contributor is its one operand's element, and it ignores the second");
static_assert(contribution(contract_node, 3.0, 5.0) == 15.0, "a contraction's contributor is the product");
static_assert(fold_of(contract_node) == ReduceOp::Sum, "a contraction's fold is a sum");

// ---- the coverage question, and which KIND each answer is -------------------------------------------
//
// M3.4's twin: every row that says it changes axes owes an element meaning. All three now have one,
// and the two kinds are not interchangeable — a rearrangement run as a fold would be a fold of one
// contributor, which is a wrong number rather than a refusal. So the kind is asserted and not merely
// the presence. That the set of rows still owed one is EMPTY is asserted in
// `tests/reference/meaning_coverage.hpp`, where it became a gate the moment `_reshape` got its meaning.
static_assert(element_kind_of(op::Reduce) == ElementKind::Fold &&
                  element_kind_of(op::Contract) == ElementKind::Fold,
              "an operation this milestone built has no element meaning, or has the wrong kind of one");
static_assert(element_kind_of(op::Reshape) == ElementKind::Rearrange,
              "a rearrangement is not a fold: one element of its result is one element of its operand");
static_assert(axis_changing_rows_without_an_element_meaning(kOpTable, kOpCount) == 0,
              "a row that changes axes is owed an element meaning nobody wrote");
static_assert(element_kind_of(op::Add) == ElementKind::None,
              "an element meaning is written for a per-sample operation");

// ---- the refusals ----------------------------------------------------------------------------------

constexpr std::string_view kNoJoin =
    "heinzel::joined_elements: two operands' axes are neither equal nor one of them one";
constexpr std::string_view kNoContributor =
    "heinzel::contribution: this collapses axes and carries no attribute saying what one is";

constexpr std::string_view kStreamed =
    "heinzel::collapse_over: this collapses the streamed axis, which a run here cannot do";
constexpr std::string_view kNoSuchAxis = "heinzel::collapse_over: this collapses an axis the operand does not have";
constexpr std::string_view kTwice = "heinzel::collapse_over: the same axis is named twice among the ones that collapse";

// A collapse over the streamed axis is a fold over TIME — over operands that do not all exist yet.
// Named both ways it can be spelled: by number, and as `kInnermostAxis`.
bool collapsing_the_streamed_axis_is_refused() {
    try {
        (void)collapse_over(Axes{2}, false, kSix);
        return false;
    } catch (const error& e) {
        if (e.message() != kStreamed) {
            return false;
        }
    }
    try {
        (void)collapse_over(Axes{kInnermostAxis}, false, kSix);
        return false;
    } catch (const error& e) {
        return e.message() == kStreamed;
    }
}

bool collapsing_an_axis_that_is_not_there_is_refused() {
    try {
        (void)collapse_over(Axes{5}, false, kSix);
        return false;
    } catch (const error& e) {
        return e.message() == kNoSuchAxis;
    }
}

// The same axis twice would multiply the fan-in by its extent again and read every element twice.
bool naming_one_axis_twice_is_refused() {
    try {
        (void)collapse_over(Axes{1, 1}, false, kSix);
        return false;
    } catch (const error& e) {
        return e.message() == kTwice;
    }
}

// An operation classified `AxisChanging` whose attribute says nothing about which axes collapse, or
// nothing about what the fold is. Two questions, two messages, because a node can be half-filled.
bool a_collapse_with_no_axes_attribute_is_refused() {
    Node bare{};
    bare.op = op::Reduce;
    try {
        (void)collapse_of(bare, kSix);
        return false;
    } catch (const error& e) {
        if (e.message() !=
            std::string_view{"heinzel::collapse_of: this collapses axes and carries no attribute saying which"}) {
            return false;
        }
    }
    try {
        (void)fold_of(bare);
        return false;
    } catch (const error& e) {
        return e.message() == std::string_view{"heinzel::fold_of: this collapses axes and carries no "
                                               "attribute saying what the fold is"};
    }
}

// WHAT USED TO BE CHECKED HERE AND CANNOT BE ANY MORE, said rather than deleted quietly. A row that
// says it changes axes and has no element meaning written was observed here being refused by the walk
// in its own words. Every such row now has one — `_reshape` got its rearrangement with M5's views —
// so the state is unreachable from the real table, and what replaced the observation is stronger than
// it was: `tests/reference/meaning_coverage.hpp` asserts the outstanding set is EMPTY while the program is
// compiled, so the situation cannot arise rather than being caught when somebody runs it. The refusal
// itself stays written in the reference walk, for a row that is added and not yet written.

// Two operands whose axes are neither equal nor one of them one. The analysis pass would not have
// accepted such a pair, and this stops rather than picking one of the two extents — the same reason
// `element_read_by` fails instead of reading a neighbouring element.
bool a_pair_that_does_not_broadcast_is_refused() {
    try {
        (void)joined_elements(elements_of(Shape{2, kT}), elements_of(Shape{3, kT}));
        return false;
    } catch (const error& e) {
        return e.message() == kNoJoin;
    }
}

// A node that collapses axes and carries no attribute saying what one contributor is. The same shape
// of refusal `collapse_of` and `fold_of` make, and the third of the three questions a runner asks of
// the one writing.
bool a_collapse_with_no_contributor_attribute_is_refused() {
    Node bare{};
    bare.op = op::Reduce;
    try {
        (void)contribution(bare, 1.0, 2.0);
        return false;
    } catch (const error& e) {
        return e.message() == kNoContributor;
    }
}

} // namespace

int main() {
    if (!a_pair_that_does_not_broadcast_is_refused()) return 7;
    if (!a_collapse_with_no_contributor_attribute_is_refused()) return 8;
    if (!collapsing_the_streamed_axis_is_refused()) return 1;
    if (!collapsing_an_axis_that_is_not_there_is_refused()) return 2;
    if (!naming_one_axis_twice_is_refused()) return 3;
    if (!a_collapse_with_no_axes_attribute_is_refused()) return 4;

    // And an ordinary collapse still answers, so none of the four is refusing everything.
    return collapse_over(Axes{1}, false, kSix).fan_in == 3 ? 0 : 6;
}
