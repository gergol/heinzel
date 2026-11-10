// Sections in lanes only where the skewed stage holds all the past it reads in
// registers, since it reads nothing along time from memory but at one lane (`loops/validate.hpp` `check_links`). The
// rule declines a run whose sections read a value's past past a gap in its run of lags (`rewrite/like_sections.hpp`
// `past_in_registers`), and the planner cuts no run of a skewed stage's lags (`loops/planner/chains.hpp`
// `state_depth`). Before, the rule took both, the lowering refused the skewed stage, and `compile` planned the graph as
// written, every rule lost (Normal's double state among them). A link opens no slot, so it fills no gap: before, the
// lowering held y[n-2] beside the link's read and lost its slot between runs (`loops/implied.hpp` `find_state`).
// A lag of a lag is held only where the inner lag is the cycle's own state, which stands before its definition;
// one of a value made before it moves before its maker and leaves the outer lag behind, read from memory
// (`loops/planner/chains.hpp` `lags_before_their_makers`). Before, the rule took the three, and the lowering refused.
//
//   bpf4      four RBJ band-passes, y = b0 x - b0 x[n-2] - a1 y[n-1] - a2 y[n-2]         declined
//   lag d     eight y = a s[n-d] + 0.5 s + p y[n-1], d = 2, 3, 9, 64                     declined
//   third8    eight third-order sections, lags 1-3 of s and y: the depth was cut at two  in lanes
//   older8    eight y = a s + b s[n-1] - q y[n-2]: y's lag 2 alone, no rule changes it   declined
//   input8    eight s1 = s[n-1], y = a s + b s1 + a s1[n-1] - p y[n-1]                   declined
//   feed8     eight t = a s, t1 = t[n-1], y = t + b t1 + a t1[n-1] - p y[n-1]            declined
//   three8    eight y1 = y[n-1], y2 = y1[n-1], y = a s - p y1 - q y2 + b y2[n-1]         declined
//   two8      eight y1 = y[n-1], y = a s - p y1 - q y1[n-1]: the cycle's own state       in lanes
//
// Their coefficients are Params. Each through `compile()` (release, zen2, Normal) plans `rewrite::rewritten`'s graph,
// not the graph as written (older8's the same), skewed where the lanes are taken. On `Executor<…, zen2>`, in blocks of
// 256 over 4096 samples of noise, each is bit for bit the walk of its compiled graph and within its bound of the
// cascade in double (the walk of the graph written in f64): the rules' error (bpf4 1.4e-7; as written, before the rule,
// 1.9e-6; the others 4-6e-8, as written 1.5-2.9e-7). older8 linked by hand, as the rule linked it before, the lowering
// refuses by name; before, it ran it 0.094 from the cascade in blocks of 256, bit for bit its walk in one run.

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "heinzel/compile.hpp"
#include "heinzel/executor.hpp"
#include "heinzel/frontend/declarations.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/planner/plan.hpp"
#include "heinzel/rewrite/rewritten.hpp"
#include "heinzel/rewrite/sections_in_lanes.hpp"
#include "heinzel/schedule/machine.hpp"
#include "heinzel/sema/analyze.hpp"

#include "interpret.hpp"
#include "linked_sections.hpp"
#include "param_arms_graphs.hpp"

using namespace heinzel;

namespace {

int g_wrong = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++g_wrong;
        std::printf("FAILED: %s\n", what.c_str());
    }
}

constexpr std::size_t kSamples = 4096;
constexpr std::size_t kPlanes = 1024;
constexpr std::size_t kBlock = 256;
constexpr Index n{};

// ---- the graphs ----------------------------------------------------------------------------------------------------

// Section j's Params.
constexpr std::array<std::array<const char*, 4>, 8> kNames{{
    {"a0", "b0", "p0", "q0"},
    {"a1", "b1", "p1", "q1"},
    {"a2", "b2", "p2", "q2"},
    {"a3", "b3", "p3", "q3"},
    {"a4", "b4", "p4", "q4"},
    {"a5", "b5", "p5", "q5"},
    {"a6", "b6", "p6", "q6"},
    {"a7", "b7", "p7", "q7"},
}};

