#pragma once

// A graph run the way users run it, on `Executor`, and checked bit for bit against the reference walk.
//
// Both run the graph at `High`, which is the walk's arithmetic: every operation computed in double and
// rounded once to its result's type, in the order written. So the two agree in every bit, and a
// difference is a bug in one of them, never a tolerance. A test prints `Executor`'s numbers.
//
// `executed` does it all; a test that runs its own `Executor` asks `walked` and `require_walked`. `walked`
// holds the program `Executor` makes of the graph at `High` to what the runner accepts (`well_formed.hpp`),
// once; `held(ex)` holds the program of an `Executor` a test builds itself, at its own precision and target.
// A graph the loop tree refuses stays on the walk with its refusal pinned (`still_refused`), so the test
// fails, and the workload moves here, on the day the loop tree learns it.

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "heinzel/compile.hpp"
#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"

#include "interpret.hpp"
#include "well_formed.hpp"

namespace heinzel::testing {

// One named input's numbers, sample-major: `data[n * width + e]`. The walk reads them as doubles, an F32
// port as floats, so they are numbers a float holds.
struct NamedFeed {
    std::string_view name;
    std::variant<const double*, const float*> data;
    std::int32_t width{1};
};

// Each output's numbers, sample-major: `[output][n * width + e]`.
using Outputs = std::vector<std::vector<double>>;

template <auto Build>
constexpr Graph at_high() {
    Graph g = Build();
    set_default_precision(g, numerics::Precision::High);
    return g;
}

// `Build`'s graph at `High`, analysed with a `Length` bound to `frames` (0: it has none).
template <auto Build>
constexpr Graph analysed(std::size_t frames) {
    Graph g = at_high<Build>();
    Bindings sizes{};
    if (frames > 0) {
        sizes.bind(Dim{kStreamLength}, static_cast<std::int32_t>(frames));
    }
    if (!infer(g, sizes).ok) {
        fail("heinzel::testing: the graph does not analyse");
    }
    return g;
}

// `Build`'s ports, in order, with their types: its graph at `High`, analysed (an `Executor` keeps none of its compile
// result).
template <auto Build>
inline constexpr Graph kPortsOf = analysed<Build>(0);

// The walk's block (the stream every port shares) and its store (a plane per number per sample).
struct Store {
    std::size_t samples{0};
    std::size_t planes{0};
};

template <auto Build, std::size_t Frames>
constexpr Store store_of() {
    const Graph g = analysed<Build>(Frames);
    const Shape& s = g.values[static_cast<std::size_t>(g.outputs[0].value)].shape;
    Store store{static_cast<std::size_t>(literal_of(extent_at(s, s.rank - 1))), 0};
    for (const Value& v : g.values) {
        store.planes += static_cast<std::size_t>(numbers_per_sample(v.shape));
    }
    return store;
}

// The feed of each of `g`'s inputs, in its order, as doubles: `width * samples` numbers each.
inline std::vector<std::vector<double>> in_order(const Graph& g, std::initializer_list<NamedFeed> named,
                                                 std::size_t samples) {
    std::vector<std::vector<double>> fed;
    for (const NamedValue& in : g.inputs) {
        const NamedFeed* f = std::ranges::find(named, in.name.view(), &NamedFeed::name);
        if (f == named.end()) {
            fail("heinzel::testing: an input has no feed");
        }
        const std::size_t count = static_cast<std::size_t>(f->width) * samples;
        fed.push_back(std::visit([&](const auto* p) { return std::vector<double>(p, p + count); }, f->data));
    }
    if (fed.size() != named.size()) {
        fail("heinzel::testing: a feed names no input");
    }
    return fed;
}

// The program `Executor<Build, M, C>` holds (none: the loop tree refuses the graph), made again here, at run time, and
// held once to what the runner accepts — or the test exits, naming the rule it breaks.
template <auto Build, const Machine& M = default_machine, bool C = detail::kChecked>
void well_formed_once() {
    static const bool checked = [] {
        const std::unique_ptr<const CompileResult> compiled(new auto(try_compile(Build(), M, Bindings{}, C)));
        if (!compiled->refusal.view().empty()) {
            return true;
        }
        if (const std::expected<void, loops::Refusal> w = loops::well_formed(compiled->compiled.program); !w) {
            std::fprintf(stderr, "the program Executor runs is not well formed: %.*s\n", static_cast<int>(w.error().size()),
                         w.error().data());
            std::exit(1);
        }
        if (!loops::outputs_whole(compiled->compiled.program)) {
            std::fprintf(stderr, "the program Executor runs has an output it cannot copy out whole\n");
            std::exit(1);
        }
        return true;
    }();
    (void)checked;
}

// For a test that builds an `Executor` itself: its program, held once.
template <auto Build, const Machine& M, bool C>
void held(const Executor<Build, M, C>&) {
    well_formed_once<Build, M, C>();
}

// `Build`'s graph at `High` walked over its block, or `Frames` samples of a `Length` graph, fed `named`.
template <auto Build, std::size_t Frames = 0>
Outputs walked(std::initializer_list<NamedFeed> named = {}) {
    well_formed_once<at_high<Build>>();
    constexpr Store store = store_of<Build, Frames>();
    const Graph g = analysed<Build>(Frames);
    const std::vector<std::vector<double>> fed = in_order(g, named, store.samples);
    Inputs feeds{};
    for (const std::vector<double>& f : fed) {
        feeds.push_back(InputFeed{f.data(), static_cast<std::int32_t>(f.size() / store.samples)});
    }
    const auto signals = std::make_unique<Signals<store.samples, store.planes>>();
    run(g, feeds, *signals);

    Outputs out(g.outputs.size());
    for (std::size_t o = 0; o < out.size(); ++o) {
        const std::int32_t v = g.outputs[o].value;
        const auto width = static_cast<std::size_t>(numbers_per_sample(g.values[static_cast<std::size_t>(v)].shape));
        out[o].resize(width * store.samples);
        for (std::size_t i = 0; i < out[o].size(); ++i) {
            out[o][i] = sample_at(*signals, v, static_cast<std::int32_t>(i % width), i / width);
        }
    }
    return out;
}

// Exits naming the first number of `ran`, a run's output, that differs from the walk's in any bit.
template <class Numbers>
void require_walked(std::string_view output, const Numbers& ran, const std::vector<double>& walk) {
    if (std::size(ran) != walk.size()) {
        fail("heinzel::testing::require_walked: the run and the walk hold different counts");
    }
    std::size_t i = 0;
    for (const auto number : ran) {
        const auto x = static_cast<double>(number);
        if (std::bit_cast<std::uint64_t>(x) != std::bit_cast<std::uint64_t>(walk[i])) {
            std::fprintf(stderr, "differs: output %.*s, number %zu (sample-major): walked %.17g, Executor %.17g\n",
                         static_cast<int>(output.size()), output.data(), i, walk[i], x);
            std::exit(1);
        }
        ++i;
    }
}

// A port is named while compiling (`PortName` is consteval), so each is bound by its index in `G`, its graph.
template <class E, const Graph& G, std::size_t I>
void bind_input(E& ex, const std::vector<double>& fed, std::vector<float>& f32) {
    constexpr const NamedValue& port = G.inputs[I];
    if (G.values[static_cast<std::size_t>(port.value)].type == ElemType::F32) {
        f32.assign(fed.begin(), fed.end());
        ex.input(port.name.data_) = f32.data();
    } else {
        ex.input(port.name.data_) = fed.data();
    }
}

template <class E, const Graph& G, std::size_t O>
void bind_output(E& ex, std::vector<double>& f64, std::vector<float>& f32) {
    constexpr const NamedValue& port = G.outputs[O];
    if (G.values[static_cast<std::size_t>(port.value)].type == ElemType::F32) {
        f32.resize(f64.size());
        ex.output(port.name.data_) = f32.data();
    } else {
        ex.output(port.name.data_) = f64.data();
    }
}

// One run, `prepare(samples)` and `run(samples)` for a `Length` graph.
template <class E, const Graph& G, std::size_t... I, std::size_t... O>
void run_bound(const std::vector<std::vector<double>>& fed, std::size_t samples, Outputs& out,
               std::index_sequence<I...>, std::index_sequence<O...>) {
    E ex;
    std::vector<std::vector<float>> in(fed.size());
    std::vector<std::vector<float>> back(out.size());
    (bind_input<E, G, I>(ex, fed[I], in[I]), ...);
    (bind_output<E, G, O>(ex, out[O], back[O]), ...);
    if constexpr (E::has_length) {
        ex.prepare(static_cast<std::int64_t>(samples));
        ex.run(static_cast<std::int64_t>(samples));
    } else {
        ex.run();
    }
    for (std::size_t o = 0; o < out.size(); ++o) {
        std::copy(back[o].begin(), back[o].end(), out[o].begin());
    }
}

// `Build`'s graph at `High` run once on `Executor`, planned for `M`, fed `named`, over its block or `Frames` samples of
// a `Length` graph; its outputs, required to be the walk's.
template <auto Build, std::size_t Frames = 0, const Machine& M = default_machine>
Outputs executed(std::initializer_list<NamedFeed> named = {}) {
    using E = Executor<at_high<Build>, M>;
    well_formed_once<at_high<Build>, M>();
    constexpr std::size_t samples = store_of<Build, Frames>().samples;
    const Outputs walk = walked<Build, Frames>(named);
    Outputs out(walk.size());
    for (std::size_t o = 0; o < out.size(); ++o) {
        out[o].resize(walk[o].size());
    }
    static constexpr const Graph& g = kPortsOf<Build>;
    run_bound<E, g>(in_order(g, named, samples), samples, out, std::make_index_sequence<g.inputs.size()>{},
                    std::make_index_sequence<g.outputs.size()>{});
    for (std::size_t o = 0; o < out.size(); ++o) {
        require_walked(g.outputs[o].name.view(), out[o], walk[o]);
    }
    return out;
}

// The loop tree refuses `Build`'s graph at `High`, saying `why`. Exits when it says anything else.
template <auto Build>
void still_refused(std::string_view why) {
    const std::unique_ptr<const CompileResult> compiled(new auto(try_compile(at_high<Build>())));
    const std::string_view said = compiled->refusal.view();
    if (said != why) {
        std::fprintf(stderr, "the loop tree's answer changed (empty: it compiles; run it on Executor): \"%.*s\"\n",
                     static_cast<int>(said.size()), said.data());
        std::exit(1);
    }
}

} // namespace heinzel::testing
