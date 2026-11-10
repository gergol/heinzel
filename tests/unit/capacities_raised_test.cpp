// Every capacity override raised (`tests/CMakeLists.txt`, `docs/limitations.md`): graphs just over each default
// compile and run on `Executor`, at `High`, bit for bit against hand loops that round as `High` does (each operation in
// double, rounded once to float).
//   chain:  150 additions in a row: values, nodes and statements;
//   wide:   the sum of 97 inputs: inputs and buffers;
//   delays: 17 delays of a computed value in a row: 18 loop nests;
//   cycle:  a recurrence of 97 nodes, kept in one stage;
//   dim:    a stream whose `Dim` has id 8;
//   pool:   a constant of 1,056 numbers.
// And `deep`, 26 layers of max(h·w, 0), lowers to more tree nodes and stages than the defaults hold. It is compiled,
// not run: what it shows is that the program fits. Three targets build this file (`-DPART=1`: chain, 2: deep, 3: the
// rest), so that no one translation unit is the suite's slowest by far.
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#include "heinzel/compile.hpp"
#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/notation/einsum.hpp"

using namespace heinzel;
using numerics::Precision;

namespace {

constexpr Index n{};
constexpr int kMaps = 150;
constexpr int kInputs = 97;
constexpr int kDelays = 17;
constexpr int kCycleMaps = 93;
constexpr int kLayers = 26;
constexpr int kT = 64;
constexpr int kNumbers = 1056;

static_assert(kMaxValues > 128 && kMaxNodes > 128 && kMaxIO > 32 && kMaxDimDecls > 8 && kMaxConstants > 1024 && loops::kMaxStmts > 128 &&
                  loops::kMaxTreeNodes > 512 && loops::kMaxBuffers > 96 && loops::kMaxStages > 16 && loops::kMaxStageStmts > 128,
              "every override is raised above its default");

constexpr Graph at_high(Graph g) {
    set_default_precision(g, Precision::High);
    return g;
}

constexpr double step(int i) { return i % 2 == 0 ? 0.25 : -0.125; }

constexpr Graph chain() {
    Input x{"x", Signal{kT}};
    const Expr up = constant(step(0));
    const Expr down = constant(step(1));
    Expr acc = x;
    for (int i = 0; i < kMaps; ++i) {
        acc = acc + (i % 2 == 0 ? up : down);
    }
    Output y{"y"};
    y = acc;
    return at_high(y);
}

constexpr std::array<std::array<char, 4>, kInputs> kNames = [] {
    std::array<std::array<char, 4>, kInputs> names{};
    for (int i = 0; i < kInputs; ++i) {
        names[i] = {'x', static_cast<char>(i < 10 ? '0' + i : '0' + i / 10), static_cast<char>(i < 10 ? '\0' : '0' + i % 10), '\0'};
    }
    return names;
}();

constexpr Graph wide() {
    Expr sum = Input{kNames[0].data(), Signal{kT}};
    for (int i = 1; i < kInputs; ++i) {
        sum = sum + Input{kNames[i].data(), Signal{kT}};
    }
    Output y{"y"};
    y = sum;
    return at_high(y);
}

constexpr Graph delays() {
    Input x{"x", Signal{kT}};
    Expr v = x * 1.0;
    for (int i = 0; i < kDelays; ++i) {
        v = v[n - 1] + 0.25;
    }
    Output y{"y"};
    y = v;
    return at_high(y);
}

constexpr Graph cycle() {
    Input x{"x", Signal{8, kT}};
    Variable y;
    Expr acc = y[n - 1] * 0.5;
    for (int i = 0; i < kCycleMaps; ++i) {
        acc = acc + step(i);
    }
    y[n] = x + acc;
    return at_high(finish(Output{"y"} = y));
}

constexpr Graph dim() {
    Expr x = detail::input("x", Shape{Dim{8}}, ElemType::F32, DimDecl{8, DimConstraints{}});
    Output y{"y"};
    y = x * 2.0;
    return at_high(y);
}

constexpr std::array<float, kNumbers> kPool = [] {
    std::array<float, kNumbers> v{};
    for (std::size_t i = 0; i < v.size(); ++i) {
        v[i] = 0.125f * static_cast<float>(static_cast<int>((i * 37 + 11) % 61) - 30);
    }
    return v;
}();

constexpr Graph pool() {
    Input x{"x", {kNumbers}};
    Output y{"y"};
    y = x * constant(kPool);
    return at_high(y);
}

constexpr Graph deep() {
    Expr h = Input{"x", {8, 8}};
    Input w{"w", {8, 8}};
    for (int l = 0; l < kLayers; ++l) {
        h = max(einsum("ik,kj->ij", h, w), 0.0);
    }
    Output y{"y"};
    y = h;
    return y;
}

int g_wrong = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        ++g_wrong;
        std::printf("  FAILING: %s\n", what);
    }
}

#if PART == 2
constexpr CompileResult kDeep = try_compile(deep());
static_assert(kDeep.refusal.view().empty() && kDeep.compiled.program.nodes.size() > 512 && kDeep.compiled.program.stages.size() > 16,
              "deep: 26 layers lower to more tree nodes and stages than the defaults hold");

