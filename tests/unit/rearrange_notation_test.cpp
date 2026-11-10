// MB.S5a — the einops `rearrange` / `reduce` notation lowers to the SAME view and reduce nodes the tests
// build by hand, and to nothing else.
//
// A split `"b (c f) t -> b c f t"` becomes one `_reshape` to the split extents; a merge the reverse
// reshape; a transpose `"c t -> t c"` one `_transpose` with the permutation the labels name; a fold
// `"b c f t -> b c"` one `_reduce` over the dropped axes, a fold that also reorders the survivors a
// reduce THEN a transpose, and a spec that reorders and regroups at once a split, a transpose and a merge, each only
// where its side groups — never a new kind of node, and never a cost-model consultation (there is nothing
// here to consult one about). Everything is built at COMPILE TIME and handed to the analysis pass: a spec
// that did not lower to a graph the pass accepts is a build failure. That the emitted node is the same one
// the view/reduce tests already measure is what carries M4/M5's numeric parity across without re-running it.
// A regroup is recorded as written and worked out by the analysis (Plan/28-dsl-proposals/03 B3): over an input its
// analysed graph is the one the lowering built before, and its refusals are the analysis's, for any operand, read by
// a node whose shapes disagree or not.
#include <cstdint>
#include <string_view>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/notation/rearrange.hpp"
#include "heinzel/sema/analyze.hpp"

#include "graph_hash.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t count_op(const Graph& g, OpCode code) {
    std::int32_t n = 0;
    for (const Node& node : g.nodes) {
        if (node.op == code) {
            ++n;
        }
    }
    return n;
}

constexpr const Node& only_op(const Graph& g, OpCode code) {
    const Node* found = nullptr;
    for (const Node& node : g.nodes) {
        if (node.op == code) {
            if (found != nullptr) {
                fail("rearrange_notation_test: more than one node of the expected op");
            }
            found = &node;
        }
    }
    if (found == nullptr) {
        fail("rearrange_notation_test: no node of the expected op");
    }
    return *found;
}

constexpr Graph resolved(Graph g) {
    if (!infer(g).ok) {
        fail("rearrange_notation_test: the notation graph did not resolve");
    }
    return g;
}

constexpr std::int32_t B = 2;
constexpr std::int32_t C = 3;
constexpr std::int32_t F = 4;
constexpr std::int32_t T = 5; // tensors, but for the two over a stream of symbolic length

// ---- split: "b (c f) t -> b c f t" re-cuts the packed (c f) axis, one reshape ------------------------
constexpr Graph kSplit = resolved([] {
    Input x{"x", {B, C * F, T}};
    return finish(Output{"y"} = rearrange("b (c f) t -> b c f t", x, {{"f", F}}));
}());

static_assert(count_op(kSplit, op::Reshape) == 1);
static_assert(count_op(kSplit, op::Transpose) == 0);
static_assert(count_op(kSplit, op::Reduce) == 0);
static_assert(only_op(kSplit, op::Reshape).attr.kind == AttrKind::View);
static_assert(only_op(kSplit, op::Reshape).attr.view.kind == ViewKind::Reshape);
static_assert(only_op(kSplit, op::Reshape).attr.view.shape == Shape{B, C, F, T, 1});

// ---- merge: the reverse "b c f t -> b (c f) t" packs c and f back together, one reshape --------------
constexpr Graph kMerge = resolved([] {
    Input x{"x", {B, C, F, T}};
    return finish(Output{"y"} = rearrange("b c f t -> b (c f) t", x));
}());

static_assert(count_op(kMerge, op::Reshape) == 1);
static_assert(only_op(kMerge, op::Reshape).attr.view.shape == Shape{B, C * F, T, 1});

// ---- the same over a stream whose length is a symbol: the stream passes through whole, symbol and all ---
constexpr Graph kSplitOverASymbol = resolved([] {
    Input x{"x", Signal{B, C * F, T, Dim{0}}};
    return finish(Output{"y"} = rearrange("b (c f) t -> b c f t", x, {{"f", F}}));
}());
static_assert(only_op(kSplitOverASymbol, op::Reshape).attr.view.shape == Shape{B, C, F, T, Dim{0}});
constexpr Graph kMergeOverASymbol = resolved([] {
    Input x{"x", Signal{B, C, F, T, Dim{0}}};
    return finish(Output{"y"} = rearrange("b c f t -> b (c f) t", x));
}());
static_assert(only_op(kMergeOverASymbol, op::Reshape).attr.view.shape == Shape{B, C * F, T, Dim{0}});

