// Sections in lanes, the whole path (`docs/sections-in-lanes.md`). The linked graphs over a `Length`, at High
// (`support/linked_sections.hpp`: the TPT pair, LR8, the five peaks, their coefficients [5, 1] inputs, the eight tanh
// sections and the pair into the eight), and the K = 8 Param cascade as `compile` plans it (eight `df1_param` sections
// over a Length at Normal, merged by `sections_in_lanes` and given the rules after it: the gather of the 40 Params, the
// cycle in double) go through `compile()`, which leaves each as written, and run skewed on `Executor<…, zen2>`, each
// bit for bit as its walk (`tests/reference/`) over 4096 samples of noise: in one run, in blocks of 1, k − 1, k, 32 and
// 256, in blocks of uneven lengths, and again after `reset()`.

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "heinzel/compile.hpp"
#include "heinzel/executor.hpp"
#include "heinzel/frontend/declarations.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/rewrite/rewritten.hpp"
#include "heinzel/rewrite/sections_in_lanes.hpp"
#include "heinzel/schedule/machine.hpp"
#include "heinzel/sema/analyze.hpp"
#include "heinzel/support/fail.hpp"

#include "interpret.hpp"
#include "linked_sections.hpp"
#include "sections_in_lanes_graphs.hpp"

using namespace heinzel;
namespace ls = heinzel::testing::linked_sections;

namespace {

int g_wrong = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        ++g_wrong;
        std::printf("FAILED: %s\n", what);
    }
}

constexpr std::size_t kSamples = static_cast<std::size_t>(ls::kLength);
constexpr std::size_t kPlanes = 1024; // (the cascade's values hold about 600 numbers a sample)
constexpr Index n{};

// ---- the graphs ----------------------------------------------------------------------------------------------------

constexpr Graph tpt_pair() { return ls::tpt_pair(ls::Form::Linked, ls::kStream); }
constexpr Graph lr8() { return ls::lr8(ls::Form::Linked, ls::kStream); }
constexpr Graph peaks() { return ls::peaks(ls::Form::Linked, ls::kStream); }
constexpr Graph tanh_sections() { return ls::saturating_chain(ls::Form::Linked, ls::kStream); }
constexpr Graph in_series() { return ls::in_series(ls::Form::Linked, ls::kStream); }

// Section j's five Params, named as `lanes::named` names them.
constexpr std::array<std::array<const char*, 5>, 8> kNames{{
    {"b0_0", "b1_0", "b2_0", "a1_0", "a2_0"},
    {"b0_1", "b1_1", "b2_1", "a1_1", "a2_1"},
    {"b0_2", "b1_2", "b2_2", "a1_2", "a2_2"},
    {"b0_3", "b1_3", "b2_3", "a1_3", "a2_3"},
    {"b0_4", "b1_4", "b2_4", "a1_4", "a2_4"},
    {"b0_5", "b1_5", "b2_5", "a1_5", "a2_5"},
    {"b0_6", "b1_6", "b2_6", "a1_6", "a2_6"},
    {"b0_7", "b1_7", "b2_7", "a1_7", "a2_7"},
}};

// `lanes::df1_param` in f32, while compiling: section j, its coefficients its five Params.
constexpr Expr df1_section(Expr x, std::size_t j) {
    const std::array<const char*, 5>& name = kNames[j];
    Param b0{name[0]};
    Param b1{name[1]};
    Param b2{name[2]};
    Param a1{name[3]};
    Param a2{name[4]};
    Variable y;
    y[n] = b0 * x + b1 * x[n - 1] + b2 * x[n - 2] - a1 * y[n - 1] - a2 * y[n - 2];
    return y;
}

// `lanes::raw(8)` over a Length, merged by `sections_in_lanes` and given the rules after it, as `compile` plans it.
constexpr Graph param_cascade() {
    Input x{"x", Signal{Length{}}, f32};
    Expr s = x;
    for (std::size_t j = 0; j < kNames.size(); ++j) {
        s = df1_section(s, j);
    }
    Output y{"y"};
    y = s;
    const Graph written = lanes::analysed(y, numerics::Precision::Normal);
    const Graph merged = rewrite::sections_in_lanes(written, zen2);
    return rewrite::detail::after_the_form(merged, zen2);
}

// ---- what runs them ------------------------------------------------------------------------------------------------

