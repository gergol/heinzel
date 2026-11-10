#pragma once

// The corpus every operation must appear in: graphs that are run, walked and — where the loop tree
// compiles them — on `Executor` too.
//
// Milestone M4.1's teeth. The condition is not "forty-two rows exist" — the rule table's own
// coverage check already proves that, and `tests/reference/meaning_coverage.hpp`'s proves each row that owes a
// meaning has one. What neither can prove is that every operation is RUN end to end somewhere, and
// runs the same on the walk and on the loop tree.
//
// So this is a corpus, and the check over it is a COVERAGE ASSERTION rather than a list of tests
// somebody remembered to write. Which operations a graph reaches is read off the graph's own nodes,
// not declared beside it — a hand-maintained list of forty-two is a list that will be forty-one the
// day the forty-third arrives.
//
// Every entry is walked except the spectral one: `_fft`'s arithmetic is a backend not written yet
// (`ElementKind::Boundary`), so its graph is analysed only. The entries the loop tree compiles (the
// per-sample four, the linked one and the run-start one) also run on `Executor` at `High` and must give the walk's
// numbers bit for bit.
//
// STAYING IN DOMAIN IS PART OF THE DESIGN HERE. A comparison of two blocks uses `==`, and a NaN is
// never equal to itself — so a graph that produced one would fail this check for a reason that has
// nothing to do with the runners. Every operand below is built from a value known to be at least two,
// or from one known to be within plus or minus one, and `no_nan` asserts the result rather than
// leaving it to be discovered. The input is declared within plus or minus one, so the proof of ranges
// (Plan/26-value-ranges) knows it too.

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"

#include "compute_reference.hpp"
#include "hand_built_graph.hpp"
#include "axis_changing_contributions.hpp"