// ---- over an input, the graph the lowering built before the analysis worked the regroup out ----------------
// Equal to the reshape with its target written, and each hash the one these graphs had at 2bc62564, the commit before.
constexpr Graph kSplitWritten = resolved([] {
    Input x{"x", {B, C * F, T}};
    return finish(Output{"y"} = detail::reshape(x, Shape{B, C, F, T, 1}));
}());
static_assert(kSplit == kSplitWritten);
static_assert(hash_graph(kSplit) == 0xa8e5208f801b36a3ull && hash_graph(kMerge) == 0xe724c47ecd523819ull);
static_assert(hash_graph(kSplitOverASymbol) == 0x614f20d66a0ce3cfull &&
              hash_graph(kMergeOverASymbol) == 0xdf6550a211cb1d73ull);

// ---- the regroup's refusals, the analysis's once the operand has its shape -----------------------------------
// An input's and a computed value's alike: a symbolic element axis has no literal to split or to multiply, and the sizes
// a split names must fit its axis. A spec that names a different number of axes than its operand has is refused by the
// ranks it recorded (`OpAttr::named_ranks`) before the regroup is read; read without them, the regroup refuses it too.
constexpr bool says(Graph g, std::string_view text) {
    const Diagnostic d = infer(g);
    return !d.ok && d.message.view() == text;
}

template <int Case>
constexpr Graph refused_regroup() {
    Input x{"x", Case == 0 ? Signal{Dim{1}, 1} : Case == 1 ? Signal{Dim{1}, 3, 1} : Signal{12, 1}};
    Expr twice = x * 2.0;
    Output y{"y"};
    if constexpr (Case == 0) {
        y = rearrange("(c f) -> c f", x, {{"f", 4}});
    } else if constexpr (Case == 1) {
        y = rearrange("a b -> (a b)", twice);
    } else if constexpr (Case == 2) {
        y = rearrange("(c f) -> c f", twice, {{"f", 5}});
    } else if constexpr (Case == 3) {
        y = rearrange("(c f) -> c f", twice, {{"c", 3}, {"f", 5}});
    } else if constexpr (Case == 4) {
        y = rearrange("(a b c) -> a b c", twice, {{"a", 2}});
    } else if constexpr (Case == 5) {
        y = rearrange("(a b) c -> a b c", twice, {{"a", 2}});
    } else {
        const notation::rearrange_detail::Parse spec = notation::rearrange_detail::parse("(a b) c d -> a b c d");
        y = notation::rearrange_detail::lower_rearrange(spec, twice, {{"a", 2}});
    }
    return finish(y);
}
static_assert(says(refused_regroup<0>(), "heinzel::rearrange: a split needs the operand's extent known now, not a symbol"));
static_assert(says(refused_regroup<1>(), "heinzel::rearrange: a merge needs its extents known now, not a symbol"));
static_assert(says(refused_regroup<2>(), "heinzel::rearrange: the split sizes do not divide the axis extent"));
static_assert(says(refused_regroup<3>(), "heinzel::rearrange: the split sizes do not multiply to the axis extent"));
static_assert(says(refused_regroup<4>(), "heinzel::rearrange: a split leaves more than one axis unknown; size all but one"));
static_assert(says(refused_regroup<5>(), "heinzel: the spec names a different number of axes than the value has"));
static_assert(says(refused_regroup<6>(),
                   "heinzel::rearrange: the left side names a different number of axes than the operand"));

// Read by a node, a regroup that cannot be completed is refused in its own words, not the reader's: still marked, it
// hands on no shape, so the reader never disagrees with its operand's (the reviews of package B).
template <int Case>
constexpr Graph read_regroup() {
    Input x{"x", Signal{8, 1}};
    Input z{"z", Signal{3, 2, 1}};
    Expr twice = x * 2.0;
    Output y{"y"};
    if constexpr (Case == 0) {
        Expr split = rearrange("(a b) -> a b", x, {{"a", 3}});
        y = split * z;
    } else if constexpr (Case == 1) {
        Expr split = rearrange("(a b) -> a b", twice, {{"a", 3}});
        y = split * z;
    } else if constexpr (Case == 2) {
        Expr split = rearrange("(a b) -> a b", twice, {{"a", 3}});
        Expr halved = split * 0.5;
        y = halved + z;
    } else {
        Expr split = rearrange("(a b) c -> a b c", twice, {{"a", 2}});
        y = split * z;
    }
    return finish(y);
}
constexpr std::string_view kNotDivided = "heinzel::rearrange: the split sizes do not divide the axis extent";
static_assert(says(read_regroup<0>(), kNotDivided) && says(read_regroup<1>(), kNotDivided) && says(read_regroup<2>(), kNotDivided));
static_assert(says(read_regroup<3>(), "heinzel: the spec names a different number of axes than the value has"));

