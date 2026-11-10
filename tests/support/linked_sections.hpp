#pragma once

// The linked graphs (`docs/sections-in-lanes.md`): sections linked in lanes, each beside its k-cycle graph,
// the same sections written one after the other, each a cycle of its own reading the one before at this sample, with
// no link. The two forms are built apart; what they share is one section's arithmetic, which both are made of. Each
// graph has the input `x` (the peaks also their five coefficient inputs) and the output `y`, is `kLength` samples
// long or a `Length` (to run in blocks), and is analysed at `High`.
//   - a TPT pair (k = 2), its gains a constant over [2, 1];
//   - LR8 as four RBJ biquads (k = 4, the poles repeated), its coefficients constants over [4, 1];
//   - five RBJ peaks whose coefficients are [5, 1] tensor inputs;
//   - eight tanh(... + 0.1) sections (k = 8);
//   - the TPT pair into the eight: two links in series.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "heinzel/frontend/declarations.hpp"
#include "heinzel/ir/ir.hpp"
#include "heinzel/sema/analyze.hpp"
#include "heinzel/support/fail.hpp"

#include "hand_built_graph.hpp"

namespace heinzel::testing::linked_sections {

inline constexpr std::int32_t kLength = 4096;

// A biquad's coefficients b0, b1, b2, a1, a2: as numbers, and as the values that hold them.
using Coefficients = std::array<double, 5>;
using Biquad = std::array<std::int32_t, 5>;

// TPT gains G = g / (1 + g), g = tan(pi fc / 48 kHz), at 1 kHz and 5 kHz.
inline constexpr std::array<double, 2> kTptGains{0.061511768503621556, 0.25342728698434797};

// LR8 at 2 kHz, 48 kHz: a 4th-order Butterworth twice, so its two RBJ low-passes (Q 0.5412 and 1.3066) repeat.
inline constexpr std::array<Coefficients, 4> kLr8{{
    {0.013749370199047557, 0.027498740398095113, 0.013749370199047557, -1.5590543011416917, 0.614051781937882},
    {0.01550170836114682, 0.03100341672229364, 0.01550170836114682, -1.7577536094827266, 0.8197604429273138},
    {0.013749370199047557, 0.027498740398095113, 0.013749370199047557, -1.5590543011416917, 0.614051781937882},
    {0.01550170836114682, 0.03100341672229364, 0.01550170836114682, -1.7577536094827266, 0.8197604429273138},
}};

// Five RBJ peaks at 48 kHz: 100 Hz +6 dB Q 0.7, 400 Hz -4 dB Q 1, 1.6 kHz +5 dB Q 1.4, 4 kHz -6 dB Q 2, 10 kHz +3 dB
// Q 0.9.
inline constexpr std::array<Coefficients, 5> kPeaks{{
    {1.006544411378863, -1.986678653173975, 0.9803044599580008, -1.986678653173975, 0.9868488713368639},
    {0.9882301709814868, -1.9335607479135402, 0.9479840912861899, -1.9335607479135402, 0.9362142622676766},
    {1.0410509284527867, -1.8531089507302831, 0.8534575785865213, -1.8531089507302831, 0.8945085070393076},
    {0.925143272268684, -1.4721223044116283, 0.7747171451955277, -1.4721223044116283, 0.6998604174642116},
    {1.1283256601771918, -0.35661941317973866, 0.24954594520825513, -0.35661941317973866, 0.377871605385447},
}};
inline constexpr std::array<Name, 5> kPeakInputs{Name{"b0"}, Name{"b1"}, Name{"b2"}, Name{"a1"}, Name{"a2"}};

// The tanh sections' feedback, one a section.
inline constexpr std::array<double, 8> kFeedback{0.30, 0.35, 0.40, 0.45, 0.50, 0.55, 0.60, 0.65};

// ---- the parts -----------------------------------------------------------------------------------------------------

// How long every value of a graph here is: as its input `x`, the first value it holds (`graph_of`).
constexpr Extent time_of(const Graph& g) {
    const Shape& x = g.values[0].shape;
    return extent_at(x, x.rank - 1);
}

// One number at every sample.
constexpr std::int32_t scalar(Graph& g, double number) {
    const auto v = add_value(g, ElemType::F64, Shape{time_of(g)}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(v)].constant = number;
    return v;
}

// One number a section, over [k, 1]: a constant tensor.
constexpr std::int32_t per_section(Graph& g, std::span<const double> numbers) {
    OpAttr pooled{};
    pooled.kind = AttrKind::Constant;
    pooled.generator.shape = Shape{static_cast<std::int32_t>(numbers.size()), 1};
    pooled.generator.type = ElemType::F64;
    pooled.generator.first = static_cast<std::int32_t>(g.constants.size());
    for (const double number : numbers) {
        push_constant(g, number);
    }
    const auto v = add_node(g, op::Constant, kNoValue, kNoValue, kNoValue, pooled);
    g.values[static_cast<std::size_t>(v)].timeless = true;
    return v;
}

constexpr std::int32_t variable(Graph& g, Shape shape) {
    return add_value(g, ElemType::F64, shape, ValueCategory::Variable);
}

constexpr std::int32_t lag(Graph& g, std::int32_t v, std::int32_t samples) {
    OpAttr back{};
    back.kind = AttrKind::Delay;
    back.delay.samples = samples;
    return add_node(g, op::FixedDelay, v, kNoValue, kNoValue, back);
}

// The recurrence that defines `v` as `next`.
constexpr std::int32_t settle(Graph& g, std::int32_t next, std::int32_t v) {
    OpAttr defines{};
    defines.kind = AttrKind::Recurrence;
    defines.recurrence.defines = v;
    return add_node(g, op::Recurrence, next, kNoValue, kNoValue, defines);
}

// Element j of `v` along axis 0, as a value of `shape`.
constexpr std::int32_t section_of(Graph& g, std::int32_t v, std::int32_t j, Shape shape) {
    OpAttr cut{};
    cut.kind = AttrKind::View;
    cut.view.kind = ViewKind::Slice;
    cut.view.axis = 0;
    cut.view.offset = j;
    cut.view.length = 1;
    cut.view.step = 1;
    const auto sliced = add_node(g, op::Slice, v, kNoValue, kNoValue, cut);
    OpAttr flatten{};
    flatten.kind = AttrKind::View;
    flatten.view.kind = ViewKind::Reshape;
    flatten.view.shape = shape;
    return add_node(g, op::Reshape, sliced, kNoValue, kNoValue, flatten);
}

// ---- one section's arithmetic, over one section or over all k at once ----------------------------------------------

// A TPT one-pole low-pass: v = G (in - s[n-1]), y = v + s[n-1], s = y + v. Defines `state`; returns y.
constexpr std::int32_t tpt(Graph& g, std::int32_t in, std::int32_t gain, std::int32_t state) {
    const auto before = lag(g, state, 1);
    const auto error = add_node(g, op::Sub, in, before);
    const auto v = add_node(g, op::Mul, gain, error);
    const auto y = add_node(g, op::Add, v, before);
    const auto next = add_node(g, op::Add, y, v);
    settle(g, next, state);
    return y;
}

// A biquad in direct form 1, y = b0 in + b1 in[n-1] + b2 in[n-2] - a1 y[n-1] - a2 y[n-2] summed in that order. `state`
// is y; returns the recurrence that defines it.
constexpr std::int32_t biquad(Graph& g, std::int32_t in, const Biquad& c, std::int32_t state) {
    const auto in1 = lag(g, in, 1);
    const auto in2 = lag(g, in, 2);
    const auto out1 = lag(g, state, 1);
    const auto out2 = lag(g, state, 2);
    const auto fed0 = add_node(g, op::Mul, c[0], in);
    const auto fed1 = add_node(g, op::Mul, c[1], in1);
    const auto fed2 = add_node(g, op::Mul, c[2], in2);
    const auto back1 = add_node(g, op::Mul, c[3], out1);
    const auto back2 = add_node(g, op::Mul, c[4], out2);
    const auto sum1 = add_node(g, op::Add, fed0, fed1);
    const auto sum2 = add_node(g, op::Add, sum1, fed2);
    const auto sum3 = add_node(g, op::Sub, sum2, back1);
    const auto sum4 = add_node(g, op::Sub, sum3, back2);
    return settle(g, sum4, state);
}

// y = tanh(c in + d y[n-1] + offset). `state` is y; returns the recurrence that defines it.
constexpr std::int32_t saturating(Graph& g, std::int32_t in, std::int32_t c, std::int32_t d, std::int32_t offset,
                                  std::int32_t state) {
    const auto before = lag(g, state, 1);
    const auto fed = add_node(g, op::Mul, c, in);
    const auto kept = add_node(g, op::Mul, d, before);
    const auto sum = add_node(g, op::Add, fed, kept);
    const auto shifted = add_node(g, op::Add, sum, offset);
    const auto bent = add_node(g, op::Tanh, shifted);
    return settle(g, bent, state);
}

// ---- k sections linked: u = _chain(in, Y), Y the sections' outputs over [k, time]; each returns section k - 1's ----

constexpr std::int32_t linked_tpt(Graph& g, std::int32_t in, std::span<const double> gains) {
    const auto k = static_cast<std::int32_t>(gains.size());
    const auto outputs = variable(g, Shape{k, time_of(g)});
    const auto states = variable(g, Shape{k, time_of(g)});
    const auto u = add_node(g, op::Chain, in, outputs);
    const auto gain = per_section(g, gains);
    const auto y = tpt(g, u, gain, states);
    settle(g, y, outputs);
    return section_of(g, outputs, k - 1, Shape{time_of(g)});
}

constexpr std::int32_t linked_biquads(Graph& g, std::int32_t in, const Biquad& c, std::int32_t k) {
    const auto outputs = variable(g, Shape{k, time_of(g)});
    const auto u = add_node(g, op::Chain, in, outputs);
    biquad(g, u, c, outputs);
    return section_of(g, outputs, k - 1, Shape{time_of(g)});
}

constexpr std::int32_t linked_saturating(Graph& g, std::int32_t in, std::span<const double> feedback) {
    const auto k = static_cast<std::int32_t>(feedback.size());
    const auto outputs = variable(g, Shape{k, time_of(g)});
    const auto u = add_node(g, op::Chain, in, outputs);
    const auto c = scalar(g, 0.5);
    const auto d = per_section(g, feedback);
    const auto offset = scalar(g, 0.1);
    saturating(g, u, c, d, offset, outputs);
    return section_of(g, outputs, k - 1, Shape{time_of(g)});
}

// ---- the same sections as k cycles, section j reading section j - 1's output of this sample; no link --------------

constexpr std::int32_t cycles_tpt(Graph& g, std::int32_t in, std::span<const double> gains) {
    std::int32_t through = in;
    for (const double number : gains) {
        const auto state = variable(g, Shape{time_of(g)});
        const auto gain = scalar(g, number);
        through = tpt(g, through, gain, state);
    }
    return through;
}

// `of_section(j)` gives section j's coefficients.
template <class OfSection>
constexpr std::int32_t cycles_biquads(Graph& g, std::int32_t in, OfSection of_section, std::int32_t k) {
    std::int32_t through = in;
    for (std::int32_t j = 0; j < k; ++j) {
        const auto state = variable(g, Shape{time_of(g)});
        const Biquad c = of_section(j);
        through = biquad(g, through, c, state);
    }
    return through;
}

constexpr std::int32_t cycles_saturating(Graph& g, std::int32_t in, std::span<const double> feedback) {
    const auto c = scalar(g, 0.5);
    const auto offset = scalar(g, 0.1);
    std::int32_t through = in;
    for (const double number : feedback) {
        const auto state = variable(g, Shape{time_of(g)});
        const auto d = scalar(g, number);
        through = saturating(g, through, c, d, offset, state);
    }
    return through;
}

// ---- coefficients --------------------------------------------------------------------------------------------------

// Each coefficient one constant over [k, 1], one number a section.
template <std::size_t K>
constexpr Biquad constants_per_section(Graph& g, const std::array<Coefficients, K>& sections) {
    Biquad c{};
    for (std::size_t i = 0; i < c.size(); ++i) {
        std::array<double, K> column{};
        for (std::size_t j = 0; j < K; ++j) {
            column[j] = sections[j][i];
        }
        c[i] = per_section(g, column);
    }
    return c;
}

constexpr Biquad scalars(Graph& g, const Coefficients& numbers) {
    Biquad c{};
    for (std::size_t i = 0; i < c.size(); ++i) {
        c[i] = scalar(g, numbers[i]);
    }
    return c;
}

// The peaks' coefficients as [5, 1] tensor inputs, after `x`.
constexpr Biquad peak_inputs(Graph& g) {
    Biquad c{};
    for (std::size_t i = 0; i < c.size(); ++i) {
        c[i] = add_value(g, ElemType::F64, Shape{5, 1}, ValueCategory::Input);
        g.values[static_cast<std::size_t>(c[i])].timeless = true;
        g.inputs.push_back(NamedValue{kPeakInputs[i], c[i]});
    }
    return c;
}

// Section j's number of each, sliced out of the inputs.
constexpr Biquad sections_of(Graph& g, const Biquad& inputs, std::int32_t j) {
    Biquad c{};
    for (std::size_t i = 0; i < c.size(); ++i) {
        c[i] = section_of(g, inputs[i], j, Shape{1});
    }
    return c;
}

// ---- the graphs ----------------------------------------------------------------------------------------------------

enum class Form : std::uint8_t { Linked, Cycles };

// A graph of the input `x` (any other input `body` declares after it), `length` samples long, and the output `y`, what
// `body` makes of `x`, analysed at `High`. A `Length` is declared as `Signal{Length{}}` declares it.
template <class Body>
constexpr Graph graph_of(Body body, Extent length = kLength) {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{length}, ValueCategory::Input);
    g.inputs.push_back(NamedValue{Name{"x"}, x});
    if (is_symbol(length)) {
        g.dims.push_back(DimDecl{symbol_of(length), DimConstraints{}});
    }
    const auto y = body(g, x);
    g.outputs.push_back(NamedValue{Name{"y"}, y});
    set_default_precision(g, numerics::Precision::High);
    if (const Diagnostic d = infer(g); !d.ok) {
        fail(d.message.view());
    }
    return g;
}

