// Views of a constant on the loop-nest path, against the reference walk.
//
// A constant is an immediate: held nowhere, read as a number. Autodiff makes SHAPED ones — the seed of a
// loss's adjoint, reshaped to broadcast back — and a view of one is still that number everywhere, so the
// view's statement stores it: a fill, as a pad's margin is. What reads the view reads that buffer as it
// reads any view's — a map, a contraction, a lag — and a view that is a result is stored for the caller.
// Built here with a placeholder input `c`, then turned into the constant 2.0: the frontend's `constant()`
// is rank 0, and no view of it analyses.
//
//   turned       x * transpose(c)                     a broadcast of the turned constant
//   a chain      x + transpose(reshape(c))            the reshape is nothing, the transpose a fill
//   part         s - slice(c, from 2, 3 of them)
//   product      einsum(a, transpose(c))              a contraction reads the fill's buffer
//   late         s + reshape(c)[n - 2]           silence first, then the constant
//   a result     transpose(c) as the first output, beside s * 2
//   gradients    d/dp of Σ_0 (x·p − 1)² and of Σ_0 (x·p − 1): the seeds autodiff reshapes; and of a
//                slice of x·p, whose adjoint PADS the seed — a part of a constant is that constant too
//   inexact      x * transpose(c) under High with c = 0.1: the view is stored in the value's type, so it
//                holds 0.1 rounded to float — the walk, which keeps a constant in double, gets that number
//                when it is given float(0.1) (N-5: until the analysis rounds every F32 constant). A part of
//                a constant — a pad or a join of one — is stored the same way and differs the same way.
//
// Refused by name, by the planner: a window over a constant, a contraction of
// one and a table read over one — each reads its operand from storage, and a constant is stored nowhere —
// and an output no node makes (a gradient its loss does not reach). The table read is pinned in
// `nest_agreement_test`, whose corpus holds it.

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/gradients.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/notation/einsum.hpp"
#include "heinzel/sema/analyze.hpp"

#include "canaried.hpp"
#include "interpret.hpp"
#include "planned.hpp"
#include "tree_facts.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string_view>
#include <vector>

using namespace heinzel;

