// `Executor` (Plan/archive/14-dsl/04): what it runs is what `loops::run` runs over the program `compile` makes, bit
// for bit — for graphs of the corpus and for graphs spelled with the declarations, literal and with a
// `Length` at several lengths (each from `prepare`, a fresh start), with several outputs (a timeless one
// beside signals), F32 and F64 params, a graph built by a lambda, an `Executor` moved after binding, an
// input nothing reads left unbound, and one buffer bound as an input and as an output. State carries from run to
// run: `reset`, and two runs in place against one over both blocks (`executor_state_test`). Every run-time refusal is
// shown with its text; what must not compile is in `gates/refusals/executor.cpp`.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

#include "heinzel/compile.hpp"
#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/notation/einsum.hpp"

#include "corpus_graphs.hpp"
#include "hand_built_graph.hpp"
#include "well_formed.hpp"

using namespace heinzel;

namespace {

constexpr Index n{};

// ---- graphs spelled with the declarations ----------------------------------------------------------

constexpr Graph comb() {
    Input x{"x", Signal{64}};
    Output y{"y"};
    y[n] = x[n] + 0.5 * y[n - 3];
    return y;
}

// ...whose output alone (8 KiB) outgrows an arena inside its workspace: on the heap, so a move empties it.
constexpr Graph long_comb() {
    Input x{"x", Signal{2048}};
    Output y{"y"};
    y[n] = x[n] + 0.5 * y[n - 3];
    return y;
}

constexpr Length kFrames{.multiple_of = 4, .min = 4, .expect = 64};
constexpr Graph streamed_comb() {
    Input x{"x", Signal{2, kFrames}};
    Param gain{"gain"};
    Output y{"y"};
    y[n] = gain * x[n] + 0.5 * y[n - 3];
    return y;
}

// A signal output beside a timeless one, and an F64 param.
constexpr Graph signal_and_tensor() {
    Input x{"x", Signal{3, 16}, f64};
    Input w{"w", {3}, f64};
    Param bias{"bias", f64};
    Output y{"y"};
    Output scaled{"scaled"};
    y = x * w + bias;
    scaled = w * 2.0;
    return finish(y, scaled);
}

// A streamed contraction with a `Length`.
constexpr Graph streamed_einsum() {
    Input a{"a", Signal{4, 3, Length{}}};
    Input b{"b", Signal{3, 5, Length{}}};
    Output y{"y"};
    y = einsum("ik,kj->ij", a, b);
    return y;
}

constexpr Graph scaled_by(double factor) {
    Input x{"x", Signal{32}};
    Output y{"y"};
    y = x * factor;
    return y;
}

// An input in the graph that no node reads. (A declared input joins a graph only through a node that
// reads it, and the planner computes a node no output needs, so a frontend graph reads every input it has.)
constexpr Graph with_an_unread_input() {
    Graph g{};
    const auto a = add_value(g, ElemType::F32, Shape{8}, ValueCategory::Input);
    const auto b = add_value(g, ElemType::F32, Shape{8}, ValueCategory::Input);
    const auto unused_gain = add_value(g, ElemType::F32, Shape{}, ValueCategory::Input);
    g.inputs.push_back(NamedValue{"a", a});
    g.inputs.push_back(NamedValue{"b", b});
    g.inputs.push_back(NamedValue{"gain", unused_gain}); // a Param no node reads: never set
    g.outputs.push_back(NamedValue{"y", add_node(g, op::Add, a, a)});
    return g;
}

// A length whose declaration is not id 0: its constraints are looked up by the program's symbol.
constexpr Graph length_of_id_one() {
    Expr x = detail::input("x", Shape{Dim{1}}, ElemType::F32, DimDecl{1, DimConstraints{4, 4}});
    Output y{"y"};
    y = x * 2.0;
    return y;
}

// An input read in its past: the program reads the feed's earlier samples.
constexpr Graph fir() {
    Input x{"x", Signal{64}};
    Output y{"y"};
    y = 0.5 * x[n] + 0.25 * x[n - 1] + 0.25 * x[n - 2];
    return y;
}

// ---- the reference: loops::run over compile's program, every input fed the same numbers -------------

template <auto Build>
constexpr Compiled kCompiled = compile(Build());

// Numbers for every input of `g` at `frames` samples (params: one number each), deterministic and
// distinct per input and element.
struct Feeds {
    std::vector<std::vector<float>> f32;
    std::vector<std::vector<double>> f64;
    loops::Feeds pointers{};
};

std::int64_t elements(const Shape& s, std::int64_t frames) {
    std::int64_t count = 1;
    for (std::int32_t k = 0; k < s.rank; ++k) {
        count *= is_symbol(extent_at(s, k)) ? frames : literal_of(extent_at(s, k));
    }
    return count;
}

Feeds feeds_for(const Graph& g, std::int64_t frames) {
    Feeds f;
    f.f32.resize(g.inputs.size());
    f.f64.resize(g.inputs.size());
    for (std::size_t i = 0; i < g.inputs.size(); ++i) {
        const Value& v = g.values[static_cast<std::size_t>(g.inputs[i].value)];
        const auto count = static_cast<std::size_t>(elements(v.shape, frames));
        for (std::size_t e = 0; e < count; ++e) {
            const double x = 0.1 + 0.25 * static_cast<double>((e * 7 + i * 13) % 23) - 2.5; // 0.1: no float holds it
            f.f32[i].push_back(static_cast<float>(x));
            f.f64[i].push_back(x);
        }
        f.pointers[i] = v.type == ElemType::F32 ? static_cast<const void*>(f.f32[i].data()) : f.f64[i].data();
    }
    return f;
}

// Output `o` of a run, as bytes.
template <const loops::Program& P>
std::vector<std::byte> bytes_of_output(loops::Workspace<P>& work, std::size_t o) {
    const auto b = static_cast<std::size_t>(P.outputs[o]);
    const auto size = static_cast<std::size_t>(loops::element_count(P.buffers[b], work.sizes()) * loops::bytes_of(P.buffers[b].type));
    std::vector<std::byte> out(size);
    std::memcpy(out.data(), work.bytes_of(b), size);
    return out;
}

template <auto Build>
std::vector<std::vector<std::byte>> reference(std::int64_t frames) {
    constexpr const Compiled& c = kCompiled<Build>;
    static_assert(loops::accepted<c.program>());
    loops::Sizes sizes{};
    sizes.fill(frames);
    const Feeds f = feeds_for(c.graph, frames);
    loops::Workspace<c.program> work(sizes);
    loops::run<c.program>(f.pointers, work);
    std::vector<std::vector<std::byte>> outs;
    for (std::size_t o = 0; o < c.program.outputs.size(); ++o) {
        outs.push_back(bytes_of_output<c.program>(work, o));
    }
    return outs;
}

// The same through `Executor`, every port bound by its name (read off the graph while compiling).
template <auto Build, std::size_t... I, std::size_t... O>
std::vector<std::vector<std::byte>> through_executor(std::int64_t frames, std::index_sequence<I...>, std::index_sequence<O...>) {
    using Ex = Executor<Build>;
    static constexpr const Graph& g = kCompiled<Build>.graph; // (an `Executor` keeps none of its compile result)
    const Feeds f = feeds_for(g, frames);
    std::vector<std::vector<std::byte>> outs(sizeof...(O));
    Ex ex;
    const auto bind = [&]<std::size_t K>() {
        constexpr NamedValue port = g.inputs[K];
        constexpr const Value& v = g.values[static_cast<std::size_t>(port.value)];
        if constexpr (v.shape.rank == 0) {
            ex.param(g.inputs[K].name.data_) = v.type == ElemType::F32 ? double(f.f32[K][0]) : f.f64[K][0];
        } else if constexpr (v.type == ElemType::F32) {
            ex.input(g.inputs[K].name.data_) = f.f32[K].data();
        } else {
            ex.input(g.inputs[K].name.data_) = f.f64[K].data();
        }
    };
    (bind.template operator()<I>(), ...);
    const auto sink = [&]<std::size_t K>() {
        constexpr const Value& v = g.values[static_cast<std::size_t>(g.outputs[K].value)];
        outs[K].resize(static_cast<std::size_t>(elements(v.shape, frames)) * static_cast<std::size_t>(loops::bytes_of(v.type)));
        if constexpr (v.type == ElemType::F32) {
            ex.output(g.outputs[K].name.data_) = reinterpret_cast<float*>(outs[K].data());
        } else {
            ex.output(g.outputs[K].name.data_) = reinterpret_cast<double*>(outs[K].data());
        }
    };
    (sink.template operator()<O>(), ...);
    if constexpr (Ex::has_length) {
        ex.prepare(frames);
        ex.run(frames);
    } else {
        ex.run();
    }
    return outs;
}

template <auto Build>
bool agrees(std::int64_t frames = 1) {
    return reference<Build>(frames) == through_executor<Build>(frames, std::make_index_sequence<kCompiled<Build>.graph.inputs.size()>{},
                                                               std::make_index_sequence<kCompiled<Build>.graph.outputs.size()>{});
}

int g_wrong = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        ++g_wrong;
        std::printf("  FAILED: %s\n", what);
    }
}

