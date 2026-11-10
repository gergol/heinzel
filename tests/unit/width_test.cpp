// How many numbers one sample of a value is — its WIDTH — and how the walk lays a value out by it.
//
// Milestone M5's executor needs somewhere to put a result whose extent differs from its operand's;
// the runtime had no notion of a value's shape although the IR has carried one since M1. This is the
// file that checks the notion it gained: `numbers_per_sample`, a value's elements, which element of an
// operand an element of a result reads, and the walk's planes.
//
// THE ONE DECISION IN IT, and it is a decision rather than a lookup. A shape here includes the
// STREAMED axis — the biquad's every value is `Shape{64}` with 64 the block length — so a width is
// the product of the OTHER axes, and which axis is the streamed one has to be settled. It is the
// innermost, for two reasons already committed to the tree rather than chosen here:
//
//   * a lag runs along the innermost axis only: `_fdelay` names no axis, so a lag along any other cannot
//     be written (until `_delay` folded into it, Decision 60, the analysis refused one by name).
//   * `ShapeRule::Elementwise` broadcasts numpy-fashion, aligned from the innermost outwards. With
//     time innermost a mono `Shape{64}` broadcasts against a two-channel `Shape{2, 64}`, which is
//     what a signal library needs; with time outermost the same pair would align 2 against 64 and
//     refuse.
//
// The plausible wrong answer is multiplying every extent together. It gives the biquad storage
// sixty-four times too big while every number it computes stays exactly the same — the kind of
// mistake that never announces itself — so the biquad's width is asserted next to its rank below.
#include <string_view>

#include "heinzel/ir/ir.hpp"
#include "heinzel/sema/analyze.hpp"

#include "elements.hpp"
#include "filters.hpp"
#include "hand_built_graph.hpp"
#include "interpret.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t kBlockSize = 64;

// ---- the rule itself, over shapes written out ---------------------------------------------------
static_assert(numbers_per_sample(Shape{}) == 1, "a rank-0 value is one number and has no streamed axis");
static_assert(numbers_per_sample(Shape{kBlockSize}) == 1,
              "a rank-1 value's one axis IS the streamed one, so it is one number per sample");
static_assert(numbers_per_sample(Shape{3, kBlockSize}) == 3, "three numbers per sample, streamed 64 times");
static_assert(numbers_per_sample(Shape{2, 3, kBlockSize}) == 6, "every axis outside the streamed one multiplies");

// A symbol INSIDE — the streamed axis — says nothing about the width, and a symbol outside it makes
// the width unknown. The two cases are opposite and a rule that read the wrong end would pass one
// of them by accident.
constexpr Dim kLength{0};
static_assert(numbers_per_sample(Shape{Extent{kLength}}) == 1,
              "a symbolic block length leaves the width at one, because the block is not width");
static_assert(numbers_per_sample(Shape{Extent{kLength}, kBlockSize}) == kWidthNotKnown,
              "a symbolic axis outside the streamed one is a width nobody can state yet");
static_assert(numbers_per_sample(Shape{no_extent(), kBlockSize}) == kWidthNotKnown,
              "and neither is an axis nobody has stated a size for — a different claim, the same width");

// ---- the filters this tree actually runs are one number per sample ------------------------------
//
// Asserted together with the RANK, because that is what makes it a check of the rule and not of the
// filter: the biquad's values are rank one with sixty-four in them, and a width of one is the
// interesting answer rather than the obvious one.
constexpr bool every_value_is_one_number(const Graph& g) {
    for (const Value& v : g.values) {
        if (numbers_per_sample(v.shape) != 1) {
            return false;
        }
    }
    return true;
}

constexpr bool some_value_has_the_block_in_its_shape(const Graph& g) {
    for (const Value& v : g.values) {
        if (v.shape.rank == 1 && !is_symbol(extent_at(v.shape, 0)) &&
            literal_of(extent_at(v.shape, 0)) == kBlockSize) {
            return true;
        }
    }
    return false;
}

