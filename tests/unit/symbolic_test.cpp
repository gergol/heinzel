// Oracle O7: a graph whose extent is a symbol, bound to N, agrees with its literal-N twin.
//
// WHY THIS ORACLE IS OWED. Every check this tree already runs operates on graphs whose extents are
// literal. The cross-tree numeric comparison, the dual-mode construction check, the three-way
// execution agreement — none of them has ever seen a symbolic extent, so none of them could notice
// the symbolic path diverging. This is the one that can, and it is what makes M4's twenty-nine
// operations safe to write before M5 makes a symbol flow through them.
//
// What "agrees" means, in the two halves the milestone asks for:
//
//   (a) EVERY VALUE'S RESOLVED SHAPE equals the corresponding value's shape in the twin. Not just
//       the output's — a symbol that resolved correctly at the output and wrongly at an internal
//       value is exactly the failure that stays hidden until something else reads that value.
//   (b) THE EXECUTED OUTPUT is bit-identical to the twin's over at least 64 samples. Bit-identical
//       is meant literally, not "close": a binding is a substitution and a substitution that
//       changed a number would be a defect, not a tolerance.
//
// The compiled ARTIFACTS are deliberately not required to be equal. The symbolic one legitimately
// records a symbol, so demanding that would be demanding something unsatisfiable.
#include <array>
#include <cstdio>

#include "heinzel/compile.hpp"
#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"

#include "executed.hpp"
#include "filters.hpp"
#include "hand_built_graph.hpp"
#include "interpret.hpp"
#include "well_formed.hpp"

using namespace heinzel;

namespace {

// Sixty-four, because the condition asks for at least that many samples and because it is the block
// length the parity biquad already declares — so the symbolic twin is that same filter with its
// block declared as a symbol, rather than a new filter invented to be convenient.
constexpr std::size_t kBlock = 64;
constexpr std::int32_t kBlockSize = static_cast<std::int32_t>(kBlock);
constexpr Dim kLength{0};

// ---- the two halves of "agrees" ----------------------------------------------------------------
// The filters, the impulse, the walk and the is-it-a-real-signal check are `support/filters.hpp`.

constexpr bool every_shape_agrees(const Graph& a, const Graph& b) {
    if (a.values.size() != b.values.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.values.size(); ++i) {
        if (!(a.values[i].shape == b.values[i].shape)) {
            return false;
        }
    }
    return true;
}

// ---- graph one: the biquad, its block length declared both ways --------------------------------
//
// The biquad's symbolic extent is its BLOCK LENGTH, which is worth naming because "make one extent
// symbolic" is not obviously satisfiable for a sample-at-a-time filter until you notice which
// extent it has.

constexpr Graph kBiquadLiteral = biquad(literal_extent(kBlockSize), Bindings{});
constexpr Graph kBiquadSymbolic = biquad(Extent{kLength}, Bindings{}.bind(kLength, kBlockSize));

// (a) — every value, not only the output.
static_assert(every_shape_agrees(kBiquadSymbolic, kBiquadLiteral),
              "binding the block length gives every value the shape its literal twin has");

// The symbolic graph really did declare a symbol, so the assertion above is not comparing a thing
// with itself. Without this, a binding that was quietly ignored would look like a passing test.
static_assert(!is_static(biquad(Extent{kLength}, Bindings{})), "unbound, the graph still carries its symbol");
static_assert(is_static(kBiquadSymbolic), "and bound, it carries none");

// In fact the two graphs come out equal ENTIRELY, not only in their shapes. That is stronger than
// the condition asks for and is recorded as a result rather than relied on: a binding is a
// substitution, so there is nothing left in a bound graph to remember that a symbol was ever there.
static_assert(kBiquadSymbolic == kBiquadLiteral, "a bound symbolic graph IS its literal twin");

// Spelled out as well as compared, so a failure says WHAT the shape is rather than only that the
// two disagree — and so the test states what the block length is supposed to become.
static_assert(kBiquadSymbolic.values[static_cast<std::size_t>(kBiquadSymbolic.inputs[0].value)].shape ==
              Shape{kBlockSize});

// (b) — bit-identical over 64 samples.
static_assert(is_a_real_signal<kBlock>(walk<kBlock>(kBiquadLiteral)), "the impulse response is not silence");
static_assert(walk<kBlock>(kBiquadSymbolic) == walk<kBlock>(kBiquadLiteral), "and the numbers are identical, not close");

// ---- graph two: a different filter, so "both" is two executable graphs and not one -------------
//
// A feed-forward comb into a one-pole. Different operations from the biquad — a subtraction and a
// two-sample delay of the INPUT rather than of the output — so a defect in how one particular
// arrangement resolves does not pass twice.

constexpr Graph kCombLiteral = comb(literal_extent(kBlockSize), Bindings{});
constexpr Graph kCombSymbolic = comb(Extent{kLength}, Bindings{}.bind(kLength, kBlockSize));

static_assert(every_shape_agrees(kCombSymbolic, kCombLiteral));
static_assert(kCombSymbolic == kCombLiteral);
static_assert(is_a_real_signal<kBlock>(walk<kBlock>(kCombLiteral)));
static_assert(walk<kBlock>(kCombSymbolic) == walk<kBlock>(kCombLiteral));

// And it is a different filter, not the biquad under another name.
static_assert(!(walk<kBlock>(kCombLiteral) == walk<kBlock>(kBiquadLiteral)));

// ---- graph three: a reduction, which exercises a SECOND shape rule under a symbol --------------
//
// The biquad and the comb both resolve their symbol through `ShapeRule::Elementwise`. A reduction
// resolves it through `ShapeRule::ReduceAxes`, which is a different piece of code reading a
// different column, and a solver wired correctly into one and not the other would pass everything
// above.
//
// ONE HALF OF THE CONDITION CANNOT BE CHECKED FOR THIS GRAPH, and it is worth being exact about
// which and why rather than quietly checking the other. The sum collapses axis 1, the streamed axis,
// and no runner folds over time: the reference walk refuses it (`collapse_over` in
// `tests/reference/elements.hpp`) and so does the loop tree ("this collapses the streamed axis"). So
// this graph cannot be EXECUTED, at any extent, symbolic or literal, and its half (b) does not exist.
// Half (a) is checked in full, over every value.
//
// This is why the condition's "at least two graphs" is satisfied by the biquad and the comb, which
// satisfy BOTH halves, and the reduction is a third graph on top of them rather than one of the two.

constexpr Graph reduction(Extent rows, const Bindings& sizes) {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{rows, 8}, ValueCategory::Input);
    const auto bias = add_value(g, ElemType::F32, Shape{rows, 8}, ValueCategory::Input);
    const auto sum = add_node(g, op::Add, x, bias);