template <class Run>
bool refused(Run run, std::string_view text) {
    try {
        run();
    } catch (const heinzel::error& e) {
        return e.message().find(text) != std::string_view::npos;
    }
    return false;
}

std::vector<float> ramp(std::size_t count) {
    std::vector<float> xs(count);
    for (std::size_t i = 0; i < count; ++i) {
        xs[i] = 0.125f * static_cast<float>(i % 17) - 1.0f;
    }
    return xs;
}

// The spelling a user writes, against the comb computed by hand.
bool comb_by_name() {
    const std::vector<float> x = ramp(64);
    std::vector<float> y(64);
    std::vector<float> want(64);
    Executor<comb> ex;
    ex.input("x") = x.data();
    ex.output("y") = y.data();
    ex.run();
    for (std::size_t t = 0; t < 64; ++t) {
        want[t] = x[t] + 0.5f * (t >= 3 ? want[t - 3] : 0.0f);
    }
    return y == want;
}

// Bound, then moved, then run: the moved `Executor` reads its own param, not the moved-from one's.
bool moved_after_binding() {
    const std::vector<float> x = ramp(2 * 12);
    std::vector<float> y(2 * 12);
    std::vector<float> want(2 * 12);
    Executor<streamed_comb> first;
    Executor<streamed_comb> unmoved;
    first.input("x") = x.data();
    unmoved.input("x") = x.data();
    first.output("y") = y.data();
    unmoved.output("y") = want.data();
    first.param("gain") = 0.5;
    unmoved.param("gain") = 0.5;
    Executor<streamed_comb> second = std::move(first);
    first.param("gain") = 100.0; // the moved-from object's slot: the moved one must not read it
    second.prepare(12);
    second.run(12);
    unmoved.prepare(12);
    unmoved.run(12);
    return y == want && y != std::vector<float>(2 * 12);
}

