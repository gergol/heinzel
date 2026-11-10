#pragma once

// `Executor`: a graph function compiled while this program compiles, bound by name, and run.
//
//     Executor<comb> comb;
//     comb.input("x") = x.data();
//     comb.output("y") = y.data();
//     comb.run();

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

#include "heinzel/compile.hpp"
#include "heinzel/loops/layout.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/loops/streams.hpp"
#include "heinzel/loops/workspace.hpp"
#include "heinzel/ports.hpp"
#include "heinzel/ranges/interval.hpp"
#include "heinzel/sema/resolve_extent.hpp"
#include "heinzel/support/fail.hpp"

// A run is the runner inlined into its caller, as `loops::run` is. Left out of line, one function shared by every call
// cost each run about 1.2 ns more (a 64-sample graph: 5.1 ns against 3.9) and a one-site binary 60–90 B; inlined, each
// call site carries the runner (0.5–2.4 KB a site on the zero-overhead workloads), so a program that runs one graph
// from several places wraps `run` in a function of its own (Gerald, 2026-10-05: speed first; `Plan/18-zero-copy/02`).
// Forced only when optimizing, as `HEINZEL_LOOPS_INLINE` is.
#if defined(__OPTIMIZE__)
#define HEINZEL_RUN_INLINE [[gnu::always_inline]]
#else
#define HEINZEL_RUN_INLINE
#endif

namespace heinzel {

// The graph is analysed, planned and lowered at compile time (`try_compile`). A graph the analysis or the planner
// refuses, or one over a capacity, is one build error whose message is the reason (C++26; on the C++23 floor a fixed
// text, and `try_compile(graph)` gives the reason). A graph function that stops while it runs (a `fail`, as an Output
// assigned twice does) is one error too, whose notes give the reason and the line, on both. A port name is checked at
// compile time too. A run needs a buffer for every input the program reads and a value for every Param. An output is
// written straight into the buffer bound to it, unless it keeps a past (it is copied out of the workspace after the
// run) or its buffer overlaps an input the program reads or another output (in place: it is written aside and copied,
// so every binding means what it says). What a run needs is read off the compile result while compiling, so none of it
// reaches an optimized binary; `compile(graph)` shows the result.
//
// Every run carries state: a recurrence, a delay, an input read in its past continue where the run before
// stopped, so a signal run in blocks gives the numbers of one run over the whole. `reset()` starts over from
// silence.
//
// What a run is given is checked in a debug build only (`Checked`, off under `NDEBUG`, as `assert` is): every input it
// reads bound, every Param set, `prepare` before a run and before `reset`, the frames in range and within the
// `Length`'s constraints; and against their ranges (Plan/26-value-ranges/04), a Param when it is set, an input's
// numbers before each run, and after it each promise the proof could not prove, which the program writes as an output
// of its own (`#k`). So a debug and a release build plan different programs, as different types: a unit of one never
// shares an `Executor` with a unit of the other. A buffer's element type is checked in every build.
//
// It plans for the machine `M` (`Executor<comb, zen2>`), `default_machine` when none is named. It takes the
// graph as its function, never as a value: a grown class-type template argument is not passed by value
// (clang miscompiles it), so the graph is made inside the class.
template <auto Build, const Machine& M = default_machine, bool Checked = detail::kChecked>
class Executor {
    static_assert(std::is_same_v<decltype(Build()), Graph>, "heinzel::Executor: the graph function returns a Graph");

    // Whether the graph function runs to its end. One that stops while it runs (a `fail`, or the step or depth limit,
    // which count here the function alone) is no constant expression, and every member read off `result` would fail
    // again (about 20 errors in some 340 lines): asked here, that is `false`, `result` a refusal, and `checks()` runs
    // it once more as the one error; a limit reached later, while `try_compile` plans, still fails each member. So a
    // graph function runs twice while compiling, a few per cent of its planning; no route runs it once (Decision 77).
    static constexpr bool built = requires { typename std::bool_constant<(Build(), true)>; };
    static constexpr CompileResult result =
        built ? try_compile(Build(), M, Bindings{}, Checked)
              : CompileResult{"heinzel::Executor: the graph function stopped while it ran; the error below says why"};
    static constexpr bool refused = !result.refusal.view().empty();
    static constexpr const Graph& graph = result.compiled.graph;
    static constexpr const loops::Program& program = result.compiled.program;
    static constexpr loops::StreamSymbol symbol = loops::stream_symbol(program);
    static constexpr std::string_view not_run = detail::why_not_run(program);

public:
    // The graph has a `Length`: `prepare(max_frames)`, then `run(frames)`. Otherwise `run()`.
    static constexpr bool has_length = symbol.id >= 0;

private:
    // A graph this class runs: not refused, and none of `why_not_run`'s reasons. Its members exist only then, and every
    // member function asks it first, so a graph it refuses is one error.
    static constexpr bool usable = !refused && not_run.empty();

