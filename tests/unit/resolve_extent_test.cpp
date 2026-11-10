// MD S2 (frontend growth, item c): the prepare()-time resolver for a runtime-resolved extent.
//
// A graph whose signal has a `Length` (MD S1) carries the constraints its streamed length must satisfy.
// This step adds the resolver that, at prepare(), validates the size the symbol is bound to against
// those constraints and refuses a size that breaks one — or a dimension left unbound. It reads no rule
// column and changes no number; it is pure value logic, so every case is a `static_assert`, and each
// refusal is pointed at a deliberately broken size and its exact message checked.
//
// What it must prove (MD.2): a size violating `multiple_of`, one below `min`, a non-positive size, and
// a symbol left unbound at prepare() are each refused with their own message; a size that satisfies
// every constraint is accepted.
#include <string_view>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"
#include "heinzel/sema/resolve_extent.hpp"
#include "heinzel/sema/solver.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t kLengthId = kStreamLength; // the symbol a Length declares

// A whole number of stereo frames, at least two of them, a typical 256.
constexpr DimConstraints kBlockConstraints{/*multiple_of*/ 2, /*min*/ 2};

// ---- check_dimension_size: one size against one set of constraints -------------------------------

// A size that satisfies every constraint is accepted, and says nothing.
static_assert(check_dimension_size(kBlockConstraints, 256).ok, "a valid block size is accepted");
static_assert(check_dimension_size(kBlockConstraints, 2).ok, "the minimum, exactly, is accepted");
static_assert(check_dimension_size(kBlockConstraints, 4).ok, "a larger multiple is accepted");

// The three refusals, each with its own message — pointed at a size that breaks exactly one rule so a
// message that named the wrong rule, or stopped naming any, fails the build.
constexpr Diagnostic kNotMultiple = check_dimension_size(kBlockConstraints, 3); // 3 is odd
static_assert(!kNotMultiple.ok, "a size that is not a multiple of the factor is refused");
static_assert(kNotMultiple.message.view() == std::string_view{"heinzel::resolve_extent: size 3 is not a multiple of 2"},
              "and the refusal names the size and the factor");

constexpr Diagnostic kBelowMin = check_dimension_size(DimConstraints{/*multiple_of*/ 1, /*min*/ 8}, 4);
static_assert(!kBelowMin.ok, "a size below the minimum is refused");
static_assert(kBelowMin.message.view() == std::string_view{"heinzel::resolve_extent: size 4 is below the minimum 8"},
              "and the refusal names the size and the minimum");

constexpr Diagnostic kNonPositive = check_dimension_size(kBlockConstraints, 0);
static_assert(!kNonPositive.ok, "a non-positive size is refused before any other rule");
static_assert(kNonPositive.message.view() ==
                  std::string_view{"heinzel::resolve_extent: a resolved size is at least one sample, not 0"},
              "and the refusal names the offending size");

// `multiple_of <= 1` is no constraint, and it must not divide by zero: a malformed factor of 0 is
// treated as absent rather than trapping.
static_assert(check_dimension_size(DimConstraints{/*multiple_of*/ 0, /*min*/ 0}, 7).ok,
              "a multiple_of of zero is no constraint, not a division by zero");
static_assert(check_dimension_size(DimConstraints{/*multiple_of*/ 1, /*min*/ 0}, 7).ok,
              "a multiple_of of one is no constraint");

// ---- resolve_dimensions: every declared dimension of a graph, against the bindings ----------------
//
// A `[2, N]` graph built through the frontend, N declared with `kBlockConstraints`.
constexpr Graph runtime_length_graph() {
    constexpr Length block{.multiple_of = kBlockConstraints.multiple_of, .min = kBlockConstraints.min};
    Input x{"x", Signal{2, block}, ElemType::F64};
    Expr y = -x;
    Graph g = finish(Output{"y"} = y);
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the runtime-length graph did not infer");
    }
    return g;
}

constexpr Graph kGraph = runtime_length_graph();

// Bound to a size the constraints allow: the whole graph's dimensions resolve.
static_assert(resolve_dimensions(kGraph, Bindings{}.bind(Dim{kLengthId}, 256)).ok,
              "a graph whose one dimension is bound to a valid size resolves");
static_assert(resolve_dimensions(kGraph, Bindings{}.bind(Dim{kLengthId}, 64)).ok,
              "and at a different valid size, from the one compiled graph");

// Bound to a size that breaks a constraint: the graph's resolution carries that violation up.
static_assert(!resolve_dimensions(kGraph, Bindings{}.bind(Dim{kLengthId}, 3)).ok,
              "a graph whose dimension is bound to an invalid size is refused");
static_assert(resolve_dimensions(kGraph, Bindings{}.bind(Dim{kLengthId}, 3)).message.view() ==
                  std::string_view{"heinzel::resolve_extent: size 3 is not a multiple of 2"},
              "and the graph-level refusal is the size-level one");

// Left unbound at prepare(): a declared dimension with no size is its own refusal.
constexpr Diagnostic kUnbound = resolve_dimensions(kGraph, Bindings{});
static_assert(!kUnbound.ok, "a declared dimension left unbound at prepare() is refused");
static_assert(kUnbound.message.view() ==
                  std::string_view{"heinzel::resolve_extent: dimension 0 was left unbound at prepare()"},
              "and the refusal names the unbound dimension");

// A fully static graph declares no dimension, so nothing to resolve — accepted with no binding at all.
constexpr Graph static_graph() {
    Input x{"x", Signal{2, 256}, ElemType::F64};
    Expr y = -x;
    Graph g = finish(Output{"y"} = y);
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the static graph did not infer");
    }
    return g;
}
static_assert(resolve_dimensions(static_graph(), Bindings{}).ok,
              "a static graph declares no dimension, so it resolves with no bindings");

} // namespace

int main() { return 0; }