    OpAttr collapse{};
    collapse.kind = AttrKind::Reduce;
    collapse.reduce.axes = Axes{1};
    const auto total = add_node(g, op::Reduce, sum, kNoValue, kNoValue, collapse);

    // And back out again through a rule that keeps the axis, so the symbol is read on both sides of
    // a collapse rather than only on the way in.
    OpAttr keep{};
    keep.kind = AttrKind::Reduce;
    keep.reduce.axes = Axes{0};
    keep.reduce.keep_axes = true;
    add_node(g, op::Reduce, total, kNoValue, kNoValue, keep);

    const Diagnostic d = infer(g, sizes);
    if (!d.ok) {
        fail("the reduction graph did not resolve");
    }
    return g;
}

constexpr Graph kReductionLiteral = reduction(literal_extent(kBlockSize), Bindings{});
constexpr Graph kReductionSymbolic = reduction(Extent{kLength}, Bindings{}.bind(kLength, kBlockSize));

static_assert(every_shape_agrees(kReductionSymbolic, kReductionLiteral),
              "a symbol resolved through ReduceAxes agrees with its literal twin, on every value");

// Spelled out as well as compared, so a failure says which shape moved rather than only that one
// did — and so the test states what the reduction is supposed to do.
static_assert(kReductionSymbolic == kReductionLiteral);
static_assert(kReductionSymbolic.values[2].shape == Shape{kBlockSize, 8}, "the sum keeps both axes");
static_assert(kReductionSymbolic.values[3].shape == Shape{kBlockSize}, "collapsing axis 1 drops it");
static_assert(kReductionSymbolic.values[4].shape == Shape{1}, "collapsing axis 0 while keeping it leaves extent 1");

// Unbound, the symbol survives the collapse rather than being resolved to something convenient.
static_assert(reduction(Extent{kLength}, Bindings{}).values[3].shape == Shape{kLength},
              "an unbound symbol comes out of a reduction still a symbol");