    // ---- what a run needs, read off the compile result while compiling ----

    static constexpr std::size_t kInputs = usable ? graph.inputs.size() : 0;
    static constexpr std::size_t kOutputs = usable ? graph.outputs.size() : 0;
    static constexpr std::size_t kChecks = usable ? result.ranges.promises.size() : 0; // the last outputs, `#k`
    static constexpr std::array<bool, kMaxIO> kRead = detail::inputs_read(program);
    static constexpr auto kInputType = detail::value_types<kInputs>(graph, graph.inputs);
    static constexpr auto kOutputType = detail::value_types<kOutputs>(graph, graph.outputs);
    static constexpr bool is_param(std::size_t i) { return detail::is_param(graph, graph.inputs[i]); }

    // The Params: each one's ordinal among them and its place in `params_`, the F64s first so that each stands aligned.
    static constexpr std::size_t kParams = detail::param_count(graph, kInputs);
    using ParamPlaces = detail::ParamPlaces<kInputs>;
    static constexpr ParamPlaces kParamPlaces = detail::param_places(graph, kInputType);
    // ...and those a run reads, as a mask of `set_`.
    static constexpr detail::Bits<kParams> kParamsRead = [] {
        detail::Bits<kParams> mask{};
        if constexpr (kParams > 0) {
            for (std::size_t i = 0; i < kInputs; ++i) {
                mask = is_param(i) && kRead[i] ? mask | detail::bit<kParams>(kParamPlaces.ordinal[i]) : mask;
            }
        }
        return mask;
    }();

    static constexpr const loops::Buffer& output_buffer(std::size_t o) { return program.buffers[static_cast<std::size_t>(program.outputs[o])]; }

    // Each input's feed buffer in the program (-1: the program reads none), whose bytes `route` weighs.
    static constexpr auto kFeedBuffer = detail::feed_buffers<kInputs>(program);
    // The outputs written where the caller bound them: each the first to name a buffer `kInPlaceOf` holds.
    static constexpr bool writes_in_place(std::size_t o) {
        const std::int32_t b = program.outputs[o];
        bool first = loops::kInPlaceSlotOf<program>[static_cast<std::size_t>(b)] >= 0;
        for (std::size_t e = 0; e < o; ++e) {
            first = first && program.outputs[e] != b;
        }
        return first;
    }
    static constexpr std::size_t kInPlaceOutputs = [] {
        std::size_t n = 0;
        for (std::size_t o = 0; o < kOutputs; ++o) {
            n += writes_in_place(o) ? 1 : 0;
        }
        return n;
    }();

    // A `Length`'s constraints, which `prepare` and `run` hold their frames to.
    static constexpr DimConstraints kConstraints = detail::constraints_of(graph, symbol.id);

    // What a debug build checks, and the words of its failures, copied off the compile result while compiling (read
    // there, they would bring the whole result into the binary): each input's range, `ranged` where one is declared,
    // and each promise's.
    static constexpr std::array<Range, kInputs> kInputRange = detail::first<kInputs>(result.ranges.inputs);
    static constexpr std::array<Range, kChecks> kPromised = detail::first<kChecks>(result.ranges.promises);
    static constexpr bool ranged(std::size_t i) { return Checked && !ranges::is_whole(kInputRange[i]); }
    template <std::size_t I>
    static constexpr detail::Words kInputWords = detail::port_words(is_param(I), graph.inputs[I].name.view(), kInputRange[I]);
    static constexpr std::array<bool, kChecks> kByElement = detail::timeless_outputs<kChecks>(graph);
    template <std::size_t K>
    static constexpr detail::Words kPromiseWords = detail::promise_words(K, kByElement[K], kPromised[K]);

public:
    Executor() {
        checks();
        if constexpr (usable) {
            point_params();
        }
        if constexpr (kChecks > 0 && !has_length) {
            make_checks();
            route();
        }
    }

