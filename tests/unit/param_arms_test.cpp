// The benchmarks' Param arms (`docs/sections-in-lanes.md`), their knobs at rest, as a release
// `Executor<…, zen2>` runs them (`compile()` rewrites them, sections in lanes where the price takes them), against the
// walk of the graph as written (`tests/reference/`), over 4096 samples of noise in one run; over a `Length`, in blocks
// of 256 and 32 too, bit for bit that run. The walk rounds every operation to float in the order written, its state
// too; the program sums in the order the terms are ready and keeps the lanes' state in double. So the two differ by the
// walk's own rounding, which the poles amplify (kEq8's 40 Hz high-pass most), and each row is held to its benchmark's
// bound against the cascade in double, relative to the walk's largest magnitude.
//
//   biquad8/heinzel-param           kEq8's eight sections, their 40 coefficients Params    whole, B256   lanes
//   biquad3/heinzel-param           kEq's three (`biquad3dyn` over a Length)               whole, B256   lanes
//   lr8/heinzel                     kLr8's four, their coefficients constants              whole, B256   lanes
//   knobs8/heinzel                  eight RBJ peaks, each worked out from three Params      B32, B256     lanes
//   knobs2/heinzel                  two of them, 500 Hz and 1 kHz                           B32, B256     the cascade
//   knobs1/heinzel                  the 1 kHz one                                           B32, B256     one section
//   knobs2, 1/heinzel-coefficients  the same as Param biquads, set from the peaks' design   B32, B256     as knobs2, 1
//
// Each program's skewed stages are counted, so that the lanes are what runs here where the price takes them, and the
// cascade where it keeps it.

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "heinzel/compile.hpp"
#include "heinzel/executor.hpp"
#include "heinzel/schedule/machine.hpp"
#include "heinzel/sema/analyze.hpp"
#include "heinzel/support/fail.hpp"

#include "interpret.hpp"
#include "param_arms_graphs.hpp"

using namespace param_arms;

namespace {

int g_wrong = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        ++g_wrong;
        std::printf("FAILED: %s\n", what);
    }
}

constexpr std::size_t kSamples = 4096;
constexpr std::size_t kPlanes = 1024; // (the eight peaks' values hold about 450 numbers a sample)

// ---- what the Params hold ------------------------------------------------------------------------------------------

// Band j's knobs at rest (`bench_knobs`): 62.5 · 2^j Hz, +6 dB, Q 1.
constexpr std::array<double, 3> knobs_of(std::size_t j) { return {62.5 * static_cast<double>(1u << j), 6.0, 1.0}; }

// The RBJ peak's coefficients in double from its knobs, rounded to float as a plugin keeps them (`section_of`).
Coefficients designed(std::size_t band) {
    const auto [freq, gain, q] = knobs_of(band);
    const double w = 2.0 * std::numbers::pi * freq / kRate;
    const double big = std::pow(10.0, gain / 40.0);
    const double alpha = std::sin(w) / (2.0 * q);
    const double a0 = 1.0 + alpha / big;
    const auto f = [](double v) { return static_cast<float>(v); };
    const double k = -2.0 * std::cos(w) / a0;
    return {f((1.0 + alpha * big) / a0), f(k), f((1.0 - alpha * big) / a0), f(k), f((1.0 - alpha / big) / a0)};
}

// What Param `name` holds: section j's coefficient of `sections` (`b1_3`: section 3's b1), or band j's knob
// (`freq3`), as the float a Param holds.
double number_of(std::string_view name, const std::vector<Coefficients>& sections) {
    for (std::size_t j = 0; j < kKnob.size(); ++j) {
        for (std::size_t i = 0; i < 5; ++i) {
            if (j < sections.size() && name == kCoefficient[j][i]) {
                return static_cast<double>(sections[j][i]);
            }
            if (i < 3 && name == kKnob[j][i]) {
                return static_cast<double>(static_cast<float>(knobs_of(j)[i]));
            }
        }
    }
    fail("param_arms_test: a Param no arm sets");
    return 0;
}

// ---- what runs them ------------------------------------------------------------------------------------------------

// Graph `g` as written, a Length bound to kSamples, walked over x, each Param fed what it holds at every sample: y.
std::vector<double> walked(Graph g, const std::vector<double>& x, const std::vector<Coefficients>& sections) {
    Bindings sizes{};
    sizes.bind(Dim{kStreamLength}, static_cast<std::int32_t>(kSamples));
    if (const Diagnostic d = infer(g, sizes); !d.ok) {
        fail(d.message.view());
    }
    std::vector<std::vector<double>> fed{};
    for (const NamedValue& port : g.inputs) {
        const std::string_view name = port.name.view();
        if (name == "x") {
            fed.push_back(x);
            continue;
        }
        const double held = number_of(name, sections);
        fed.push_back(std::vector<double>(kSamples, held));
    }
    Inputs feeds{};
    for (const std::vector<double>& f : fed) {
        feeds.push_back(InputFeed{f.data(), 1});
    }
    const auto walk = std::make_unique<Signals<kSamples, kPlanes>>();
    run(g, feeds, *walk);
    std::vector<double> y(kSamples);
    for (std::size_t s = 0; s < kSamples; ++s) {
        y[s] = sample_at(*walk, g.outputs[0].value, 0, s);
    }
    return y;
}

// `Build`'s ports, in order (an `Executor` keeps none of its compile result).
template <auto Build>
inline constexpr Graph kPorts = Build();

