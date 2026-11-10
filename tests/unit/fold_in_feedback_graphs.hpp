// The graphs and walk helpers of fold_in_feedback_test and fold_in_feedback_planned_test.
#pragma once

#include "heinzel/compile.hpp"
#include "heinzel/executor.hpp"
#include "heinzel/frontend/declarations.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/sema/analyze.hpp"

#include "canaried.hpp"
#include "executed.hpp"
#include "fold_in_feedback.hpp"
#include "interpret.hpp"
#include "loops_schedules.hpp"
#include "planned.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string_view>
#include <type_traits>
#include <vector>

using namespace heinzel;
namespace ff = fold_in_feedback;
using ff::Bank;

namespace {

int g_wrong = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        ++g_wrong;
        std::printf("FAILED: %s\n", what);
    }
}

constexpr Index n{};
constexpr std::size_t kSamples = 300; // more than the largest block, run in blocks of 256 and the rest

template <class T>
constexpr ElemType kType = std::is_same_v<T, float> ? f32 : f64;

template <class T, std::size_t V>
constexpr std::array<T, V> each_voice(double (*coefficient)(std::int32_t)) {
    std::array<T, V> c{};
    for (std::size_t k = 0; k < V; ++k) {
        c[k] = static_cast<T>(coefficient(static_cast<std::int32_t>(k)));
    }
    return c;
}

// ---- the graphs (what `fold_in_feedback.hpp`'s trees compute) ------------------------------------------------

template <class T, std::size_t V>
constexpr Graph plain() {
    Input x{"x", Signal{Length{}}, kType<T>};
    Variable y;
    Variable v;
    const Expr u = x - ff::kGain * y[n - 1];
    v[n] = constant(each_voice<T, V>(ff::c0)) * u + constant(each_voice<T, V>(ff::c1)) * u[n - 1] + constant(each_voice<T, V>(ff::a1)) * v[n - 1];
    y[n] = Expr(v).sum(0) + ff::kDirect * u;
    return finish(Output{"y"} = y);
}

template <class T, std::size_t V>
constexpr Graph direct() {
    Input x{"x", Signal{Length{}}, kType<T>};
    Variable y;
    Variable v;
    v[n] = constant(each_voice<T, V>(ff::c0)) * x - constant(each_voice<T, V>(ff::c1)) * y[n - 1] + constant(each_voice<T, V>(ff::a1)) * v[n - 1];
    y[n] = Expr(v).sum(0) + ff::kDirect * x;
    return finish(Output{"y"} = y);
}

template <class T, std::size_t V>
constexpr Graph own_lag() {
    Input x{"x", Signal{Length{}}, kType<T>};
    Variable s;
    Variable v;
    const Expr u = x - ff::kGain * s[n - 1];
    v[n] = u + constant(each_voice<T, V>(ff::a1)) * v[n - 1];
    s[n] = Expr(v).sum(0);
    return finish(Output{"y"} = ff::kDirect * s);
}

template <class T, std::size_t V>
constexpr Graph tdf2() {
    Input x{"x", Signal{Length{}}, kType<T>};
    Variable y;
    Variable w;
    const Expr u = x - ff::kGain * y[n - 1];
    const Expr o = constant(each_voice<T, V>(ff::c0)) * u + w[n - 1];
    y[n] = o.sum(0);
    w[n] = constant(each_voice<T, V>(ff::c1)) * u - constant(each_voice<T, V>(ff::a1)) * o;
    return finish(Output{"y"} = y);
}

template <Bank B, class T, std::size_t V>
constexpr Graph graph_of() {
    if constexpr (B == Bank::Plain) {
        return plain<T, V>();
    } else if constexpr (B == Bank::Direct) {
        return direct<T, V>();
    } else if constexpr (B == Bank::OwnLag) {
        return own_lag<T, V>();
    } else {
        return tdf2<T, V>();
    }
}

// Graph `g` at High (or Normal), its length `kSamples`, walked over `x`: its output.
std::vector<double> walked(Graph g, const std::vector<double>& x, bool high = true) {
    set_default_precision(g, high ? numerics::Precision::High : numerics::Precision::Normal);
    Bindings sizes{};
    sizes.bind(Dim{kStreamLength}, static_cast<std::int32_t>(kSamples));
    if (!infer(g, sizes).ok) {
        fail("fold_in_feedback_test: a graph does not analyse");
    }
    Inputs feeds{};
    feeds.push_back(InputFeed{x.data(), 1});
    const auto signals = std::make_unique<Signals<kSamples, 256>>();
    run(g, feeds, *signals);
    std::vector<double> y(kSamples);
    for (std::size_t i = 0; i < kSamples; ++i) {
        y[i] = sample_at(*signals, g.outputs[0].value, 0, i);
    }
    return y;
}

std::vector<double> noise() {
    std::vector<double> x(kSamples);
    for (std::size_t i = 0; i < kSamples; ++i) {
        x[i] = static_cast<double>(static_cast<std::int32_t>((i * 2654435761u) % 2001u) - 1000) / 1024.0; // (a float holds each)
    }
    return x;
}

bool same_bits(const std::vector<double>& a, const std::vector<double>& b, std::size_t from = 0) {
    for (std::size_t i = 0; i < b.size(); ++i) {
        if (std::bit_cast<std::uint64_t>(a[from + i]) != std::bit_cast<std::uint64_t>(b[i])) {
            std::printf("  sample %zu: ran %.17g, walked %.17g\n", i, a[from + i], b[i]);
            return false;
        }
    }
    return true;
}

// The graph at High, analysed over a `Length`, and its program lowered from the planner's layout of the stage.
template <Bank B, class T, std::size_t V>
constexpr Graph kAtHigh = [] {
    Graph g = graph_of<B, T, V>();
    set_default_precision(g, numerics::Precision::High);
    if (!infer(g).ok) {
        fail("fold_in_feedback_test: a graph does not analyse");
    }
    return g;
}();
// The plain bank with a second fold beside its sum (y = Σv + max v + d·u), and TDF2's lanes reading their sum
// (w = c1·y − a1·o, n7 the sum, n8 y), at High over a `Length`.
constexpr Graph at_high(Graph g) {
    set_default_precision(g, numerics::Precision::High);
    if (!infer(g).ok) {
        fail("fold_in_feedback_test: a graph does not analyse");
    }
    return g;
}
constexpr Graph two_folds() {
    Input x{"x", Signal{Length{}}, f64};
    Variable y;
    Variable v;
    const Expr u = x - ff::kGain * y[n - 1];
    v[n] = constant(each_voice<double, 8>(ff::c0)) * u + constant(each_voice<double, 8>(ff::a1)) * v[n - 1];
    y[n] = Expr(v).sum(0) + Expr(v).max(0) + ff::kDirect * u;
    return at_high(finish(Output{"y"} = y));
}
} // namespace
