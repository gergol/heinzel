// A check is a reader of the value it checks (Plan/26-value-ranges/04), so a rewrite that needs the value to have no
// other reader declines in a debug build: a cascade in its parallel form. A promise on its first section keeps a debug
// build serial, the program of the cascade read in its middle, and leaves release the form; one on its last keeps the
// form in both; each runs a cascade's numbers, the serial one a float cascade's and the bank a double one's. Split out
// of `ranges_checked_test.cpp`. The debug and release Executors are spelled `Executor<g, M, true>` and `<g, M, false>`,
// so that the test means the same with and without `NDEBUG`.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#include "heinzel/compile.hpp"
#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"

using namespace heinzel;

namespace {

constexpr Index n{};

int g_wrong = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        ++g_wrong;
        std::printf("  FAILED: %s\n", what);
    }
}

std::unique_ptr<const CompileResult> compiled(const Graph& g, bool checked, const Machine& m = default_machine) {
    return std::unique_ptr<const CompileResult>(new auto(try_compile(g, m, Bindings{}, checked)));
}

// ---- a cascade in its parallel form ----------------------------------------------------------------------------

constexpr std::int32_t kBlock = 4096;

struct Section {
    // y = b0 x + b1 x[n-1] + b2 x[n-2] - a1 y[n-1] - a2 y[n-2]
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;
};
// KFR's three-section EQ (`benchmarks/iir_arms.hpp`), as `rewrite_parallel_form_test.cpp` has it.
constexpr std::array<Section, 3> kEq3{{
    {0x1.fc38e2p-1f, -0x1.fc38e2p+0f, 0x1.fc38e2p-1f, -0x1.fc355p+0f, 0x1.f878e4p-1f},
    {0x1.0b4082p+0f, -0x1.e533bcp+0f, 0x1.bc4618p-1f, -0x1.e533bcp+0f, 0x1.d2c71cp-1f},
    {0x1.3d86bap-3f, 0x1.3d86bap-2f, 0x1.3d86bap-3f, -0x1.3d86bap-1f, 0x1.ec35d2p-3f},
}};
constexpr double f(float v) { return static_cast<double>(v); }

// The sections in series, section `Promised`'s definition promised a range the proof cannot prove.
template <std::size_t Promised>
constexpr Graph eq3() {
    Input x{"x", Signal{kBlock}, ge(-1), le(1)};
    Expr s = x;
    for (std::size_t k = 0; k < kEq3.size(); ++k) {
        const Section& c = kEq3[k];
        Variable y;
        const Expr definition = f(c.b0) * s + f(c.b1) * s[n - 1] + f(c.b2) * s[n - 2] - f(c.a1) * y[n - 1] - f(c.a2) * y[n - 2];
        y[n] = k == Promised ? definition.promise(ge(-100), le(100)) : definition;
        s = y;
    }
    Output out{"y"};
    out = s;
    return out;
}

std::size_t recurrences(const Graph& g) {
    std::size_t count = 0;
    for (const Node& node : g.nodes) {
        count += node.attr.kind == AttrKind::Recurrence ? 1 : 0;
    }
    return count;
}

std::vector<float> noise() {
    std::vector<float> v(kBlock);
    std::uint32_t state = 7;
    for (float& x : v) {
        state = state * 1664525u + 1013904223u;
        x = static_cast<float>(static_cast<std::int32_t>(state >> 8) % 2001 - 1000) * 0.001f;
    }
    return v;
}

// The cascade by hand in `T`, direct form I.
template <class T>
std::vector<double> by_hand(const std::vector<float>& x) {
    std::array<T, 3> x1{};
    std::array<T, 3> x2{};
    std::array<T, 3> y1{};
    std::array<T, 3> y2{};
    std::vector<double> y(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        T u = x[i];
        for (std::size_t k = 0; k < 3; ++k) {
            const Section& c = kEq3[k];
            const T o = T(c.b0) * u + T(c.b1) * x1[k] + T(c.b2) * x2[k] - T(c.a1) * y1[k] - T(c.a2) * y2[k];
            x2[k] = x1[k], x1[k] = u, y2[k] = y1[k], y1[k] = o, u = o;
        }
        y[i] = static_cast<double>(u);
    }
    return y;
}