// ---- oracles O2 and O3, extended to a graph with a symbolic extent -----------------------------
//
// M2 says it EXTENDS these two, and this is where. Both were written against literal extents only,
// so neither could have seen a symbolic path diverging — O2 comparing two construction paths that
// both mishandle a symbol identically, and O3 comparing a planned run with the walk of a graph that
// never had one.

// The graph `Build` makes, run once on `Executor` with an impulse into its one input, in buffers of
// the graph's element type `V`.
template <auto Build, class V = double>
std::array<double, kBlock> executed() {
    Executor<Build> ex;
    testing::held(ex);
    std::array<V, kBlock> in{};
    std::array<V, kBlock> out{};
    in[0] = 1;
    ex.input("x") = in.data();
    ex.output("y") = out.data();
    ex.run();
    std::array<double, kBlock> got{};
    for (std::size_t n = 0; n < kBlock; ++n) {
        got[n] = static_cast<double>(out[n]);
    }
    return got;
}

// O3, on a symbolic graph: walked, and run as planned (`High`, where the loop tree computes as the walk
// does). A plan may not change a number, and now that is required of a graph whose size arrived as a
// binding rather than as a literal.
constexpr Graph biquad_symbolic_high() {
    return biquad(Extent{kLength}, Bindings{}.bind(kLength, kBlockSize), numerics::Precision::High);
}

// ---- graph four: an unfold whose FRAME COUNT is a symbol, executed (M5.3's twin) ---------------
//
// The reduction above checks only half (a): it cannot be executed, so its numbers are not comparable.
// An unfold CAN be executed now — M5's axis-capable executor runs it — so this is the graph that closes
// M5.3's "output bit-identical to the literal twin" for `_unfold`. What is under test is a symbolic
// FRAME COUNT: the framed axis L is a symbol bound to six, and the frame count `N_out = (L-window)/hop
// +1` is a DERIVED symbol the pass records for `_unfold`, the first production caller of `relate()`.
// Bound, it must resolve to the same four frames its literal twin has and produce the same numbers.
//
// The framed value's numbers are folded back to one per sample so the graph runs as a narrow filter
// does; a symbolic frame count that resolved wrongly would give a buffer the wrong width or the fold
// the wrong number of elements, and the run would diverge or fault.

constexpr Dim kRows{0};    // the framed axis, L
constexpr Dim kFrames{1};  // the declared frame count, N_out

constexpr Graph unfolding(Extent rows, std::int32_t frame_symbol, const Bindings& sizes) {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{kBlockSize}, ValueCategory::Input);
    // A wide value of `rows` bands, one number per sample, DERIVED from the input so the walk still
    // takes one number per sample. A literal is one number and repeats over the band axis.
    const auto gain = add_value(g, ElemType::F64, Shape{rows, kBlockSize}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(gain)].constant = 0.5;
    const auto band = add_node(g, op::Mul, x, gain);

    OpAttr frame{};
    frame.kind = AttrKind::View;
    frame.view.kind = ViewKind::Unfold;
    frame.view.axis = 0;
    frame.view.window = 3;
    frame.view.hop = 1;
    frame.view.frames = frame_symbol; // -1 for the literal twin, the declared N_out for the symbolic one
    const auto framed = add_node(g, op::Unfold, band, kNoValue, kNoValue, frame);

    OpAttr narrow{};
    narrow.kind = AttrKind::Reduce;
    narrow.reduce.axes = Axes{0, 1}; // fold the frame axis and the window axis back to one number/sample
    const auto y = add_node(g, op::Reduce, framed, kNoValue, kNoValue, narrow);

    g.inputs.push_back(NamedValue{Name{"x"}, x});
    g.outputs.push_back(NamedValue{Name{"y"}, y});
    const Diagnostic d = infer(g, sizes);
    if (!d.ok) {
        fail("the unfold graph did not resolve");
    }
    return g;
}

constexpr Graph kUnfoldLiteral = unfolding(literal_extent(6), -1, Bindings{});
constexpr Graph kUnfoldSymbolic = unfolding(Extent{kRows}, kFrames.id, Bindings{}.bind(kRows, 6));

// (a) every value's resolved shape agrees — value 3 is the framed value, four frames of window three.
static_assert(every_shape_agrees(kUnfoldSymbolic, kUnfoldLiteral),
              "a symbol resolved into a derived frame count agrees with its literal twin, on every value");