namespace heinzel::testing {


// ---- building one ------------------------------------------------------------------------------

struct Body {
    Graph g{};
    std::int32_t x{kNoValue};    // the named input
    std::int32_t big{kNoValue};  // >= 2 everywhere: safe for logs, roots, reciprocals and powers
    std::int32_t small{kNoValue}; // within +/- 1 everywhere: safe for the inverse circular family
};

constexpr std::int32_t constant(Graph& g, double value) {
    const auto v = add_value(g, ElemType::F64, Shape{kBlock}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(v)].constant = value;
    return v;
}

// An input, and the two safe operands every graph here is built out of. On an impulse the input is
// one at the first sample and zero after it, so `big` is three then two and `small` is a half then
// a quarter — both comfortably inside every domain used below. Declared in [-1, 1], `big` is proven
// in [1, 3] and `small` in [0.25, 0.75].
constexpr Body begin() {
    Body b{};
    b.x = add_value(b.g, ElemType::F64, Shape{kBlock}, ValueCategory::Input);
    add_range(b.g, b.x, Range{{Interval{-1.0, 1.0, false, false}}});
    b.big = add_node(b.g, op::Add, b.x, constant(b.g, 2.0));
    b.small = add_node(b.g, op::Mul, b.big, constant(b.g, 0.25));
    b.g.inputs.push_back(NamedValue{Name{"x"}, b.x});
    return b;
}

constexpr Graph finish(Body b, std::int32_t out) {
    b.g.outputs.push_back(NamedValue{Name{"y"}, out});
    return b.g;
}

// A one-operand operation applied to `in`, added onto what has been accumulated so far. Chaining
// this way keeps a graph small while reaching many operations: what the corpus must show is that no
// operation is covered by NONE, not that each gets a graph of its own.
constexpr std::int32_t also(Graph& g, std::int32_t sum, OpCode which, std::int32_t in,
                            std::int32_t second = kNoValue) {
    return add_node(g, op::Add, sum, add_node(g, which, in, second));
}

// ---- the entries -------------------------------------------------------------------------------

// Arithmetic and choice. `_fma`, `_madd` and `_select` take three operands and `_sign` answers from one, so
// they are built directly rather than through `also`. `_madd` rounds as the machine rounds, a freedom the analysis
// allows only where granted, so its value is pinned to `Normal` (in double, the walk and the loop tree compute it
// with one function, so the two still agree bit for bit).
constexpr Graph arithmetic_graph() {
    Body b = begin();
    Graph& g = b.g;
    std::int32_t sum = add_node(g, op::Sub, b.big, b.small);
    sum = also(g, sum, op::Div, b.big, b.small);
    sum = also(g, sum, op::Neg, b.small);
    sum = also(g, sum, op::Min, b.big, b.small);
    sum = also(g, sum, op::Max, b.big, b.small);
    sum = add_node(g, op::Add, sum, add_node(g, op::Fma, b.big, b.small, b.x));
    const std::int32_t machine_rounded = add_node(g, op::MultiplyAdd, b.big, b.small, b.x);
    g.values[static_cast<std::size_t>(machine_rounded)].precision = numerics::Precision::Normal;
    sum = add_node(g, op::Add, sum, machine_rounded);
    sum = also(g, sum, op::Sign, b.small);
    sum = also(g, sum, op::Abs, b.x);
    // `_realize` is value-for-value the identity (a materialisation barrier, `docs/decisions.md`), so it
    // is reached here among the per-sample operations rather than beside the axis-changing views: run both
    // ways it must hand `big` back unchanged, bit for bit, which is the whole of its M5.1 obligation.
    sum = also(g, sum, op::Realize, b.big);
    return finish(b, sum);
}

// Comparison and choice. Each answers one or zero in the operands' own type, and `_select` picks
// between two of them — so the graph reaches all four and stays in domain by construction.
constexpr Graph comparison_graph() {
    Body b = begin();
    Graph& g = b.g;
    const std::int32_t less = add_node(g, op::Lt, b.small, b.big);
    std::int32_t sum = also(g, less, op::Le, b.big, b.small);
    sum = also(g, sum, op::Eq, b.big, b.big);
    sum = add_node(g, op::Add, sum, add_node(g, op::Select, less, b.big, b.small));
    return finish(b, sum);
}

// Rounding, and the one operation whose result type is stated rather than derived.
constexpr Graph rounding_graph() {
    Body b = begin();
    Graph& g = b.g;
    std::int32_t sum = add_node(g, op::Floor, b.small);
    sum = also(g, sum, op::Ceil, b.small);
    sum = also(g, sum, op::Round, b.small);
    sum = also(g, sum, op::Trunc, b.small);
    OpAttr narrow{};
    narrow.kind = AttrKind::Cast;
    narrow.cast.to = ElemType::F32;
    sum = add_node(g, op::Add, sum, add_node(g, op::Cast, b.big, kNoValue, kNoValue, narrow));
    return finish(b, sum);
}

// Roots, and the exponential family. Every argument is at least two, so no logarithm sees a
// non-positive number and no root sees a negative one.
constexpr Graph exponential_graph() {
    Body b = begin();
    Graph& g = b.g;
    std::int32_t sum = add_node(g, op::Sqrt, b.big);
    sum = also(g, sum, op::Rsqrt, b.big);
    sum = also(g, sum, op::Exp, b.small);
    sum = also(g, sum, op::Exp2, b.small);
    sum = also(g, sum, op::Log, b.big);
    sum = also(g, sum, op::Log2, b.big);
    sum = also(g, sum, op::Log10, b.big);
    sum = also(g, sum, op::Tanh, b.small);
    sum = add_node(g, op::Add, sum, add_node(g, op::Pow, b.big, b.small));
    return finish(b, sum);
}

// The circular family and its inverses. `_asin` and `_acos` are the reason `small` exists: their
// argument has to be within plus or minus one or the answer is not a number.
constexpr Graph circular_graph() {
    Body b = begin();
    Graph& g = b.g;
    std::int32_t sum = add_node(g, op::Sin, b.small);
    sum = also(g, sum, op::Cos, b.small);
    sum = also(g, sum, op::Tan, b.small);
    sum = also(g, sum, op::SinPi, b.small);
    sum = also(g, sum, op::Atan, b.big);
    sum = also(g, sum, op::Asin, b.small);
    sum = also(g, sum, op::Acos, b.small);
    sum = add_node(g, op::Add, sum, add_node(g, op::Atan2, b.big, b.small));
    return finish(b, sum);
}

// The lags and the recurrence: everything whose meaning is about WHEN rather than what.
//
// The tap delay line's position is `small`, which is a quarter or a half — so the tap is at this
// sample and the blend is between it and the one before, which is what makes a delay line different
// from the fixed tap beside it rather than a slower spelling of it.
constexpr Graph stateful_graph() {
    Body b = begin();
    Graph& g = b.g;

    OpAttr fixed{};
    fixed.kind = AttrKind::Delay;
    fixed.delay.samples = 2;
    std::int32_t sum = add_node(g, op::FixedDelay, b.big, kNoValue, kNoValue, fixed);

    OpAttr deeper{};
    deeper.kind = AttrKind::Delay;
    deeper.delay.samples = 3; // (once `_delay`, the lag that named its axis)
    sum = add_node(g, op::Add, sum, add_node(g, op::FixedDelay, b.big, kNoValue, kNoValue, deeper));

    OpAttr tapped{};
    tapped.kind = AttrKind::SignalledDelay;
    tapped.signalled_delay.max_delay = 4;
    sum = add_node(g, op::Add, sum, add_node(g, op::DelayLine, b.big, b.small, kNoValue, tapped));

    // A value defined in terms of its own past. Nothing else produces it; the recurrence names it,
    // and a fixed delay reads it back.
    const auto state = add_value(g, ElemType::F64, Shape{kBlock}, ValueCategory::Variable);
    OpAttr previous{};
    previous.kind = AttrKind::Delay;
    previous.delay.samples = 1;
    const auto back = add_node(g, op::FixedDelay, state, kNoValue, kNoValue, previous);
    const auto next = add_node(g, op::Add, sum, add_node(g, op::Mul, back, constant(g, 0.5)));
    OpAttr defines{};
    defines.kind = AttrKind::Recurrence;
    defines.recurrence.defines = state;
    const auto y = add_node(g, op::Recurrence, next, kNoValue, kNoValue, defines);
    return finish(b, y);
}

// STRUCTURAL: the operations that change axes (`_reduce`, `_contr`, `_reshape`, `_xpose`, `_slice` and
// the contributions in `axis_changing_contributions.hpp`), each with an ELEMENT meaning, so the entry is
// walked for numbers. The time axis is innermost, so the wide values are DERIVED from the narrow named
// input, and the entry folds them back to one number per sample.
constexpr Graph axis_changing_graph() {
    Body b = begin();
    Graph& g = b.g;
    const Shape wide{4, kBlock};

    // Four numbers per sample, derived from one. A literal is one number and repeats over the
    // elements of the value it stands for, which is what widens `x` without a wide input.
    const auto gain = add_value(g, ElemType::F64, wide, ValueCategory::Const);
    g.values[static_cast<std::size_t>(gain)].constant = 0.5;
    const auto channels = add_node(g, op::Mul, b.x, gain);

    // The contraction's second operand, DERIVED rather than named: the walk here takes one named input
    // of one number per sample. A contraction of two independently computed wide operands is still a
    // contraction of two operands, and `_mul` is already reached by the arithmetic entry.
    const auto weight = add_value(g, ElemType::F64, wide, ValueCategory::Const);
    g.values[static_cast<std::size_t>(weight)].constant = 0.25;
    const auto w = add_node(g, op::Mul, b.x, weight);

    OpAttr fold{};
    fold.kind = AttrKind::Reduce;
    fold.reduce.axes = Axes{0};
    const auto total = add_node(g, op::Reduce, channels, kNoValue, kNoValue, fold);

    OpAttr summed{};
    summed.kind = AttrKind::Contract;
    summed.contract.axes = Axes{0};
    const auto contracted = add_node(g, op::Contract, channels, w, kNoValue, summed);

    // The two rearrangements that MOVE elements — the fold and the contraction above collapse,
    // `_reshape` below keeps the sequence, these two do not. Each is a named step above, and each folds
    // back to one number per sample so the entry stays runnable.
    const auto folded = a_transposed_contribution(g, b.x);
    const auto stripped = a_sliced_contribution(g, b.x);
    const auto framed = an_unfolded_contribution(g, b.x);
    const auto framed2d = an_unfolded_2d_contribution(g, b.x);
    const auto reversed = a_flipped_contribution(g, b.x);
    const auto grown = a_padded_contribution(g, b.x);
    const auto joined_back = a_concatenated_contribution(g, b.x);
    const auto packed_back = a_packed_contribution(g, b.x);
    const auto overlapped_back = an_overlap_added_contribution(g, b.x);
    const auto ordered_back = an_ordered_contribution(g, b.x);
    const auto tabled_back = a_table_read_contribution(g, b.x);
    const auto counted_back = an_iota_contribution(g); // a generator: no operand, so it takes no `x`
    const auto views = add_node(g, op::Add, add_node(g, op::Add, add_node(g, op::Add, total, contracted), folded), stripped);
    const auto some = add_node(g, op::Add, add_node(g, op::Add, add_node(g, op::Add, views, framed), framed2d), reversed);
    const auto both = add_node(g, op::Add, add_node(g, op::Add, some, grown), joined_back);
    const auto scattered = add_node(g, op::Add, add_node(g, op::Add, both, packed_back), overlapped_back);
    const auto gathered = add_node(g, op::Add, add_node(g, op::Add, scattered, ordered_back), tabled_back);
    const auto all = add_node(g, op::Add, add_node(g, op::Add, gathered, counted_back), a_constant_contribution(g)); // + the generators'
    OpAttr flatten{};
    flatten.kind = AttrKind::View;
    flatten.view.kind = ViewKind::Reshape;
    flatten.view.shape = Shape{kBlock};
    const auto out = add_node(g, op::Reshape, all, kNoValue, kNoValue, flatten);

    return finish(b, out);
}

// The SPECTRAL entry (milestone M5, S4a). `_fft` is an intrinsic boundary: its shape and precondition are
// modelled, its arithmetic is a backend not written yet, so this entry is analysed and not run. A forward
// real transform of the block: `Shape{kBlock}` -> `Shape{2*(kBlock/2+1)}`. One node, and the only entry
// that reaches `op::Fft`, so the coverage assertion needs it.
constexpr Graph spectral_graph() {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{kBlock}, ValueCategory::Input);
    g.inputs.push_back(NamedValue{Name{"x"}, x});
    OpAttr k{};
    k.kind = AttrKind::Fft;
    k.fft.n = kBlock;
    k.fft.dir = FftDir::Forward;
    const auto spectrum = add_node(g, op::Fft, x, kNoValue, kNoValue, k);
    g.outputs.push_back(NamedValue{Name{"y"}, spectrum});
    return g;
}