enum class Form : std::uint8_t { BandPass, Lagged, Third, Older, InputLagLag, FeedLagLag, StateThree, StateTwo };

// `K` sections of `F` in series over x of `T`, `Samples` long (0: a Length); section j's Params a_j, b_j, p_j, q_j, as
// many as its form reads; `D` the lag of `Form::Lagged`.
template <Form F, std::size_t K, std::int32_t D = 0, std::int32_t Samples = 0, ElemType T = f32>
constexpr Graph sections() {
    Input x{"x", Samples == 0 ? Signal{Length{}} : Signal{Samples}, T};
    Expr s = x;
    for (std::size_t j = 0; j < K; ++j) {
        Param a{kNames[j][0], T};
        Param b{kNames[j][1], T};
        Param p{kNames[j][2], T};
        Param q{kNames[j][3], T};
        Variable y;
        if constexpr (F == Form::BandPass) {
            y[n] = a * s - a * s[n - 2] - p * y[n - 1] - q * y[n - 2];
        } else if constexpr (F == Form::Lagged) {
            y[n] = a * s[n - D] + 0.5 * s + p * y[n - 1];
        } else if constexpr (F == Form::Third) {
            y[n] = a * s + b * s[n - 1] + a * s[n - 2] + b * s[n - 3] - p * y[n - 1] - q * y[n - 2] - p * y[n - 3];
        } else if constexpr (F == Form::Older) {
            y[n] = a * s + b * s[n - 1] - q * y[n - 2];
        } else if constexpr (F == Form::InputLagLag) {
            const Expr s1 = s[n - 1];
            y[n] = a * s + b * s1 + a * s1[n - 1] - p * y[n - 1];
        } else if constexpr (F == Form::FeedLagLag) {
            const Expr t = a * s;
            const Expr t1 = t[n - 1];
            y[n] = t + b * t1 + a * t1[n - 1] - p * y[n - 1];
        } else if constexpr (F == Form::StateThree) {
            const Expr y1 = y[n - 1];
            const Expr y2 = y1[n - 1];
            y[n] = a * s - p * y1 - q * y2 + b * y2[n - 1];
        } else {
            const Expr y1 = y[n - 1];
            y[n] = a * s - p * y1 - q * y1[n - 1];
        }
        s = y;
    }
    return finish(Output{"y"} = s);
}

// What Param `name` (`p3`: section 3's p) holds, as the float a Param holds: RBJ's band-pass at 900 + 100 j Hz (a its
// b0, p its a1, q its a2); the lagged sections' gains and poles; the third-order and older sections', the same in each,
// and the lags of lags', the same in each, near unit gain.
double held(Form form, std::string_view name) {
    const auto j = static_cast<double>(name[1] - '0');
    const char what = name[0];
    double v = 0;
    if (form == Form::BandPass) {
        const double w = 2.0 * std::numbers::pi * (900.0 + 100.0 * j) / 48000.0;
        const double alpha = std::sin(w) / 2.0;
        const double a0 = 1.0 + alpha;
        v = what == 'a' ? alpha / a0 : what == 'p' ? -2.0 * std::cos(w) / a0 : (1.0 - alpha) / a0;
    } else if (form == Form::Lagged) {
        v = what == 'a' ? 0.3 + 0.05 * j : 0.6 + 0.04 * j;
    } else if (form >= Form::InputLagLag) {
        v = what == 'a' ? 0.25 : what == 'b' ? 0.25 : what == 'p' ? -0.25 : 0.1;
    } else {
        v = what == 'a' ? 0.2 : what == 'b' ? 0.1 : what == 'p' ? 0.1 : 0.05;
    }
    return static_cast<double>(static_cast<float>(v));
}

// ---- what runs them ------------------------------------------------------------------------------------------------