    // A move takes the workspace; the source keeps its Params and bindings as they were and an empty workspace, so that
    // a run of it is a run of an `Executor` never prepared; but an arena inside the workspace is copied, and the source
    // runs on from where the move left both.
    Executor(Executor&& from) noexcept
        : work_(static_cast<Work&&>(from.work_)), feeds_(from.feeds_), sinks_(from.sinks_), params_(from.params_), set_(from.set_),
          routed_(from.routed_), checks_(static_cast<Checks&&>(from.checks_)) {
        point_params();
        moved_checks(from);
    }
    Executor& operator=(Executor&& from) noexcept {
        if (this != &from) {
            work_ = static_cast<Work&&>(from.work_);
            feeds_ = from.feeds_, sinks_ = from.sinks_, params_ = from.params_, set_ = from.set_, routed_ = from.routed_;
            checks_ = static_cast<Checks&&>(from.checks_);
            point_params();
            moved_checks(from);
        }
        return *this;
    }

    // `input("x") = data`: where the program reads x from.
    struct Feed {
        Executor* ex;
        std::size_t port;
        template <class V>
        void operator=(const V* data) const {
            ex->bind(ex->feeds_, kInputType, port, data);
        }
    };
    // `output("y") = data`: where each run's results land.
    struct Sink {
        Executor* ex;
        std::size_t port;
        template <class V>
        void operator=(V* data) const {
            static_assert(!std::is_const_v<V>, "heinzel::Executor: an output buffer is written to, so it is not const");
            if constexpr (!std::is_const_v<V>) { // (so the assert is the one error)
                ex->bind(ex->sinks_, kOutputType, port, data);
            }
        }
    };
    // `param("gain") = 0.5`: its value for the following runs, held in the port's element type.
    struct Knob {
        Executor* ex;
        std::size_t port;
        void operator=(double value) const { ex->set_param(port, value); }
    };

    Feed input(PortName<result, Port::Input> p) { return {this, p.index}; }
    Sink output(PortName<result, Port::Output> p) { return {this, p.index}; }
    Knob param(PortName<result, Port::Param> p) { return {this, p.index}; }

    // One run of a graph whose shapes are literal.
    HEINZEL_RUN_INLINE void run() {
        static_assert(refused || !has_length, "heinzel::Executor: this graph has a Length: prepare, then run(frames)");
        if constexpr (usable && !has_length) {
            if constexpr (Checked) {
                if (kKeeps && work_.arena() == nullptr) {
                    fail("heinzel::Executor: this one was moved from");
                }
            }
            run_bound(0);
        }
    }

    // A graph with a `Length`: storage for up to `max_frames`, and a fresh start.
    void prepare(std::int64_t max_frames) {
        static_assert(refused || has_length, "heinzel::Executor: this graph has no Length: run()");
        if constexpr (usable && has_length) {
            if constexpr (Checked) {
                fits(max_frames);
            }
            work_ = Work(max_frames);
            if constexpr (kChecks > 0) {
                make_checks();
            }
            route();
        }
    }

    // One run of `frames ≤ max_frames` samples.
    HEINZEL_RUN_INLINE void run(std::int64_t frames) {
        static_assert(refused || has_length, "heinzel::Executor: this graph has no Length: run()");
        if constexpr (usable && has_length) {
            if constexpr (Checked) {
                if (work_.built_for() == 0) { // (made for a length, or not: a program that keeps nothing has no arena)
                    fail("heinzel::Executor: prepare first");
                }
                if (frames > work_.built_for()) {
                    fail("heinzel::Executor: more frames than prepare made room for");
                }
                fits(frames);
            }
            work_.set_length(frames);
            run_bound(frames);
        }
    }

    // Forget the state earlier runs left: back to silence.
    void reset() {
        if constexpr (usable) {
            if constexpr (Checked && has_length) {
                if (work_.built_for() == 0) {
                    fail("heinzel::Executor: prepare first");
                }
            } else if constexpr (Checked) {
                if (kKeeps && work_.arena() == nullptr) {
                    fail("heinzel::Executor: this one was moved from");
                }
            }
            work_.reset();
        }
    }

private:
    // The graph-level checks, in the constructor: at class scope a failure would mark the whole class invalid, and
    // every later use of it would add an error of its own.
    static constexpr void checks() {
        if constexpr (!built) {
            static_assert((Build(), true)); // the one error: its notes give the reason and the line
        }
#if __cplusplus > 202302L
        static_assert(!built || !refused, result.refusal.view());
        static_assert(refused || not_run.empty(), not_run);
#else
        static_assert(!built || !refused, "heinzel::Executor: the graph does not compile; try_compile(graph) says why");
        static_assert(refused || not_run != detail::kOffTime, "heinzel::Executor: at most one symbol, on the time axis");
        static_assert(refused || not_run != detail::kOffStream, "heinzel::Executor: this graph's state does not run along the stream");
#endif
    }