static_assert(some_value_has_the_block_in_its_shape(biquad(literal_extent(kBlockSize))),
              "the biquad's values really do carry the block length, so the line below is not vacuous");
static_assert(every_value_is_one_number(biquad(literal_extent(kBlockSize))),
              "and every one of them is one number per sample, not sixty-four");

// ---- a value's elements, and which of an operand's one of a result's reads ----------------------
//
// `tests/reference/elements.hpp`. The indexing around the arithmetic, in one place — a second answer to it
// somewhere would be a plausible wrong number rather than a failure.
static_assert(elements_of(Shape{kBlockSize}).rank == 0 && elements_of(Shape{kBlockSize}).count == 1,
              "a rank-1 value's one axis is the streamed one, so it has no width axes at all");
static_assert(elements_of(Shape{3, kBlockSize}).rank == 1 && elements_of(Shape{3, kBlockSize}).extent[0] == 3,
              "the width axes are the shape's own, less the streamed one");
static_assert(elements_of(Shape{2, 3, kBlockSize}).count == 6, "and their product is the width");

constexpr Elements kScalar = elements_of(Shape{kBlockSize});
constexpr Elements kThree = elements_of(Shape{3, kBlockSize});
constexpr Elements kTwoByThree = elements_of(Shape{2, 3, kBlockSize});
constexpr Elements kOneByThree = elements_of(Shape{1, 3, kBlockSize});
constexpr Elements kTwoByOne = elements_of(Shape{2, 1, kBlockSize});

// The same shape reads itself straight through.
static_assert(element_read_by(kThree, kThree, 0) == 0 && element_read_by(kThree, kThree, 2) == 2);

// An operand with no width axes at all repeats: one number feeds every element of the result. That
// is the case every constant in this tree is in.
static_assert(element_read_by(kThree, kScalar, 0) == 0 && element_read_by(kThree, kScalar, 2) == 0,
              "a value of one number feeds every element of a wider result");

// Axes lined up from the INNERMOST outwards, which is the alignment the shape column already uses.
// An operand short of an axis repeats along it; so does one carrying it at extent one — and the two
// spellings must give the same answer, because the shape rule treats them the same.
static_assert(element_read_by(kTwoByThree, kThree, 0) == 0 && element_read_by(kTwoByThree, kThree, 3) == 0 &&
              element_read_by(kTwoByThree, kThree, 4) == 1 && element_read_by(kTwoByThree, kThree, 5) == 2,
              "an operand short of the outer axis repeats along it");
static_assert(element_read_by(kTwoByThree, kOneByThree, 4) == element_read_by(kTwoByThree, kThree, 4),
              "an extent of one and a missing axis are the same claim to the shape rule, so the same here");
static_assert(element_read_by(kTwoByThree, kTwoByOne, 0) == 0 && element_read_by(kTwoByThree, kTwoByOne, 2) == 0 &&
              element_read_by(kTwoByThree, kTwoByOne, 3) == 1 && element_read_by(kTwoByThree, kTwoByOne, 5) == 1,
              "an operand of extent one on the INNER axis repeats along that one instead");

// ---- a walked run at width ------------------------------------------------------------------------
//
// WHAT THIS CAN AND CANNOT SHOW AT THIS STEP, said plainly rather than left for a reader to notice.
// Nothing in the operation set yet MAKES a wide value whose elements differ: a constant is one
// number and repeats, an input is one number per sample, and every operation here is elementwise. So
// a wide graph's elements are all equal by construction, and what a walk of one can demonstrate is
// that the element loop and the broadcast indexing disturb NOTHING — every element of a three-wide
// filter is exactly what its one-wide twin computes, bit for bit. Which element differs from which
// is checked above, on the indexing itself, where it can be. An operation that distinguishes
// elements arrives with the axis-changing meanings.
// The fixture itself is `wide_filter` in `tests/support/filters.hpp`: a second test file runs it
// now, and the reason it is shared is the reason `biquad` is — two copies of one filter drift, and
// then two tests that look like they run the same graph do not.
// The graph really is three numbers per sample, so the comparison below is not two narrow graphs.
static_assert(numbers_per_sample(wide_filter(3, kBlockSize).values[static_cast<std::size_t>(wide_filter(3, kBlockSize).outputs[0].value)].shape) == 3,
              "the wide filter's output is not three numbers per sample");