// The stream's length, bound by `prepare`: `Signal{Length{}}`'s.
inline constexpr Extent kStream{Dim{kStreamLength}};

constexpr Graph tpt_pair(Form form, Extent length = kLength) {
    const auto body = [form](Graph& g, std::int32_t x) {
        return form == Form::Linked ? linked_tpt(g, x, kTptGains) : cycles_tpt(g, x, kTptGains);
    };
    return graph_of(body, length);
}

constexpr Graph lr8(Form form, Extent length = kLength) {
    const auto body = [form](Graph& g, std::int32_t x) {
        if (form == Form::Linked) {
            const Biquad c = constants_per_section(g, kLr8);
            return linked_biquads(g, x, c, 4);
        }
        const auto of_section = [&g](std::int32_t j) { return scalars(g, kLr8[static_cast<std::size_t>(j)]); };
        return cycles_biquads(g, x, of_section, 4);
    };
    return graph_of(body, length);
}

constexpr Graph peaks(Form form, Extent length = kLength) {
    const auto body = [form](Graph& g, std::int32_t x) {
        const Biquad inputs = peak_inputs(g);
        if (form == Form::Linked) {
            return linked_biquads(g, x, inputs, 5);
        }
        const auto of_section = [&g, &inputs](std::int32_t j) { return sections_of(g, inputs, j); };
        return cycles_biquads(g, x, of_section, 5);
    };
    return graph_of(body, length);
}