// ---- transpose: "c t -> t c" swaps the two element axes, the streamed axis untouched ----------------
constexpr Graph kTranspose = resolved([] {
    Input x{"x", {C, T}};
    return finish(Output{"y"} = rearrange("c t -> t c", x));
}());

static_assert(count_op(kTranspose, op::Transpose) == 1);
static_assert(count_op(kTranspose, op::Reshape) == 0);
static_assert(only_op(kTranspose, op::Transpose).attr.view.kind == ViewKind::Transpose);
static_assert(only_op(kTranspose, op::Transpose).attr.view.order == Axes{1, 0, 2});

// ---- both: "(h a) (w b) -> (h w) (a b)" splits the left side's groups, reorders, merges the right side's ---------
// einops' own lowering: the graph of the three views written out, its two regroups worked out by the analysis.
constexpr Graph kPatches = resolved([] {
    Input x{"x", {B * C, F * T}};
    return finish(Output{"y"} = rearrange("(h a) (w b) -> (h w) (a b)", x, {{"a", C}, {"b", T}}));
}());
constexpr Graph kPatchesWritten = resolved([] {
    Input x{"x", {B * C, F * T}};
    Expr split = detail::reshape(x, Shape{B, C, F, T, 1});
    Expr moved = detail::transpose(split, Axes{0, 2, 1, 3, 4});
    return finish(Output{"y"} = detail::reshape(moved, Shape{B * F, C * T, 1}));
}());
static_assert(kPatches == kPatchesWritten);
static_assert(count_op(kPatches, op::Reshape) == 2);
static_assert(count_op(kPatches, op::Transpose) == 1);

// A regroup only where its side groups: a split then a transpose, a transpose then a merge.
constexpr Graph kSplitThenTranspose = resolved([] {
    Input x{"x", {B * C, F}};
    return finish(Output{"y"} = rearrange("(b c) f -> f b c", x, {{"c", C}}));
}());
static_assert(count_op(kSplitThenTranspose, op::Reshape) == 1);
static_assert(only_op(kSplitThenTranspose, op::Reshape).attr.view.shape == Shape{B, C, F, 1});
static_assert(only_op(kSplitThenTranspose, op::Transpose).attr.view.order == Axes{2, 0, 1, 3});
constexpr Graph kTransposeThenMerge = resolved([] {
    Input x{"x", {C, F}};
    return finish(Output{"y"} = rearrange("c f -> (f c)", x));
}());
static_assert(count_op(kTransposeThenMerge, op::Reshape) == 1);
static_assert(only_op(kTransposeThenMerge, op::Transpose).attr.view.order == Axes{1, 0, 2});
static_assert(only_op(kTransposeThenMerge, op::Reshape).attr.view.shape == Shape{F * C, 1});

// Its refusals are the regroup's, over a computed value too: a split whose sizes do not divide, a merge of a symbol,
// and a spec of the wrong rank, by the rank it recorded on the split.
template <int Case>
constexpr Graph refused_patches() {
    Input x{"x", Case == 1 ? Signal{Dim{1}, 3, 1} : Signal{12, 1}};
    Expr twice = x * 2.0;
    Output y{"y"};
    if constexpr (Case == 0) {
        y = rearrange("(a b) -> b a", twice, {{"a", 5}});
    } else if constexpr (Case == 1) {
        y = rearrange("a b -> (b a)", twice);
    } else {
        y = rearrange("(a b) c -> c b a", twice, {{"a", 2}});
    }
    return finish(y);
}
static_assert(says(refused_patches<0>(), kNotDivided));
static_assert(says(refused_patches<1>(), "heinzel::rearrange: a merge needs its extents known now, not a symbol"));
static_assert(says(refused_patches<2>(), "heinzel: the spec names a different number of axes than the value has"));