static_assert(kUnfoldSymbolic.values[3].shape == Shape{4, 3, kBlockSize},
              "the framed axis of six becomes (6-3)/1+1 = 4 frames of window 3");

// The symbolic graph really carried symbols, so the agreement above is not a thing compared with
// itself: unbound it is not static, and bound the symbols are gone.
static_assert(!is_static(unfolding(Extent{kRows}, kFrames.id, Bindings{})),
              "unbound, the unfold still carries its framed axis and derived frame count");
static_assert(is_static(kUnfoldSymbolic), "and bound, it carries none");

// (b) the EXECUTED output is bit-identical to the literal twin's over 64 samples, run on `Executor`
// (in `main`).
constexpr Graph unfold_literal() { return kUnfoldLiteral; }
constexpr Graph unfold_symbolic() { return kUnfoldSymbolic; }

// ---- graph five: a 2-D unfold whose BOTH frame counts are symbols, executed (M5.3's twin, S3f-2) ----
//
// The 1-D twin above closed M5.3 for `_unfold`; this closes it for `_unfld2d`, whose derived count is
// two `relate()` calls rather than one. Two outer axes L1, L2 are symbols bound to six and five, each
// framed to a DERIVED count the pass records; bound, both must resolve to the counts the literal twin
// has and produce the same numbers. The 2-D framed value is folded back to one per sample so the graph
// runs as a narrow filter does — a count that resolved wrongly would give a
// buffer the wrong width or the fold the wrong element count, and the run would diverge or fault.

constexpr Dim kA1{0}; // the first framed axis, L1
constexpr Dim kN1{1}; // its declared frame count
constexpr Dim kA2{2}; // the second framed axis, L2
constexpr Dim kN2{3}; // its declared frame count

constexpr Graph unfolding2d(Extent r1, Extent r2, std::int32_t n1, std::int32_t n2, const Bindings& sizes) {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{kBlockSize}, ValueCategory::Input);
    // A value of r1 x r2 bands, one number per sample, DERIVED from the input so the walk still takes
    // one number per sample; the two outer axes are what the 2-D window frames.
    const auto gain = add_value(g, ElemType::F64, Shape{r1, r2, kBlockSize}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(gain)].constant = 0.5;
    const auto band = add_node(g, op::Mul, x, gain);

    OpAttr frame{};
    frame.kind = AttrKind::View;
    frame.view.kind = ViewKind::Unfold2d;
    frame.view.axis = 0;
    frame.view.window = 3;
    frame.view.hop = 1; // (6-3)/1+1 = 4 frames of window 3 on the first axis
    frame.view.frames = n1;
    frame.view.axis2 = 1;
    frame.view.window2 = 2;
    frame.view.hop2 = 1; // (5-2)/1+1 = 4 frames of window 2 on the second axis
    frame.view.frames2 = n2;
    const auto framed = add_node(g, op::Unfold2d, band, kNoValue, kNoValue, frame);

    OpAttr narrow{};
    narrow.kind = AttrKind::Reduce;
    narrow.reduce.axes = Axes{0, 1, 2, 3}; // fold both frame axes and both window axes back to one/sample
    const auto y = add_node(g, op::Reduce, framed, kNoValue, kNoValue, narrow);

    g.inputs.push_back(NamedValue{Name{"x"}, x});
    g.outputs.push_back(NamedValue{Name{"y"}, y});
    const Diagnostic d = infer(g, sizes);
    if (!d.ok) {
        fail("the 2-D unfold graph did not resolve");
    }
    return g;
}

constexpr Graph kUnfold2dLiteral = unfolding2d(literal_extent(6), literal_extent(5), -1, -1, Bindings{});
constexpr Graph kUnfold2dSymbolic =
    unfolding2d(Extent{kA1}, Extent{kA2}, kN1.id, kN2.id, Bindings{}.bind(kA1, 6).bind(kA2, 5));

// (a) every value's resolved shape agrees — value 3 is the framed value, [4, 3, 4, 2, kBlockSize].
static_assert(every_shape_agrees(kUnfold2dSymbolic, kUnfold2dLiteral),
              "two symbols resolved into two derived frame counts agree with the literal twin, everywhere");
static_assert(kUnfold2dSymbolic.values[3].shape == Shape{4, 3, 4, 2, kBlockSize},
              "axis 0 of six -> 4 frames of window 3, axis 1 of five -> 4 frames of window 2, interleaved");