// The GENERAL-CONTRACTION entry (milestone MB). A transposing contraction `"ik,kj->ij"` at K=1: a
// `[I=1, K=1]` named input against a constant weight b `[K=1, J=2]` over the streamed block gives
// `[I=1, J=2]`, two numbers per sample. The contracted axis is extent one only so the input stays one
// number per sample. The only entry that reaches `op::MatMul`, so the coverage assertion needs it;
// `matmul_test.cpp` runs `_matmul` in full.
constexpr Graph matmul_graph() {
    Graph g{};
    const auto a = add_value(g, ElemType::F64, Shape{1, 1, kBlock}, ValueCategory::Input);
    g.inputs.push_back(NamedValue{Name{"a"}, a});
    const auto b = add_value(g, ElemType::F64, Shape{1, 2, kBlock}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(b)].constant = 0.5;
    OpAttr k{};
    k.kind = AttrKind::MatMul;
    k.matmul.a_contract = 1; // a's K axis (extent 1)
    k.matmul.b_contract = 0; // b's K axis (extent 1)
    const auto out = add_node(g, op::MatMul, a, b, kNoValue, k);
    g.outputs.push_back(NamedValue{Name{"y"}, out});
    return g;
}

// The LINKED entry (`docs/sections-in-lanes.md`): two one-pole sections in lanes, their states `V` over
// `[2, time]`, section 1 reading section 0's output of this sample through the link `u = _chain(small, V)`. The only
// entry that reaches `op::Chain`, so the coverage assertion needs it. Walked in two passes a sample (`passes_of`); the
// loop tree runs it in one skewed stage.
constexpr Graph linked_graph() {
    Body b = begin();
    Graph& g = b.g;
    const auto states = add_value(g, ElemType::F64, Shape{2, kBlock}, ValueCategory::Variable);
    const auto u = add_node(g, op::Chain, b.small, states);
    OpAttr previous{};
    previous.kind = AttrKind::Delay;
    previous.delay.samples = 1;
    const auto back = add_node(g, op::FixedDelay, states, kNoValue, kNoValue, previous);
    const auto fed = add_node(g, op::Mul, u, constant(g, 0.5));
    const auto kept = add_node(g, op::Mul, back, constant(g, 0.25));
    const auto next = add_node(g, op::Add, fed, kept);
    OpAttr defines{};
    defines.kind = AttrKind::Recurrence;
    defines.recurrence.defines = states;
    const auto settled = add_node(g, op::Recurrence, next, kNoValue, kNoValue, defines);
    OpAttr last{};
    last.kind = AttrKind::View;
    last.view.kind = ViewKind::Slice;
    last.view.axis = 0;
    last.view.offset = 1;
    last.view.length = 1;
    last.view.step = 1;
    const auto section1 = add_node(g, op::Slice, settled, kNoValue, kNoValue, last);
    OpAttr flatten{};
    flatten.kind = AttrKind::View;
    flatten.view.kind = ViewKind::Reshape;
    flatten.view.shape = Shape{kBlock};
    const auto out = add_node(g, op::Reshape, section1, kNoValue, kNoValue, flatten);
    return finish(b, out);
}

