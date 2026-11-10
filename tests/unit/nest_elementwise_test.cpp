// The elementwise slice of the loop-nest path, end to end: plan, lower, run — against the arithmetic written
// by hand. Fused chains, lanes with every remainder length, broadcasting (a constant, a per-sample
// operand against a wide one, a per-element one), both element types, and `High`, which computes an
// F32 value via double and rounds once where every other level computes it in float.

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/sema/analyze.hpp"

#include "canaried.hpp"
#include "planned.hpp"
#include "tree_facts.hpp"

#include <cstdio>
#include <vector>

using namespace heinzel;

namespace {

namespace loops = heinzel::loops;

constexpr Graph analysed(Graph g) {
    if (!infer(g).ok) {
        fail("nest_elementwise_test: a graph did not resolve");
    }
    return g;
}

constexpr Length kSamples{.multiple_of = 1, .min = 1, .expect = 64};

// y = a*b + b over a run-time length, one number per sample.
constexpr Graph chain(ElemType type, numerics::Precision level) {
    Input a{"a", Signal{kSamples}, type};
    Input b{"b", Signal{kSamples}, type};
    Graph g = finish(Output{"y"} = a * b + b);
    set_default_precision(g, level);
    return analysed(g);
}

// y[c, t] = max(x[c, t] * gain[c] + bias[t], 0.25) over 5 columns: a per-element operand, a per-sample
// one, and a constant, all read against a wide value.
constexpr std::int32_t kColumns = 5;
constexpr Graph wide() {
    Input x{"x", Signal{kColumns, kSamples}};
    Input gain{"gain", {kColumns}};
    Input bias{"bias", Signal{kSamples}};
    return analysed(finish(Output{"y"} = max(x * gain + bias, 0.25)));
}

// A choice whose CONDITION is wider than its branches: narrowing 1e-300 to float would make it zero and
// pick the other branch.
constexpr Graph wide_condition() {
    Input a{"a", Signal{kSamples}};
    Input d{"d", Signal{kSamples}, ElemType::F64};
    return analysed(finish(Output{"y"} = where(d, a, a * a + 0.1)));
}
constexpr Graph kWideCondition = wide_condition();
constexpr planned::Held kWideConditionNestHeld{planned::program_of(kWideCondition, zen2)};
constexpr const loops::Program& kWideConditionNest = kWideConditionNestHeld.program;

constexpr Graph kChainF32 = chain(ElemType::F32, numerics::Precision::Normal);
constexpr Graph kChainF64 = chain(ElemType::F64, numerics::Precision::Normal);
constexpr Graph kChainHigh = chain(ElemType::F32, numerics::Precision::High);
constexpr Graph kWide = wide();

constexpr planned::Held kChainF32NestHeld{planned::program_of(kChainF32, zen2)};
constexpr const loops::Program& kChainF32Nest = kChainF32NestHeld.program;
constexpr planned::Held kChainF64NestHeld{planned::program_of(kChainF64, zen2)};
constexpr const loops::Program& kChainF64Nest = kChainF64NestHeld.program;
constexpr planned::Held kChainHighNestHeld{planned::program_of(kChainHigh, zen2)};
constexpr const loops::Program& kChainHighNest = kChainHighNestHeld.program;
constexpr planned::Held kWideNestHeld{planned::program_of(kWide, zen2)};
constexpr const loops::Program& kWideNest = kWideNestHeld.program;

// One stage of four statements walked by one strip of lanes over the samples: a Loop in steps of `lanes`, a
// Vector of as many inside it, and nothing else.
constexpr bool walked_in_lanes(const loops::Program& p, std::int32_t lanes) {
    const auto w = tree_facts::walks(p, 0);
    return p.stages.size() == 1 && p.stages[0].count == 4 && w.size() == 2 && loops::is_strip(w[0]) && w[0].axis == 0 && w[0].step == lanes &&
           w[1].kind == loops::NodeKind::Vector && w[1].axis == 0 && w[1].step == lanes;
}

// What was decided, read off the program: one fused stage, one lane loop as wide as an eighth of zen2's registers
// hold of the element type, two (a map's lanes, `planner/loops.hpp`), the two intermediate values never stored.
static_assert(walked_in_lanes(kChainF32Nest, 16) && walked_in_lanes(kChainF64Nest, 8), "the chain is one stage walked in lanes of 16 floats or 8 doubles");
static_assert(tree_facts::stmt(kChainF32Nest, 0, 0).kind == loops::StmtKind::Load && tree_facts::stmt(kChainF32Nest, 0, 1).kind == loops::StmtKind::Load &&
                  tree_facts::stmt(kChainF32Nest, 0, 2).store.source == loops::Source::None &&
                  tree_facts::stmt(kChainF32Nest, 0, 3).store.source == loops::Source::Buffer && kChainF32Nest.buffers.size() == 3,
              "b is read once though two nodes read it; the product stays in a register; only the output has storage");
// (the walk over time, then the strip of lanes over the elements and its Vector)
static_assert(tree_facts::walks(kWideNest, 0).size() == 3 && tree_facts::walk(kWideNest, 0, 0).kind == loops::NodeKind::Loop &&
                  tree_facts::walk(kWideNest, 0, 0).axis == 1 && tree_facts::walk(kWideNest, 0, 1).axis == 0 &&
                  tree_facts::walk(kWideNest, 0, 2).kind == loops::NodeKind::Vector && tree_facts::walk(kWideNest, 0, 2).axis == 0,
              "a wide value is walked sample by sample, in lanes across its elements");
static_assert(planned::program_of(kChainF32, zen2) == kChainF32Nest, "lowering is a function of its inputs");

int g_wrong = 0;

template <class T>
std::vector<T> ramp(std::size_t n, double from, double step) {
    std::vector<T> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = static_cast<T>(from + step * static_cast<double>(i % 97));
    }
    return v;
}