// An input in the graph that the program never reads needs no buffer, and a Param none reads no value.
bool unread_input_left_unbound() {
    const std::vector<float> a = ramp(8);
    std::vector<float> y(8);
    Executor<with_an_unread_input> ex;
    ex.input("a") = a.data();
    ex.output("y") = y.data();
    ex.run();
    bool doubled = kCompiled<with_an_unread_input>.graph.inputs.size() == 3; // b and gain unread, unbound, unset
    for (std::size_t t = 0; t < 8; ++t) {
        doubled = doubled && y[t] == a[t] + a[t];
    }
    return doubled;
}

// One buffer as the input and the output: the run reads the whole feed before the output is copied.
bool in_place() {
    const std::vector<float> x = ramp(64);
    std::vector<float> apart(64);
    std::vector<float> shared = x;
    Executor<fir> out_of_place;
    Executor<fir> over_itself;
    out_of_place.input("x") = x.data();
    out_of_place.output("y") = apart.data();
    out_of_place.run();
    over_itself.input("x") = shared.data();
    over_itself.output("y") = shared.data();
    over_itself.run();
    return shared == apart;
}

// Runs shorter than `prepare`'s: each over exactly its frames — the caller's output of that length, guarded on both
// sides and untouched past it — and each, after `reset`, what loops::run gives at that length, fed the same numbers.
std::size_t port(const NamedValue* ports, std::size_t count, std::string_view name) {
    std::size_t i = 0;
    while (i < count && ports[i].name.view() != name) {
        ++i;
    }
    return i;
}
bool runs_shorter_than_prepared() {
    using Ex = Executor<streamed_comb>;
    constexpr const Graph& g = kCompiled<streamed_comb>.graph;
    constexpr std::int64_t kGuard = 8;
    const std::size_t x = port(g.inputs.begin(), g.inputs.size(), "x");
    const std::size_t gain = port(g.inputs.begin(), g.inputs.size(), "gain");
    bool same = true;
    Ex ex;
    ex.prepare(16);
    for (const std::int64_t frames : {8, 16, 12, 4}) {
        const Feeds f = feeds_for(g, frames);
        std::vector<float> y(static_cast<std::size_t>(2 * frames + 2 * kGuard), 7.0f);
        ex.input("x") = f.f32[x].data();
        ex.param("gain") = f.f32[gain][0];
        ex.output("y") = y.data() + kGuard;
        ex.reset();
        ex.run(frames);
        const std::vector<std::vector<std::byte>> want = reference<streamed_comb>(frames);
        same = same && std::memcmp(y.data() + kGuard, want[0].data(), want[0].size()) == 0;
        for (std::int64_t k = 0; k < kGuard; ++k) {
            same = same && y[static_cast<std::size_t>(k)] == 7.0f && y[static_cast<std::size_t>(kGuard + 2 * frames + k)] == 7.0f;
        }
    }
    return same;
}

