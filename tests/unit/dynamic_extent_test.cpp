// MD S1 (frontend growth, item c): minting a runtime-resolved streamed extent at the frontend.
//
// A block length that is not a compile-time literal but a symbol bound later — the audio-plugin
// `prepareToPlay()` shape. The frontend spelling is a `Length` in a `Signal` (MD's `declare_dim` / `extent_of`,
// gone since Plan/archive/14-dsl L9); it carries the declared dimension's constraints onto the graph, and adds NO
// operation and changes no number. What it must prove:
//
//   (1) a `[2, N]` elementwise graph built through the frontend, with N a declared dimension on the
//       innermost (streamed) axis, INFERS and reports `is_static == false`, and the constraint record
//       round-trips on the graph — every field, unread by the analysis pass;
//   (2) a static graph built the same way declares no dimension, and the new field leaves its
//       artifact hash exactly where it was (the field is folded into the oracle only when non-empty),
//       while a declared dimension — or a change to one's constraints — DOES move that hash, so the
//       field is inside the oracle when it carries anything;
//   (3) sharing one N across two inputs records it once (dedup by id), and two `Length`s with
//       conflicting constraints are refused.
//
// A symbol on a WIDTH axis (`[N, 2]`) is no runtime length: `Executor` refuses it at compile time
// ("at most one symbol, on the time axis"), shown by the `build_refuses_executor_a_symbol_off_time` fixture.
#include <string_view>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"
#include "heinzel/support/fail.hpp"

#include "graph_hash.hpp"

using namespace heinzel;

namespace {

// The declared dimension's id: a `Length` is the stream's symbol, 0.
constexpr std::int32_t kLengthId = kStreamLength;

// The constraints a block length carries: a whole number of stereo frames, at least two of them (and a
// typical 256, a hint the graph does not keep).
constexpr Length kBlock{.multiple_of = 2, .min = 2, .expect = 256};
constexpr DimConstraints kBlockConstraints{/*multiple_of*/ 2, /*min*/ 2};

// ---- (1) a [2, N] graph infers, is not static, and carries its constraint -----------------------
//
// `x` is two channels (width 2) of a runtime-resolved block (N innermost). Nothing binds N, so the graph
// stays symbolic.
constexpr Graph runtime_length_graph_with(Length length) {
    Input x{"x", Signal{2, length}, f64};
    Output y{"y"};
    y = -x;
    Graph g = y;
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the runtime-length graph did not infer");
    }
    return g;
}

constexpr Graph kRuntime = runtime_length_graph_with(kBlock);

static_assert(!is_static(kRuntime), "a declared runtime dimension leaves the graph non-static");
static_assert(kRuntime.dims.size() == 1, "the declared dimension is recorded on the graph exactly once");
static_assert(kRuntime.dims[0].id == kLengthId, "and it is the id the frontend minted");
static_assert(kRuntime.dims[0].constraints == kBlockConstraints,
              "and every constraint field round-trips, unread by the analysis pass");

// The input value really does carry the symbol on its innermost axis (and width 2 beside it), so the
// assertions above are not describing a graph that quietly lost its dimension.
static_assert(kRuntime.values[static_cast<std::size_t>(kRuntime.inputs[0].value)].shape ==
                  Shape{2, Extent{Dim{kLengthId}}},
              "the input's shape is [2, N] with N the declared symbol, streamed innermost");

// ---- (2) the static path is unmoved; the field is inside the oracle when it carries anything -----
//
// The same shape with a literal length and no declared dimension: a fully static graph. It declares
// no dimension, so `hash_graph` folds nothing new in and its hash is exactly what it was before this
// field existed (the conservation oracle, unchanged tree-wide, is the standing proof; here we pin the
// local facts the artifact-hash comment promises are checked rather than asserted there).
constexpr Graph static_length_graph() {
    Input x{"x", Signal{2, 256}, f64};
    Output y{"y"};
    y = -x;
    Graph g = y;
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the static graph did not infer");
    }
    return g;
}

constexpr Graph kStatic = static_length_graph();

static_assert(is_static(kStatic), "a literal length leaves the graph static");
static_assert(kStatic.dims.size() == 0, "and declares no runtime dimension");

// A declared dimension moves the graph hash — the field is genuinely inside the oracle, not folded in
// name only. The only difference between these two graphs is the constraint carried on the declared
// dimension; the shapes, values and nodes are identical, so a hash that did not read the dims table
// would report them equal.
static_assert(hash_graph(runtime_length_graph_with(Length{.multiple_of = 2, .min = 2, .expect = 256})) !=
                  hash_graph(runtime_length_graph_with(Length{.multiple_of = 4, .min = 2, .expect = 256})),
              "a change to a declared dimension's constraint moves the graph hash");

// And two graphs that differ ONLY in that one declares a runtime dimension and the other is its
// static twin hash differently — the symbolic vs literal extent already separates them, and the dims
// table is a further difference, never a silent equality.
static_assert(hash_graph(kRuntime) != hash_graph(kStatic),
              "the runtime-length graph and its static twin are not the same artifact");

// ---- (3) sharing one length across inputs records it once ----------------------------------------
//
// Two inputs given the SAME `Length` share one runtime dimension: the graph records it once, not
// once per input. This is the shape a stereo pair of same-length blocks has.
constexpr Graph two_inputs_one_length() {
    Input a{"a", Signal{2, kBlock}, f64};
    Input b{"b", Signal{2, kBlock}, f64};
    Output y{"y"};
    y = a + b;
    Graph g = y;
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the shared-length graph did not infer");
    }
    return g;
}

static_assert(two_inputs_one_length().dims.size() == 1, "one length shared across two inputs is recorded once");
static_assert(two_inputs_one_length().inputs.size() == 2, "and both inputs are present");

// ---- the refusal, observed rather than asserted --------------------------------------------------
//
// It is a `fail()`, so under constant evaluation it IS the compile error and cannot be a `static_assert`
// — the same reason `width_test.cpp` and `solver_test.cpp` observe their refusals in `main`. The
// predicate is pointed at a deliberately wrong input and its exact message checked, so a refusal that
// stopped refusing, or refused with different words, fails the test.

constexpr std::string_view kConflict = "heinzel: a length declared twice with different constraints";

// (3, cont.) Two `Length`s with DIFFERENT constraints are a contradiction the frontend refuses — the two
// declarations disagree about what the one size must be.
bool a_conflicting_redeclaration_is_refused() {
    try {
        Input a{"a", Signal{2, Length{.multiple_of = 2, .min = 2, .expect = 256}}, f64};
        Input b{"b", Signal{2, Length{.multiple_of = 4, .min = 8, .expect = 512}}, f64}; // the same symbol, other constraints
        Output y{"y"};
        y = a + b;
        (void)Graph(y);
        return false;
    } catch (const error& e) {
        return e.message() == kConflict;
    }
}

} // namespace

int main() {
    if (!a_conflicting_redeclaration_is_refused()) return 1;
    return 0;
}
