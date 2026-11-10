#pragma once

// Rewriting a graph between the analysis and the planner: an analysed graph in, one that computes the same results in
// an order or a form the processor runs faster out. A rule that changes rounding fires only on values whose precision
// grants it, so at `High` nothing does. Its rules name no operation: what an operation is to a sum comes from the
// rule table. `compile` and `try_compile` call it after `infer`, so `Executor` runs what it makes; the tests'
// `planned::` helpers plan a graph as it was written.

#include "heinzel/ir/ir.hpp"
#include "heinzel/rewrite/double_state.hpp"
#include "heinzel/rewrite/equal_values.hpp"
#include "heinzel/rewrite/feedback_sums.hpp"
#include "heinzel/rewrite/parallel_form.hpp"
#include "heinzel/rewrite/priced.hpp"
#include "heinzel/rewrite/product_adds.hpp"
#include "heinzel/rewrite/sections_in_lanes.hpp"
#include "heinzel/schedule/machine.hpp"

namespace heinzel::rewrite {

// The graph rewritten for machine `m`: the same results, its inputs and outputs by the same names. The rules, in the
// order they run: a linear filter in its parallel form, a bank in double (`parallel_form.hpp`), inside a loop only
// where the loop's chain does not grow (`priced.hpp`, which prices it after the rules below); a run of like sections,
// each reading the one before, one cycle over a section axis run skewed in lanes, where the cost model, pricing it
// after the rules below, finds it cheaper than the cascade (`sections_in_lanes.hpp`); a feedback sum in the order its
// terms are ready (`feedback_sums.hpp`), the bank's among them, fused where the machine has the multiply-add (the
// program is the same in every build; a build without the instruction rounds twice); a cycle in double where the cost
// model prices it no dearer, or where Normal promises it (`double_state.hpp`); outside the cycles, an add or a subtract
// of a product fused where the machine has the multiply-add (`product_adds.hpp`); last, each value written twice
// computed once (`equal_values.hpp`), after the rules, which see the graph as written.
namespace detail {

// The rules after the parallel form. A function of its own, so that the deepest path, the parallel form pricing its
// candidates, holds one graph in `rewritten`'s frame, as before the last rule: an unoptimized build's frames hold every
// temporary, a raised-capacity graph is 150 KB, and `rewrite_parallel_form`'s run comes near 8 MB.
constexpr Graph after_the_form(const Graph& g, const Machine& m) {
    Graph out = rewrite::lanes_sums_and_state(g, m);
    rewrite::fuse_product_adds(out, m);
    rewrite::compute_equal_values_once(out);
    return out;
}

} // namespace detail

constexpr Graph rewritten(const Graph& g, const Machine& m) { return detail::after_the_form(rewrite::parallel_form(g, m), m); }

} // namespace heinzel::rewrite
