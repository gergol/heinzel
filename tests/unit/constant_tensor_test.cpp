// Constant tensors (Plan/19-constants): numbers known when the program compiles, written into the graph with
// `constant(…)` and read where they lie (`Home::Constant`). Each graph runs on `Executor` against the reference walk
// (`executed`), and against its twin, the same graph with the constant an `Input` bound to the same numbers, bit for bit
// at `High` (and at `Normal`, on `Executor`, where both plan alike): a constant broadcast along a signal, the sums of a
// constant's rows, a 3-tap kernel over every window, a constant weight contracted with an input (relaid on tiny
// caches too), a constant table, a constant output over a `Length`, and a gradient through a constant weight. Then the
// frontend: a braced f32 list is rounded where it is recorded, the pool keeps a NaN's payload and -0 apart, `{}` after
// a braced list is a shape (one number), and a constant whose numbers do not fill its shape, one of no type and a pool
// over its capacity are refused (the last at run time with its whole text), as are a hand-built constant's miscount and
// range. A relaid constant lies on a cache line.
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "heinzel/autodiff/gradients.hpp"
#include "heinzel/compile.hpp"
#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/notation/einsum.hpp"

#include "edited_schedules.hpp"
#include "executed.hpp"

using namespace heinzel;
using namespace heinzel::testing;