    using Work = std::conditional_t<usable, loops::Workspace<program>, detail::Nothing>;
    // A static program's arena has bytes, so that "moved from" can be told (one that keeps none, or whose pieces hold no
    // byte, has no arena).
    static constexpr bool kKeeps = usable && !has_length && loops::kPlacementOf<program, 1, 0>.size > 0;

    // A length the program's symbol allows: its declaration's constraints, or at least one sample.
    static void fits(std::int64_t frames) {
        if (const Diagnostic d = check_dimension_size(kConstraints, frames); !d.ok) {
            fail(d.message.view());
        }
    }

    // A port bound: its buffer's element type checked (in every build), then where each output is written decided again.
    template <class Ports, std::size_t N, class V>
    void bind(Ports& ports, const std::array<ElemType, N>& type, std::size_t port, V* data) {
        if constexpr (usable) {
            if (detail::element_type_of<std::remove_const_t<V>>() != type[port]) {
                fail("heinzel::Executor: the buffer's element type is not the port's");
            }
            ports[port] = data;
            route();
        }
    }

    // Which outputs are written where the caller bound them (`Plan/18-zero-copy/02`): each in-place output whose bytes
    // overlap no input the program reads and no other bound output; the others are written into the workspace and
    // copied after the run (`routed_`), so that any binding, in place included, means what it meant. Decided at every
    // binding and at `prepare`, a stream program's bytes at its most frames.
    void route() {
        if constexpr (kInPlaceOutputs > 0) {
            routed_ = {};
            [&]<std::size_t... O>(std::index_sequence<O...>) {
                (route_output<O>(), ...);
            }(std::make_index_sequence<kOutputs>{});
        }
    }
    template <std::size_t O>
    void route_output() {
        if constexpr (writes_in_place(O)) {
            const Bytes mine = output_range<O>();
            const bool apart = sinks_[O] != nullptr && [&]<std::size_t... I>(std::index_sequence<I...>) {
                return (!overlap(mine, input_range<I>()) && ...);
            }(std::make_index_sequence<kInputs>{}) && [&]<std::size_t... E>(std::index_sequence<E...>) {
                return ((E == O || !overlap(mine, output_range<E>())) && ...);
            }(std::make_index_sequence<kOutputs>{});
            work_.bind(std::integral_constant<std::size_t, static_cast<std::size_t>(program.outputs[O])>{}, apart ? sinks_[O] : nullptr);
            routed_ = apart || sinks_[O] == nullptr ? routed_ : routed_ | detail::bit<kOutputs>(O);
        }
    }
    // A port's bytes, as addresses: empty for a Param, an input the program does not read, or a port bound to nothing.
    struct Bytes {
        std::uintptr_t begin{0};
        std::uintptr_t end{0};
    };
    static bool overlap(Bytes a, Bytes b) { return a.begin < b.end && b.begin < a.end; }
    static Bytes range(const void* at, std::int64_t bytes) {
        const auto begin = reinterpret_cast<std::uintptr_t>(at);
        return at == nullptr ? Bytes{} : Bytes{begin, begin + static_cast<std::uintptr_t>(bytes)};
    }
    // Buffer B's bytes in a run: its whole, or a sample per frame of a stream program's most. Constants, so that no
    // reference into the program reaches run time.
    template <std::size_t B>
    std::int64_t bytes_of_a_run() const {
        constexpr const loops::Buffer& b = program.buffers[B];
        constexpr bool streamed = b.shape.rank >= 1 && is_symbol(extent_at(b.shape, b.shape.rank - 1));
        constexpr std::int64_t sample = loops::bytes_per_sample(b);
        if constexpr (streamed) {
            return sample * work_.built_for();
        } else {
            constexpr std::int64_t whole = loops::element_count(b, loops::Sizes{}) * loops::bytes_of(b.type);
            return whole;
        }
    }
    template <std::size_t I>
    Bytes input_range() const {
        if constexpr (is_param(I) || kFeedBuffer[I] < 0) {
            return {};
        } else {
            return range(feeds_[I], bytes_of_a_run<static_cast<std::size_t>(kFeedBuffer[I])>());
        }
    }
    template <std::size_t O>
    Bytes output_range() const {
        return range(sinks_[O], bytes_of_a_run<static_cast<std::size_t>(program.outputs[O])>());
    }