// Analysed graph `g`, `kSamples` long, walked over x, each Param fed what it holds: y.
std::vector<double> walked(const Graph& g, Form form, const std::vector<double>& x) {
    std::vector<std::vector<double>> fed{};
    for (const NamedValue& port : g.inputs) {
        const std::string_view name = port.name.view();
        fed.push_back(name == "x" ? x : std::vector<double>(kSamples, held(form, name)));
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

// `Build` on a release `Executor<…, zen2>`, its Params set to what they hold, over x in blocks of `kBlock`: y.
template <auto Build>
std::vector<double> executed(Form form, const std::vector<double>& x) {
    using E = Executor<Build, zen2, false>;
    const auto ex = std::make_unique<E>();
    ex->prepare(static_cast<std::int64_t>(kBlock));
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        const auto set = [&]<std::size_t K>() {
            static constexpr Name name = kPorts<Build>.inputs[K].name;
            if constexpr (name.view() != "x") {
                ex->param(name.data_) = held(form, name.view());
            }
        };
        (set.template operator()<I>(), ...);
    }(std::make_index_sequence<kPorts<Build>.inputs.size()>{});
    const std::vector<float> in(x.begin(), x.end());
    std::vector<float> y(x.size());
    for (std::size_t at = 0; at < x.size(); at += kBlock) {
        ex->input("x") = in.data() + at;
        ex->output("y") = y.data() + at;
        ex->run(static_cast<std::int64_t>(kBlock));
    }
    return std::vector<double>(y.begin(), y.end());
}

// ---- the checks ----------------------------------------------------------------------------------------------------

// `Build` (over a Length) through `compile()`: the rule takes its run or declines it (`lanes`), and the program is that
// of the graph rewritten, not as written where the rules change it (`rules`), holding `lanes ? 1 : 0` skewed stages.
template <auto Build>
[[gnu::noinline]] void compiled(const std::string& what, bool lanes, bool rules) {
    const auto written = std::make_unique<Graph>(Build());
    (void)infer(*written);
    const auto linked = std::make_unique<Graph>(rewrite::detail::linked_runs(*written, zen2));
    const auto rewritten = std::make_unique<Graph>(rewrite::rewritten(*written, zen2));
    const std::unique_ptr<const CompileResult> c(new auto(try_compile(Build(), zen2, Bindings{}, false)));
    std::size_t skewed = 0;
    for (const loops::Stage& stage : c->compiled.program.stages) {
        skewed += stage.skewed ? 1 : 0;
    }
    const bool taken = !(*linked == *written);
    const bool as_rewritten = c->compiled.graph == *rewritten && (*rewritten == *written) != rules;
    check(taken == lanes, what + (lanes ? ": taken" : ": declined"));
    check(c->refusal.view().empty(), what + ": compiled");
    check(as_rewritten, what + ": the graph rewritten planned, not as written");
    check(skewed == (lanes ? 1u : 0u), what + ": its skewed stages");
}

// `Build` as `compiled` takes it, then on its Executor bit for bit the walk of its compiled graph (its Length bound)
// and within `bound` of `Wide`, the cascade in double. No rule changes older8, whose past is not held.
template <auto Build, auto Wide>
[[gnu::noinline]] void ran(const char* what, Form form, bool lanes, double bound) {
    compiled<Build>(what, lanes, form != Form::Older);
    Bindings sizes{};
    sizes.bind(Dim{kStreamLength}, static_cast<std::int32_t>(kSamples));
    const std::unique_ptr<const CompileResult> whole(new auto(try_compile(Build(), zen2, sizes, false)));
    const auto wide = std::make_unique<Graph>(Wide());
    (void)infer(*wide);
    const std::vector<double> x = param_arms::noise(kSamples);
    const std::vector<double> got = executed<Build>(form, x);
    const std::vector<double> walk = walked(whole->compiled.graph, form, x);
    const std::vector<double> want = walked(*wide, form, x);
    const double off = param_arms::off_by(got, want);
    const auto same = [](double a, double b) { return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b); };
    std::printf("%s: %.3g from the cascade in double\n", what, off);
    check(std::equal(got.begin(), got.end(), walk.begin(), same), std::string(what) + ": bit for bit the walk of its compiled graph");
    check(off <= bound, std::string(what) + ": within the rules' bound of the cascade in double");
}