namespace {

constexpr std::int32_t kCells = 16;
constexpr float kSmooth[] = {0.25f, 0.5f, 0.25f};
constexpr double kGains[] = {0.5, -1.25, 3.0};

// Numbers a float holds exactly, none of them repeating soon.
template <std::size_t N>
constexpr std::array<float, N> spread(float scale) {
    std::array<float, N> v{};
    for (std::size_t i = 0; i < N; ++i) {
        v[i] = scale * static_cast<float>(static_cast<std::int32_t>((i * 37 + 11) % 61) - 30);
    }
    return v;
}
constexpr std::array<float, 4 * 5> kRows = spread<4 * 5>(0.25f);
constexpr std::array<float, 21 * 19> kWeights = spread<21 * 19>(0.0625f);
constexpr std::array<float, kCells> kTable = spread<kCells>(0.5f);

// ---- the graphs, each beside its twin ---------------------------------------------------------------------

constexpr Graph scaled() {
    Input x{"x", Signal{3, 16}, f64};
    Output y{"y"};
    y = x * constant(kGains);
    return y;
}
constexpr Graph scaled_twin() {
    Input x{"x", Signal{3, 16}, f64};
    Input k{"k", {3}, f64};
    Output y{"y"};
    y = x * k;
    return y;
}

constexpr Graph row_sums() {
    Input x{"x", {4}};
    Output y{"y"};
    y = x + constant(kRows, {4, 5}).sum(1);
    return y;
}
constexpr Graph row_sums_twin() {
    Input x{"x", {4}};
    Input k{"k", {4, 5}};
    Output y{"y"};
    y = x + k.sum(1);
    return y;
}

constexpr Graph smoothed() {
    Input x{"x", {64}};
    Output y{"y"};
    y = (x.unfold(0, 3) * constant(kSmooth)).sum(1);
    return y;
}
constexpr Graph smoothed_twin() {
    Input x{"x", {64}};
    Input k{"k", {3}};
    Output y{"y"};
    y = (x.unfold(0, 3) * k).sum(1);
    return y;
}

constexpr Graph weighted() { // (`constant_ramps_test`'s tall shape: tiny caches relay its right operand)
    Input x{"x", {50, 21}};
    Output y{"y"};
    y = einsum("ik,kj->ij", x, constant(kWeights, {21, 19}));
    return y;
}
constexpr Graph weighted_twin() {
    Input x{"x", {50, 21}};
    Input k{"k", {21, 19}};
    Output y{"y"};
    y = einsum("ik,kj->ij", x, k);
    return y;
}

// `y = 2 · table[frac(p) · N]`.
constexpr Graph table_read(Expr table) {
    Input phase{"p", Signal{64}};
    return finish(Output{"y"} = wavetable(table, phase) * 2.0);
}
constexpr Graph tabled() { return table_read(constant(kTable)); }
constexpr Graph tabled_twin() { return table_read(Input{"k", {kCells}}); }

// Each constant buffer of `p` is read where it lies, its numbers inside the program's pool.
constexpr bool constants_lie_where_they_are(const loops::Program& p, std::int32_t expected) {
    std::int32_t seen = 0;
    for (const loops::Buffer& b : p.buffers) {
        if (b.first >= 0) {
            seen += b.home == loops::Home::Constant ? 1 : -100;
        }
    }
    return seen == expected && p.constants.size() != 0 && loops::well_formed(p).has_value();
}
template <auto Build, const Machine& M = default_machine>
constexpr Compiled kCompiled = compile(at_high<Build>(), M);
static_assert(constants_lie_where_they_are(kCompiled<scaled>.program, 1) && constants_lie_where_they_are(kCompiled<smoothed>.program, 1) &&
                  constants_lie_where_they_are(kCompiled<weighted>.program, 1) && constants_lie_where_they_are(kCompiled<tabled>.program, 1),
              "a constant is read where it lies");

// Tiny caches relay the weight (`Plan/19-constants/02` §2): its relaid copy is a constant too, so no stage copies it,
// and every number of the weight lies at its place among the blocks, the padding zero.
template <const loops::Program& P>
consteval bool relaid_where_it_lies() {
    constexpr std::size_t relaid = [] {
        std::size_t at = P.buffers.size();
        for (std::size_t b = 0; b < P.buffers.size(); ++b) {
            at = P.buffers[b].blocked_axis >= 0 ? b : at;
        }
        return at;
    }();
    if constexpr (relaid == P.buffers.size()) {
        return false;
    } else {
        constexpr loops::Buffer b = P.buffers[relaid];
        const auto& sample = loops::kConstantOf<P, relaid>;
        std::array<bool, sample.size()> placed{};
        bool ok = b.home == loops::Home::Constant && P.stages.size() == 1 && sample.size() > kWeights.size();
        for (std::size_t e = 0; e < kWeights.size(); ++e) {
            const auto at = static_cast<std::size_t>(loops::element_offset(b, static_cast<std::int64_t>(e)));
            ok = ok && !placed[at] && sample[at] == kWeights[e];
            placed[at] = true;
        }
        for (std::size_t at = 0; at < sample.size(); ++at) {
            ok = ok && (placed[at] || sample[at] == 0.0f);
        }
        return ok;
    }
}
static_assert(relaid_where_it_lies<kCompiled<weighted, edited_schedules::kTinyCaches>.program>(),
              "a relaid constant is laid out when the program compiles, every number at its place, the padding zero");

// A constant as an output, beside a signal over a `Length`.
constexpr Graph constant_out() {
    Input x{"x", Signal{2, Length{}}};
    Output y{"y"};
    Output k{"k"};
    y = x * 2.0;
    k = constant(kRows, {4, 5});
    return finish(y, k);
}

// The loss's gradient with respect to `x` is the constant weight itself.
constexpr Graph weight_gradient() {
    Input x{"x", {3}, f64};
    Output loss{"loss"};
    loss = (x * constant(kGains)).sum(0);
    Graph g = loss;
    set_default_precision(g, numerics::Precision::High); // (the walk's level, set before the analysis autodiff runs)
    const std::int32_t gx = autodiff::gradients(g, g.outputs[0].value, {g.inputs[0].value})[0];
    g.outputs.push_back(NamedValue{detail::name_of("gx"), gx});
    return g;
}

// ---- the frontend -----------------------------------------------------------------------------------------

// `{}` is a shape, one number, as for `Input{"x", {}}`: never a type.
constexpr Graph one_number() {
    Input x{"x", {}};
    Output y{"y"};
    y = x * constant({0.5}, {});
    return y;
}
static_assert(one_number().nodes[0].attr.generator.shape == Shape{1} && one_number().nodes[0].attr.generator.type == ElemType::F32,
              "constant({0.5}, {}) records the shape {} (recorded {1}), f32");

// One number more than the default pool holds.
inline constexpr std::array<float, kMaxConstants + 1> kOverThePool{};
Graph over_the_pool() {
    Input x{"x", {static_cast<std::int32_t>(kMaxConstants + 1)}};
    Output y{"y"};
    y = x * constant(kOverThePool);
    return y;
}

// A constant built by hand: its numbers counted where they are emitted, its range checked by the analysis.
std::string emitted_with(std::size_t numbers) {
    BuildNode n{};
    n.op = op::Constant;
    n.attr.kind = AttrKind::Constant;
    n.attr.generator.shape = n.shape = Shape{2, 1};
    n.attr.generator.type = ElemType::F32;
    n.timeless = true;
    n.numbers = std::vector<double>(numbers, 0.5);
    try {
        Output y{"y"};
        y = Input{"x", {2}} * detail::one_node(n);
        const Graph g = y;
        (void)g;
    } catch (const heinzel::error& e) {
        return std::string(e.message());
    }
    return "";
}
std::string analysed_with(std::int32_t first) {
    Output y{"y"};
    y = Input{"x", {2}} * constant({0.25, 0.75});
    Graph g = y;
    for (Node& n : g.nodes) {
        n.attr.generator.first = n.attr.kind == AttrKind::Constant ? first : n.attr.generator.first;
    }
    const Diagnostic d = infer(g);
    return std::string(d.message.view());
}

// The relaid weight's numbers lie on a cache line, as a workspace block does.
bool relaid_on_a_line() {
    constexpr const loops::Program& p = kCompiled<weighted, edited_schedules::kTinyCaches>.program;
    constexpr std::size_t relaid = [] {
        std::size_t at = 0;
        for (std::size_t b = 0; b < p.buffers.size(); ++b) {
            at = p.buffers[b].blocked_axis >= 0 ? b : at;
        }
        return at;
    }();
    return reinterpret_cast<std::uintptr_t>(loops::kConstantOf<p, relaid>.data()) % 64 == 0;
}

constexpr Graph braced() {
    Input x{"x", {3}};
    Output y{"y"};
    y = x * constant({0.1, 0.2, 0.3});
    return y;
}
constexpr bool rounded_where_recorded() {
    const Graph g = braced();
    return g.constants.size() == 3 && std::bit_cast<double>(g.constants[0]) == static_cast<double>(0.1f) &&
           std::bit_cast<double>(g.constants[2]) == static_cast<double>(0.3f);
}
static_assert(rounded_where_recorded(), "a braced f32 list is rounded to f32 where the frontend records it");

constexpr Graph one_of(double v) {
    Input x{"x", {1}, f64};
    Output y{"y"};
    y = x * constant({v}, f64);
    return y;
}
static_assert(one_of(0.0) != one_of(-0.0) && one_of(0.0) == one_of(0.0), "the pool keeps -0 apart from 0");
static_assert(one_of(std::bit_cast<double>(0x7ff8000000000001ULL)) != one_of(std::bit_cast<double>(0x7ff8000000000002ULL)),
              "the pool keeps a NaN's payload");

int g_wrong = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        ++g_wrong;
        std::printf("  FAILED: %s\n", what);
    }
}