// Blocks of 8, 16, 12 and 4 frames, one after the other with nothing reset: the numbers of one run over the 40.
// Both ports are sample-major, so block b starts 2 * (frames before it) elements in.
bool blocks_of_a_length() {
    const std::vector<float> x = ramp(2 * 40);
    std::vector<float> whole(2 * 40);
    std::vector<float> blocks(2 * 40);
    Executor<streamed_comb> once;
    Executor<streamed_comb> in_blocks;
    once.input("x") = x.data();
    once.output("y") = whole.data();
    once.param("gain") = 0.75;
    once.prepare(40);
    once.run(40);
    in_blocks.param("gain") = 0.75;
    in_blocks.prepare(16);
    std::size_t at = 0;
    for (const std::int64_t frames : {8, 16, 12, 4}) {
        in_blocks.input("x") = x.data() + at;
        in_blocks.output("y") = blocks.data() + at;
        in_blocks.run(frames);
        at += static_cast<std::size_t>(2 * frames);
    }
    return blocks == whole && whole != std::vector<float>(2 * 40);
}

// A recurrence no signal drives, its length declared (Plan/archive/14-dsl/10): the knob set once, then blocks of 8, 16, 12
// and 4 frames — the numbers of one run over the 40.
constexpr Graph knob_oscillator() {
    Param f{"f"};
    Variable phase{Signal{kFrames}};
    phase[n] = frac(phase[n - 1] + f * 0.01);
    Output y{"y"};
    y = 2.0 * phase - 1.0;
    return y;
}
bool a_declared_length_in_blocks() {
    std::vector<float> whole(40);
    std::vector<float> blocks(40);
    Executor<knob_oscillator> once;
    Executor<knob_oscillator> in_blocks;
    once.output("y") = whole.data();
    once.param("f") = 3.0;
    once.prepare(40);
    once.run(40);
    in_blocks.param("f") = 3.0;
    in_blocks.prepare(16);
    std::size_t at = 0;
    for (const std::int64_t frames : {8, 16, 12, 4}) {
        in_blocks.output("y") = blocks.data() + at;
        in_blocks.run(frames);
        at += static_cast<std::size_t>(frames);
    }
    return blocks == whole && whole != std::vector<float>(40);
}

// An output left unbound is simply not copied.
bool an_output_left_unbound() {
    const std::vector<double> x(3 * 16, 0.5);
    const std::vector<double> w{1, 2, 3};
    std::vector<double> y(3 * 16);
    Executor<signal_and_tensor> ex;
    ex.input("x") = x.data();
    ex.input("w") = w.data();
    ex.param("bias") = 0.25;
    ex.output("y") = y.data();
    ex.run();
    return y[0] == 0.5 * 1 + 0.25 && y[3 * 16 - 1] == 0.5 * 3 + 0.25;
}

