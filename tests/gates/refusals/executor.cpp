// USES OF `Executor` THAT MUST NOT COMPILE (Plan/archive/14-dsl/04 §9).
//
// Each case is built as its own target (`-DCASE=<k>`, tests/CMakeLists.txt) that must fail to build for
// its own reason, matched on the message:
//   1–3. a misspelt input, output and param;
//   4–5. `input()` on a Param, `param()` on a signal;
//   6.   a graph the analysis refuses: its reason (C++26), a fixed text (C++23);
//   7–9. `prepare` and `run(frames)` on a graph without a `Length`, `run()` on one with;
//   10–12. a second symbol beside the stream, a symbol only on a fold's axis, and a symbol off time;
//   13–14. a refused graph with ports bound, reset and run, and one held in an `std::optional`: the
//          refusal is the only error;
//   15–17. state that does not run along the stream: a one-sample recurrence and a lagged one-sample input,
//          each broadcast over a longer signal, and two literal streams of different lengths that both carry
//          state (refused conservatively, DS-14);
//   18.  an output bound to a const buffer;
//   19.  `Executor::graph`, which is gone: an `Executor` exposes nothing of its compile result (ZO-7);
//   20.  `#0`, a debug build's check of a promise (Plan/26-value-ranges/04): no port of the caller's;
//   21.  so a gradient named `#d` is refused, as an `Output`'s name is, where it could never be bound by name;
//   22–24. a graph function that does not run to its end is one error with its reason, not one for each member read
//          off the compile result (Decision 77): a graph over a capacity (built with -DHEINZEL_MAX_VALUES=4: the
//          analysis's refusal, C++26, a fixed text, C++23), an Output assigned twice and a graph function's own `fail`
//          (the reason in a note, on both);
//   25.  a graph the loop tree does not run: the refusal names the operation and its operands' shapes (C++26), a
//        fixed text (C++23).
#include <optional>

#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/gradients.hpp"

#include "hand_built_graph.hpp"

using namespace heinzel;

constexpr Index n{};

constexpr Graph comb() {
    Input x{"x", Signal{64}};
    Output y{"y"};
    y[n] = x[n] + 0.5 * y[n - 3];
    return y;
}

constexpr Graph streamed() {
    Input x{"x", Signal{Length{}}};
    Param gain{"gain"};
    Output y{"y"};
    y = x * gain;
    return y;
}

// Shapes that disagree: the analysis refuses the graph.
constexpr Graph broken() {
    Input a{"a", Signal{3, 8}};
    Input b{"b", Signal{4, 8}};
    Output y{"y"};
    y = a + b;
    return y;
}

// y = x + x beside z = side * side, `side` sized by a symbol of its own.
constexpr Graph side_sized() {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{Dim{0}}, ValueCategory::Input);
    const auto side = add_value(g, ElemType::F32, Shape{Dim{1}}, ValueCategory::Input);
    g.inputs.push_back(NamedValue{"x", x});
    g.inputs.push_back(NamedValue{"side", side});
    g.outputs.push_back(NamedValue{"y", add_node(g, op::Add, x, x)});
    g.outputs.push_back(NamedValue{"z", add_node(g, op::Mul, side, side)});
    return g;
}

// y = Σ over axis 0 of (x + c), c of shape {Dim 0, Dim 0}: the stream's own symbol also sizes an axis the fold
// collapses, which no store walks — the one-symbol check passes it, the time-axis rule does not.
constexpr Graph domain_sized() {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{Dim{0}}, ValueCategory::Input);
    const auto c = add_value(g, ElemType::F32, Shape{Dim{0}, Dim{0}}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(c)].constant = 1.0;
    OpAttr sum{};
    sum.kind = AttrKind::Reduce;
    sum.reduce.op = ReduceOp::Sum;
    sum.reduce.axes = Axes{0};
    g.inputs.push_back(NamedValue{"x", x});
    g.outputs.push_back(NamedValue{"y", add_node(g, op::Reduce, add_node(g, op::Add, x, c), kNoValue, kNoValue, sum)});
    return g;
}

// A symbol on an element axis: the stream is the literal 4.
constexpr Graph symbol_off_time() {
    Input x{"x", Signal{Dim{0}, 4}};
    Output y{"y"};
    y = x * 2.0;
    return y;
}