    // Each Param's feed is its place in `params_`: pointed at once, and again after a move.
    void point_params() {
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (point_param<I>(), ...);
        }(std::make_index_sequence<kInputs>{});
    }
    template <std::size_t I>
    void point_param() {
        if constexpr (is_param(I)) {
            feeds_[I] = params_.data() + kParamPlaces.at[I];
        }
    }
    void set_param(std::size_t port, double value) {
        if constexpr (usable) {
            [&]<std::size_t... I>(std::index_sequence<I...>) {
                ((port == I ? set_param_at<I>(value) : void()), ...);
            }(std::make_index_sequence<kInputs>{});
        }
    }
    // A debug build holds the number as given to the Param's range, before it is rounded to the port's type: the proof is
    // about true values.
    template <std::size_t I>
    void set_param_at(double value) {
        if constexpr (is_param(I)) {
            if constexpr (ranged(I)) {
                if (!ranges::holds(kInputRange[I], value)) {
                    detail::broken(kInputWords<I>, value);
                }
            }
            if constexpr (kInputType[I] == ElemType::F32) {
                const auto held = static_cast<float>(value);
                std::memcpy(params_.data() + kParamPlaces.at[I], &held, sizeof held);
            } else {
                std::memcpy(params_.data() + kParamPlaces.at[I], &value, sizeof value);
            }
            set_ |= detail::bit<kParams>(kParamPlaces.ordinal[I]);
        }
    }

    HEINZEL_RUN_INLINE void run_bound(std::int64_t frames) {
        if constexpr (Checked) {
            [&]<std::size_t... I>(std::index_sequence<I...>) {
                ((kRead[I] && !is_param(I) && feeds_[I] == nullptr ? fail("heinzel::Executor: an input has no buffer") : void()), ...);
                (check_input<I>(frames), ...);
            }(std::make_index_sequence<kInputs>{});
            if constexpr (kParams > 0) {
                if ((set_ & kParamsRead) != kParamsRead) {
                    fail("heinzel::Executor: a Param was not set");
                }
            }
        }
        if constexpr (kParams > 0) {
            // The Params copied into this frame: no pointer the run stores through can point into a fresh local, so
            // clang reads each Param once a run, where a store into an output of their type kept it in the loop.
            alignas(8) std::array<std::byte, kParamPlaces.bytes> params = params_;
            std::array<const void*, kInputs> feeds = feeds_;
            [&]<std::size_t... I>(std::index_sequence<I...>) {
                ((is_param(I) ? void(feeds[I] = params.data() + kParamPlaces.at[I]) : void()), ...);
            }(std::make_index_sequence<kInputs>{});
            loops::run<program>(feeds, work_);
        } else {
            loops::run<program>(feeds_, work_);
        }
        [&]<std::size_t... O>(std::index_sequence<O...>) {
            (copy_out<O>(frames), ...);
        }(std::make_index_sequence<kOutputs>{});
        if constexpr (kChecks > 0) {
            [&]<std::size_t... K>(std::index_sequence<K...>) {
                (check_promise<K>(frames), ...);
            }(std::make_index_sequence<kChecks>{});
        }
    }

    // Output O's bytes of this run into its buffer, if it has one and the run did not write them there: a sample each
    // frame, or the whole of a timeless one; a constant's one sample as often.
    template <std::size_t O>
    void copy_out(std::int64_t frames) {
        constexpr const loops::Buffer& buffer = output_buffer(O);
        constexpr bool streamed = buffer.shape.rank >= 1 && is_symbol(extent_at(buffer.shape, buffer.shape.rank - 1));
        constexpr std::int64_t sample = loops::bytes_per_sample(buffer);
        constexpr std::int64_t whole = loops::element_count(buffer, loops::Sizes{}) * loops::bytes_of(buffer.type);
        constexpr auto b = static_cast<std::size_t>(program.outputs[O]);
        if constexpr (sample == 0) {
            return; // (an output of no element has no byte to copy, and may lie in no arena)
        } else if constexpr (writes_in_place(O)) {
            if ((routed_ & detail::bit<kOutputs>(O)) == detail::Bits<kOutputs>{}) {
                return; // written where it is bound, or bound nowhere
            }
        } else if (sinks_[O] == nullptr) {
            return;
        }
        const std::byte* from = work_.bytes_of(std::integral_constant<std::size_t, b>{});
        if constexpr (buffer.home == loops::Home::Constant) {
            const std::int64_t samples = streamed ? frames : whole / sample;
            for (std::int64_t s = 0; s < samples; ++s) {
                std::memcpy(static_cast<std::byte*>(sinks_[O]) + s * sample, from, static_cast<std::size_t>(sample));
            }
        } else {
            std::memcpy(sinks_[O], from, static_cast<std::size_t>(streamed ? sample * frames : whole));
        }
    }

