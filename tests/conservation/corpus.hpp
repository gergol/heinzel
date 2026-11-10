#pragma once

// The corpus: the named graphs whose analysed form this tree keeps a record of.
//
// Entries are built one at a time rather than held in a list. A graph is a large value and twelve of
// them alive at once is constant-evaluation memory spent for nothing: the check hashes each entry
// and lets it go.
//
// WHAT THE CORPUS IS CHOSEN TO COVER. Each entry is here because it puts a different decision of the
// analysis under the oracle, not because more entries are better:
//
//   0  biquad                        the ordinary case, and the graph everything else is measured on
//   1  biquad                        entry 0's graph; 1 and 6 once pinned other targets of the same
//                                    graph, and stay so that the indices other tests use stay put
//   2  comb                          a different arrangement: a subtraction, and a delay of the input
//   3  biquad, block bound to 64     an extent that arrived as a binding rather than as a literal.
//                                    Its hash is IDENTICAL to entry 0's, and that is the point rather
//                                    than a redundancy: a bound graph is its literal twin, so if the
//                                    solver ever started leaving a residue behind, this entry's hash
//                                    would move while entry 0's stayed
//   4  an input read twice           once the units-annotated entry; it lost its units with the units
//                                    check and stays so that the indices other tests use stay put
//   5  reduction                     a graph whose shapes come from ReduceAxes rather than Elementwise
//   6  biquad                        entry 0's graph (see entry 1)
//   7-10                             one graph per family the numeric set is made of (below)

#include <cstddef>

#include "heinzel/ir/ir.hpp"
#include "heinzel/sema/analyze.hpp"

#include "graph_hash.hpp"

#include "filters.hpp"
#include "hand_built_graph.hpp"

namespace heinzel {

struct CorpusEntry {
    Name name{};
    Graph graph{};
};

inline constexpr std::size_t kCorpusSize = 11;
inline constexpr std::int32_t kCorpusBlock = 64;

constexpr Graph corpus_input_read_twice() {
    Input delay_time{"delay_time", Signal{kCorpusBlock}, f32};
    Output y{"y"};
    y = delay_time * 2.0 - delay_time;
    Graph g = y;
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the corpus graph reading an input twice did not resolve");
    }
    return g;
}

constexpr Graph corpus_reduction_graph() {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{kCorpusBlock, 8}, ValueCategory::Input);
    OpAttr collapse{};
    collapse.kind = AttrKind::Reduce;
    collapse.reduce.axes = Axes{1};
    add_node(g, op::Reduce, x, kNoValue, kNoValue, collapse);
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the corpus reduction graph did not resolve");
    }
    return g;
}


// ---- the graphs milestone M4.7 added, one per family the numeric set is made of ------------------
//
// M4.7 asks that the corpus grow WITH the operation set, so that a decision change in any of the
// families M4 added is visible here rather than only in whatever test happened to touch it.
//
// Pinned to graphs that change only when somebody means to change them: a corpus that gained an
// operation the day the tree does would move every recorded hash on a commit that changed no decision,
// and a conservation oracle whose record is routinely re-taken is one that has been quietly turned off.
//
// They are never RUN, only analysed and hashed, so nothing below has to stay in any function's
// domain — what is under the oracle is the decisions, not the numbers.

// The elementwise arithmetic, comparison, selection and rounding M4 added: the operations whose
// shape rule is `Elementwise` and whose cost is `Cheap`, plus the two that are `Software` without
// being transcendental.
constexpr Graph corpus_elementwise_graph() {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{kCorpusBlock}, ValueCategory::Input);

    auto v = add_node(g, op::Min, x, add_node(g, op::Max, x, x));
    v = add_node(g, op::Add, v, add_node(g, op::Le, x, v));
    v = add_node(g, op::Add, v, add_node(g, op::Eq, x, v));
    v = add_node(g, op::Add, v, add_node(g, op::Sign, x));
    v = add_node(g, op::Add, v, add_node(g, op::Floor, x));
    v = add_node(g, op::Add, v, add_node(g, op::Ceil, x));
    v = add_node(g, op::Add, v, add_node(g, op::Round, x));
    v = add_node(g, op::Add, v, add_node(g, op::Trunc, x));
    v = add_node(g, op::Add, v, add_node(g, op::Rsqrt, x));
    v = add_node(g, op::Fma, x, v, x);

    g.inputs.push_back(NamedValue{Name{"x"}, x});
    g.outputs.push_back(NamedValue{Name{"y"}, v});
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the corpus elementwise graph did not resolve");
    }
    return g;
}