// Noise in [-1, 1], each number a float (the cascade's x is one).
std::vector<double> noise() {
    std::vector<double> x(kSamples);
    std::uint32_t state = 20261025u;
    for (double& v : x) {
        state = state * 1664525u + 1013904223u;
        v = static_cast<float>(static_cast<double>(static_cast<std::int32_t>(state >> 8) % 2001 - 1000) * 0.001);
    }
    return x;
}

// What timeless input `name` holds: a peak's coefficient over the five sections (`b1`: `ls::kPeaks`' column), or a
// Param's number (`b1_3`: section 3's b1 of `lanes::rbj`), rounded to the float that holds it.
std::vector<double> numbers_of(std::string_view name) {
    constexpr std::array<std::string_view, 5> kWhat{"b0", "b1", "b2", "a1", "a2"};
    const std::size_t cut = name.find('_');
    const std::string_view what = name.substr(0, cut);
    const auto i = static_cast<std::size_t>(std::find(kWhat.begin(), kWhat.end(), what) - kWhat.begin());
    if (cut != std::string_view::npos) {
        const std::size_t j = std::stoul(std::string(name.substr(cut + 1)));
        const float held = static_cast<float>(lanes::rbj(j)[i]);
        return {static_cast<double>(held)};
    }
    std::vector<double> column{};
    for (const ls::Coefficients& section : ls::kPeaks) {
        column.push_back(section[i]);
    }
    return column;
}

// Graph `g`, its Length bound to kSamples, walked over x, each timeless input fed what it holds at every sample: y.
std::vector<double> walked(Graph g, const std::vector<double>& x) {
    Bindings sizes{};
    sizes.bind(Dim{kStreamLength}, static_cast<std::int32_t>(kSamples));
    if (const Diagnostic d = infer(g, sizes); !d.ok) {
        fail(d.message.view());
    }
    std::vector<std::vector<double>> fed{};
    for (const NamedValue& port : g.inputs) {
        const std::vector<double> numbers = port.name.view() == "x" ? x : numbers_of(port.name.view());
        std::vector<double> every = numbers;
        for (std::size_t s = 1; port.name.view() != "x" && s < kSamples; ++s) {
            every.insert(every.end(), numbers.begin(), numbers.end());
        }
        fed.push_back(every);
    }
    Inputs feeds{};
    for (std::size_t i = 0; i < g.inputs.size(); ++i) {
        const Value& in = g.values[static_cast<std::size_t>(g.inputs[i].value)];
        feeds.push_back(InputFeed{fed[i].data(), elements_of(in.shape).count});
    }
    const auto walk = std::make_unique<Signals<kSamples, kPlanes>>();
    run(g, feeds, *walk);
    std::vector<double> y(kSamples);
    for (std::size_t s = 0; s < kSamples; ++s) {
        y[s] = sample_at(*walk, g.outputs[0].value, 0, s);
    }
    return y;
}

// `Build`'s ports, in order: its graph (an `Executor` keeps none of its compile result; `compile` leaves them as they
// are).
template <auto Build>
inline constexpr Graph kPorts = Build();

template <ElemType T>
using Element = std::conditional_t<T == ElemType::F32, float, double>;

// The type of the numbers `G`'s input x holds.
template <const Graph& G>
constexpr ElemType kTypeOfX = [] {
    ElemType type = ElemType::Unknown;
    for (const NamedValue& port : G.inputs) {
        type = port.name.view() == "x" ? G.values[static_cast<std::size_t>(port.value)].type : type;
    }
    return type;
}();

// Each input of `ex` but x given what it holds: a Param set, a tensor pointed at `held[i]`.
template <class E, const Graph& G, std::size_t... I>
void bind_timeless(E& ex, std::vector<std::vector<double>>& held, std::index_sequence<I...>) {
    const auto bind = [&]<std::size_t K>() {
        static constexpr Name name = G.inputs[K].name;
        constexpr std::int32_t rank = G.values[static_cast<std::size_t>(G.inputs[K].value)].shape.rank;
        if constexpr (name.view() != "x") {
            held[K] = numbers_of(name.view());
            if constexpr (rank == 0) {
                ex.param(name.data_) = held[K][0];
            } else {
                ex.input(name.data_) = held[K].data();
            }
        }
    };
    (bind.template operator()<I>(), ...);
}

