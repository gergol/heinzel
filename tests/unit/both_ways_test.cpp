// MILESTONE M4.1: every operation is reached by a graph that is run end to end — walked, and on `Executor`
// at `High` where the loop tree compiles it — and the two agree bit for bit.
//
// The condition's teeth, and the reason it is not the coverage checks the tree already has. Those prove
// that every row is complete (`ir/op_rules.hpp`) and that every row owing a meaning has one written
// (`tests/reference/meaning_coverage.hpp`). Neither can prove that an operation is ever RUN, or that it
// runs the same on the walk and on the loop tree.
//
// So the check is a coverage assertion OVER THE TABLE, computed from the corpus's own graphs
// (`support/both_ways.hpp`). Every entry is walked but the spectral one, whose `_fft` has no arithmetic
// written yet; the per-sample four and the linked one also run on `Executor` at `High`, where the loop tree
// computes as the walk does. The others the loop tree does not compile yet, which is pinned below. The walks and the runs
// are in `main`: constant evaluation of the transcendental cores costs more than it shows.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <memory>
#include <utility>
#include <vector>

#include "heinzel/compile.hpp"
#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"

#include "both_ways.hpp"
#include "filters.hpp"
#include "interpret.hpp"
#include "well_formed.hpp"

using namespace heinzel;
using namespace heinzel::testing;

namespace {

constexpr std::size_t kSamples = static_cast<std::size_t>(kBlock);
constexpr std::size_t kSpectral = 7;

// An entry analysed at `High`.
template <std::size_t I>
constexpr Graph at_high() {
    Graph g = corpus_graph(I);
    set_default_precision(g, numerics::Precision::High);
    if (!infer(g).ok) {
        fail("both_ways_test: a corpus graph did not resolve");
    }
    return g;
}

// The entries the loop tree compiles: the per-sample four and the linked one (its stage skewed). Checked
// against `try_compile` in `main`, so an entry that starts or stops compiling is noticed rather than silently run on
// one runner only.
constexpr std::array<bool, kCorpusSize> kOnTheLoopTree{true, true, true, false, false, true, false, false, false, true, true};

// The walk's answer: every number of the one output, sample-major.
std::vector<double> walked(const Graph& g) {
    const auto signals = std::make_unique<Signals<kSamples>>();
    run(g, impulse<kSamples>(), *signals);
    const std::int32_t out = g.outputs[0].value;
    const auto width = static_cast<std::size_t>(elements_of(g.values[static_cast<std::size_t>(out)].shape).count);
    std::vector<double> got(kSamples * width);
    for (std::size_t n = 0; n < kSamples; ++n) {
        for (std::size_t e = 0; e < width; ++e) {
            got[n * width + e] = sample_at(*signals, out, static_cast<std::int32_t>(e), n);
        }
    }
    return got;
}

// The loop tree's answer; the entries it compiles are one number per sample.
template <std::size_t I>
std::vector<double> executed() {
    const std::array<double, kSamples> in = impulse<kSamples>();
    std::vector<double> out(kSamples);
    Executor<at_high<I>> ex;
    ex.input("x") = in.data();
    ex.output("y") = out.data();
    ex.run();
    return out;
}

// Not a number is never equal to itself, so a graph that produced one would fail the comparison for a
// reason that has nothing to do with the runners; and an answer that never moves agrees with anything.
bool moves_and_is_a_number(const std::vector<double>& s) {
    bool moved = false;
    for (const double v : s) {
        if (v != v) {
            return false;
        }
        moved = moved || !(v == s[0]);
    }
    return moved;
}

template <std::size_t I>
bool entry_holds() {
    const std::unique_ptr<const CompileResult> compiled(new auto(try_compile(at_high<I>())));
    if (compiled->refusal.view().empty() != kOnTheLoopTree[I]) {
        std::printf("both_ways: entry %zu %s on the loop tree now\n", I, kOnTheLoopTree[I] ? "no longer compiles" : "compiles");
        return false;
    }
    if constexpr (kOnTheLoopTree[I]) {
        if (const std::expected<void, loops::Refusal> w = loops::well_formed(compiled->compiled.program); !w) {
            std::printf("both_ways: entry %zu: the runner does not accept its program: %.*s\n", I, static_cast<int>(w.error().size()), w.error().data());
            return false;
        }
    }
    if constexpr (I != kSpectral) {
        const std::vector<double> walk = walked(at_high<I>());
        if (!moves_and_is_a_number(walk)) {
            std::printf("both_ways: entry %zu walked to a NaN or to a constant\n", I);
            return false;
        }
        if constexpr (kOnTheLoopTree[I]) {
            if (!(executed<I>() == walk)) {
                std::printf("both_ways: entry %zu on the loop tree disagrees with the walk\n", I);
                return false;
            }
        }
    }
    return true;
}

template <std::size_t... I>
bool every_entry_holds(std::index_sequence<I...>) {
    return (entry_holds<I>() & ...);
}

// ---- the coverage assertion: no operation is reached by NONE of them -------------------------------

constexpr std::uint8_t uncovered_in_the_corpus() {
    return first_operation_not_reached(kCorpusSize, [](std::size_t i) { return corpus_graph(i); });
}

// The entries that are walked: all but the spectral one.
constexpr std::uint8_t uncovered_by_the_walked_entries() {
    return first_operation_not_reached(kCorpusSize - 1, [](std::size_t i) { return corpus_graph(i < kSpectral ? i : i + 1); });
}

// A corpus with the arithmetic entry left out, and what disappears with it is `_sub` — the first operation
// the sweep then finds unreached.
constexpr std::uint8_t uncovered_without_arithmetic() {
    return first_operation_not_reached(kCorpusSize - 1, [](std::size_t i) { return corpus_graph(i + 1); });
}

} // namespace

static_assert(uncovered_in_the_corpus() == kOpCount,
              "an operation in kOpTable appears in no graph of the corpus — add it to tests/support/both_ways.hpp");
// Every operation but `_fft` is reached by a walked entry: the spectral entry is `_fft` alone.
static_assert(corpus_graph(kSpectral).nodes.size() == 1 && corpus_graph(kSpectral).nodes[0].op == op::Fft,
              "the spectral entry is `_fft` alone, so it hides no other operation from the walk");
static_assert(uncovered_by_the_walked_entries() == op::Fft.index, "only `_fft` may be reached by no walked graph");
static_assert(!is_runnable(corpus_graph(kSpectral)), "the spectral entry became runnable — walk it");
static_assert([] {
    for (std::size_t i = 0; i < kCorpusSize; ++i) {
        if (i != kSpectral && !is_runnable(corpus_graph(i))) {
            return false;
        }
    }
    return true;
}(), "an entry other than the spectral one is not runnable on the walk");

// SHOWN REJECTING, and naming which. A check only ever seen to pass is not evidence that it would ever
// fail, and a check that reported only "something is missing" would leave the reader to find out what.
static_assert(uncovered_without_arithmetic() != kOpCount,
              "the coverage assertion must object to a corpus with an operation left out");
static_assert(name_of(OpCode{uncovered_without_arithmetic()}) == OpName{"_sub"},
              "and it must name the operation that is missing, not merely that one is");

int main() {
    return every_entry_holds(std::make_index_sequence<kCorpusSize>{}) ? 0 : 1;
}