// ---- reduce: "b c f t -> b c" folds f and t with a mean, one reduce, no transpose --------------------
constexpr Graph kReduce = resolved([] {
    Input x{"x", {B, C, F, T}};
    return finish(Output{"y"} = reduce("b c f t -> b c", x, ReduceOp::Mean));
}());

static_assert(count_op(kReduce, op::Reduce) == 1);
static_assert(count_op(kReduce, op::Transpose) == 0);
static_assert(only_op(kReduce, op::Reduce).attr.kind == AttrKind::Reduce);
static_assert(only_op(kReduce, op::Reduce).attr.reduce.op == ReduceOp::Mean);
static_assert(only_op(kReduce, op::Reduce).attr.reduce.axes == Axes{2, 3});

// ---- reduce then transpose: "a b c -> c a" drops b then reorders the survivors -----------------------
// Survivors after dropping b are [a, c] in operand order; the output asks [c, a], so a transpose
// follows the reduce, reading survivor axes {1, 0, 2}. Two nodes, both already tested, no third kind.
constexpr Graph kReduceThenTranspose = resolved([] {
    Input x{"x", {2, 3, 4}};
    return finish(Output{"y"} = reduce("a b c -> c a", x, ReduceOp::Sum));
}());

static_assert(count_op(kReduceThenTranspose, op::Reduce) == 1);
static_assert(count_op(kReduceThenTranspose, op::Transpose) == 1);
static_assert(only_op(kReduceThenTranspose, op::Reduce).attr.reduce.axes == Axes{1});
static_assert(only_op(kReduceThenTranspose, op::Transpose).attr.view.order == Axes{1, 0, 2});

// ---- the retained rejections, each observed objecting rather than asserted to accept -----------------
// A lowering `fail()` is a throw, which under constant evaluation IS the compile error — so a refusal can
// only be watched from a run. The messages are LABEL-level decisions the lowering makes before any shape is
// consulted, so the operand shape below is arbitrary. `fail()` truncates past kMaxMessageLength, so the
// expected string is truncated the same way a message longer than the buffer arrives.
bool refused(std::string_view expected, auto build, Shape shape = Shape{2, 3, 1}) {
    const std::size_t cap = kMaxMessageLength - 1;
    const std::string_view want = expected.size() < cap ? expected : expected.substr(0, cap);
    try {
        Input x{"x", Signal{shape}};
        (void)build(x);
        return false;
    } catch (const error& e) {
        return e.message() == want;
    }
}

constexpr std::string_view kAxesDiffer =
    "heinzel::rearrange: the two sides name different axes (a rearrange conserves them)";
constexpr std::string_view kDiagonal =
    "heinzel::rearrange: a repeated label (a diagonal) is not supported";
constexpr std::string_view kReduceDiagonal = "heinzel::reduce: a repeated label (a diagonal) is not supported";
constexpr std::string_view kReduceRegroup =
    "heinzel::reduce: a reduce does not regroup an axis; use rearrange for that";
constexpr std::string_view kReduceInvented =
    "heinzel::reduce: the output names an axis absent from the input";

} // namespace

int main() {
    // rearrange refusals
    // (The stream is never named, so the old readings' refusals of a spec that moves or drops it have nothing to
    // refuse.)
    if (!refused(kAxesDiffer, [](Expr x) { return rearrange("c t -> c x", x); })) return 2;
    if (!refused(kDiagonal, [](Expr x) { return rearrange("c c -> c", x); })) return 3;
    // (a symbolic element axis split or merged is the analysis's to refuse now: the static_asserts above)
    // reduce refusals
    if (!refused(kReduceRegroup, [](Expr x) { return reduce("(a b) -> a", x, ReduceOp::Sum); }, Shape{6, 1})) return 5;
    if (!refused(kReduceInvented, [](Expr x) { return reduce("c t -> c t x", x, ReduceOp::Sum); })) return 7;
    if (!refused(kReduceDiagonal, [](Expr x) { return reduce("c c -> c", x, ReduceOp::Sum); })) return 11;
    // and a good spec is NOT refused (a check "made to pass" everything would slip the ones above through)
    if (refused(kAxesDiffer, [](Expr x) { return rearrange("c t -> t c", x); })) return 8;
    return 0;
}