// `Build` on `Executor<…, zen2>` over x in blocks of `blocks`, cycled, its state reset once `reset_at` samples are
// done: y.
template <auto Build>
std::vector<double> executed(const std::vector<double>& x, std::initializer_list<std::int64_t> blocks, std::size_t reset_at) {
    using E = Executor<Build, zen2>;
    static constexpr const Graph& g = kPorts<Build>;
    using In = Element<kTypeOfX<g>>;
    using Out = Element<g.values[static_cast<std::size_t>(g.outputs[0].value)].type>;
    const auto ex = std::make_unique<E>();
    ex->prepare(std::max(blocks));
    std::vector<std::vector<double>> held(g.inputs.size());
    bind_timeless<E, g>(*ex, held, std::make_index_sequence<g.inputs.size()>{});
    const std::vector<In> in(x.begin(), x.end());
    std::vector<Out> y(x.size());
    std::size_t b = 0;
    for (std::size_t at = 0; at < x.size(); ++b) {
        if (at == reset_at) {
            ex->reset();
        }
        const std::size_t end = at < reset_at ? reset_at : x.size();
        const std::size_t block = static_cast<std::size_t>(blocks.begin()[b % blocks.size()]);
        const std::size_t m = std::min(block, end - at);
        ex->input("x") = in.data() + at;
        ex->output("y") = y.data() + at;
        ex->run(static_cast<std::int64_t>(m));
        at += m;
    }
    return std::vector<double>(y.begin(), y.end());
}

// Whether `ran` holds the walk's numbers from `from` on, bit for bit; the first that differs printed.
bool same_bits(const std::vector<double>& ran, const std::vector<double>& walk, std::size_t from = 0) {
    for (std::size_t i = 0; i < walk.size(); ++i) {
        if (std::bit_cast<std::uint64_t>(ran[from + i]) != std::bit_cast<std::uint64_t>(walk[i])) {
            std::printf("  sample %zu: ran %.17g, walked %.17g\n", i, ran[from + i], walk[i]);
            return false;
        }
    }
    return true;
}

// `Build` compiled for zen2 as `Executor` compiles it: the graph left as written, `skewed` stages of its program skewed.
template <auto Build>
bool compiled_as_written(std::size_t skewed) {
    const std::unique_ptr<const CompileResult> c(new auto(try_compile(Build(), zen2)));
    std::size_t count = 0;
    for (const loops::Stage& stage : c->compiled.program.stages) {
        count += stage.skewed ? 1 : 0;
    }
    return c->refusal.view().empty() && c->compiled.graph == Build() && count == skewed;
}

// `Build` of k sections (the most in one stage) on Executor against its walk, bit for bit: one run; blocks of 1, k − 1,
// k, 32 and 256; uneven blocks; and the noise twice over, the run reset between.
template <auto Build>
void runs_as_walked(const char* what, std::int64_t k, std::size_t skewed) {
    const auto whole = static_cast<std::int64_t>(kSamples);
    const std::vector<double> x = noise();
    const std::vector<double> want = walked(Build(), x);
    std::printf("%s\n", what);
    check(compiled_as_written<Build>(skewed), "compiled as written, skewed");
    check(same_bits(executed<Build>(x, {whole}, kSamples), want), "one run");
    for (const std::int64_t block : {std::int64_t{1}, k - 1, k, std::int64_t{32}, std::int64_t{256}}) {
        check(same_bits(executed<Build>(x, {block}, kSamples), want), "in blocks");
    }
    check(same_bits(executed<Build>(x, {5, 1, 13, 2, 64, 3, 30, 1, 9, 300}, kSamples), want), "in blocks of uneven lengths");
    std::vector<double> twice = x;
    twice.insert(twice.end(), x.begin(), x.end());
    const std::vector<double> again = executed<Build>(twice, {7, 256, 1}, kSamples);
    check(same_bits(again, want) && same_bits(again, want, kSamples), "after reset");
}

} // namespace

int main() {
    try {
        runs_as_walked<tpt_pair>("the TPT pair", 2, 1);
        runs_as_walked<lr8>("LR8", 4, 1);
        runs_as_walked<peaks>("the five peaks", 5, 1);
        runs_as_walked<tanh_sections>("the eight tanh sections", 8, 1);
        runs_as_walked<in_series>("the pair into the eight", 8, 2);
        runs_as_walked<param_cascade>("the K = 8 Param cascade", 8, 1);
    } catch (const heinzel::error& e) {
        std::printf("executor_skewed: %.*s\n", static_cast<int>(e.message().size()), e.message().data());
        return 1;
    }
    return g_wrong == 0 ? 0 : 1;
}