std::vector<double> numbers(std::size_t count, double scale) {
    std::vector<double> v(count);
    for (std::size_t i = 0; i < count; ++i) {
        v[i] = scale * static_cast<double>(static_cast<std::int32_t>((i * 29 + 7) % 53) - 26);
    }
    return v;
}
template <std::size_t N>
std::vector<double> widened(const std::array<float, N>& v) {
    return {v.begin(), v.end()};
}
// A tensor input's feed beside a signal: the walk reads every port at every sample, so its numbers repeat.
std::vector<double> repeated(const std::vector<double>& v, std::size_t samples) {
    std::vector<double> out;
    for (std::size_t n = 0; n < samples; ++n) {
        out.insert(out.end(), v.begin(), v.end());
    }
    return out;
}

bool refused(void (*build)(), std::string_view why) {
    try {
        build();
    } catch (const heinzel::error& e) {
        return e.message() == why;
    }
    return false;
}

// A pool over its capacity, at run time: the graph function returns, its graph carrying which, and the analysis refuses
// it first, in its words (Decision 77).
bool over_the_pool_refused(std::string_view why) {
    Graph g = over_the_pool();
    const Diagnostic analysed = infer(g);
    return g.over == Capacity::Constants && g.values.empty() && !analysed.ok && analysed.message.view() == why;
}