// older8 linked as the rule linked it before (`support/linked_sections.hpp`'s parts, at High, `kLength` samples):
// u = _chain(x, Y), Y = 0.2 u + 0.1 u[n-1] - 0.05 Y[n-2] over the eight sections, the last sliced out.
[[gnu::noinline]] void older_linked_refused() {
    namespace ls = heinzel::testing::linked_sections;
    const auto g = std::make_unique<Graph>();
    const auto x = add_value(*g, ElemType::F64, Shape{ls::kLength}, ValueCategory::Input);
    g->inputs.push_back(NamedValue{Name{"x"}, x});
    const Shape time{ls::time_of(*g)};
    const auto outputs = ls::variable(*g, Shape{8, ls::time_of(*g)});
    const auto u = add_node(*g, op::Chain, x, outputs);
    const auto before = ls::lag(*g, u, 1);
    const auto older = ls::lag(*g, outputs, 2);
    const auto a = ls::scalar(*g, 0.2);
    const auto b = ls::scalar(*g, 0.1);
    const auto q = ls::scalar(*g, 0.05);
    const auto fed = add_node(*g, op::Mul, a, u);
    const auto fed1 = add_node(*g, op::Mul, b, before);
    const auto back = add_node(*g, op::Mul, q, older);
    const auto sum = add_node(*g, op::Add, fed, fed1);
    const auto next = add_node(*g, op::Sub, sum, back);
    ls::settle(*g, next, outputs);
    const auto y = ls::section_of(*g, outputs, 7, time);
    g->outputs.push_back(NamedValue{Name{"y"}, y});
    set_default_precision(*g, numerics::Precision::High);
    const Diagnostic analysed = infer(*g);
    const auto planned = std::make_unique<std::expected<loops::Planned, loops::Refusal>>();
    loops::plan_and_lower_into(*planned, *g, zen2);
    const std::string_view why = *planned ? std::string_view{} : planned->error().view();
    check(analysed.ok && why == "heinzel::compile: a skewed stage reads and stores what moves along time at one lane only",
          "older8 linked by hand: the lowering refuses its skewed stage by name");
}

template <Form F, std::size_t K, std::int32_t D = 0>
constexpr Graph narrow() {
    return sections<F, K, D>();
}
template <Form F, std::size_t K, std::int32_t D = 0>
constexpr Graph wide() {
    return sections<F, K, D, static_cast<std::int32_t>(kSamples), f64>();
}

} // namespace

int main() {
    try {
        ran<narrow<Form::BandPass, 4>, wide<Form::BandPass, 4>>("bpf4", Form::BandPass, false, 5e-7);
        ran<narrow<Form::Lagged, 8, 2>, wide<Form::Lagged, 8, 2>>("lag 2", Form::Lagged, false, 1.2e-7);
        ran<narrow<Form::Lagged, 8, 3>, wide<Form::Lagged, 8, 3>>("lag 3", Form::Lagged, false, 1.2e-7);
        ran<narrow<Form::Lagged, 8, 9>, wide<Form::Lagged, 8, 9>>("lag 9", Form::Lagged, false, 1.2e-7);
        ran<narrow<Form::Lagged, 8, 64>, wide<Form::Lagged, 8, 64>>("lag 64", Form::Lagged, false, 1.2e-7);
        ran<narrow<Form::Third, 8>, wide<Form::Third, 8>>("third8", Form::Third, true, 1.2e-7);
        ran<narrow<Form::Older, 8>, wide<Form::Older, 8>>("older8", Form::Older, false, 5e-7);
        ran<narrow<Form::InputLagLag, 8>, wide<Form::InputLagLag, 8>>("input8", Form::InputLagLag, false, 1.2e-7);
        ran<narrow<Form::FeedLagLag, 8>, wide<Form::FeedLagLag, 8>>("feed8", Form::FeedLagLag, false, 1.2e-7);
        ran<narrow<Form::StateThree, 8>, wide<Form::StateThree, 8>>("three8", Form::StateThree, false, 1.2e-7);
        ran<narrow<Form::StateTwo, 8>, wide<Form::StateTwo, 8>>("two8", Form::StateTwo, true, 1.2e-7);
        older_linked_refused();
    } catch (const heinzel::error& e) {
        std::printf("lanes_past_held: %.*s\n", static_cast<int>(e.message().size()), e.message().data());
        return 1;
    }
    if (g_wrong != 0) {
        std::printf("lanes_past_held_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("lanes_past_held_test: sections in lanes only where their past is held in registers; each compiled as rewritten\n");
    return 0;
}