// `Build` on a release `Executor<…, zen2>`, its Params set to what they hold, over x in blocks of `block`: y.
template <auto Build>
std::vector<double> executed(const std::vector<double>& x, std::size_t block, const std::vector<Coefficients>& sections) {
    using E = Executor<Build, zen2, false>;
    static constexpr const Graph& g = kPorts<Build>;
    const auto ex = std::make_unique<E>();
    if constexpr (E::has_length) {
        ex->prepare(static_cast<std::int64_t>(block));
    }
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        const auto set = [&]<std::size_t K>() {
            static constexpr Name name = g.inputs[K].name;
            if constexpr (name.view() != "x") {
                ex->param(name.data_) = number_of(name.view(), sections);
            }
        };
        (set.template operator()<I>(), ...);
    }(std::make_index_sequence<g.inputs.size()>{});
    const std::vector<float> in(x.begin(), x.end());
    std::vector<float> y(x.size());
    for (std::size_t at = 0; at < x.size(); at += block) {
        ex->input("x") = in.data() + at;
        ex->output("y") = y.data() + at;
        if constexpr (E::has_length) {
            ex->run(static_cast<std::int64_t>(block));
        } else {
            ex->run();
        }
    }
    return std::vector<double>(y.begin(), y.end());
}

// The skewed stages of the program a release `Executor<Build, zen2>` runs (-1: refused).
template <auto Build>
std::int32_t skewed_stages() {
    const std::unique_ptr<const CompileResult> c(new auto(try_compile(Build(), zen2, Bindings{}, false)));
    if (!c->refusal.view().empty()) {
        return -1;
    }
    std::int32_t count = 0;
    for (const loops::Stage& stage : c->compiled.program.stages) {
        count += stage.skewed ? 1 : 0;
    }
    return count;
}

// Row `row`'s arm, its Params holding `sections`' coefficients or the knobs at rest: `skewed` skewed stages; in one run
// within `bound` of the walk of its graph as written; over a Length, in blocks of 256 and 32 bit for bit that run.
template <auto Build>
[[gnu::noinline]] void at_rest(const char* row, std::int32_t skewed, double bound, const std::vector<Coefficients>& sections = {}) {
    const std::vector<double> x = noise(kSamples);
    const std::vector<double> want = walked(Build(), x, sections);
    const std::int32_t lanes = skewed_stages<Build>();
    const std::vector<double> whole = executed<Build>(x, kSamples, sections);
    const double off = off_by(whole, want);
    std::printf("%s: %d skewed stages, %.3g from the walk\n", row, lanes, off);
    check(lanes == skewed, (std::string(row) + ": the stages skewed as the price decides").c_str());
    check(off <= bound, (std::string(row) + ": within its bound of the walk").c_str());
    if constexpr (Executor<Build, zen2, false>::has_length) {
        for (const std::size_t block : {std::size_t{256}, std::size_t{32}}) {
            const std::vector<double> ran = executed<Build>(x, block, sections);
            const auto same = [](double a, double b) { return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b); };
            check(std::equal(ran.begin(), ran.end(), whole.begin(), same), (std::string(row) + ": in blocks bit for bit one run").c_str());
        }
    }
}

constexpr Graph biquad8_whole() { return param_cascade<8, static_cast<std::int32_t>(kSamples)>(); }
constexpr Graph biquad8_block() { return param_cascade<8, 0>(); }
constexpr Graph biquad3_whole() { return param_cascade<3, static_cast<std::int32_t>(kSamples)>(); }
constexpr Graph biquad3_block() { return param_cascade<3, 0>(); }
constexpr Graph lr8_whole() { return lr8<static_cast<std::int32_t>(kSamples)>(); }
constexpr Graph lr8_block() { return lr8<0>(); }
constexpr Graph one_peak() { return knob_eq<4, 1>(); }
constexpr Graph two_peaks() { return knob_eq<3, 2>(); }
constexpr Graph eight_peaks() { return knob_eq<0, 8>(); }
constexpr Graph one_biquad() { return param_cascade<1, 0>(); }
constexpr Graph two_biquads() { return param_cascade<2, 0>(); }

} // namespace

int main() {
    try {
        const std::vector<Coefficients> eq8(kEq8.begin(), kEq8.end());
        const std::vector<Coefficients> eq3(kEq.begin(), kEq.end());
        at_rest<biquad8_whole>("biquad8/heinzel-param", 1, 5e-4, eq8);
        at_rest<biquad8_block>("biquad8/heinzel-param over a Length", 1, 5e-4, eq8);
        at_rest<biquad3_whole>("biquad3/heinzel-param", 1, 1e-3, eq3);
        at_rest<biquad3_block>("biquad3dyn/heinzel-param", 1, 1e-3, eq3);
        at_rest<lr8_whole>("lr8/heinzel", 1, 3e-6);
        at_rest<lr8_block>("lr8/heinzel over a Length", 1, 3e-6);
        at_rest<eight_peaks>("knobs8/heinzel", 1, 1e-3);
        at_rest<two_peaks>("knobs2/heinzel", 0, 1e-3);
        at_rest<one_peak>("knobs1/heinzel", 0, 1e-3);
        at_rest<two_biquads>("knobs2/heinzel-coefficients", 0, 1e-3, {designed(3), designed(4)});
        at_rest<one_biquad>("knobs1/heinzel-coefficients", 0, 1e-3, {designed(4)});
    } catch (const heinzel::error& e) {
        std::printf("param_arms: %.*s\n", static_cast<int>(e.message().size()), e.message().data());
        return 1;
    }
    if (g_wrong != 0) {
        std::printf("param_arms_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("param_arms_test: every Param arm at rest within its bound of the walk of its graph as written\n");
    return 0;
}