constexpr Graph saturating_chain(Form form, Extent length = kLength) {
    const auto body = [form](Graph& g, std::int32_t x) {
        return form == Form::Linked ? linked_saturating(g, x, kFeedback) : cycles_saturating(g, x, kFeedback);
    };
    return graph_of(body, length);
}

constexpr Graph in_series(Form form, Extent length = kLength) {
    const auto body = [form](Graph& g, std::int32_t x) {
        if (form == Form::Linked) {
            const auto pair = linked_tpt(g, x, kTptGains);
            return linked_saturating(g, pair, kFeedback);
        }
        const auto pair = cycles_tpt(g, x, kTptGains);
        return cycles_saturating(g, pair, kFeedback);
    };
    return graph_of(body, length);
}

// The eight tanh sections linked, all eight the output: their sections read other than through the last one's slice.
constexpr Graph all_eight_out() {
    const auto body = [](Graph& g, std::int32_t x) {
        const auto outputs = variable(g, Shape{8, time_of(g)});
        const auto u = add_node(g, op::Chain, x, outputs);
        const auto c = scalar(g, 0.5);
        const auto d = per_section(g, kFeedback);
        const auto offset = scalar(g, 0.1);
        return saturating(g, u, c, d, offset, outputs);
    };
    return graph_of(body);
}

} // namespace heinzel::testing::linked_sections