// The symbolic graph really carried symbols: unbound it is not static, and bound the symbols are gone.
static_assert(!is_static(unfolding2d(Extent{kA1}, Extent{kA2}, kN1.id, kN2.id, Bindings{})),
              "unbound, the 2-D unfold still carries its two framed axes and two derived counts");
static_assert(is_static(kUnfold2dSymbolic), "and bound, it carries none");

// (b) the EXECUTED output is bit-identical to the literal twin's over the block (in `main`).
constexpr Graph unfold2d_literal() { return kUnfold2dLiteral; }
constexpr Graph unfold2d_symbolic() { return kUnfold2dSymbolic; }

// ---- graph six: an overlap-add whose FRAME COUNT is a symbol, executed (M5.3/M5.4's twin, S4c) ------
//
// `_ovadd` is the adjoint of `_unfold` and, unlike the old tree, it EXECUTES a symbolic frame count rather
// than rejecting one — which is the whole of what M5.4 makes load-bearing. This graph frames a band signal
// with `_unfold` (its frame count a DERIVED symbol) and reassembles it with `_ovadd`, then folds the
// reconstruction back to one number per sample so it runs as a narrow filter does.
// The symbolic frame count F reaches `_ovadd` as its input's OWN axis, not as a declared attribute; bound,
// it must resolve to the same four frames the literal twin has, `_ovadd` derive the same
// `out_len = (F-1)*hop + window`, and the run produce the same numbers. A frame count that resolved wrongly
// would give `_ovadd` the wrong output width or the reader the wrong contributors, and the run would
// diverge or fault.

constexpr Dim kOvRows{0};   // the framed axis, L
constexpr Dim kOvFrames{1}; // the frame count the unfold derives from it, F

constexpr Graph overlapping(Extent rows, std::int32_t frame_symbol, const Bindings& sizes) {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{kBlockSize}, ValueCategory::Input);
    // A wide value of `rows` bands, one number per sample, DERIVED from the input so the walk still takes
    // one number per sample; a literal is one number and repeats over the band axis.
    const auto gain = add_value(g, ElemType::F64, Shape{rows, kBlockSize}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(gain)].constant = 0.5;
    const auto band = add_node(g, op::Mul, x, gain);

    OpAttr frame{};
    frame.kind = AttrKind::View;
    frame.view.kind = ViewKind::Unfold;
    frame.view.axis = 0;
    frame.view.window = 3;
    frame.view.hop = 1;
    frame.view.frames = frame_symbol; // -1 for the literal twin, the declared N_out for the symbolic one
    const auto framed = add_node(g, op::Unfold, band, kNoValue, kNoValue, frame); // [F, 3, kBlockSize]

    OpAttr recombine{};
    recombine.kind = AttrKind::OverlapAdd;
    recombine.overlap.window = 3;
    recombine.overlap.hop = 1;
    const auto joined = add_node(g, op::OverlapAdd, framed, kNoValue, kNoValue, recombine); // [out_len, kBlockSize]

    OpAttr narrow{};
    narrow.kind = AttrKind::Reduce;
    narrow.reduce.axes = Axes{0}; // fold the reconstructed axis back to one number/sample
    const auto y = add_node(g, op::Reduce, joined, kNoValue, kNoValue, narrow);

    g.inputs.push_back(NamedValue{Name{"x"}, x});
    g.outputs.push_back(NamedValue{Name{"y"}, y});
    const Diagnostic d = infer(g, sizes);
    if (!d.ok) {
        fail("the overlap-add graph did not resolve");
    }
    return g;
}

constexpr Graph kOvaLiteral = overlapping(literal_extent(6), -1, Bindings{});
constexpr Graph kOvaSymbolic = overlapping(Extent{kOvRows}, kOvFrames.id, Bindings{}.bind(kOvRows, 6));

// (a) every value's resolved shape agrees — value 3 is the framed value (four frames of window three),
// value 4 the reconstruction: `out_len = (4-1)*1 + 3 = 6`, back to the six rows it framed.
static_assert(every_shape_agrees(kOvaSymbolic, kOvaLiteral),
              "a symbol resolved into a derived frame count and back through an overlap-add agrees with its literal twin");