void lowers_deep() { expect(kDeep.compiled.program.nodes.size() > 512, "deep: more tree nodes than the default holds"); }
#else
// ---- hand loops ---------------------------------------------------------------------------------------------

float rounded(double v) { return static_cast<float>(v); }

std::vector<float> numbers(std::size_t count, unsigned seed) {
    std::vector<float> v(count);
    for (float& e : v) {
        seed = seed * 1664525u + 1013904223u;
        e = -1.0f + 2.0f * static_cast<float>(seed >> 8) / static_cast<float>(1u << 24);
    }
    return v;
}

bool same(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}
#endif

#if PART == 1
void runs_chain() {
    static_assert(chain().values.size() > 128 && chain().nodes.size() > 128);
    const std::vector<float> x = numbers(kT, 1u);
    std::vector<float> want(kT);
    std::vector<float> y(kT);
    for (int t = 0; t < kT; ++t) {
        float acc = x[t];
        for (int i = 0; i < kMaps; ++i) {
            acc = rounded(static_cast<double>(acc) + step(i));
        }
        want[t] = acc;
    }
    Executor<chain> ex;
    ex.input("x") = x.data();
    ex.output("y") = y.data();
    ex.run();
    expect(same(y, want), "chain: 150 additions");
}
#endif

#if PART == 3
template <std::size_t... I>
void bind_wide(Executor<wide>& ex, const std::vector<std::vector<float>>& xs, std::index_sequence<I...>) {
    ((ex.input(kNames[I].data()) = xs[I].data()), ...);
}

void runs_wide() {
    std::vector<std::vector<float>> xs;
    for (int i = 0; i < kInputs; ++i) {
        xs.push_back(numbers(kT, 10u + static_cast<unsigned>(i)));
    }
    std::vector<float> want(kT);
    std::vector<float> y(kT);
    for (int t = 0; t < kT; ++t) {
        float acc = xs[0][t];
        for (int i = 1; i < kInputs; ++i) {
            acc = rounded(static_cast<double>(acc) + xs[i][t]);
        }
        want[t] = acc;
    }
    Executor<wide> ex;
    bind_wide(ex, xs, std::make_index_sequence<kInputs>{});
    ex.output("y") = y.data();
    ex.run();
    expect(same(y, want), "wide: 97 inputs");
}

void runs_delays() {
    const std::vector<float> x = numbers(kT, 2u);
    std::vector<float> v(kT);
    std::vector<float> y(kT);
    for (int t = 0; t < kT; ++t) {
        v[t] = rounded(static_cast<double>(x[t]) * 1.0);
    }
    for (int i = 0; i < kDelays; ++i) {
        for (int t = kT - 1; t >= 0; --t) { // in place, latest first: v[t - 1] is still the stage before's
            v[t] = rounded(static_cast<double>(t > 0 ? v[t - 1] : 0.0f) + 0.25);
        }
    }
    Executor<delays> ex;
    ex.input("x") = x.data();
    ex.output("y") = y.data();
    ex.run();
    expect(same(y, v), "delays: 17 in a row");
}

void runs_cycle() {
    const std::vector<float> x = numbers(8 * kT, 3u);
    std::vector<float> want(8 * kT);
    std::vector<float> y(8 * kT);
    for (int c = 0; c < 8; ++c) {
        float before = 0.0f;
        for (int t = 0; t < kT; ++t) {
            float acc = rounded(static_cast<double>(before) * 0.5);
            for (int i = 0; i < kCycleMaps; ++i) {
                acc = rounded(static_cast<double>(acc) + step(i));
            }
            before = want[t * 8 + c] = rounded(static_cast<double>(x[t * 8 + c]) + acc);
        }
    }
    Executor<cycle> ex;
    ex.input("x") = x.data();
    ex.output("y") = y.data();
    ex.run();
    expect(same(y, want), "cycle: 97 nodes");
}

void runs_dim() {
    const std::vector<float> x = numbers(kT, 4u);
    std::vector<float> want(48);
    std::vector<float> y(48);
    for (int t = 0; t < 48; ++t) {
        want[t] = rounded(static_cast<double>(x[t]) * 2.0);
    }
    Executor<dim> ex;
    ex.prepare(kT);
    ex.input("x") = x.data();
    ex.output("y") = y.data();
    ex.run(48);
    expect(same(y, want), "dim: a Dim of id 8");
}

void runs_pool() {
    const std::vector<float> x = numbers(kNumbers, 5u);
    std::vector<float> want(kNumbers);
    std::vector<float> y(kNumbers);
    for (int i = 0; i < kNumbers; ++i) {
        want[i] = rounded(static_cast<double>(x[i]) * kPool[static_cast<std::size_t>(i)]);
    }
    Executor<pool> ex;
    ex.input("x") = x.data();
    ex.output("y") = y.data();
    ex.run();
    expect(same(y, want), "pool: a constant of 1,056 numbers");
}
#endif

} // namespace

int main() {
#if PART == 1
    runs_chain();
#elif PART == 2
    lowers_deep();
#elif PART == 3
    runs_wide();
    runs_delays();
    runs_cycle();
    runs_dim();
    runs_pool();
#endif
    if (g_wrong != 0) {
        std::printf("capacities_raised_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("capacities_raised_test: every override raised, graphs over each default run bit for bit\n");
    return 0;
}
