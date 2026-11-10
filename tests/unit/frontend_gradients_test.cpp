// MG S2 — the frontend gradients() capture spelling, lowering to the closed M7 autodiff transform.
//
// A graph built entirely through the frontend — loss = exp(a*x) over two scalar-stream inputs, plus
// an unused input z — is finished with `gradients(loss, {…}, level)`, which appends the adjoint nodes with
// `heinzel::autodiff::gradients` and names the loss and each gradient as outputs. The proof is
// numbers against numbers: the graph RUNS, and da, dx read off the adjoint nodes match a genuine
// finite-difference of the loss (perturb one input, re-run) and the closed form; dz, whose loss does
// not depend on it, is a materialised zero. The capture adds no operation — it names the same numbers
// the M7 transform already produces under test — so this checks the spelling reaches the transform,
// not the transform itself.
//
// `gradients(loss, {…}, level)` on a declared `Output` (Plan/archive/14-dsl/02 §8) builds the graph `finish_gradients` built
// with a context carrying that level, at either level: both are deleted (L9), so the two graphs are pinned by
// their hashes, recorded at 22b7c9ef, re-pinned when the hash stopped mixing a value's units and the sample rate
// (Decision 59), and again when `ValueCategory::Output` went (Decision 60): the zero `dz` was marked `Output` and is
// a `Const` since. Otherwise the graphs are field for field the same. The hash does not tell a `Normal` value from
// an unmarked one, so every value is also checked to resolve to the level.
//
// The refusals are shown with their texts: a target that is no input (a computed value, a default `Expr{}`), a
// repeated name, a name starting with `#` (kept for a debug build's checks), none. The "loss must be one number per sample" refusal lives in `heinzel::autodiff::gradients`,
// and is tested there.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"
#include "heinzel/support/fail.hpp"

#include "interpret.hpp"
#include "graph_hash.hpp"
#include "gradients_capture.hpp"

using namespace heinzel;