static_assert(kOvaSymbolic.values[3].shape == Shape{4, 3, kBlockSize},
              "the framed axis of six becomes (6-3)/1+1 = 4 frames of window 3");
static_assert(kOvaSymbolic.values[4].shape == Shape{6, kBlockSize},
              "and the overlap-add reconstructs (4-1)*1+3 = 6 from the symbolic frame count");

// The frame-count symbol genuinely drives the reconstruction: bound to a DIFFERENT length it derives a
// different `out_len`, so the agreement above is not a symbol that was quietly ignored. Unbound the graph
// is REFUSED rather than deferred — an overlap-add is a CONSUMER of a frame count, not its declarer as an
// `_unfold` is, so one that never resolves has no output length to derive (that refusal is
// `spectral_ovadd_test.cpp`'s "unresolved symbolic frame count"). Bound, every symbol is gone.
static_assert(overlapping(Extent{kOvRows}, kOvFrames.id, Bindings{}.bind(kOvRows, 8)).values[4].shape ==
                  Shape{8, kBlockSize},
              "binding the framed length to eight reconstructs a length of eight — the symbol is real");
static_assert(is_static(kOvaSymbolic), "and the bound graph carries no symbol");

// (b) the EXECUTED output is bit-identical to the literal twin's over 64 samples — M5.4's headline, a
// symbolic frame count run rather than rejected as the old tree rejects it. The loop tree does not run an
// overlap-add yet, so it is the walk that runs it.
static_assert(is_a_real_signal<kBlock>(walk<kBlock>(kOvaLiteral)), "the overlap-add twin produced silence");
static_assert(walk<kBlock>(kOvaSymbolic) == walk<kBlock>(kOvaLiteral),
              "the symbolic frame count executes through an overlap-add bit-identically to its literal twin, not close");

// ---- graph seven: a split reshape whose SOURCE axis is a symbol, executed (M5.3's twin, S8) --------
//
// M5.3 asks the symbolic twin for one graph per structural family: an unfold (graph four), an
// overlap-add (graph six), and a SPLIT-OR-MERGE RESHAPE — `ViewKind::Reshape`, a rearrangement that
// keeps the element sequence and changes only the extents it is read with. Unlike the unfold and the overlap-add, a reshape declares its target extents
// outright (`viewed_shape` returns the attribute's shape), so the symbol here is not a DERIVED count but
// the SOURCE axis being split: a band value of `N` rows, `N` a symbol bound to six, read as `[2, 3]`.
// Bound, the band value must resolve to the six rows its literal twin has and the split feed the same
// numbers through the run; unbound, the band still carries `N`, so the agreement below is not a
// symbol quietly ignored — half (a) compares the band value itself, which is `{N}` until it is bound.
// The reshaped value is folded back to one number per sample so it runs as a narrow filter does.

constexpr Dim kReRows{0}; // the source axis that is split, N

constexpr Graph reshaping(Extent rows, const Bindings& sizes) {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{kBlockSize}, ValueCategory::Input);
    // A value of `rows` bands, one number per sample, DERIVED from the input so the walk still takes
    // one number per sample; a literal is one number and repeats over the band axis.
    const auto gain = add_value(g, ElemType::F64, Shape{rows, kBlockSize}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(gain)].constant = 0.5;
    const auto band = add_node(g, op::Mul, x, gain); // [N, kBlockSize]

    OpAttr split{};
    split.kind = AttrKind::View;
    split.view.kind = ViewKind::Reshape;
    split.view.shape = Shape{2, 3, kBlockSize}; // six rows read as 2x3 — one axis SPLIT into two
    const auto reshaped = add_node(g, op::Reshape, band, kNoValue, kNoValue, split); // [2, 3, kBlockSize]

    OpAttr narrow{};
    narrow.kind = AttrKind::Reduce;
    narrow.reduce.axes = Axes{0, 1}; // fold both split axes back to one number/sample
    const auto y = add_node(g, op::Reduce, reshaped, kNoValue, kNoValue, narrow);

    g.inputs.push_back(NamedValue{Name{"x"}, x});
    g.outputs.push_back(NamedValue{Name{"y"}, y});
    const Diagnostic d = infer(g, sizes);
    if (!d.ok) {
        fail("the reshape graph did not resolve");
    }
    return g;
}

constexpr Graph kReshapeLiteral = reshaping(literal_extent(6), Bindings{});
constexpr Graph kReshapeSymbolic = reshaping(Extent{kReRows}, Bindings{}.bind(kReRows, 6));