// The weight relaid by tiny caches, a constant laid out in blocks, read on every run.
bool relaid_agrees(const std::vector<double>& a, const Outputs& walk) {
    const std::vector<float> narrow(a.begin(), a.end());
    std::vector<float> y(50 * 19);
    Executor<at_high<weighted>, edited_schedules::kTinyCaches> ex;
    ex.input("x") = narrow.data();
    ex.output("y") = y.data();
    ex.run();
    ex.run();
    return std::vector<double>(y.begin(), y.end()) == walk[0];
}

// At `Normal`, on `Executor`: a constant and its twin give the same numbers.
template <auto Build, auto Twin>
bool twins_at_normal(const std::vector<float>& x, std::size_t outputs, const float* k) {
    std::vector<float> y(outputs);
    std::vector<float> z(outputs);
    Executor<Build> constant;
    constant.input("x") = x.data();
    constant.output("y") = y.data();
    constant.run();
    Executor<Twin> twin;
    twin.input("x") = x.data(), twin.input("k") = k;
    twin.output("y") = z.data();
    twin.run();
    return y == z;
}

// The constant output beside a stream over a `Length`, prepared twice and run at several lengths: a tensor, so one
// sample of its numbers, and nothing written past them.
bool constant_out_runs(const std::vector<double>& x) {
    const std::vector<float> narrow(x.begin(), x.begin() + 2 * 16);
    std::vector<float> y(2 * 16);
    std::vector<float> k(20 + 1, -1.0f);
    Executor<constant_out> ex;
    ex.input("x") = narrow.data();
    ex.output("y") = y.data(), ex.output("k") = k.data();
    bool ok = true;
    for (const auto [prepared, frames] : {std::pair{16, 16}, std::pair{16, 5}, std::pair{8, 8}, std::pair{8, 3}}) {
        ex.prepare(prepared);
        std::fill(k.begin(), k.end(), -1.0f);
        ex.run(frames);
        for (std::size_t i = 0; i < 20; ++i) {
            ok = ok && k[i] == kRows[i];
        }
        ok = ok && k[20] == -1.0f;
    }
    return ok;
}

// The edges: what the frontend, the analysis and the capacity refuse; `{}` as a shape; a relaid constant on a line.
void edges() {
    expect(refused([] { (void)constant({1.0, 2.0}, {3}); }, "heinzel: a constant's numbers do not fill its shape"),
           "too few numbers for the shape");
    expect(refused([] { (void)constant(kSmooth, {2}); }, "heinzel: a constant's numbers do not fill its shape"),
           "too many numbers for the shape");
    expect(refused([] { (void)constant({1.0}, ElemType::Unknown); }, "heinzel: a constant is f32 or f64"), "a constant of no type");
    expect(over_the_pool_refused("heinzel: a graph has more constants than kMaxConstants; rebuild with -DHEINZEL_MAX_CONSTANTS=N"),
           "a pool over its capacity, refused at run time with the whole text");
    expect(relaid_on_a_line(), "a relaid constant lies on a cache line");
    expect(emitted_with(2).empty() && emitted_with(3) == "heinzel: a constant's numbers do not fill its shape",
           "a hand-built constant's count, checked where it is emitted");
    expect(analysed_with(0).empty() && analysed_with(1) == "constant reads numbers outside the graph's pool" &&
               analysed_with(-1) == "constant reads numbers outside the graph's pool",
           "a constant's range in the pool, checked by the analysis");
    const std::vector<double> half = {3.0};
    expect(executed<one_number>({{"x", half.data(), 1}})[0] == std::vector<double>{1.5}, "constant({0.5}, {}) is one number");
}

} // namespace

