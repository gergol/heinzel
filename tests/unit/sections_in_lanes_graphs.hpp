// The graphs of rewrite_sections_in_lanes_test (`docs/sections-in-lanes.md`): cascades as users write them,
// each `kLength` samples long, its input `x` and its output `y`; and, built by hand from `support/linked_sections.hpp`'s
// parts, the linked graphs at Normal and the priced merged Param cascade without double state's widening.
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#include "heinzel/frontend/declarations.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"
#include "heinzel/support/fail.hpp"

#include "hand_built_graph.hpp"
#include "linked_sections.hpp"

namespace lanes {

using namespace heinzel;
using numerics::Precision;
namespace ls = heinzel::testing::linked_sections;

inline constexpr std::int32_t kLength = ls::kLength;
inline constexpr Index n{};

constexpr Graph analysed(Graph g, Precision precision) {
    set_default_precision(g, precision);
    if (const Diagnostic d = infer(g); !d.ok) {
        fail(d.message.view());
    }
    return g;
}

// A Param's name: what it is and its section's number, `b0_3`.
inline std::string named(const char* what, std::size_t j) { return std::string(what) + "_" + std::to_string(j); }

// ---- sections as users write them ----------------------------------------------------------------------------------

// `df1_param`'s form (`benchmarks/iir_arms.hpp`): section j's coefficients its five Params, of `type`.
inline Expr df1_param(Expr x, std::size_t j, ElemType type) {
    Param b0{named("b0", j), type};
    Param b1{named("b1", j), type};
    Param b2{named("b2", j), type};
    Param a1{named("a1", j), type};
    Param a2{named("a2", j), type};
    Variable y;
    y[n] = b0 * x + b1 * x[n - 1] + b2 * x[n - 2] - a1 * y[n - 1] - a2 * y[n - 2];
    return y;
}

// `bench_knobs`' form: an RBJ peak, its coefficients worked out from its three knobs.
inline Expr knob_peak(Expr x, std::size_t j) {
    Param freq{named("freq", j), ge(20), le(20000)};
    Param gain{named("gain", j), ge(-24), le(24)};
    Param q{named("q", j), ge(0.1), le(10)};
    const Expr turns = freq * (2.0 / 48000.0);
    const Expr c = cospi(turns);
    const Expr s = sinpi(turns);
    const Expr big = exp(gain * (std::numbers::ln10 / 40.0));
    const Expr alpha = s / (2.0 * q);
    const Expr a0 = (1.0 + alpha / big).promise(gt(0));
    const Expr b0 = (1.0 + alpha * big) / a0;
    const Expr b1 = -2.0 * c / a0;
    const Expr b2 = (1.0 - alpha * big) / a0;
    const Expr a1 = -2.0 * c / a0;
    const Expr a2 = (1.0 - alpha / big) / a0;
    Variable y;
    y[n] = b0 * x + b1 * x[n - 1] + b2 * x[n - 2] - a1 * y[n - 1] - a2 * y[n - 2];
    return y;
}

// A one-pole, y = a x + p y[n - 1].
constexpr Expr one_pole(Expr x, Expr a, Expr p) {
    Variable y;
    y[n] = a * x + p * y[n - 1];
    return y;
}

// One with Params `a_j` and `p_j` of `type`.
inline Expr one_pole_param(Expr x, std::size_t j, ElemType type) {
    Param a{named("a", j), type};
    Param p{named("p", j), type};
    return one_pole(x, a, p);
}

// One whose numbers are constants, its pole 0.5 + j / 64.
constexpr Expr one_pole_constant(Expr x, std::size_t j) { return one_pole(x, constant(0.5), constant(0.5 + static_cast<double>(j) / 64.0)); }

// `k` sections in series over `x` (of `type`, `voices` of them where more than one, `length` samples long),
// `section(s, j)` the j-th.
template <class Section>
constexpr Graph series(std::size_t k, Section section, Precision precision = Precision::Normal, ElemType type = f32, std::int32_t voices = 1,
                       std::int32_t length = kLength) {
    Input x{"x", voices == 1 ? Signal{length} : Signal{voices, length}, type};
    Expr s = x;
    for (std::size_t j = 0; j < k; ++j) {
        s = section(s, j);
    }
    Output y{"y"};
    y = s;
    return lanes::analysed(y, precision);
}

inline Graph raw(std::size_t k, Precision precision = Precision::Normal, ElemType type = f32, std::int32_t length = kLength) {
    return lanes::series(k, [type](Expr s, std::size_t j) { return lanes::df1_param(s, j, type); }, precision, type, 1, length);
}
inline Graph knobs(std::size_t k) { return lanes::series(k, lanes::knob_peak); }

// Three DF1 sections whose coefficients mix: b0 one Param all read (`g`), b1 a Param each, b2 a constant each, a1 a
// Param each (`p_j`, a pole inside the unit circle with a2) but section 1's a constant, a2 one number in all, written in
// each.
inline Graph mixed() {
    Input x{"x", Signal{kLength}};
    Param g{"g"};
    Expr s = x;
    for (std::size_t j = 0; j < 3; ++j) {
        Param b1{named("b1", j)};
        Param a1{named("p", j)};
        const Expr b2 = constant(0.125 * static_cast<double>(j + 1));
        const Expr feedback = j == 1 ? constant(-0.5) : Expr{a1};
        Variable y;
        y[n] = g * s + b1 * s[n - 1] + b2 * s[n - 2] - feedback * y[n - 1] - 0.25 * y[n - 2];
        s = y;
    }
    Output y{"y"};
    y = s;
    return lanes::analysed(y, Precision::Normal);
}

// ---- what their Params hold ----------------------------------------------------------------------------------------

// RBJ's peak at 62.5 · 2^j Hz, +6 dB, Q 1, 48 kHz: b0, b1, b2, a1, a2.
inline std::array<double, 5> rbj(std::size_t j) {
    const double w = 2.0 * std::numbers::pi * 62.5 * static_cast<double>(1u << j) / 48000.0;
    const double big = std::pow(10.0, 6.0 / 40.0);
    const double alpha = std::sin(w) / 2.0;
    const double a0 = 1.0 + alpha / big;
    return {(1.0 + alpha * big) / a0, -2.0 * std::cos(w) / a0, (1.0 - alpha * big) / a0, -2.0 * std::cos(w) / a0, (1.0 - alpha / big) / a0};
}

// What a Param named `name` holds: its name says which (`b1_3`: section 3's b1).
inline double param_number(std::string_view name) {
    const std::size_t cut = name.find('_');
    const std::string_view what = name.substr(0, cut);
    const std::size_t j = cut == std::string_view::npos ? 0 : std::stoul(std::string(name.substr(cut + 1)));
    const double at = static_cast<double>(j);
    const std::array<std::string_view, 5> coefficients{"b0", "b1", "b2", "a1", "a2"};
    for (std::size_t i = 0; i < coefficients.size(); ++i) {
        if (what == coefficients[i]) {
            return lanes::rbj(j)[i];
        }
    }
    const std::array<std::string_view, 6> others{"g", "a", "p", "freq", "gain", "q"};
    const std::array<double, 6> numbers{0.75, 0.5, 0.25 + at / 64.0, 62.5 * static_cast<double>(1u << j), 6.0 - at, 1.0 + 0.25 * at};
    for (std::size_t i = 0; i < others.size(); ++i) {
        if (what == others[i]) {
            return numbers[i];
        }
    }
    fail("sections_in_lanes_graphs: a Param no test names");
    return 0;
}

// ---- runs that break, and cascades declined ------------------------------------------------------------------------

// Four Param DF1 sections, A to D, and a one-pole reading B's output, `e`: a run broken by a reader of a middle section.
inline Graph broken_in_the_middle() {
    Input x{"x", Signal{kLength}};
    const Expr a = lanes::df1_param(x, 0, f32);
    const Expr b = lanes::df1_param(a, 1, f32);
    const Expr c = lanes::df1_param(b, 2, f32);
    const Expr d = lanes::df1_param(c, 3, f32);
    const Expr e = lanes::one_pole(b, constant(0.5), constant(0.25));
    Output y{"y"};
    Output aside{"e"};
    y = d;
    aside = e;
    return lanes::analysed(finish(y, aside), Precision::Normal);
}

// `k` Param DF1 sections, section `middle`'s output an output of its own too.
inline Graph read_in_the_middle(std::size_t k, std::size_t middle) {
    Input x{"x", Signal{kLength}};
    Expr s = x;
    Output also{"middle"};
    for (std::size_t j = 0; j < k; ++j) {
        s = lanes::df1_param(s, j, f32);
        if (j == middle) {
            also = s;
        }
    }
    Output y{"y"};
    y = s;
    return lanes::analysed(finish(y, also), Precision::Normal);
}

// Three one-poles, the first's gain a second signal `w`: like the others but for that, it reads two outside values.
inline Graph first_reads_two() {
    Input x{"x", Signal{kLength}};
    Input w{"w", Signal{kLength}};
    const Expr first = lanes::one_pole(x, w, constant(0.25));
    const Expr second = lanes::one_pole_param(first, 1, f32);
    const Expr third = lanes::one_pole_param(second, 2, f32);
    Output y{"y"};
    y = third;
    return lanes::analysed(y, Precision::Normal);
}

// Three one-poles, each reading the one before a sample back only: y_j = 0.5 y_{j-1}[n - 1] + p_j y_j[n - 1].
inline Graph only_in_the_past() {
    Input x{"x", Signal{kLength}};
    Expr s = x;
    for (std::size_t j = 0; j < 3; ++j) {
        Variable y;
        y[n] = 0.5 * s[n - 1] + (0.25 + static_cast<double>(j) / 64.0) * y[n - 1];
        s = y;
    }
    Output y{"y"};
    y = s;
    return lanes::analysed(y, Precision::Normal);
}

// Two one-poles, the second reading its own past two samples back: one row each, but not one attribute.
inline Graph lags_differ() {
    Input x{"x", Signal{kLength}};
    const Expr first = lanes::one_pole(x, constant(0.5), constant(0.25));
    Variable second;
    second[n] = 0.5 * first + 0.25 * second[n - 2];
    Output y{"y"};
    y = second;
    return lanes::analysed(y, Precision::Normal);
}

// Three leaky sums, y = in + p y[n - 1], the first's `in` a constant: its input is no voice, so it stays, and the
// other two are a run of their own.
inline Graph driven_by_a_constant() {
    Variable first{Signal{kLength}};
    first[n] = constant(1.0) + 0.25 * first[n - 1];
    Variable second;
    second[n] = first + 0.375 * second[n - 1];
    Variable third;
    third[n] = second + 0.5 * third[n - 1];
    Output y{"y"};
    y = third;
    return lanes::analysed(y, Precision::Normal);
}

// Two leaky sums that pair node for node, yet are not alike: the first, reading nothing, adds its own
// `q = 0.5 v[n - 1] + 1` to itself; the second adds the first's value to its own `q`. Where the second reads the first's
// value, the first reads its `q`, which the pair makes: no run's input.
inline Expr own_value_pair() {
    Variable first{Signal{kLength}};
    const Expr first_past = first[n - 1];
    const Expr first_half = 0.5 * first_past;
    const Expr first_own = first_half + 1.0;
    first[n] = first_own + first_own;
    Variable second;
    const Expr second_past = second[n - 1];
    const Expr second_half = 0.5 * second_past;
    const Expr second_own = second_half + 1.0;
    second[n] = first + second_own;
    return second;
}

// That pair alone.
inline Graph own_value_read() {
    Output y{"y"};
    y = lanes::own_value_pair();
    return lanes::analysed(y, Precision::Normal);
}

// Three like constant one-poles over `x`, and beside them that pair.
inline Graph one_poles_beside_own_value() {
    Input x{"x", Signal{kLength}};
    Expr s = x;
    for (std::size_t j = 0; j < 3; ++j) {
        s = lanes::one_pole_constant(s, j);
    }
    Output y{"y"};
    Output z{"z"};
    y = s;
    z = lanes::own_value_pair();
    return lanes::analysed(finish(y, z), Precision::Normal);
}

// A TPT one-pole (`linked_sections.hpp`'s `tpt`): its output, and its state.
struct Stage {
    Expr y;
    Expr s;
};
inline Stage tpt_stage(Expr in, double gain) {
    Variable s;
    const Expr before = s[n - 1];
    const Expr error = in - before;
    const Expr v = gain * error;
    const Expr y = v + before;
    s[n] = y + v;
    return Stage{y, s};
}

// Three TPT one-poles, the second reading the first's output, the third the second's state, which is the output: the
// first two link at one place, the last two at another.
inline Graph read_at_other_places() {
    Input x{"x", Signal{kLength}};
    const Stage first = lanes::tpt_stage(x, 0.0625);
    const Stage second = lanes::tpt_stage(first.y, 0.125);
    const Stage third = lanes::tpt_stage(second.s, 0.25);
    Output y{"y"};
    y = third.s;
    return lanes::analysed(y, Precision::Normal);
}

// Three Param DF1 sections over a gain of `x`, which the first reads, now and in the past, as its input.
inline Graph over_a_gain() {
    Input x{"x", Signal{kLength}};
    Expr s = 0.5 * x;
    for (std::size_t j = 0; j < 3; ++j) {
        s = lanes::df1_param(s, j, f32);
    }
    Output y{"y"};
    y = s;
    return lanes::analysed(y, Precision::Normal);
}

// Two f32 one-poles whose poles are no floats, 0.3 and 0.7; `rounded`, the same with them rounded to float.
inline Graph poles_no_floats(bool rounded) {
    const auto section = [rounded](Expr s, std::size_t j) {
        const double pole = j == 0 ? 0.3 : 0.7;
        const double held = rounded ? static_cast<double>(static_cast<float>(pole)) : pole;
        return lanes::one_pole(s, constant(0.5), constant(held));
    };
    return lanes::series(2, section);
}

// eq8sweep's band (`benchmarks/eq8_graph.hpp`): a fixed DF1 section, then one whose b0 an LFO moves every sample.
inline Graph moving_band() {
    Input x{"x", Signal{kLength}};
    Variable phase{Signal{kLength}};
    phase[n] = frac(phase[n - 1] + 0.001);
    const Expr wave = sinpi(2.0 * phase);
    const Expr moving = 0.5 + 0.25 * wave;
    const Expr fixed = lanes::one_pole(x, constant(0.5), constant(0.25));
    const Expr band = lanes::one_pole(fixed, moving, constant(0.25));
    Output y{"y"};
    y = band;
    return lanes::analysed(y, Precision::Normal);
}

// eq8fb's shape (`benchmarks/eq8_graph.hpp`): the output fed back through a clip into the cascade's input.
inline Graph inside_a_loop() {
    Input x{"x", Signal{kLength}};
    Variable y;
    const Expr clipped = clamp(y[n - 1], -0.25, 0.25);
    const Expr in = x - 0.6 * clipped;
    const Expr first = lanes::df1_param(in, 0, f32);
    y[n] = lanes::df1_param(first, 1, f32);
    Output out{"y"};
    out = y;
    return lanes::analysed(out, Precision::Normal);
}

// JUCE's ladder's shape (`benchmarks/bench_compare_juce_heinzel_filters.cpp`): s0 reads s4[n - 1], s2 read in the middle.
inline Graph ladder() {
    Input x{"x", Signal{kLength}};
    Variable s0;
    Variable s1;
    Variable s2;
    Variable s3;
    Variable s4;
    const Expr dx = 0.5 * tanh(1.25 * x);
    const Expr back = tanh(s4[n - 1]);
    s0[n] = dx + 0.75 * (back - 0.125 * dx);
    s1[n] = 0.25 * s0[n - 1] + 0.5 * s1[n - 1] + 0.25 * s0;
    s2[n] = 0.25 * s1[n - 1] + 0.5 * s2[n - 1] + 0.25 * s1;
    s3[n] = 0.25 * s2[n - 1] + 0.5 * s3[n - 1] + 0.25 * s2;
    s4[n] = 0.25 * s3[n - 1] + 0.5 * s4[n - 1] + 0.25 * s3;
    Output y{"y"};
    y = 0.875 * s2;
    return lanes::analysed(y, Precision::Normal);
}

// Sections whose range follows their input's: y = min(x, 0.5 y[n - 1]), `x` in [-1, 1].
inline Graph minimums(std::size_t k) {
    Input x{"x", Signal{kLength}, ge(-1), le(1)};
    Expr s = x;
    for (std::size_t j = 0; j < k; ++j) {
        Variable y;
        y[n] = min(s, 0.5 * y[n - 1]);
        s = y;
    }
    Output y{"y"};
    y = s;
    return lanes::analysed(y, Precision::Normal);
}

// Three saturating one-poles, y = tanh(0.5 x + p_j y[n - 1]), over `x` in [-1, 1], and beside them an output reading
// `x` and a Param in [0.1, 10]: its range, [-10, 10], is the marks', which the rewrite carries.
inline Graph saturating_beside_a_knob() {
    Input x{"x", Signal{kLength}, ge(-1), le(1)};
    Param q{"q_0", ge(0.1), le(10)};
    Expr s = x;
    for (std::size_t j = 0; j < 3; ++j) {
        Variable y;
        y[n] = tanh(0.5 * s + (0.25 + static_cast<double>(j) / 64.0) * y[n - 1]);
        s = y;
    }
    Output y{"y"};
    Output level{"level"};
    y = s;
    level = q * x;
    return lanes::analysed(finish(y, level), Precision::Normal);
}

// Three sections reading one table (JUCE's table ladder's saturator), y = lookup(tanh, 0.5 x) + lookup(tanh, p_j
// y[n - 1]): a table read in each section's cycle and one in the work feeding it, the table one value all read.
inline Graph saturating_through_a_table() {
    std::array<double, 65> points{};
    for (std::size_t i = 0; i < points.size(); ++i) {
        points[i] = std::tanh(-4.0 + 8.0 * static_cast<double>(i) / 64.0);
    }
    const Expr table = constant(points);
    Input x{"x", Signal{kLength}};
    Expr s = x;
    for (std::size_t j = 0; j < 3; ++j) {
        Variable y;
        const Expr fed = lookup(table, 0.5 * s, -4.0, 4.0);
        const Expr kept = lookup(table, (0.25 + static_cast<double>(j) / 64.0) * y[n - 1], -4.0, 4.0);
        y[n] = fed + kept;
        s = y;
    }
    Output y{"y"};
    y = s;
    return lanes::analysed(y, Precision::Normal);
}

// Two Param one-poles (their merge one node more than they), beside `pads` maps of a second input `w`.
inline Graph padded_one_poles(std::size_t pads) {
    Input x{"x", Signal{kLength}};
    Input w{"w", Signal{kLength}};
    const Expr first = lanes::one_pole_param(x, 0, f32);
    const Expr second = lanes::one_pole_param(first, 1, f32);
    Expr pad = w;
    for (std::size_t i = 0; i < pads; ++i) {
        pad = -pad;
    }
    Output y{"y"};
    Output z{"z"};
    y = second;
    z = pad;
    return lanes::analysed(finish(y, z), Precision::Normal);
}

// Analysed graph `g` beside a constant tensor of `numbers` numbers no node reads, appended to it.
inline Graph with_a_pool(Graph g, std::size_t numbers) {
    OpAttr pool{};
    pool.kind = AttrKind::Constant;
    pool.generator = GeneratorAttr{Shape{static_cast<std::int32_t>(numbers), 1}, ElemType::F64, static_cast<std::int32_t>(g.constants.size())};
    for (std::size_t i = 0; i < numbers; ++i) {
        push_constant(g, static_cast<double>(i));
    }
    const std::int32_t held = add_node(g, op::Constant, kNoValue, kNoValue, kNoValue, pool);
    Value& value = g.values[static_cast<std::size_t>(held)];
    value.type = ElemType::F64;
    value.shape = pool.generator.shape;
    value.precision = Precision::Normal;
    value.timeless = true;
    return g;
}

// Two constant one-poles (their merge a [2, 1] constant of their poles), beside a constant tensor of `numbers` numbers.
inline Graph pooled_one_poles(std::size_t numbers) {
    const Graph cascade = lanes::series(2, lanes::one_pole_constant);
    return lanes::with_a_pool(cascade, numbers);
}

// Two runs of two constant one-poles, over `x` and over `w`, each merge adding two numbers to the pool, beside a
// constant tensor of `numbers` numbers.
inline Graph pooled_runs(std::size_t numbers) {
    Input x{"x", Signal{kLength}};
    Input w{"w", Signal{kLength}};
    const Expr first = lanes::one_pole_constant(x, 0);
    const Expr second = lanes::one_pole_constant(first, 1);
    const Expr other_first = lanes::one_pole_constant(w, 0);
    const Expr other_second = lanes::one_pole_constant(other_first, 1);
    Output y{"y"};
    Output z{"z"};
    y = second;
    z = other_second;
    const Graph runs = lanes::analysed(finish(y, z), Precision::Normal);
    return lanes::with_a_pool(runs, numbers);
}

// ---- the linked and the priced forms, by hand --------------------------------------------------------------------

// `ls::graph_of` at `precision`: `x` and `y`, what `body` makes of `x`.
template <class Body>
Graph by_hand(Body body, Precision precision = Precision::Normal, ElemType type = ElemType::F64) {
    Graph g{};
    const auto x = add_value(g, type, Shape{kLength}, ValueCategory::Input);
    g.inputs.push_back(NamedValue{Name{"x"}, x});
    const auto y = body(g, x);
    g.outputs.push_back(NamedValue{Name{"y"}, y});
    return lanes::analysed(g, precision);
}

// Section j's five f32 Params, each `<coefficient>_j`, after what `g` holds.
inline ls::Biquad params_of(Graph& g, std::size_t j) {
    const char* names[5] = {"b0", "b1", "b2", "a1", "a2"};
    ls::Biquad c{};
    for (std::size_t i = 0; i < c.size(); ++i) {
        c[i] = add_value(g, ElemType::F32, Shape{}, ValueCategory::Input);
        g.values[static_cast<std::size_t>(c[i])].timeless = true;
        g.inputs.push_back(NamedValue{heinzel::detail::name_of(lanes::named(names[i], j)), c[i]});
    }
    return c;
}

// The lanes' probe's Param cascade of `k` RBJ biquads in f32, as `ls::biquad` writes a section: each its own cycle.
inline Graph params_apart(std::size_t k) {
    return lanes::by_hand(
        [k](Graph& g, std::int32_t x) {
            std::int32_t through = x;
            for (std::size_t j = 0; j < k; ++j) {
                const ls::Biquad c = lanes::params_of(g, j);
                const auto state = add_value(g, ElemType::F32, Shape{kLength}, ValueCategory::Variable);
                through = ls::biquad(g, through, c, state);
            }
            return through;
        },
        Precision::Normal, ElemType::F32);
}

// The same merged as `linked_price_test.cpp` builds it, without double state's widening: each coefficient p_0, then
// where(e_j, p_j, ·) for j = 1 ... k - 1, the one-hot masks shared; the link; one section's arithmetic over [k, time];
// section k - 1 sliced out.
inline Graph params_linked(std::size_t k) {
    return lanes::by_hand(
        [k](Graph& g, std::int32_t x) {
            std::vector<ls::Biquad> params{};
            for (std::size_t j = 0; j < k; ++j) {
                params.push_back(lanes::params_of(g, j));
            }
            std::vector<std::int32_t> masks{};
            for (std::size_t j = 1; j < k; ++j) {
                std::vector<double> one_hot(k, 0.0);
                one_hot[j] = 1.0;
                masks.push_back(ls::per_section(g, one_hot));
            }
            ls::Biquad c{};
            for (std::size_t i = 0; i < c.size(); ++i) {
                c[i] = params[0][i];
                for (std::size_t j = 1; j < k; ++j) {
                    c[i] = add_node(g, op::Select, masks[j - 1], params[j][i], c[i]);
                }
            }
            const auto outputs = add_value(g, ElemType::F32, Shape{static_cast<std::int32_t>(k), kLength}, ValueCategory::Variable);
            const auto u = add_node(g, op::Chain, x, outputs);
            ls::biquad(g, u, c, outputs);
            return ls::section_of(g, outputs, static_cast<std::int32_t>(k) - 1, Shape{kLength});
        },
        Precision::Normal, ElemType::F32);
}

} // namespace lanes