// The RUN-START entry (Plan/33 S3): a one-pole that a run's start sets anew, `v[n] = where(_first, big, v[n - 1] / 2)
// + small`. The only entry that reaches `op::First`, so the coverage assertion needs it; walked as one run, `_first` is
// 1 at the block's first sample alone (`first_sample_test.cpp` walks it in runs).
constexpr Graph started_graph() {
    Body b = begin();
    Graph& g = b.g;
    const auto state = add_value(g, ElemType::F64, Shape{kBlock}, ValueCategory::Variable);
    OpAttr previous{};
    previous.kind = AttrKind::Delay;
    previous.delay.samples = 1;
    const auto back = add_node(g, op::FixedDelay, state, kNoValue, kNoValue, previous);
    const auto halved = add_node(g, op::Mul, back, constant(g, 0.5));
    const auto starts = add_node(g, op::First, kNoValue);
    const auto chosen = add_node(g, op::Select, starts, b.big, halved);
    const auto next = add_node(g, op::Add, chosen, b.small);
    OpAttr defines{};
    defines.kind = AttrKind::Recurrence;
    defines.recurrence.defines = state;
    const auto settled = add_node(g, op::Recurrence, next, kNoValue, kNoValue, defines);
    return finish(b, settled);
}

// ---- the corpus ---------------------------------------------------------------------------------