int main() {
    const std::vector<double> x = numbers(3 * 16, 0.5);
    const std::vector<double> x4 = numbers(4, 0.25);
    const std::vector<double> x64 = numbers(64, 0.125);
    const std::vector<double> a = numbers(50 * 21, 0.125);
    const std::vector<double> p = numbers(64, 0.03125);
    const std::vector<double> gains(std::begin(kGains), std::end(kGains));
    const std::vector<double> smooth(std::begin(kSmooth), std::end(kSmooth));
    const std::vector<double> rows = widened(kRows);
    const std::vector<double> weights = widened(kWeights);
    const std::vector<double> table = widened(kTable);
    const std::vector<double> gains16 = repeated(gains, 16);
    const std::vector<double> table64 = repeated(table, 64);

    const Outputs s = executed<scaled>({{"x", x.data(), 3}});
    expect(s == walked<scaled_twin>({{"x", x.data(), 3}, {"k", gains16.data(), 3}}), "a constant along a signal, against its twin");
    bool plain = true;
    for (std::size_t i = 0; i < x.size(); ++i) {
        plain = plain && s[0][i] == x[i] * kGains[i % 3];
    }
    expect(plain, "a constant along a signal, against plain C++");

    expect(executed<row_sums>({{"x", x4.data(), 4}}) == walked<row_sums_twin>({{"x", x4.data(), 4}, {"k", rows.data(), 20}}),
           "the sums of a constant's rows, against its twin");
    expect(executed<smoothed>({{"x", x64.data(), 64}}) == walked<smoothed_twin>({{"x", x64.data(), 64}, {"k", smooth.data(), 3}}),
           "a 3-tap constant kernel, against its twin");
    const Outputs w = executed<weighted>({{"x", a.data(), 50 * 21}});
    expect(w == walked<weighted_twin>({{"x", a.data(), 50 * 21}, {"k", weights.data(), 21 * 19}}), "a constant weight contracted, against its twin");
    expect(relaid_agrees(a, w), "a constant weight relaid by tiny caches, run twice, against the walk");
    expect(executed<tabled>({{"p", p.data()}}) == walked<tabled_twin>({{"p", p.data()}, {"k", table64.data(), kCells}}),
           "a constant table, against its twin");

    const std::vector<float> x64f(x64.begin(), x64.end());
    const std::vector<float> af(a.begin(), a.end());
    expect(twins_at_normal<smoothed, smoothed_twin>(x64f, 62, kSmooth), "a 3-tap constant kernel at Normal, against its twin");
    expect(twins_at_normal<weighted, weighted_twin>(af, 50 * 19, kWeights.data()), "a constant weight at Normal, against its twin");

    const Outputs k = walked<constant_out, 8>({{"x", x.data(), 2}});
    bool whole = true;
    for (std::size_t i = 0; i < 20; ++i) {
        whole = whole && k[1][i] == static_cast<double>(kRows[i]);
    }
    expect(whole, "a constant as an output, walked: its numbers");
    expect(constant_out_runs(x), "a constant output over a Length, prepared twice, run at four lengths");

    const Outputs gx = executed<weight_gradient>({{"x", gains.data(), 3}});
    expect(gx[1] == gains, "the gradient through a constant weight is the weight");

    edges();

    if (g_wrong != 0) {
        std::printf("constant_tensor_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("constant_tensor_test: constants read where they lie give the walk's and their twins' numbers\n");
    return 0;
}
