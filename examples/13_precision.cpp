// Tour 13: precision. The mean of a million numbers at two levels, and `exp` of doubles at two more.
//
// Every value has a precision, which says what the compiler may do with its rounding:
//
//   - `Normal`, the default: a float is computed in float, its transcendentals by float cores within two units in
//     the last place, and a sum may be reordered (split across vector lanes, fused into multiply-adds). It is as
//     accurate as the libraries Heinzel is measured against, and as fast. A feedback loop keeps its state in double
//     where that costs nothing, and always where it is one voice of cheap arithmetic whose past sits in registers (a
//     phase, an envelope, a filter): far more accurate, and at most a little slower.
//   - `High`: every operation computed in double, in the written order, and rounded once to its type. It is what the
//     test suite holds every program to, bit for bit, and makes no claim on speed.
//   - `Fast`: may trade accuracy for speed too. Today that means shorter series for the double cores.
//
// A mark pins one value, written where the value is made: `x.mean(0).precision(High)`, or a port in its declaration,
// `Expr x = Input{"x", Signal{N}}.precision(High)`. A value with a name is shared by everything that reads it, so
// `y.precision(High)` on a named `y` does not compile: it would change them all. `return finish(High, y);` sets the
// level of every value the graph does not pin. A value computed from a pinned one takes the highest of its operands'
// levels and the graph's default: in a `Normal` graph, a value computed from a `Fast` one is `Normal`. A port is
// `f32` unless declared `f64` (`Input x{"x", {kPoints}, f64}`), and then bound to a `double*`.
//
// Each result is checked against the exact one, in long double.

#include <cmath>
#include <cstdio>
#include <vector>

#include "heinzel/dsl.hpp"

using namespace heinzel;

namespace {

constexpr int kCount = 1 << 20;
constexpr int kPoints = 4096;

template <Precision P>
constexpr Graph average() {
    Input x{"x", {kCount}};
    Output mean{"mean"};
    mean = x.mean(0).precision(P);
    return mean;
}

template <Precision P>
constexpr Graph growth() {
    Input x{"x", {kPoints}, f64};
    Output y{"y"};
    y = exp(x);
    return finish(P, y);
}

template <auto Build>
float mean_of(const std::vector<float>& x) {
    float mean = 0.0f;
    Executor<Build> e;
    e.input("x") = x.data();
    e.output("mean") = &mean;
    e.run();
    return mean;
}

// The largest relative error of exp over x.
template <auto Build>
double exp_error(const std::vector<double>& x) {
    std::vector<double> y(x.size());
    Executor<Build> e;
    e.input("x") = x.data();
    e.output("y") = y.data();
    e.run();
    double off = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        const long double exact = std::exp(static_cast<long double>(x[i]));
        off = std::fmax(off, static_cast<double>(std::fabs((y[i] - exact) / exact)));
    }
    return off;
}

} // namespace

int main() {
    std::vector<float> x(kCount);
    unsigned seed = 13;
    long double exact = 0.0L;
    for (float& v : x) { // uniform in [0, 1): a sum that grows to half a million, one small number at a time
        seed = seed * 1664525u + 1013904223u;
        v = static_cast<float>(seed >> 8) / 16777216.0f;
        exact += v;
    }
    exact /= kCount;
    const auto relative = [&](float mean) {
        return static_cast<double>(std::fabs((mean - exact) / exact));
    };
    const float normal_mean = mean_of<average<Normal>>(x);
    const float high_mean = mean_of<average<High>>(x);
    const double normal = relative(normal_mean);
    const double high = relative(high_mean);

    std::vector<double> points(kPoints);
    for (int i = 0; i < kPoints; ++i) {
        points[i] = -5.0 + 10.0 * i / kPoints;
    }
    const double precise = exp_error<growth<Normal>>(points);
    const double fast = exp_error<growth<Fast>>(points);

    std::printf("13_precision: a mean of 2^20 floats %.1e off at Normal, %.1e at High; exp of doubles %.1e off at Normal, "
                "%.1e at Fast\n", normal, high, precise, fast);
    // High's sum is in double, rounded once: within half a unit of a float. Normal's sums in float, many at once, so
    // its rounding grows with the count (1e-6 here), yet stays far below a thousandth of a percent. Fast's double
    // cores are shorter series (3e-6 here); Normal's are good to the last bit or two of a double.
    if (!(high < 6e-8 && normal < 1e-5 && precise < 1e-15 && fast < 1e-3 && fast > precise)) {
        std::fprintf(stderr, "13_precision: an error outside its level's bound\n");
        return 1;
    }
    return 0;
}