// One-sample state against a 16-sample signal: carried across runs, it would step once a run, not a sample.
constexpr Graph one_sample_recurrence() {
    Input w{"w", Signal{4, 1}};
    Input x{"x", Signal{4, 16}};
    Variable s;
    s[n] = w[n] + 0.5 * s[n - 1];
    Output y{"y"};
    y = x * s;
    return y;
}
constexpr Graph lagged_one_sample_input() {
    Input w{"w", Signal{4, 1}};
    Input x{"x", Signal{4, 16}};
    Output y{"y"};
    y = x * w[n - 1];
    return y;
}
constexpr Graph two_literal_streams() {
    Input a{"a", Signal{16}};
    Input b{"b", Signal{32}};
    Output y{"y"};
    Output z{"z"};
    y[n] = a[n] + 0.5 * y[n - 1];
    z[n] = b[n] + 0.5 * z[n - 1];
    return finish(y, z);
}

// A promise the proof cannot prove, which a debug build checks as the output `#0`.
constexpr Graph promised() {
    Input x{"x", Signal{64}};
    Output y{"y"};
    y = (x * 2.0).promise(ge(-1), le(1));
    return y;
}

// A gradient named as a debug build names its checks.
constexpr Graph hashed_gradient() {
    Input w{"w", Signal{8}, f64};
    Input x{"x", Signal{8}, f64};
    Output loss{"loss"};
    loss = w * x;
    return gradients(loss, {Grad{"#d", w}});
}

constexpr Graph assigned_twice() {
    Input x{"x", Signal{64}};
    Output y{"y"};
    y = x * 0.5;
    y = x * 2.0;
    return y;
}

// A graph function that holds its own configuration to what it needs, as a user's may.
constexpr int kTaps = 0;
constexpr Graph checks_its_taps() {
    if (kTaps < 1) {
        fail("my filter: kTaps is at least 1");
    }
    Input x{"x", Signal{64}};
    Output y{"y"};
    y = x * static_cast<double>(kTaps);
    return y;
}

// The three largest of six voices, which the loop tree does not lower yet: no exact rank.
constexpr Graph three_largest() {
    Input x{"x", Signal{6, 64}};
    Output y{"y"};
    y = x.topk(0, 3);
    return y;
}

float buffer[64];

// Cases 1–9 and 18–21: a port misnamed or misused, a run of the wrong kind, the object misused.
void ports() {
#if CASE == 1
    Executor<comb> ex;
    ex.input("xx") = buffer;
#elif CASE == 2
    Executor<comb> ex;
    ex.output("yy") = buffer;
#elif CASE == 3
    Executor<streamed> ex;
    ex.param("gian") = 1.0;
#elif CASE == 4
    Executor<streamed> ex;
    ex.input("gain") = buffer;
#elif CASE == 5
    Executor<streamed> ex;
    ex.param("x") = 1.0;
#elif CASE == 6
    Executor<broken> ex;
#elif CASE == 7
    Executor<comb> ex;
    ex.prepare(64);
#elif CASE == 8
    Executor<comb> ex;
    ex.run(64);
#elif CASE == 9
    Executor<streamed> ex;
    ex.run();
#elif CASE == 18
    Executor<comb> ex;
    const float* read_only = buffer;
    ex.output("y") = read_only;
#elif CASE == 19
    (void)Executor<comb>::graph;
#elif CASE == 20
    Executor<promised, default_machine, true> ex;
    ex.output("#0") = buffer;
#elif CASE == 21
    Executor<hashed_gradient> ex;
#endif
}

// Cases 10–17 and 22–25: graphs the class refuses, whatever is done with them, and graph functions that stop.
void graphs() {
#if CASE == 10
    Executor<side_sized> ex;
#elif CASE == 11
    Executor<domain_sized> ex;
#elif CASE == 12
    Executor<symbol_off_time> ex;
#elif CASE == 13
    Executor<broken> ex;
    ex.input("a") = buffer;
    ex.input("b") = buffer;
    ex.output("y") = buffer;
    ex.reset();
    ex.run();
#elif CASE == 14
    std::optional<Executor<broken>> held;
    held.emplace();
    held->input("a") = buffer;
    held->run();
    std::optional<Executor<broken>> moved = std::move(held);
#elif CASE == 15
    Executor<one_sample_recurrence> ex;
#elif CASE == 16
    Executor<lagged_one_sample_input> ex;
#elif CASE == 17
    Executor<two_literal_streams> ex;
#elif CASE == 22
    Executor<comb> ex; // seven values
#elif CASE == 23
    Executor<assigned_twice> ex;
#elif CASE == 24
    Executor<checks_its_taps> ex;
#elif CASE == 25
    Executor<three_largest> ex;
#endif
}

int main() {
    ports();
    graphs();
    return 0;
}