inline constexpr std::size_t kCorpusSize = 11;

// Whether the walk can run the entry. An operation runs when it has a sample-at-a-time meaning or an
// ELEMENT meaning; `_fft`'s kind (`ElementKind::Boundary`) names a backend not written yet, so a graph
// holding one is analysed only. The walk's scalar `run` takes one named input of one number per sample.
constexpr bool has_no_runnable_meaning(OpCode op) {
    return element_kind_of(op) == ElementKind::None || element_kind_of(op) == ElementKind::Boundary;
}

constexpr bool is_runnable(const Graph& g) {
    for (const Node& n : g.nodes) {
        if (rules_of(n.op).meaning != MeaningRule::PerSample && has_no_runnable_meaning(n.op)) {
            return false;
        }
    }
    return g.inputs.size() == 1 && numbers_per_sample(g.values[static_cast<std::size_t>(g.inputs[0].value)].shape) == 1;
}

constexpr Graph corpus_graph(std::size_t i) {
    switch (i) {
    case 0:
        return arithmetic_graph();
    case 1:
        return comparison_graph();
    case 2:
        return rounding_graph();
    case 3:
        return exponential_graph();
    case 4:
        return circular_graph();
    case 5:
        return stateful_graph();
    case 6:
        return axis_changing_graph();
    case 7:
        return spectral_graph();
    case 8:
        return matmul_graph();
    case 9:
        return linked_graph();
    case 10:
        return started_graph();
    default:
        fail("heinzel::testing: both-ways corpus entry out of range");
    }
    return Graph{};
}

// ---- the coverage assertion ---------------------------------------------------------------------
//
// Which operations the corpus reaches, read off the graphs. `first_operation_not_reached` answers
// `kOpCount` when every row is covered, and otherwise the code of one that is not — so the failure
// can NAME it rather than only report that something is missing.
//
// Taken over an explicit corpus size and graph function so that a test can hand it a deliberately
// incomplete corpus and watch it object. A check only ever seen to pass is not evidence that it
// would fail.
template <class Entry>
constexpr std::uint8_t first_operation_not_reached(std::size_t size, Entry entry) {
    bool reached[256]{};
    for (std::size_t i = 0; i < size; ++i) {
        const Graph g = entry(i);
        for (const Node& n : g.nodes) {
            reached[n.op.index] = true;
        }
    }
    for (std::uint8_t code = 0; code < kOpCount; ++code) {
        if (!reached[code] && !is_retired(OpCode{code})) {
            return code;
        }
    }
    return kOpCount;
}

} // namespace heinzel::testing