static_assert(numbers_per_sample(wide_filter(1, kBlockSize).values[static_cast<std::size_t>(wide_filter(1, kBlockSize).outputs[0].value)].shape) == 1);

template <std::size_t BlockSize>
constexpr std::array<double, BlockSize> walked_element(const Graph& g, std::int32_t element) {
    Signals<BlockSize> signals{};
    run(g, impulse<BlockSize>(), signals);
    std::array<double, BlockSize> out{};
    for (std::size_t n = 0; n < BlockSize; ++n) {
        out[n] = sample_at(signals, g.outputs[0].value, element, n);
    }
    return out;
}

constexpr std::size_t kWalk = 16;
static_assert(is_a_real_signal<kWalk>(walked_element<kWalk>(wide_filter(1, kBlockSize), 0)),
              "the one-wide twin produced silence, so agreeing with it would mean nothing");
static_assert(walked_element<kWalk>(wide_filter(3, kBlockSize), 0) == walked_element<kWalk>(wide_filter(1, kBlockSize), 0),
              "element 0 of a three-wide filter is not what its one-wide twin computes");
static_assert(walked_element<kWalk>(wide_filter(3, kBlockSize), 1) == walked_element<kWalk>(wide_filter(1, kBlockSize), 0),
              "element 1 is not");
static_assert(walked_element<kWalk>(wide_filter(3, kBlockSize), 2) == walked_element<kWalk>(wide_filter(1, kBlockSize), 0),
              "element 2 is not");

// The elements really are in DIFFERENT planes rather than three names for one. A value's elements
// are consecutive planes, and a layout that gave them all the same one would satisfy every line
// above.
constexpr bool the_three_elements_are_three_planes() {
    Signals<kWalk> signals{};
    run(wide_filter(3, kBlockSize), impulse<kWalk>(), signals);
    const std::int32_t out = wide_filter(3, kBlockSize).outputs[0].value;
    const std::int32_t first = signals.first[static_cast<std::size_t>(out)];
    signals.at[static_cast<std::size_t>(first + 1)][0] = 99.0;
    return sample_at(signals, out, 1, 0) == 99.0 && sample_at(signals, out, 0, 0) != 99.0 &&
           sample_at(signals, out, 2, 0) != 99.0;
}
static_assert(the_three_elements_are_three_planes(), "a value's elements share a plane");

// A graph whose one NAMED input is wider than one number per sample.
constexpr Graph wide_input() {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{3, kBlockSize}, ValueCategory::Input);
    const auto y = add_node(g, op::Neg, x);
    g.inputs.push_back(NamedValue{"x", x});
    g.outputs.push_back(NamedValue{"y", y});
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the wide-input graph did not resolve");
    }
    return g;
}

// One value with more elements than the walk has planes. `kMaxPlanes` starts at `kMaxValues`, which
// is exactly a graph in which every value is one number per sample.
constexpr Graph too_many_planes() {
    Graph g{};
    add_value(g, ElemType::F64, Shape{static_cast<std::int32_t>(kMaxPlanes) + 1, kBlockSize},
              ValueCategory::Input);
    return g;
}

