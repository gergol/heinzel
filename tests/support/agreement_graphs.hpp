#pragma once

// The surface corpus (`Plan/archive/13-loop-tree/06` §4): graphs shaped like the ones the surfaces built before they left the
// tree (Plan/archive/14-dsl DS-20), each once with its stream a run-time symbol (`sym`) and once a literal 64, with the verdict
// `try_compile` gives each — lowered, or the start of its refusal. `nest_agreement` pins those verdicts. It grew out
// of `Plan/probes/p5_surface.cpp`.

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/gradients.hpp"
#include "heinzel/notation/einsum.hpp"
#include "heinzel/notation/rearrange.hpp"

#include <cstdint>
#include <string_view>

#include "hand_built_graph.hpp"

namespace agreement_graphs {

using namespace heinzel;

inline bool sym = true; // the streamed extent a symbol (true) or 64 (false)

// The stream as an `Extent` (a direct `Signal{…, T()}` declares nothing), and a signal whose length is the stream
// (`Length{}` declares symbol 0, the dimension `T()` names).
inline Extent T() { return sym ? Extent{Dim{0}} : Extent{64}; }
inline Signal streamed(auto... e) { return sym ? Signal{e..., Length{}} : Signal{e..., 64}; }

constexpr Index n{};

inline Graph elementwise() {
    Input x{"x", streamed()};
    Expr a = clamp(x * 2.0, -1.0, 1.0);
    Expr b = tanh(a) + sin(x) + sinpi(x) + exp(a) + log(abs(x) + 1.0) + sqrt(abs(x)) + rsqrt(abs(x) + 1.0);
    Expr c = where(x > constant(0.0), b, frac(x)) + floor(x);
    Output y{"y"};
    y = max(c, min(x, 0.5));
    return y;
}
inline Graph gain_param_rank0() {
    Input x{"x", streamed()};
    Param k{"k"};
    Output y{"y"};
    y = x * k;
    return y;
}
inline Graph gain_param_stream() {
    Input x{"x", streamed()};
    Input k{"k", streamed()};
    Output y{"y"};
    y = x * k;
    return y;
}
inline Graph fir_delayed() {
    Input x{"x", streamed()};
    Expr d1 = x[n - 1];
    Expr d3 = x[n - 3];
    Output y{"y"};
    y = x + 0.5 * d1 + 0.25 * d3;
    return y;
}
inline Graph onepole() {
    Input x{"x", streamed()};
    Output y{"y"};
    y[n] = x[n] + 0.9 * y[n - 1];
    return y;
}
// A rank-1 cycle whose only lag reaches two samples back: no state handed on in a register, so no inner loops.
inline Graph lag_of_two() {
    Input x{"x", streamed()};
    Output y{"y"};
    y[n] = x[n] + 0.5 * y[n - 2];
    return y;
}
inline Graph biquad() {
    Input x{"x", streamed()};
    Output y{"y"};
    Expr ff = 0.2 * x[n] + 0.3 * x[n - 1] + 0.1 * x[n - 2];
    Expr fb = 0.5 * y[n - 1] - 0.25 * y[n - 2];
    y[n] = ff + fb;
    return y;
}
inline Graph layernorm() {
    Input x{"x", streamed(8)};
    Expr mu = x.mean(0, keepdims);
    Expr c = x - mu;
    Expr var = (c * c).mean(0, keepdims);
    Output y{"y"};
    y = c * rsqrt(var + 1e-5);
    return y;
}
inline Graph matmul_stream() {
    Input a{"a", streamed(4, 3)};
    Input b{"b", streamed(3, 5)};
    Output y{"y"};
    y = einsum("ik,kj->ij", a, b);
    return y;
}
inline Graph dense_relu() {
    Input x{"x", streamed(16)};
    Input wt{"w", streamed(8, 16)};
    Expr h = einsum("i,oi->o", x, wt);
    Output y{"y"};
    y = max(h, 0.0);
    return y;
}
inline Graph rearrange_split() {
    Input x{"x", streamed(12)};
    Expr s = rearrange("(c f) -> c f", x, {{"f", 4}});
    Output y{"y"};
    y = s * 2.0;
    return y;
}
inline Graph median_odd() {
    Input x{"x", streamed(5)};
    Output y{"y"};
    y = x.median(0) + 0.0;
    return y;
}
inline Graph median_even() {
    Input x{"x", streamed(4)};
    Output y{"y"};
    y = x.median(0) + 0.0;
    return y;
}
inline Graph median_filter3() {
    Input x{"x", streamed(16)};
    Expr f = x.unfold(0, 3);
    Output y{"y"};
    y = f.median(1) + 0.0;
    return y;
}
inline Graph sort_axis() {
    Input x{"x", streamed(6)};
    Output y{"y"};
    y = x.sort(0) + 0.0;
    return y;
}
inline Graph topk_axis() {
    Input x{"x", streamed(6)};
    Output y{"y"};
    y = x.topk(0, 2) + 0.0;
    return y;
}
inline Graph quantile_between() {
    Input x{"x", streamed(6)};
    Output y{"y"};
    y = x.quantile(0, 0.3) + 0.0;
    return y;
}
inline Graph grads() {
    Input x{"x", streamed(4)};
    Input p{"p", streamed(4)};
    Expr e = x * p - 1.0;
    Output loss{"loss"};
    loss = (e * e).sum(0);
    return gradients(loss, {Grad{"dp", p}});
}
inline Graph two_outputs() {
    Input x{"x", streamed()};
    Expr a = x * 2.0;
    Expr b = a + 1.0;
    return finish(Output{"a"} = a, Output{"b"} = b);
}
inline Graph f64_input() {
    Input x{"x", streamed(), ElemType::F64};
    Output y{"y"};
    y = x * 2.0 + 1.0;
    return y;
}
inline Graph high_precision() {
    Input x{"x", streamed()};
    Output y{"y"};
    y = tanh(x) * 2.0;
    Graph g = y;
    set_default_precision(g, numerics::Precision::High);
    return g;
}
inline Graph views() {
    Input x{"x", streamed(3, 4)};
    Expr t = x.transpose({1, 0});
    Expr r = t.reshape({12});
    Expr s = r.slice(0, 2, 5);
    Expr i = iota(Signal{5, T()});
    Output y{"y"};
    y = s + i;
    return y;
}
inline Graph realize_view() {
    Input x{"x", streamed(3, 4)};
    Output y{"y"};
    y = x.transpose({1, 0}).realize() + 1.0;
    return y;
}
inline Graph framed_stream() {
    Input x{"x", streamed()};
    Expr f = x.unfold(0, 4, 2);
    Expr ms = (f * f).mean(0);
    Output y{"y"};
    y = sqrt(ms);
    return y;
}
inline Graph timeless_matmul() {
    Input a{"a", {4, 3}};
    Input b{"b", {3, 5}};
    Output y{"y"};
    y = einsum("ik,kj->ij", a, b);
    return y;
}

// A SHAPED constant, as autodiff makes one: built with a placeholder input "c", which then becomes the
// constant 2.0 and leaves the inputs. (The frontend's `constant()` is rank 0; a view of it does not analyse.)
inline Graph with_constant(Graph g) {
    for (std::size_t i = 0; i < g.inputs.size(); ++i) {
        if (g.inputs[i].name.view() == "c") {
            Value& v = g.values[static_cast<std::size_t>(g.inputs[i].value)];
            v.category = ValueCategory::Const;
            v.constant = 2.0;
            for (std::size_t j = i + 1; j < g.inputs.size(); ++j) {
                g.inputs[j - 1] = g.inputs[j];
            }
            g.inputs.pop_back();
            break;
        }
    }
    return g;
}
inline Graph const_view_map() {
    Input x{"x", streamed(3, 4)};
    Input c{"c", Signal{1, 3, T()}};
    Output y{"y"};
    y = x * c.transpose({1, 0});
    return with_constant(y);
}
inline Graph const_view_chain() {
    Input x{"x", streamed(3, 4)};
    Input c{"c", Signal{12, T()}};
    Expr r = c.reshape({4, 3});
    Output y{"y"};
    y = x + r.transpose({1, 0});
    return with_constant(y);
}
inline Graph const_view_slice() {
    Input x{"x", streamed(3)};
    Input c{"c", Signal{8, T()}};
    Output y{"y"};
    y = x - c.slice(0, 2, 3);
    return with_constant(y);
}
inline Graph const_view_contraction() {
    Input a{"a", streamed(4, 3)};
    Input c{"c", Signal{5, 3, T()}};
    Output y{"y"};
    y = einsum("ik,kj->ij", a, c.transpose({1, 0}));
    return with_constant(y);
}
inline Graph const_bare_contraction() {
    Input a{"a", streamed(4, 3)};
    Input c{"c", Signal{3, 5, T()}};
    Output y{"y"};
    y = einsum("ik,kj->ij", a, c);
    return with_constant(y);
}
inline Graph const_view_lag() {
    Input x{"x", streamed(3)};
    Input c{"c", Signal{1, 3, T()}};
    Expr v = c.reshape({3});
    Output y{"y"};
    y = x + v[n - 2];
    return with_constant(y);
}
inline Graph const_window() {
    Input x{"x", streamed(14)};
    Input c{"c", Signal{16, T()}};
    Expr f = c.unfold(0, 3).sum(1);
    Output y{"y"};
    y = x + f;
    return with_constant(y);
}
inline Graph const_view_output() {
    Input x{"x", streamed()};
    Input c{"c", Signal{1, 3, T()}};
    Output y{"y"};
    Output z{"z"};
    y = x * 2.0;
    z = c.transpose({1, 0});
    return with_constant(finish(y, z));
}
inline Graph iota_views() {
    Input x{"x", streamed(3, 4)};
    Expr i = iota(Signal{4, 3, T()});
    Expr v = i.transpose({1, 0});
    Expr s = i.slice(0, 1, 3); // {3, 3, T}: rows 1..3 of the ramp
    Output y{"y"};
    y = x * v + v[n - 1] + s.sum(1, keepdims);
    return y;
}
inline Graph made_then_viewed() {
    Input x{"x", streamed(3, 4)};
    Output y{"y"};
    y = (x * 2.0).transpose({1, 0}) + 1.0;
    return y;
}
inline Graph grads_of_a_slice() { // the slice's adjoint pads the loss's seed, a shaped constant
    Input x{"x", streamed(4)};
    Input p{"p", streamed(4)};
    Output loss{"loss"};
    loss = (x * p).slice(0, 1, 1);
    return gradients(loss, {Grad{"dp", p}});
}
inline Graph table_over_a_constant() { // a table read by a phase, the table a shaped constant
    Graph g{};
    const Extent t = sym ? Extent{Dim{0}} : Extent{64};
    const auto phase = add_value(g, ElemType::F32, Shape{t}, ValueCategory::Input);
    const auto table = add_value(g, ElemType::F32, Shape{9, t}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(table)].constant = 0.5;
    OpAttr read{};
    read.kind = AttrKind::Table;
    read.table.size = 9; // the table is its nine cells
    const auto y = add_node(g, op::TableRead, phase, table, kNoValue, read);
    g.inputs.push_back(NamedValue{"phase", phase});
    g.outputs.push_back(NamedValue{"y", y});
    return g;
}
// A table read over a window (of one frame: nine cells a sample), or over a table of three axes.
inline Graph table_read_over(bool window) {
    Graph g{};
    const Extent t = sym ? Extent{Dim{0}} : Extent{64};
    const auto phase = add_value(g, ElemType::F32, Shape{t}, ValueCategory::Input);
    const auto x = add_value(g, ElemType::F32, window ? Shape{9, t} : Shape{3, 3, t}, ValueCategory::Input);
    OpAttr frame{};
    frame.kind = AttrKind::View;
    frame.view.kind = ViewKind::Unfold;
    frame.view.window = 9;
    frame.view.hop = 1;
    frame.view.frames = -1;
    const auto table = window ? add_node(g, op::Unfold, x, kNoValue, kNoValue, frame) : x;
    OpAttr read{};
    read.kind = AttrKind::Table;
    read.table.size = 9;
    const auto y = add_node(g, op::TableRead, phase, table, kNoValue, read);
    g.inputs.push_back(NamedValue{"phase", phase});
    g.inputs.push_back(NamedValue{"x", x});
    g.outputs.push_back(NamedValue{"y", y});
    return g;
}
inline Graph table_over_a_window() { return table_read_over(true); }
inline Graph table_of_three_axes() { return table_read_over(false); }
inline Graph window_contracted() { // a convolution written as a contraction over frames
    Input x{"x", streamed(16)};
    Input k{"k", streamed(3)};
    Output y{"y"};
    y = einsum("fk,k->f", x.unfold(0, 3), k);
    return y;
}
inline Graph ramp_lagged() {
    Input x{"x", streamed(4)};
    Output y{"y"};
    y = x + iota(Signal{4, T()})[n - 2];
    return y;
}
// Three graphs whose verdict hangs on what is a result: a view read by a view, a window, and a producer
// folded into a window's reader — each a result beside another.
inline Graph viewed_result() {
    Expr t = Input{"x", streamed(3, 4)}.transpose({1, 0});
    return finish(Output{"t"} = t, Output{"y"} = t.slice(0, 1, 2) + 1.0);
}
inline Graph window_result() {
    Input x{"x", streamed(6)};
    Output f{"f"};
    Output y{"y"};
    f = x.unfold(0, 3);
    y = x * 2.0;
    return finish(f, y);
}
inline Graph folded_producer_result() {
    Expr a = Input{"x", streamed(16)} * 2.0;
    Input h{"h", streamed(3)};
    return finish(Output{"a"} = a, Output{"y"} = (a.unfold(0, 3) * h).sum(1));
}
// A slice of a map over a slice, offered to a fold's group after its fold, which keeps its axes: read where the
// fold's first point stands (review of loop-tree M3).
inline Graph slice_after_a_fold() {
    Input x{"x", streamed(5, 3)};
    Expr s = x.slice(0, 1, 2);
    Expr v = x + 1.0;
    Expr m = s * 2.0;
    Expr z = m.slice(0, 0, 1);
    Expr f = v.sum(0, keepdims);
    return finish(Output{"z"} = z, Output{"y"} = max(v, f));
}
inline Graph grads_unreached() {
    Input x{"x", streamed(4)};
    Input p{"p", streamed(4)};
    Output loss{"loss"};
    loss = (x * x).sum(0);
    return gradients(loss, {Grad{"dp", p}});
}
inline Graph cli_static_gain() {  // the CLI's static graph: F64, a rank-0 param
    Input x{"x", streamed(), ElemType::F64};
    Param gain{"gain", ElemType::F64};
    Output y{"y"};
    y = x * gain;
    return y;
}
inline Graph cli_energy_fraction() {  // the CLI's framed graph, as the CLI's own test hand-built it
    Graph g{};
    const Extent n = sym ? Extent{Dim{0}} : Extent{64};
    const auto x = add_value(g, ElemType::F64, Shape{n}, ValueCategory::Input);
    OpAttr frame{};
    frame.kind = AttrKind::View;
    frame.view.kind = ViewKind::Unfold;
    frame.view.axis = 0;
    frame.view.window = 4;
    frame.view.hop = 2;
    frame.view.frames = sym ? 1 : -1;
    const auto framed = add_node(g, op::Unfold, x, kNoValue, kNoValue, frame);
    const auto sq = add_node(g, op::Mul, framed, framed);
    OpAttr mean{};
    mean.kind = AttrKind::Reduce;
    mean.reduce.op = ReduceOp::Mean;
    mean.reduce.axes = Axes{0};
    const auto meansq = add_node(g, op::Reduce, sq, kNoValue, kNoValue, mean);
    const auto rms = add_node(g, op::Sqrt, meansq);
    const auto thr = add_value(g, ElemType::F64, Shape{}, ValueCategory::Input);
    const auto above = add_node(g, op::Lt, thr, rms);
    g.inputs.push_back(NamedValue{"x", x});
    g.inputs.push_back(NamedValue{"threshold", thr});
    g.outputs.push_back(NamedValue{"above", above});
    return g;
}
inline constexpr std::int32_t kVampN = 32;
inline Graph vamp_stateless() {
    Input x{"x", Signal{kVampN}};
    Input gain{"gain", Signal{kVampN}};
    Input bias{"bias", Signal{kVampN}};
    Expr scaled = x * gain;
    Output y{"y"};
    y = scaled + bias;
    return y;
}
inline Graph vamp_one_pole() {
    Input x{"x", Signal{kVampN}};
    Input a{"a", Signal{kVampN}};
    Input b{"b", Signal{kVampN}};
    Output y{"y"};
    Expr ff = a * x[n];
    Expr fb = b * y[n - 1];
    y[n] = ff + fb;
    return y;
}
inline Graph vamp_with_rate() {
    Input x{"x", Signal{kVampN}};
    Input sr{"sample_rate", Signal{kVampN}};
    Output y{"y"};
    y = x + sr;
    return y;
}

inline Graph quantile_least() {
    Input x{"x", streamed(5)};
    Output y{"y"};
    y = x.quantile(0, 0.0) + 0.0;
    return y;
}
inline Graph quantile_most() {
    Input x{"x", streamed(5)};
    Output y{"y"};
    y = x.quantile(0, 1.0) + 0.0;
    return y;
}

inline constexpr std::string_view kLowers = "";
inline constexpr std::string_view kFramed = "heinzel::compile: unfold: a window over no element axis";
inline constexpr std::string_view kFrameCount = "unfold unfolds axis 0, whose size is not known yet";

// What a graph gets: `lowers` when its refusal is empty, else the start of its refusal — once with a
// symbolic stream, once with a literal one.
struct Expected {
    const char* name;
    Graph (*build)();
    std::string_view symbolic;
    std::string_view literal;
};

inline constexpr Expected kCorpus[] = {
    {"an elementwise chain, sin and log included", elementwise, kLowers, kLowers},
    {"a rank-0 param", gain_param_rank0, kLowers, kLowers},
    {"a streamed param", gain_param_stream, kLowers, kLowers},
    {"a FIR of delays", fir_delayed, kLowers, kLowers},
    {"a one-pole", onepole, kLowers, kLowers},
    {"a lag of two, fed back", lag_of_two, kLowers, kLowers},
    {"a biquad", biquad, kLowers, kLowers},
    {"layernorm", layernorm, kLowers, kLowers},
    {"a contraction over time", matmul_stream, kLowers, kLowers},
    {"a dense layer and relu", dense_relu, kLowers, kLowers},
    {"a split, then doubled", rearrange_split, kLowers, kLowers},
    {"the median of 5", median_odd, kLowers, kLowers},
    {"the median of 4", median_even, "heinzel::compile: median is not one exact rank", "heinzel::compile: median is not one exact rank"},
    {"a sliding median", median_filter3, kLowers, kLowers},
    {"the quantile 0", quantile_least, kLowers, kLowers},
    {"the quantile 1", quantile_most, kLowers, kLowers},
    {"a sort", sort_axis, "heinzel::compile: sort is not one exact rank", "heinzel::compile: sort is not one exact rank"},
    {"the 2 largest", topk_axis, "heinzel::compile: topk 2 is not one exact rank", "heinzel::compile: topk 2 is not one exact rank"},
    {"the quantile 0.3", quantile_between, "heinzel::compile: quantile 0.3 is not one exact rank",
     "heinzel::compile: quantile 0.3 is not one exact rank"},
    {"gradients through a sum", grads, kLowers, kLowers},
    {"two outputs", two_outputs, kLowers, kLowers},
    {"an F64 input", f64_input, kLowers, kLowers},
    {"High", high_precision, kLowers, kLowers},
    {"transpose, reshape, slice, iota", views, kLowers, kLowers},
    {"a realized transpose", realize_view, kLowers, kLowers},
    {"a framed stream", framed_stream, kFrameCount, kFramed},
    {"a timeless contraction", timeless_matmul, kLowers, kLowers},
    {"a view of a constant, mapped", const_view_map, kLowers, kLowers},
    {"two views of a constant", const_view_chain, kLowers, kLowers},
    {"a slice of a constant", const_view_slice, kLowers, kLowers},
    {"a view of a constant, contracted", const_view_contraction, kLowers, kLowers},
    {"a constant, contracted", const_bare_contraction, "heinzel::compile: einsum: a contraction of a constant",
     "heinzel::compile: einsum: a contraction of a constant"},
    {"a view of a constant, lagged", const_view_lag, kLowers, kLowers},
    {"a window over a constant", const_window, "heinzel::compile: unfold: a window over a constant",
     "heinzel::compile: unfold: a window over a constant"},
    {"a view of a constant as a result", const_view_output, kLowers, kLowers},
    {"views of a ramp", iota_views, kLowers, kLowers},
    {"a view of a map", made_then_viewed, kLowers, kLowers},
    {"gradients through a slice (a pad of the seed)", grads_of_a_slice, kLowers, kLowers},
    {"a table over a constant", table_over_a_constant, "heinzel::compile: _tblread: a table read over a constant",
     "heinzel::compile: _tblread: a table read over a constant"},
    {"a window, contracted", window_contracted, "heinzel::compile: einsum: a contraction of a constant or a window",
     "heinzel::compile: einsum: a contraction of a constant or a window"},
    {"a ramp, lagged", ramp_lagged, kLowers, kLowers},
    {"a table read over a window", table_over_a_window, "heinzel::compile: _tblread: a table read over a constant or a window",
     "heinzel::compile: _tblread: a table read over a constant or a window"},
    {"a table of three axes", table_of_three_axes, "heinzel::compile: _tblread: a table with axes besides its cells and time",
     "heinzel::compile: _tblread: a table with axes besides its cells and time"},
    {"a view that is a result, viewed", viewed_result, kLowers, kLowers},
    {"a window that is a result", window_result, "heinzel::compile: unfold: a window that is itself a result",
     "heinzel::compile: unfold: a window that is itself a result"},
    {"a result folded into a window's reader", folded_producer_result, kLowers, kLowers},
    {"a slice read after a fold that keeps its axes", slice_after_a_fold, kLowers, kLowers},
    {"a gradient its loss does not reach", grads_unreached, "heinzel::compile: an output that no node makes", "heinzel::compile: an output that no node makes"},
    {"the CLI's gain", cli_static_gain, kLowers, kLowers},
    {"the CLI's energy_fraction", cli_energy_fraction, kFramed, kFramed},
    {"Vamp: gain and bias", vamp_stateless, kLowers, kLowers},
    {"Vamp: a one-pole", vamp_one_pole, kLowers, kLowers},
    {"Vamp: the host's rate", vamp_with_rate, kLowers, kLowers},
};

} // namespace agreement_graphs
