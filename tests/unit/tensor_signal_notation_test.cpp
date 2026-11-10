// The public notation against the old (Plan/archive/14-dsl L4): `einsum`, `rearrange` and `reduce(spec…)` with the
// stream unnamed, graph-equal to the old readings with it named (their bodies, beneath the new ones since L9); a
// computed value's spec checked once its shapes settle; the label bound.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string_view>
#include <vector>

#include "heinzel/compile.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/notation/einsum.hpp"
#include "heinzel/notation/rearrange.hpp"
#include "heinzel/sema/analyze.hpp"

#include "canaried.hpp"
#include "hand_built_graph.hpp"

using namespace heinzel;

namespace {

constexpr Graph analysed(Graph g) {
    if (const Diagnostic d = infer(g); !d.ok) {
        fail(d.message.view());
    }
    return g;
}

// What the analysis says of a graph: nothing (ok) or its refusal's text.
constexpr bool says(Graph g, std::string_view text) {
    const Diagnostic d = infer(g);
    return text.empty() ? d.ok : !d.ok && d.message.view() == text;
}

// The old readings, the stream a label of the spec: deleted at L9, their bodies are what the public ones lower through.
constexpr Expr old_einsum(std::string_view spec, Expr x) {
    return notation::einsum_detail::lower_one(notation::einsum_detail::parse(spec), x);
}
constexpr Expr old_einsum(std::string_view spec, Expr a, Expr b) {
    return notation::einsum_detail::lower_two(notation::einsum_detail::parse(spec), a, b);
}
constexpr Expr old_rearrange(std::string_view spec, Expr x, std::initializer_list<AxisSize> sizes = {}) {
    return notation::rearrange_detail::lower_rearrange(notation::rearrange_detail::parse(spec), x, sizes);
}
constexpr Expr old_reduce(std::string_view spec, Expr x, ReduceOp how) {
    return notation::rearrange_detail::lower_reduce(notation::rearrange_detail::parse(spec), x, how);
}

template <bool New>
constexpr Graph notation_pair() {
    Input a{"a", {4, 3}};
    Input b{"b", {3, 5}};
    Input x{"x", Signal{4, 6, 16}};
    Output p{"p"};
    Output q{"q"};
    Output s{"s"};
    Output t{"t"};
    Output r{"r"};
    Output m{"m"};
    if constexpr (New) {
        p = einsum("ik,kj->ij", a, b);
        q = einsum("ab,ab->a", a, a);
        s = einsum("ab->a", a);
        t = rearrange("a b -> b a", x);
        r = rearrange("a (b c) -> a b c", x, {{"b", 2}});
        m = reduce("a b -> b", x, ReduceOp::Max);
    } else {
        p = old_einsum("ikt,kjt->ijt", a, b);
        q = old_einsum("abt,abt->at", a, a);
        s = old_einsum("abt->at", a);
        t = old_rearrange("a b t -> b a t", x);
        r = old_rearrange("a (b c) t -> a b c t", x, {{"b", 2}});
        m = old_reduce("a b t -> b t", x, ReduceOp::Max);
    }
    return finish(p, q, s, t, r, m);
}
static_assert(analysed(notation_pair<true>()) == analysed(notation_pair<false>()) && notation_pair<true>() != notation_pair<false>() &&
                  says(notation_pair<true>(), ""),
              "equal once the ranks the specs named are checked and cleared");

// A computed value's rank is not known while building: the spec's rides on the node it lowered to and is checked once
// shapes settle — too few axes named, too many, and a correct spec.
constexpr std::string_view kNamesEvery = "heinzel: the spec names a different number of axes than the value has";
template <int Case>
constexpr Graph computed_spec() {
    Input x{"x", Signal{2, 3, 4, 16}};
    Output y{"y"};
    const Expr twice = x * 2.0;
    if constexpr (Case == 0) {
        y = reduce("a b -> a", twice, ReduceOp::Max);
    } else if constexpr (Case == 1) {
        y = einsum("ab->a", twice);
    } else if constexpr (Case == 2) {
        y = einsum("ab,ab->a", twice, twice);
    } else if constexpr (Case == 3) {
        y = rearrange("a b -> b a", twice);
    } else if constexpr (Case == 4) {
        y = einsum("abcd->a", twice);
    } else if constexpr (Case == 5) {
        y = einsum("abc->a", twice) + reduce("a b c -> a", twice, ReduceOp::Sum);
    } else if constexpr (Case == 6 || Case == 7 || Case == 10) { // two operands apart (a `_matmul`): b's rank wrong, a's
        Input a{"a", Case == 10 ? Signal{2, 3, 5, 16} : Signal{2, 3, 16}};
        Input b{"b", Case == 6 ? Signal{3, 4, 5, 16} : Signal{3, 4, 16}};
        y = einsum("ik,kj->ij", a * 2.0, b * 2.0);
    } else if constexpr (Case < 11) { // a fold, then a transpose of what it kept: the rank rides past the transpose to the fold
        Input c{"c", Case == 8 ? Signal{2, 3, 4, 5, 16} : Signal{2, 3, 4, 16}};
        y = einsum("abc->ca", c * 2.0);
    } else { // a product, its sum, then a transpose (Decision 78): the ranks ride past both to the product
        Input c{"c", Case == 11 ? Signal{2, 3, 4, 5, 16} : Signal{2, 3, 4, 16}};
        Expr twice_c = c * 2.0;
        y = einsum("abc,abc->ca", twice_c, twice_c);
    }
    return y;
}
static_assert(says(computed_spec<0>(), kNamesEvery) && says(computed_spec<1>(), kNamesEvery) && says(computed_spec<2>(), kNamesEvery) &&
              says(computed_spec<3>(), kNamesEvery) && says(computed_spec<4>(), kNamesEvery) && says(computed_spec<5>(), ""));
static_assert(says(computed_spec<6>(), kNamesEvery) && says(computed_spec<7>(), "") && says(computed_spec<8>(), kNamesEvery) &&
              says(computed_spec<9>(), "") && says(computed_spec<10>(), kNamesEvery));
static_assert(says(computed_spec<11>(), kNamesEvery) && says(computed_spec<12>(), ""));

// The bound is per term: three labels each, six across both.
constexpr Graph three_and_three() {
    Input a{"a", {2, 3, 4}};
    Input b{"b", {4, 5, 6}};
    Output y{"y"};
    y = einsum("abc,cde->abde", a, b);
    return y;
}
static_assert(three_and_three().nodes.size() == 1);

} // namespace

int main() {
    std::printf("tensor_signal_notation_test: the public notation as the old readings\n");
    return 0;
}