// (a) every value's resolved shape agrees — value 2 is the band, the one carrying the symbol; value 3
// the reshape reads its six rows with the split extents.
static_assert(every_shape_agrees(kReshapeSymbolic, kReshapeLiteral),
              "a symbol resolved as the source axis of a split reshape agrees with its literal twin, everywhere");
static_assert(kReshapeSymbolic.values[2].shape == Shape{6, kBlockSize},
              "the band value's symbolic axis resolves to the six rows the split reads as 2x3");
static_assert(kReshapeSymbolic.values[3].shape == Shape{2, 3, kBlockSize},
              "and the reshape reads those six rows with the split extents");

// The band really carried a symbol: unbound the graph is not static, and bound the symbol is gone.
static_assert(!is_static(reshaping(Extent{kReRows}, Bindings{})),
              "unbound, the reshape still carries its symbolic source axis");
static_assert(is_static(kReshapeSymbolic), "and bound, it carries none");

// (b) the EXECUTED output is bit-identical to the literal twin's over 64 samples — M5.3's second half
// for the reshape family, run on `Executor` (in `main`).
constexpr Graph reshape_literal() { return kReshapeLiteral; }
constexpr Graph reshape_symbolic() { return kReshapeSymbolic; }

// A twin's run, against its literal twin's, and the literal one not silence.
template <auto Literal, auto Symbolic>
bool runs_as_its_twin(const char* what) {
    const std::array<double, kBlock> literal = executed<Literal>();
    const bool ok = is_a_real_signal<kBlock>(literal) && executed<Symbolic>() == literal;
    if (!ok) {
        std::printf("symbolic: the %s twin does not run bit for bit as its literal twin\n", what);
    }
    return ok;
}

} // namespace

int main() {
    // O2, on a symbolic graph: the same graph is BUILT AGAIN here, while the program runs, and must
    // come out as the value the compiler built — binding and all. The failure this rules out is a
    // compiler disagreeing with itself between its two paths, which the old tree actually observed,
    // and which nothing in this tree could previously have seen happen to a symbol.
    const Graph rebuilt = biquad(Extent{kLength}, Bindings{}.bind(kLength, kBlockSize));
    if (!(rebuilt == kBiquadSymbolic)) return 1;
    static constexpr Compiled kCompiled = compile(kBiquadSymbolic);
    static_assert(loops::accepted<kCompiled.program>());
    if (!(compile(rebuilt) == kCompiled)) return 2;

    // The unbound graph too, so what is compared is a graph that still HAS a symbol in it rather
    // than one where the symbol has already been substituted away.
    const Graph unbound_here = biquad(Extent{kLength}, Bindings{});
    if (!(unbound_here == biquad(Extent{kLength}, Bindings{}))) return 3;
    if (is_static(unbound_here)) return 4;

    // And the reduction, whose shapes are the only half of it this tree can run.
    const Graph reduction_again = reduction(Extent{kLength}, Bindings{}.bind(kLength, kBlockSize));
    if (!(reduction_again == kReductionSymbolic)) return 5;

    // The numbers, at run time as well as while compiling: the half a static_assert cannot show is
    // that the same code gives the same answer when the compiler is not doing the arithmetic.
    if (!(walk<kBlock>(kBiquadSymbolic) == walk<kBlock>(kBiquadLiteral))) return 6;
    if (!(walk<kBlock>(kCombSymbolic) == walk<kBlock>(kCombLiteral))) return 7;

    // O3: the biquad run as planned gives what the walk gives.
    if (!(executed<biquad_symbolic_high, float>() == walk<kBlock>(biquad_symbolic_high()))) return 8;

    // The unfold, 2-D unfold and reshape twins, run as planned.
    if (!runs_as_its_twin<unfold_literal, unfold_symbolic>("unfold")) return 9;
    if (!runs_as_its_twin<unfold2d_literal, unfold2d_symbolic>("2-D unfold")) return 10;
    if (!runs_as_its_twin<reshape_literal, reshape_symbolic>("reshape")) return 11;

    // The overlap-add twin at run time — a symbolic frame count EXECUTED, which is M5.4's whole point.
    if (!(walk<kBlock>(kOvaSymbolic) == walk<kBlock>(kOvaLiteral))) return 12;
    return 0;
}
