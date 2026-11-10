// Built with HEINZEL_KNOB_MATH=0 and HEINZEL_LANES=0 (`docs/knob-math.md`, `docs/sections-in-lanes.md`, "Switch"):
// eight Param RBJ biquads (`biquad8/heinzel-param`) and an RBJ peak from three knobs (`knobs1/heinzel`), on skewed
// and kept, hold no skewed or kept stage on zen2, and run on a release `Executor` within param_arms' bound of the walk.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
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

static_assert(HEINZEL_KNOB_MATH == 0 && HEINZEL_LANES == 0, "switches_off: built with both switches at 0");

using namespace param_arms;

namespace {

int g_wrong = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++g_wrong;
        std::printf("FAILED: %s\n", what.c_str());
    }
}

constexpr std::size_t kSamples = 4096;
constexpr std::size_t kBand = 4; // (the 1 kHz peak)

// What Param `name` holds: section j's coefficient of kEq8 (`b1_3`), or band 4's knob at rest (1 kHz, +6 dB, Q 1).
double number_of(std::string_view name) {
    constexpr std::array<double, 3> knobs{1000.0, 6.0, 1.0};
    for (std::size_t j = 0; j < kEq8.size(); ++j) {
        for (std::size_t i = 0; i < 5; ++i) {
            if (name == kCoefficient[j][i]) {
                return static_cast<double>(kEq8[j][i]);
            }
            if (i < 3 && name == kKnob[kBand][i]) {
                return knobs[i];
            }
        }
    }
    fail("switches_off_test: a Param no graph sets");
}

// Graph `g` as written, its Length bound to kSamples, walked over x, each Param fed what it holds: y.
std::vector<double> walked(Graph g, const std::vector<double>& x) {
    Bindings sizes{};
    sizes.bind(Dim{kStreamLength}, static_cast<std::int32_t>(kSamples));
    if (const Diagnostic d = infer(g, sizes); !d.ok) {
        fail(d.message.view());
    }
    std::vector<std::vector<double>> fed{};
    for (const NamedValue& port : g.inputs) {
        const std::string_view name = port.name.view();
        fed.push_back(name == "x" ? x : std::vector<double>(kSamples, number_of(name)));
    }
    Inputs feeds{};
    for (const std::vector<double>& f : fed) {
        feeds.push_back(InputFeed{f.data(), 1});
    }
    const auto walk = std::make_unique<Signals<kSamples, 1024>>();
    run(g, feeds, *walk);
    std::vector<double> y(kSamples);
    for (std::size_t s = 0; s < kSamples; ++s) {
        y[s] = sample_at(*walk, g.outputs[0].value, 0, s);
    }
    return y;
}

// `Build` on a release `Executor<…, zen2>`, its Params set, over x in blocks of `block`: y.
template <auto Build>
std::vector<double> executed(const std::vector<double>& x, std::size_t block) {
    static constexpr Graph g = Build();
    const auto ex = std::make_unique<Executor<Build, zen2, false>>();
    ex->prepare(static_cast<std::int64_t>(block));
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        const auto set = [&]<std::size_t K>() {
            static constexpr Name name = g.inputs[K].name;
            if constexpr (name.view() != "x") {
                ex->param(name.data_) = number_of(name.view());
            }
        };
        (set.template operator()<I>(), ...);
    }(std::make_index_sequence<g.inputs.size()>{});
    const std::vector<float> in(x.begin(), x.end());
    std::vector<float> y(x.size());
    for (std::size_t at = 0; at < x.size(); at += block) {
        ex->input("x") = in.data() + at;
        ex->output("y") = y.data() + at;
        ex->run(static_cast<std::int64_t>(block));
    }
    return std::vector<double>(y.begin(), y.end());
}

// Row `row`: the program a release `Executor<Build, zen2>` runs holds no skewed and no kept stage, and runs within
// `bound` of the walk in blocks of 256 and 32.
template <auto Build>
void switched_off(const std::string& row, double bound) {
    const std::unique_ptr<const CompileResult> c(new auto(try_compile(Build(), zen2, Bindings{}, false)));
    std::int32_t skewed = 0;
    std::int32_t kept = 0;
    for (const loops::Stage& stage : c->compiled.program.stages) {
        skewed += stage.skewed ? 1 : 0;
        kept += stage.kept ? 1 : 0;
    }
    check(c->refusal.view().empty() && skewed == 0 && kept == 0, row + ": compiled, no stage skewed, none kept");
    const std::vector<double> x = noise(kSamples);
    const std::vector<double> want = walked(Build(), x);
    for (const std::size_t block : {std::size_t{256}, std::size_t{32}}) {
        const std::vector<double> got = executed<Build>(x, block);
        const double off = off_by(got, want);
        std::printf("%s, B%zu: %d skewed, %d kept, %.3g from the walk\n", row.c_str(), block, skewed, kept, off);
        check(off <= bound, row + ": within its bound of the walk");
    }
}

constexpr Graph biquad8() { return param_cascade<8, 0>(); }
constexpr Graph knobs1() { return knob_eq<kBand, 1>(); }

} // namespace

int main() {
    try {
        switched_off<biquad8>("biquad8/heinzel-param", 5e-4);
        switched_off<knobs1>("knobs1/heinzel", 1e-3);
    } catch (const heinzel::error& e) {
        std::printf("switches_off: %.*s\n", static_cast<int>(e.message().size()), e.message().data());
        return 1;
    }
    if (g_wrong != 0) {
        std::printf("switches_off_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("switches_off_test: switched off, no stage skewed or kept, every arm within its bound\n");
    return 0;
}