namespace {

constexpr std::size_t N = 16;

// Small magnitudes so exp(a*x) stays O(1) and the finite difference is well conditioned.
std::array<double, N> signal_a() {
    std::array<double, N> v{};
    for (std::size_t n = 0; n < N; ++n) {
        v[n] = 0.25 * std::sin(0.7 * static_cast<double>(n + 1));
    }
    return v;
}
std::array<double, N> signal_x() {
    std::array<double, N> v{};
    for (std::size_t n = 0; n < N; ++n) {
        v[n] = 0.9 * std::cos(0.4 * static_cast<double>(n + 2));
    }
    return v;
}

// loss = exp(a*x); da, dx its gradients; dz the gradient w.r.t. an input the loss ignores (a zero). The level is
// the graph's default precision.
Graph build(numerics::Precision level = numerics::Precision::Normal) {
    Input a{"a", Signal{N}, f64};
    Input x{"x", Signal{N}, f64};
    Input z{"z", Signal{N}, f64}; // z declared, unused
    Output loss{"loss"};
    loss = exp(a * x);
    return gradients(loss, {Grad{"da", a}, Grad{"dx", x}, Grad{"dz", z}}, level);
}

// Run the graph feeding a, x, z by their names (g.inputs order is arena order, not the call order).
void feed_run(const Graph& g, const std::array<double, N>& a, const std::array<double, N>& x,
              const std::array<double, N>& z, Signals<N>& out) {
    Inputs feeds{};
    for (std::size_t i = 0; i < g.inputs.size(); ++i) {
        const std::string_view nm = g.inputs[i].name.view();
        const double* p = nm == std::string_view{"a"} ? a.data()
                          : nm == std::string_view{"x"} ? x.data()
                                                        : z.data();
        feeds.push_back(InputFeed{p, 1});
    }
    run(g, feeds, out);
}

std::int32_t out_value(const Graph& g, std::string_view name) {
    for (std::size_t i = 0; i < g.outputs.size(); ++i) {
        if (g.outputs[i].name.view() == name) {
            return g.outputs[i].value;
        }
    }
    return kNoValue;
}

// A one-input forward difference of the loss: bump `which` by eps, re-run, read the loss back.
double fd_wrt(const Graph& g, std::string_view which, const std::array<double, N>& a,
              const std::array<double, N>& x, const std::array<double, N>& z, std::size_t n, double eps) {
    std::array<double, N> ap = a;
    std::array<double, N> xp = x;
    std::array<double, N> zp = z;
    if (which == std::string_view{"a"}) { ap[n] += eps; }
    else if (which == std::string_view{"x"}) { xp[n] += eps; }
    Signals<N> base{};
    Signals<N> bumped{};
    feed_run(g, a, x, z, base);
    feed_run(g, ap, xp, zp, bumped);
    const std::int32_t loss = out_value(g, "loss");
    return (sample_at(bumped, loss, 0, n) - sample_at(base, loss, 0, n)) / eps;
}

int check_gradients() {
    Graph g = build();
    if (!infer(g).ok) {
        return 1;
    }
    const std::array<double, N> a = signal_a();
    const std::array<double, N> x = signal_x();
    std::array<double, N> z{}; // any values; the loss ignores it
    for (std::size_t n = 0; n < N; ++n) {
        z[n] = 3.0 * static_cast<double>(n) - 5.0;
    }

    Signals<N> base{};
    feed_run(g, a, x, z, base);
    const std::int32_t da = out_value(g, "da");
    const std::int32_t dx = out_value(g, "dx");
    const std::int32_t dz = out_value(g, "dz");

    const double eps = 1e-6;
    for (std::size_t n = 0; n < N; ++n) {
        const double e = std::exp(a[n] * x[n]);
        const double got_da = sample_at(base, da, 0, n);
        const double got_dx = sample_at(base, dx, 0, n);
        const double got_dz = sample_at(base, dz, 0, n);
        const double tol_a = 1e-4 * (1.0 + std::fabs(got_da));
        const double tol_x = 1e-4 * (1.0 + std::fabs(got_dx));
        if (std::fabs(got_da - x[n] * e) > tol_a || std::fabs(got_da - fd_wrt(g, "a", a, x, z, n, eps)) > tol_a) {
            return 2;
        }
        if (std::fabs(got_dx - a[n] * e) > tol_x || std::fabs(got_dx - fd_wrt(g, "x", a, x, z, n, eps)) > tol_x) {
            return 3;
        }
        if (got_dz != 0.0) { // an ignored target's gradient is an exact materialised zero
            return 4;
        }
    }
    return 0;
}

// A gradient graph is analysed while it is being built, so the level is an argument: set on the returned graph it
// would come too late. Every value of it — the primal, the adjoints, the constants autodiff emits — resolves to it,
// and the graph records the level it was resolved under (neither of which the pinned hash reads).
bool every_value_at(numerics::Precision level) {
    const Graph g = build(level);
    if (g.precision_resolved_under != level) {
        return false;
    }
    for (const Value& v : g.values) {
        if (v.precision != level) {
            return false;
        }
    }
    return !g.values.empty();
}

bool the_graph_finish_gradients_built() {
    return hash_graph(build(numerics::Precision::Normal)) == 0xf54f1e48ddba3025ull &&
           hash_graph(build(numerics::Precision::High)) == 0x1beb15573aaeb6dcull;
}

// Shown rejecting, each with its text: a target that is a computed value, not an input; a gradient named as the
// loss is; no target; a default `Expr{}`; a gradient named `#…`, as an `Output` is refused.
bool refusals() {
    const auto refused = [](auto build, std::string_view text) {
        try {
            build();
        } catch (const heinzel::error& e) {
            return e.message().find(text) != std::string_view::npos;
        }
        return false;
    };
    const auto loss_of = [](Output& loss, const Input& a) { loss = exp(a * 2.0); };
    return refused([] {
        Input a{"a", Signal{N}, f64};
        Input x{"x", Signal{N}, f64};
        Output loss{"loss"};
        const Expr prod = a * x; // an internal value, not an input
        loss = exp(prod);
        return gradients(loss, {Grad{"dbad", prod}});
    }, "must be an Input") && refused([&] {
        Input a{"a", Signal{N}, f64};
        Output loss{"loss"};
        loss_of(loss, a);
        return gradients(loss, {Grad{"loss", a}});
    }, "two ports share a name") && refused([&] {
        Input a{"a", Signal{N}, f64};
        Output loss{"loss"};
        loss_of(loss, a);
        return gradients(loss, {});
    }, "no target") && refused([&] {
        Input a{"a", Signal{N}, f64};
        Output loss{"loss"};
        loss_of(loss, a);
        return gradients(loss, {Grad{"dnothing", Expr{}}});
    }, "must be an Input") && refused([&] {
        Input a{"a", Signal{N}, f64};
        Output loss{"loss"};
        loss_of(loss, a);
        return gradients(loss, {Grad{"#da", a}});
    }, "heinzel: an output's name starting with # is kept for the checks of promises");
}

} // namespace

int main() {
    if (!the_graph_finish_gradients_built()) {
        return 7;
    }
    if (const int rc = check_gradients(); rc != 0) {
        return rc;
    }
    if (!every_value_at(numerics::Precision::Normal) || !every_value_at(numerics::Precision::High)) {
        return 6;
    }
    if (!refusals()) {
        return 5;
    }
    return 0;
}