    // ---- a debug build's checks of ranges (Plan/26-value-ranges/04) ----

    // Buffer B's numbers in a run, and in a sample: a sample's each frame, or the whole of a timeless one, as `copy_out`
    // counts them.
    template <std::size_t B>
    static constexpr std::int64_t kSample = loops::bytes_per_sample(program.buffers[B]) / loops::bytes_of(program.buffers[B].type);
    template <std::size_t B>
    static std::int64_t numbers_of_a_run(std::int64_t frames) {
        constexpr const loops::Buffer& b = program.buffers[B];
        if constexpr (b.shape.rank >= 1 && is_symbol(extent_at(b.shape, b.shape.rank - 1))) {
            return kSample<B> * frames;
        } else {
            return loops::element_count(b, loops::Sizes{});
        }
    }

    // Input I's numbers this run, where it has a range and the run reads it, named by element.
    template <std::size_t I>
    void check_input(std::int64_t frames) const {
        if constexpr (!is_param(I) && kRead[I] && kFeedBuffer[I] >= 0 && ranged(I)) {
            constexpr auto b = static_cast<std::size_t>(kFeedBuffer[I]);
            detail::hold(static_cast<const detail::Number<kInputType[I]>*>(feeds_[I]), numbers_of_a_run<b>(frames), 1, kInputRange[I],
                         kInputWords<I>, detail::kIndexEnd);
        }
    }
    // Promise K's output after the run, as its buffer holds it, named by sample, or by element where its value has no
    // time axis; a moved-from `Executor` has none.
    template <std::size_t K>
    void check_promise(std::int64_t frames) const {
        constexpr std::size_t O = kOutputs - kChecks + K;
        constexpr auto b = static_cast<std::size_t>(program.outputs[O]);
        const auto* y = static_cast<const detail::Number<kOutputType[O]>*>(sinks_[O]);
        detail::hold(y, y == nullptr ? 0 : numbers_of_a_run<b>(frames), kByElement[K] ? 1 : kSample<b>, kPromised[K], kPromiseWords<K>, ": ");
    }

    // The promises' buffers, the sinks of `#k`, owned here: made for a run (a stream program's at its most frames) by the
    // constructor or `prepare`, and pointed at again after a move, which leaves the moved-from one none.
    using Checks = std::conditional_t<(kChecks > 0), std::array<std::unique_ptr<std::byte[]>, kChecks>, detail::Nothing>;
    void make_checks() {
        [&]<std::size_t... K>(std::index_sequence<K...>) {
            ((checks_[K] = std::make_unique<std::byte[]>(static_cast<std::size_t>(
                  bytes_of_a_run<static_cast<std::size_t>(program.outputs[kOutputs - kChecks + K])>()))),
             ...);
        }(std::make_index_sequence<kChecks>{});
        point_checks();
    }
    void point_checks() {
        for (std::size_t k = 0; k < kChecks; ++k) {
            sinks_[kOutputs - kChecks + k] = checks_[k].get();
        }
    }
    void moved_checks(Executor& from) {
        if constexpr (kChecks > 0) {
            point_checks();
            route();
            from.point_checks();
            from.route();
        }
    }

    [[no_unique_address]] Work work_{};
    [[no_unique_address]] std::array<const void*, kInputs> feeds_{};
    [[no_unique_address]] std::array<void*, kOutputs> sinks_{};
    alignas(kParamPlaces.bytes > 0 ? 8 : 1) [[no_unique_address]] std::array<std::byte, kParamPlaces.bytes> params_{};
    [[no_unique_address]] detail::Bits<kParams> set_{};
    [[no_unique_address]] std::conditional_t<(kInPlaceOutputs > 0), detail::Bits<kOutputs>, detail::Nothing> routed_{}; // copied after a run
    [[no_unique_address]] Checks checks_{};
};

} // namespace heinzel