// A recurrence whose variable is narrower than the expression defining it. Built wide so that the
// analysis pass resolves it, then narrowed — the case the walk's check is for: left as it stands, the
// second write would land in the next value's planes.
constexpr Graph mismatched_recurrence() {
    Graph g{};
    const Shape wide{3, kBlockSize};
    const auto x = add_value(g, ElemType::F64, Shape{kBlockSize}, ValueCategory::Input);
    const auto gain = add_value(g, ElemType::F64, wide, ValueCategory::Const);
    g.values[static_cast<std::size_t>(gain)].constant = 0.5;
    const auto spread = add_node(g, op::Mul, x, gain);
    const auto state = add_value(g, ElemType::F64, wide, ValueCategory::Variable);
    OpAttr defines{};
    defines.kind = AttrKind::Recurrence;
    defines.recurrence.defines = state;
    const auto y = add_node(g, op::Recurrence, spread, kNoValue, kNoValue, defines);
    g.inputs.push_back(NamedValue{"x", x});
    g.outputs.push_back(NamedValue{"y", y});
    if (!infer(g).ok) {
        fail("the recurrence graph did not resolve");
    }
    g.values[static_cast<std::size_t>(state)].shape = Shape{kBlockSize};
    return g;
}

// ---- the refusals, each observed rather than asserted --------------------------------------------
//
// One named predicate each, for the reason `tests/reference/meaning_coverage.hpp` gives about its checks: a
// check that has only ever been seen to pass is not evidence that it would ever fail, and a
// predicate can be pointed at a deliberately broken input. They are here rather than as
// `static_assert`s because under constant evaluation a `fail()` IS the compile error — the same
// reason `solver_test.cpp` observes its refusals in `main`.

constexpr std::string_view kUnstatedAxis =
    "heinzel::elements_of: an axis outside the streamed one has no size, so its width is unknown";
constexpr std::string_view kPastTheType = "heinzel::numbers_per_sample: more numbers per sample than an int32 holds";
constexpr std::string_view kWideInput = "heinzel::run: this interpreter takes an input of one number per sample";
constexpr std::string_view kNoPlanes = "heinzel::run: this graph needs more planes than its Signals hold";
constexpr std::string_view kRecurrenceWidth = "heinzel::run: a recurrence and the variable it defines are different widths";

bool an_unstated_outer_axis_is_refused() {
    try {
        (void)elements_of(Shape{Extent{kLength}, kBlockSize});
        return false;
    } catch (const error& e) {
        return e.message() == kUnstatedAxis;
    }
}

// A width that leaves the range of the type it comes back in. Six axes are allowed and five of them
// multiply, so this is reachable rather than theoretical, and an overflow would come back as a
// plausible small width that storage is then sized from.
bool a_width_past_the_type_is_refused() {
    try {
        (void)numbers_per_sample(Shape{65536, 65536, kBlockSize});
        return false;
    } catch (const error& e) {
        return e.message() == kPastTheType;
    }
}

// `input_block` is one number per sample, so the value it fills must be too. A wide input is a
// different signature rather than a wider loop, and without this it would arrive with only its first
// element filled and the rest silently zero.
bool a_wide_input_is_refused() {
    try {
        Signals<kWalk> signals{};
        run(wide_input(), impulse<kWalk>(), signals);
        return false;
    } catch (const error& e) {
        return e.message() == kWideInput;
    }
}

// More planes than the walk's store has. A chosen size, so the message names the store rather than
// implying the graph is wrong.
bool more_planes_than_there_are_is_refused() {
    try {
        Signals<kWalk> signals{};
        lay_out(too_many_planes(), signals);
        return false;
    } catch (const error& e) {
        return e.message() == kNoPlanes;
    }
}

bool a_recurrence_of_a_different_width_is_refused() {
    try {
        Signals<kWalk> signals{};
        run(mismatched_recurrence(), impulse<kWalk>(), signals);
        return false;
    } catch (const error& e) {
        return e.message() == kRecurrenceWidth;
    }
}

} // namespace

int main() {
    if (!an_unstated_outer_axis_is_refused()) return 1;
    if (!a_width_past_the_type_is_refused()) return 3;
    if (!a_wide_input_is_refused()) return 4;
    if (!more_planes_than_there_are_is_refused()) return 5;
    if (!a_recurrence_of_a_different_width_is_refused()) return 7;

    // And the guards are not simply refusing everything: the widest product that DOES fit still
    // answers — 65536 * 32767 is 2147418112, one short of the edge guarded above.
    return numbers_per_sample(Shape{65536, 32767, kBlockSize}) == 2147418112 ? 0 : 6;
}
