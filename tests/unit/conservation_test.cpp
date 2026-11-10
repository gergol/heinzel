// Oracle O5: today's analysed graph for a named graph equals the one this tree recorded earlier.
//
// This is CONSERVATION, and it is the one thing the tree's other oracles structurally cannot say. A
// check that compares two paths of the same build stays green when a change moves both, and the
// cross-tree numeric check compares numbers, which many graphs produce alike. Only a recorded earlier
// graph can say that a decision of the analysis changed — and only within one tree and one toolchain,
// which is why `recorded.hpp` names the commit it was taken at. The loop tree's programs are pinned by
// the `tests/goldens/loops_*` listings.
//
// The corpus is `conservation/corpus.hpp`; the record is `conservation/recorded.hpp`; the tool that
// re-records is `tools/record_conservation_hashes.cpp`.
#include "graph_hash.hpp"
#include "corpus.hpp"
#include "recorded.hpp"

using namespace heinzel;

namespace {

constexpr bool conserved(std::size_t i) { return corpus_hash(i) == kRecorded[i].graph; }

// A hash that returned a constant would agree with every record ever taken, so the corpus is
// required to be genuinely distinguishable first.
//
// EIGHT distinct graphs out of eleven entries, and the number is exact rather than a floor: entries
// 0, 1, 3 and 6 share one graph on purpose (3 is the bound twin of 0). A ninth distinct graph
// appearing here would mean an entry stopped being the graph it was recorded as.
constexpr int distinct_graphs_in_the_corpus() {
    int distinct = 0;
    for (std::size_t i = 0; i < kCorpusSize; ++i) {
        bool seen_before = false;
        for (std::size_t j = 0; j < i; ++j) {
            if (corpus_hash(i) == corpus_hash(j)) {
                seen_before = true;
            }
        }
        if (!seen_before) {
            ++distinct;
        }
    }
    return distinct;
}

// ---- the walk really reads the fields it walks ---------------------------------------------------
//
// The hash walks a graph field by field rather than reading its bytes, which is what makes it work
// while compiling. The cost of that is a field added to the graph and not added to the walk: the
// oracle would narrow silently, which is the worst way for a check to weaken. So the fields are
// moved one at a time and the hash is required to notice.
constexpr std::uint64_t biquad_hash_with(void (*change)(Graph&)) {
    Graph g = corpus_entry(0).graph;
    change(g);
    return hash_graph(g);
}

} // namespace

static_assert(distinct_graphs_in_the_corpus() == 8, "a hash agreeing with everything would pass this file");

static_assert(biquad_hash_with([](Graph& g) { g.values[0].shape = Shape{32}; }) != kRecorded[0].graph,
              "a value's shape is inside the oracle");
static_assert(biquad_hash_with([](Graph& g) { g.values[0].type = ElemType::F64; }) != kRecorded[0].graph,
              "a value's element type is inside the oracle");
static_assert(biquad_hash_with([](Graph& g) { g.nodes[0].attr.delay.samples += 1; }) != kRecorded[0].graph,
              "an operation's attribute is inside the oracle");
// Precision sits behind an identity (Normal, and the not-yet-analysed Inherit, mix nothing), so it is
// moved FROM there to show it is walked: a value's level, and the graph's default.
static_assert(biquad_hash_with([](Graph& g) { g.values[0].precision = numerics::Precision::High; }) != kRecorded[0].graph,
              "a value's precision is inside the oracle");
static_assert(biquad_hash_with([](Graph& g) { g.default_precision = numerics::Precision::Fast; }) != kRecorded[0].graph,
              "the graph's default precision is inside the oracle");
static_assert(biquad_hash_with([](Graph&) {}) == kRecorded[0].graph,
              "and changing nothing changes nothing, so the five above are not all trivially true");

// A block length that arrived as a binding is its literal twin: the solver left no residue.
static_assert(corpus_hash(3) == corpus_hash(0), "a bound extent left a residue in the analysed graph");

// ---- every entry is conserved --------------------------------------------------------------------
static_assert(conserved(0), "biquad");
static_assert(conserved(1), "biquad again");
static_assert(conserved(2), "comb");
static_assert(conserved(3), "biquad, block bound to 64");
static_assert(conserved(4), "an input read twice");
static_assert(conserved(5), "a reduction over one axis");
static_assert(conserved(6), "biquad once more");
static_assert(conserved(7), "the elementwise set at M4");
static_assert(conserved(8), "the transcendental set at M4");
static_assert(conserved(9), "a contraction of two operands");
static_assert(conserved(10), "the two lags that arrived at M4");
static_assert(kCorpusSize == 11, "add an entry, record it, and update this count");

// ---- SHOWN OBJECTING ----------------------------------------------------------------------------
//
// A check that has only ever been seen to agree is not evidence that it would ever disagree.
//
// A CHANGED ANALYSED GRAPH: one value carries a different constant — a change to what the graph MEANS.
static_assert(biquad_hash_with([](Graph& g) { g.values[0].constant += 1.0; }) != kRecorded[0].graph,
              "a changed constant is a changed graph");

int main() {
    // The comparison again while the program runs, which is the half a static_assert cannot show.
    for (std::size_t i = 0; i < kCorpusSize; ++i) {
        if (!conserved(i)) {
            return static_cast<int>(i) + 1;
        }
    }
    return 0;
}