template <const loops::Program& P, class T, class Want>
void check_chain(const char* what, std::size_t n, Want want) {
    const std::vector<T> a = ramp<T>(n, -1.3, 0.0371);
    const std::vector<T> b = ramp<T>(n, 0.71, -0.0113);
    canaried::Workspace<P> work(loops::Sizes{static_cast<std::int64_t>(n)});
    canaried::run<P>(loops::Feeds{a.data(), b.data()}, work);
    const T* y = work.template data<T>(static_cast<std::size_t>(P.outputs[0]));
    for (std::size_t i = 0; i < n; ++i) {
        if (y[i] != want(a[i], b[i]) && ++g_wrong <= 10) {
            std::printf("  %s, n=%zu, sample %zu: got %a, want %a\n", what, n, i, static_cast<double>(y[i]),
                        static_cast<double>(want(a[i], b[i])));
        }
    }
}

void check_wide(std::size_t n) {
    const std::vector<float> x = ramp<float>(n * kColumns, -2.0, 0.0517);
    const std::vector<float> gain = ramp<float>(kColumns, 0.5, 0.25);
    const std::vector<float> bias = ramp<float>(n, -0.4, 0.013);
    canaried::Workspace<kWideNest> work(loops::Sizes{static_cast<std::int64_t>(n)});
    canaried::run<kWideNest>(loops::Feeds{x.data(), gain.data(), bias.data()}, work);
    const float* y = work.data<float>(static_cast<std::size_t>(kWideNest.outputs[0]));
    for (std::size_t t = 0; t < n; ++t) {
        for (std::size_t c = 0; c < kColumns; ++c) {
            const float scaled = x[t * kColumns + c] * gain[c] + bias[t];
            const float want = scaled > 0.25f ? scaled : 0.25f;
            if (y[t * kColumns + c] != want && ++g_wrong <= 10) {
                std::printf("  wide, n=%zu, sample %zu column %zu: got %a, want %a\n", n, t, c,
                            static_cast<double>(y[t * kColumns + c]), static_cast<double>(want));
            }
        }
    }
}

void check_wide_condition(std::size_t n) {
    const std::vector<float> a = ramp<float>(n, -1.3, 0.0371);
    std::vector<double> d(n);
    for (std::size_t i = 0; i < n; ++i) {
        d[i] = i % 3 == 0 ? 0.0 : 1e-300;
    }
    canaried::Workspace<kWideConditionNest> work(loops::Sizes{static_cast<std::int64_t>(n)});
    loops::Feeds feeds{};
    for (std::size_t i = 0; i < kWideCondition.inputs.size(); ++i) { // feeds follow the GRAPH's input order
        feeds[i] = kWideCondition.inputs[i].name == Name{"a"} ? static_cast<const void*>(a.data()) : d.data();
    }
    canaried::run<kWideConditionNest>(feeds, work);
    const float* y = work.data<float>(static_cast<std::size_t>(kWideConditionNest.outputs[0]));
    for (std::size_t i = 0; i < n; ++i) {
        const float other = a[i] * a[i] + 0.1f;
        if (y[i] != (d[i] != 0.0 ? a[i] : other) && ++g_wrong <= 10) {
            std::printf("  a wide condition, n=%zu, sample %zu: got %a\n", n, i, static_cast<double>(y[i]));
        }
    }
}

} // namespace

int main() {
    for (const std::size_t n : {1u, 3u, 7u, 8u, 9u, 16u, 31u, 1000u}) {
        check_chain<kChainF32Nest, float>("f32", n, [](float a, float b) { return a * b + b; });
        check_chain<kChainF64Nest, double>("f64", n, [](double a, double b) { return a * b + b; });
        // `High`: the reference walk's arithmetic — each node in double, rounded to float once.
        check_chain<kChainHighNest, float>("f32 at High", n, [](float a, float b) {
            const float product = static_cast<float>(static_cast<double>(a) * static_cast<double>(b));
            return static_cast<float>(static_cast<double>(product) + static_cast<double>(b));
        });
        check_wide(n);
        check_wide_condition(n);
    }
    if (g_wrong != 0) {
        std::printf("%d elements are wrong\n", g_wrong);
        return 1;
    }
    return 0;
}