std::vector<float> in_float(const std::vector<double>& y) { return std::vector<float>(y.begin(), y.end()); }

// The largest error against `want`, relative to its largest magnitude.
double error(const std::vector<float>& got, const std::vector<double>& want) {
    double scale = 0;
    double worst = 0;
    for (std::size_t i = 0; i < want.size(); ++i) {
        scale = std::fmax(scale, std::fabs(want[i]));
        worst = std::fmax(worst, std::fabs(static_cast<double>(got[i]) - want[i]));
    }
    return worst / std::fmax(scale, 1e-300);
}

template <auto Build, bool Checked>
std::vector<float> executed(const std::vector<float>& x) {
    const auto ex = std::make_unique<Executor<Build, zen2, Checked>>();
    std::vector<float> y(x.size());
    ex->input("x") = x.data(), ex->output("y") = y.data();
    ex->run();
    return y;
}

// The sections in series, the first's value an output too: a chain read in its middle, which the parallel form declines.
constexpr Graph eq3_read_in_the_middle() {
    Input x{"x", Signal{kBlock}, ge(-1), le(1)};
    Expr s = x;
    Output middle{"middle"};
    for (std::size_t k = 0; k < kEq3.size(); ++k) {
        const Section& c = kEq3[k];
        Variable y;
        y[n] = f(c.b0) * s + f(c.b1) * s[n - 1] + f(c.b2) * s[n - 2] - f(c.a1) * y[n - 1] - f(c.a2) * y[n - 2];
        if (k == 0) {
            middle = y;
        }
        s = y;
    }
    Output out{"y"};
    out = s;
    return finish(out, middle);
}

// Debug plans the first section's cascade serial, since its check reads section 1: the program of the same cascade read
// in its middle, bit for bit, and a float cascade's numbers (Normal gives a serial cascade float's rounding); release
// plans the parallel form, within twice the double cascade's error, in float, as `rewrite_parallel_form_test` holds it.
// A check on the last section names the `_recur`'s value, which the bank's sum carries: the form in both.
void cascade() {
    const auto first_debug = compiled(eq3<0>(), true, zen2);
    const auto first_release = compiled(eq3<0>(), false, zen2);
    const auto last_debug = compiled(eq3<2>(), true, zen2);
    const auto last_release = compiled(eq3<2>(), false, zen2);
    expect(recurrences(first_debug->compiled.graph) == 3 && recurrences(first_release->compiled.graph) == 1,
           "a check inside the cascade keeps debug serial, release in the parallel form");
    expect(recurrences(last_debug->compiled.graph) == 1 && recurrences(last_release->compiled.graph) == 1 &&
               last_debug->compiled.graph.outputs.size() == 2,
           "a check on the last section keeps the parallel form");
    const std::vector<float> x = noise();
    const std::vector<double> exact = by_hand<long double>(x);
    const double in_double = error(in_float(by_hand<double>(x)), exact);
    const double in_floats = error(in_float(by_hand<float>(x)), exact);
    const std::vector<float> serial = executed<eq3<0>, true>(x);
    const double serial_error = error(serial, exact);
    const double bank = error(executed<eq3<0>, false>(x), exact);
    const double last = error(executed<eq3<2>, true>(x), exact);
    std::printf("  the cascade from long double: debug, serial %.3g (the float cascade %.3g); release, the bank %.3g, and debug's with a "
                "check on it %.3g (the double cascade in float %.3g)\n",
                serial_error, in_floats, bank, last, in_double);
    expect(serial == executed<eq3_read_in_the_middle, false>(x) && serial_error <= 2 * in_floats,
           "debug runs the cascade read in its middle, a float cascade's numbers");
    expect(bank <= 2 * in_double && last <= 2 * in_double, "release's bank, and debug's with a check on it, a double cascade's numbers");
}

} // namespace

int main() {
    cascade();
    if (g_wrong != 0) {
        std::printf("ranges_cascade_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("ranges_cascade_test: a check inside a cascade keeps debug serial, one on its last section keeps the form\n");
    return 0;
}