namespace {

namespace loops = heinzel::loops;

constexpr std::int32_t kBlock = 19;

constexpr Graph analysed(Graph g) {
    if (!infer(g).ok) {
        fail("nest_constant_views_test: a graph did not resolve");
    }
    return g;
}

// The placeholder input `c` becomes the constant 2.0 and leaves the inputs — a shaped constant, as
// autodiff makes one.
constexpr Graph with_constant(Graph g, double value = 2.0) {
    for (std::size_t i = 0; i < g.inputs.size(); ++i) {
        if (g.inputs[i].name.view() == "c") {
            Value& c = g.values[static_cast<std::size_t>(g.inputs[i].value)];
            c.category = ValueCategory::Const;
            c.constant = value;
            for (std::size_t j = i + 1; j < g.inputs.size(); ++j) {
                g.inputs[j - 1] = g.inputs[j];
            }
            g.inputs.pop_back();
            break;
        }
    }
    return analysed(g);
}

enum class Case { Turned, Chain, Part, Product, Late, AResult, Squares, Sums, SquaresHigh, Sliced, TwoSlices, Inexact, InexactRounded };

// 0.1 as the walk must be handed it to agree with a view that holds it in float.
constexpr double kTenth = 0.1;
constexpr double kTenthInFloat = static_cast<double>(static_cast<float>(0.1));

constexpr Graph graph_of(Case which) {
    const bool high = which == Case::SquaresHigh || which == Case::Inexact || which == Case::InexactRounded;
    const numerics::Precision level = high ? numerics::Precision::High : numerics::Precision::Normal;
    constexpr Index n{};
    Input x{"x", Signal{3, 4, kBlock}};
    Input s{"s", Signal{3, kBlock}};
    switch (which) {
    case Case::Turned:
        return with_constant(finish(Output{"y"} = x * Input{"c", Signal{1, 3, kBlock}}.transpose({1, 0})));
    case Case::Chain: {
        Expr r = Input{"c", Signal{12, kBlock}}.reshape({4, 3});
        return with_constant(finish(Output{"y"} = x + r.transpose({1, 0})));
    }
    case Case::Part:
        return with_constant(finish(Output{"y"} = s - Input{"c", Signal{8, kBlock}}.slice(0, 2, 3)));
    case Case::Product: {
        Input a{"a", Signal{4, 3, kBlock}};
        Expr t = Input{"c", Signal{5, 3, kBlock}}.transpose({1, 0});
        return with_constant(finish(Output{"y"} = einsum("ik,kj->ij", a, t)));
    }
    case Case::Late:
        return with_constant(finish(Output{"y"} = s + Input{"c", Signal{1, 3, kBlock}}.reshape({3})[n - 2]));
    case Case::AResult: {
        Expr turned = Input{"c", Signal{1, 3, kBlock}}.transpose({1, 0});
        Expr doubled = s * 2.0;
        Graph g = finish(Output{"z"} = turned, Output{"y"} = doubled);
        set_default_precision(g, level);
        return with_constant(g);
    }
    case Case::Inexact:
    case Case::InexactRounded: {
        Graph g = finish(Output{"y"} = x * Input{"c", Signal{1, 3, kBlock}}.transpose({1, 0}));
        set_default_precision(g, level);
        return with_constant(g, which == Case::Inexact ? kTenth : kTenthInFloat);
    }
    case Case::Sliced:
    case Case::TwoSlices: {
        Input xs{"x", Signal{4, kBlock}};
        Input p{"p", Signal{4, kBlock}};
        Output loss{"loss"};
        loss = which == Case::Sliced ? (xs * p).slice(0, 1, 1) : (xs * p).slice(0, 3, 1) + p.slice(0, 0, 1);
        return analysed(gradients(loss, {Grad{"dp", p}}, level));
    }
    case Case::Squares:
    case Case::SquaresHigh:
    case Case::Sums: {
        Input xs{"x", Signal{4, kBlock}};
        Input p{"p", Signal{4, kBlock}};
        Expr e = xs * p - 1.0;
        Output loss{"loss"};
        loss = (which == Case::Sums ? e : e * e).sum(0);
        return analysed(gradients(loss, {Grad{"dp", p}}, level));
    }
    }
    fail("nest_constant_views_test: no such case");
}

template <Case Which>
constexpr planned::Held kNestHeld{planned::program_of(graph_of(Which), zen2)};
template <Case Which>
constexpr const loops::Program& kNest = kNestHeld<Which>.program;

static_assert(tree_facts::stmt(kNest<Case::Turned>, 0, 0).in[0].source == loops::Source::Immediate,
              "the view of the constant is a Load of its number");
static_assert(kNest<Case::AResult>.outputs.size() == 2, "the view that is a result is stored for the caller");

int g_wrong = 0;

std::vector<float> noise(std::size_t n, std::uint32_t seed) {
    std::vector<float> v(n);
    std::uint32_t state = seed;
    for (float& x : v) {
        state = state * 1664525u + 1013904223u;
        x = static_cast<float>(static_cast<std::int32_t>(state >> 8) % 2001 - 1000) * 0.0625f;
    }
    return v;
}

// Every element of every result, against the walk of `Oracle` (the same graph unless said otherwise): bit
// for bit, or within `tolerance` relative to the answer's magnitude plus one where the schedule takes a
// freedom the walk does not. At `Normal` the gradients are a few float roundings of terms below 2^12 —
// a product, a subtraction, a fused multiply-add where granted, a sum of four — each within 2^-24 of its
// terms, so 1e-6 of (|answer| + 1) is the bound with room for the summation order.
template <Case Which, Case Oracle = Which>
void check(const char* what, double tolerance = 0.0) {
    static constexpr Graph g = graph_of(Oracle);
    const std::size_t widest = 5 * 3 * kBlock;
    const std::vector<float> x = noise(widest, 1);
    const std::vector<float> s = noise(widest, 2);
    const std::vector<float> a = noise(widest, 3);
    const std::vector<float> p = noise(widest, 4);
    const auto named = [&](const Name& name) -> const std::vector<float>& {
        return name.data_[0] == 'x' ? x : name.data_[0] == 's' ? s : name.data_[0] == 'a' ? a : p;
    };
    std::vector<std::vector<double>> widened;
    Inputs feeds{};
    loops::Feeds nest_feeds{};
    for (std::size_t i = 0; i < g.inputs.size(); ++i) {
        const std::vector<float>& from = named(g.inputs[i].name);
        widened.emplace_back(from.begin(), from.end());
        nest_feeds[i] = from.data();
    }
    for (std::size_t i = 0; i < g.inputs.size(); ++i) {
        const Shape& shape = g.values[static_cast<std::size_t>(g.inputs[i].value)].shape;
        feeds.push_back(InputFeed{widened[i].data(), elements_of(shape).count});
    }
    const auto out = std::make_unique<Signals<kBlock>>();
    run(g, feeds, *out);
    canaried::Workspace<kNest<Which>> work(loops::Sizes{});
    canaried::run<kNest<Which>>(nest_feeds, work);
    for (std::size_t o = 0; o < g.outputs.size(); ++o) {
        const float* y = work.template data<float>(static_cast<std::size_t>(kNest<Which>.outputs[o]));
        const std::int32_t result = g.outputs[o].value;
        const Shape& shape = g.values[static_cast<std::size_t>(result)].shape;
        const std::size_t width = static_cast<std::size_t>(elements_of(shape).count);
        for (std::size_t t = 0; t < kBlock; ++t) {
            for (std::size_t e = 0; e < width; ++e) {
                const double want = static_cast<double>(static_cast<float>(sample_at(*out, result, static_cast<std::int32_t>(e), t)));
                const double got = static_cast<double>(y[t * width + e]);
                const bool near = tolerance == 0.0 ? got == want : std::fabs(got - want) <= tolerance * (std::fabs(want) + 1.0);
                if (!near && ++g_wrong <= 10) {
                    std::printf("  %s, output %zu [sample %zu, element %zu]: got %a, want %a\n", what, o, t, e, got, want);
                }
            }
        }
    }
}

void expect_refused(const char* what, const Graph& g, std::string_view says) {
    const loops::Refusal held = planned::refusal_of(g, zen2);
    const std::string_view why = held.view();
    if (!why.starts_with(says)) {
        ++g_wrong;
        std::printf("  %s: expected a refusal starting \"%.*s\", got \"%.*s\"\n", what, static_cast<int>(says.size()), says.data(),
                    static_cast<int>(why.size()), why.data());
    }
}

void check_refusals() {
    Input x{"x", Signal{14, kBlock}};
    Input c{"c", Signal{16, kBlock}};
    expect_refused("a window over a constant",
                   with_constant(finish(Output{"y"} = x + c.unfold(0, 3).sum(1))),
                   "heinzel::compile: unfold: a window over a constant");
    Input a{"a", Signal{4, 3, kBlock}};
    Input k{"c", Signal{3, 5, kBlock}};
    expect_refused("a contraction of a constant", with_constant(finish(Output{"y"} = einsum("ik,kj->ij", a, k))),
                   "heinzel::compile: einsum: a contraction of a constant");
    Input xs{"x", Signal{4, kBlock}};
    Input unused{"p", Signal{4, kBlock}};
    Output loss{"loss"};
    loss = (xs * xs).sum(0);
    expect_refused("a gradient its loss does not reach", analysed(gradients(loss, {Grad{"dp", unused}})),
                   "heinzel::compile: an output that no node makes");
}

} // namespace

int main() {
    check<Case::Turned>("turned");
    check<Case::Chain>("a chain");
    check<Case::Part>("part");
    check<Case::Product>("product", 1e-6);
    check<Case::Late>("late");
    check<Case::AResult>("a result");
    check<Case::Squares>("gradients of squares", 1e-6);
    check<Case::Sums>("gradients of sums", 1e-6);
    check<Case::SquaresHigh>("gradients of squares, High");
    check<Case::Sliced>("gradients of a slice", 1e-6);
    check<Case::TwoSlices>("gradients of two slices", 1e-6);
    check<Case::Inexact, Case::InexactRounded>("an inexact constant seen through a view, High");
    check_refusals();
    if (g_wrong != 0) {
        std::printf("nest_constant_views_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("nest_constant_views_test: views of constants agree with the walk\n");
    return 0;
}
