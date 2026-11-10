// Q2.1 — the exported application-layer header `heinzel/gradients.hpp` is self-contained and usable
// from the app layer WITHOUT any test-support include path.
//
// This translation unit includes the exported header (through the `heinzel_app` interface), the core
// headers a consumer would use, and the reference walk it runs the graph on; it does NOT add
// `tests/support/`. That the header compiles with no test folder at all is `gradients_header_alone.cpp`.
// This one builds a gradient graph with the exported `gradients` spelling, runs it, and checks da, dx
// against the closed form of loss = exp(a*x): d/da = x·exp(a·x), d/dx = a·exp(a·x). If the export path
// stopped composing the two leaves, this stops compiling or fails.
//
// It is deliberately NOT a re-test of the autodiff transform (frontend_gradients / the M7 groups do
// that) — it is the packaging proof: the shippable header works standalone.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"
#include "heinzel/support/fail.hpp"

#include "heinzel/gradients.hpp" // the exported header under test — from app/, via heinzel_app

#include "interpret.hpp"

using namespace heinzel;

namespace {

constexpr std::size_t N = 8;

Graph build() {
    Input a{"a", Signal{N}, ElemType::F64};
    Input x{"x", Signal{N}, ElemType::F64};
    Output loss{"loss"};
    loss = exp(a * x);
    return gradients(loss, {Grad{"da", a}, Grad{"dx", x}});
}

std::int32_t out_value(const Graph& g, std::string_view name) {
    for (std::size_t i = 0; i < g.outputs.size(); ++i) {
        if (g.outputs[i].name.view() == name) {
            return g.outputs[i].value;
        }
    }
    return kNoValue;
}

} // namespace

int main() {
    Graph g = build();
    if (!infer(g).ok) {
        fail("gradients_export: the exported gradient graph did not infer");
    }

    std::array<double, N> a{};
    std::array<double, N> x{};
    for (std::size_t n = 0; n < N; ++n) {
        a[n] = 0.25 * std::sin(0.7 * static_cast<double>(n + 1));
        x[n] = 0.9 * std::cos(0.4 * static_cast<double>(n + 2));
    }

    Inputs feeds{};
    for (std::size_t i = 0; i < g.inputs.size(); ++i) {
        const std::string_view nm = g.inputs[i].name.view();
        feeds.push_back(InputFeed{nm == std::string_view{"a"} ? a.data() : x.data(), 1});
    }
    Signals<N> out{};
    run(g, feeds, out);

    const std::int32_t da = out_value(g, "da");
    const std::int32_t dx = out_value(g, "dx");
    double max_err = 0.0;
    for (std::size_t n = 0; n < N; ++n) {
        const double e = std::exp(a[n] * x[n]);
        max_err = std::fmax(max_err, std::fabs(sample_at(out, da, 0, n) - x[n] * e));
        max_err = std::fmax(max_err, std::fabs(sample_at(out, dx, 0, n) - a[n] * e));
    }
    if (max_err > 1e-9) {
        fail("gradients_export: da/dx did not match the closed form of exp(a*x)");
    }
    std::printf("app_gradients_export ok\n");
    return 0;
}