void refusals() {
    std::vector<float> x = ramp(64);
    std::vector<float> y(64);
    std::vector<double> wide(64);
    expect(refused([&] { Executor<comb> ex; ex.input("x") = wide.data(); }, "the buffer's element type is not the port's"),
           "an input buffer of the wrong type");
    expect(refused([&] { Executor<comb> ex; ex.output("y") = wide.data(); }, "the buffer's element type is not the port's"),
           "an output buffer of the wrong type");
    expect(kCompiled<streamed_einsum>.graph.dims.size() == 1 && Executor<corpus::lag_of_a_lag>::has_length == false,
           "has_length is read off the program");
#ifndef NDEBUG // what a run is given, checked in debug builds only (Plan/15-zero-overhead, OQ-1)
    expect(refused([] { Executor<comb> ex; ex.run(); }, "heinzel::Executor: an input has no buffer"), "an unbound input");
    expect(refused([] { Executor<comb> ex; ex.input("x") = static_cast<const float*>(nullptr); ex.run(); }, "heinzel::Executor: an input has no buffer"),
           "an input bound to null");
    expect(refused([] { Executor<length_of_id_one> ex; ex.prepare(6); }, "size 6 is not a multiple of 4"), "the constraints of a symbol of id 1");
    expect(refused(
               [&] {
                   Executor<long_comb> ex;
                   Executor<long_comb> moved = std::move(ex);
                   ex.input("x") = x.data();
                   ex.run();
               },
               "heinzel::Executor: this one was moved from"),
           "a moved-from Executor run");
    expect(refused([&] { Executor<streamed_comb> ex; ex.input("x") = x.data(); ex.prepare(16); ex.run(16); },
                   "heinzel::Executor: a Param was not set"), "a Param never set");
    const auto prepared = [&](std::int64_t max_frames, std::int64_t frames) {
        Executor<streamed_comb> ex;
        ex.input("x") = x.data();
        ex.param("gain") = 1.0;
        ex.prepare(max_frames);
        ex.run(frames);
    };
    expect(refused([&] { prepared(16, 20); }, "more frames than prepare made room for"), "more frames than prepared");
    expect(refused([&] { prepared(16, 10); }, "size 10 is not a multiple of 4"), "frames against the Length's multiple");
    expect(refused([&] { prepared(2, 2); }, "size 2 is below the minimum 4"), "prepare against the Length's minimum");
    expect(refused([&] { Executor<streamed_comb> ex; ex.input("x") = x.data(); ex.param("gain") = 1.0; ex.run(8); },
                   "heinzel::Executor: prepare first"),
           "a run before prepare");
    // A symbol no declaration constrains: at least one sample, and nothing more.
    expect(refused([] { Executor<streamed_einsum> ex; ex.prepare(0); }, "at least one sample"), "prepare(0) with no declaration");
#endif
}

// The programs only this test's own Executors run (the others run in `reference` too), held to what the runner accepts.
template <auto... Build>
constexpr bool kAccepted = ((loops::accepted<kCompiled<Build>.program>() && loops::outputs_whole(kCompiled<Build>.program)) && ...);
static_assert(kAccepted<with_an_unread_input, fir, knob_oscillator, length_of_id_one>);

} // namespace

int main() {
    expect(comb_by_name(), "the comb, bound by name");
    expect(moved_after_binding(), "an Executor moved after binding");
    expect(unread_input_left_unbound(), "an unread input left unbound");
    expect(in_place(), "one buffer as the input and the output");
    expect(runs_shorter_than_prepared(), "runs shorter than prepared, each over its own frames");
    expect(an_output_left_unbound(), "an output left unbound");
    expect(blocks_of_a_length(), "blocks of several lengths equal one run over all of them");
    expect(a_declared_length_in_blocks(), "a declared length driven by a knob, in blocks, equals one run");
    refusals();
    expect(agrees<corpus::elem>(1000) && agrees<corpus::elem>(64), "elem at two lengths");
    expect(agrees<corpus::rowsum<16>>(37), "rowsum: a Length of id 1");
    expect(agrees<corpus::matmul<8, 5, 3>>() && agrees<corpus::lag_of_a_lag>(), "a contraction, a lag of a lag");
    expect(agrees<[] { return corpus::one_pole_bank(4, 0.5, 64); }>(64), "a recurrence over a Length, built by a lambda");
    expect(agrees<corpus::fir_over_a_feed>() && agrees<corpus::delay_line>() && agrees<corpus::padded>(), "a feed's past, a delay line, a value in parts");
    expect(agrees<comb>() && agrees<streamed_comb>(12) && agrees<streamed_comb>(64), "the comb, literal and streamed");
    expect(agrees<signal_and_tensor>() && agrees<streamed_einsum>(9), "a timeless output beside a signal, an F64 param; a streamed einsum");
    expect(agrees<[] { return scaled_by(0.75); }>(), "a graph with a parameter");
    if (g_wrong != 0) {
        std::printf("executor_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("executor_test: Executor runs what loops::run runs, bit for bit\n");
    return 0;
}