// The fifteen transcendentals.
constexpr Graph corpus_transcendental_graph() {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{kCorpusBlock}, ValueCategory::Input);

    auto v = add_node(g, op::Exp, x);
    v = add_node(g, op::Add, v, add_node(g, op::Log, x));
    v = add_node(g, op::Add, v, add_node(g, op::Exp2, x));
    v = add_node(g, op::Add, v, add_node(g, op::Log2, x));
    v = add_node(g, op::Add, v, add_node(g, op::Log10, x));
    v = add_node(g, op::Add, v, add_node(g, op::Sin, x));
    v = add_node(g, op::Add, v, add_node(g, op::Cos, x));
    v = add_node(g, op::Add, v, add_node(g, op::Tan, x));
    v = add_node(g, op::Add, v, add_node(g, op::SinPi, x));
    v = add_node(g, op::Add, v, add_node(g, op::Atan, x));
    v = add_node(g, op::Add, v, add_node(g, op::Asin, x));
    v = add_node(g, op::Add, v, add_node(g, op::Acos, x));
    v = add_node(g, op::Add, v, add_node(g, op::Tanh, x));
    v = add_node(g, op::Add, v, add_node(g, op::Atan2, x, v));
    v = add_node(g, op::Pow, v, x);

    g.inputs.push_back(NamedValue{Name{"x"}, x});
    g.outputs.push_back(NamedValue{Name{"y"}, v});
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the corpus transcendental graph did not resolve");
    }
    return g;
}

// A contraction of two operands: the shape rule `ContractAxes`, which is a broadcast and a collapse
// and is the one shape relation the reduction entry above does not reach.
constexpr Graph corpus_contraction_graph() {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{kCorpusBlock, 8}, ValueCategory::Input);
    const auto w = add_value(g, ElemType::F32, Shape{kCorpusBlock, 8}, ValueCategory::Input);
    OpAttr summed{};
    summed.kind = AttrKind::Contract;
    summed.contract.axes = Axes{1};
    add_node(g, op::Contract, x, w, kNoValue, summed);
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the corpus contraction graph did not resolve");
    }
    return g;
}

// The two lags M4 added beside the fixed tap the biquad already carries: the one that named its axis
// (`_delay`, a fixed tap since Plan/17 03) and the one whose distance is a SIGNAL.
constexpr Graph corpus_lags_graph() {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{kCorpusBlock}, ValueCategory::Input);

    OpAttr fixed{};
    fixed.kind = AttrKind::Delay;
    fixed.delay.samples = 3;
    const auto lagged = add_node(g, op::FixedDelay, x, kNoValue, kNoValue, fixed);

    OpAttr tapped{};
    tapped.kind = AttrKind::SignalledDelay;
    tapped.signalled_delay.max_delay = 4;
    const auto read = add_node(g, op::DelayLine, x, lagged, kNoValue, tapped);

    g.inputs.push_back(NamedValue{Name{"x"}, x});
    g.outputs.push_back(NamedValue{Name{"y"}, read});
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the corpus lags graph did not resolve");
    }
    return g;
}

constexpr CorpusEntry corpus_entry(std::size_t i) {
    switch (i) {
    case 0:
        return CorpusEntry{Name{"biquad"}, biquad(literal_extent(kCorpusBlock))};
    case 1:
        return CorpusEntry{Name{"biquad again"}, biquad(literal_extent(kCorpusBlock))};
    case 2:
        return CorpusEntry{Name{"comb"}, comb(literal_extent(kCorpusBlock))};
    case 3:
        return CorpusEntry{Name{"biquad, block bound to 64"}, biquad(Extent{Dim{0}}, Bindings{}.bind(Dim{0}, kCorpusBlock))};
    case 4:
        return CorpusEntry{Name{"an input read twice"}, corpus_input_read_twice()};
    case 5:
        return CorpusEntry{Name{"a reduction over one axis"}, corpus_reduction_graph()};
    case 6:
        return CorpusEntry{Name{"biquad once more"}, biquad(literal_extent(kCorpusBlock))};
    case 7:
        return CorpusEntry{Name{"the elementwise set at M4"}, corpus_elementwise_graph()};
    case 8:
        return CorpusEntry{Name{"the transcendental set at M4"}, corpus_transcendental_graph()};
    case 9:
        return CorpusEntry{Name{"a contraction of two operands"}, corpus_contraction_graph()};
    case 10:
        return CorpusEntry{Name{"the two lags that arrived at M4"}, corpus_lags_graph()};
    default:
        fail("heinzel: corpus entry out of range");
    }
    return CorpusEntry{};
}

// The analysed graph's hash for one entry.
constexpr std::uint64_t corpus_hash(std::size_t i) { return hash_graph(corpus_entry(i).graph); }

} // namespace heinzel
