// The declarations (Plan/archive/14-dsl/01 §2, 02 §3): `Input` as a tensor or a `Signal`, `Param`, `Length`,
// `Output` with `=` and `[n] =`, and `finish(a, b, …)`. Where an older spelling says the same, the
// graph is the one it builds — a tensor with the length-1 stream the old spelling wrote by hand, and
// `timeless` set, which the old spelling cannot say and which is cleared before comparing (nothing reads
// it before the analysis learns time, L4). Checked while compiling and again at run time. Every refusal
// is shown with its text; what must not compile is in `gates/refusals/index_notation.cpp`.
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"

#include "graph_hash.hpp"

using namespace heinzel;

namespace {

constexpr Index n{};
using V = detail::variable_access; // the old spelling of a Variable's steps, kept underneath

constexpr Graph without_timeless(Graph g) {
    for (Value& v : g.values) {
        v.timeless = false;
    }
    return g;
}

// ---- inputs --------------------------------------------------------------------------------------

constexpr Graph tensor_new() {
    Input w{"w", {64, 3}};
    Output y{"y"};
    y = w * 2.0;
    return y;
}
constexpr Graph tensor_old() { Expr w = detail::input("w", Shape{64, 3, 1}); return detail::finish("y", w * 2.0); }

constexpr Graph number_new() {
    Input g{"g", {}, f64};
    Output y{"y"};
    y = g * 2.0;
    return y;
}
constexpr Graph number_old() { Expr g = detail::input("g", Shape{1}, ElemType::F64); return detail::finish("y", g * 2.0); }

constexpr Graph signal_new() {
    Input x{"x", Signal{2, 64}, f64};
    Output y{"y"};
    y = x * 2.0;
    return y;
}
constexpr Graph signal_old() { Expr x = detail::input("x", Shape{2, 64}, ElemType::F64); return detail::finish("y", x * 2.0); }

constexpr Length kFrames{.multiple_of = 4, .min = 8, .expect = 512};
constexpr Graph length_new() {
    Input x{"x", Signal{2, kFrames}};
    Output y{"y"};
    y = x * 2.0;
    return y;
}
constexpr Graph length_old() {
    const DimDecl frames{0, DimConstraints{4, 8}};
    Expr x = detail::input("x", Shape{2, Extent{Dim{frames.id}}}, ElemType::F32, frames);
    return detail::finish("y", x * 2.0);
}

// A whole shape, and a symbol given as it is: neither declares a dimension.
constexpr Graph shaped_new() {
    Input x{"x", Signal{Shape{3, 64}}};
    Input z{"z", Signal{3, Dim{0}}};
    Output y{"y"};
    y = x + z;
    return y;
}
constexpr Graph shaped_old() {
    Expr x = detail::input("x", Shape{3, 64});
    Expr z = detail::input("z", Shape{3, Dim{0}});
    return detail::finish("y", x + z);
}

constexpr Graph param_new() {
    Input x{"x", Signal{64}};
    Param gain{"gain"};
    Output y{"y"};
    y = x * gain;
    return y;
}
constexpr Graph param_old() {
    Expr x = detail::input("x", Shape{64});
    Expr gain = detail::input("gain", Shape{});
    return detail::finish("y", x * gain);
}

static_assert(without_timeless(tensor_new()) == tensor_old() && tensor_new() != tensor_old());
static_assert(without_timeless(number_new()) == number_old());
static_assert(signal_new() == signal_old() && length_new() == length_old() && shaped_new() == shaped_old());
static_assert(without_timeless(param_new()) == param_old());
static_assert(tensor_new().values[0].timeless && param_new().values[1].timeless && !param_new().values[0].timeless);
static_assert(length_new().dims.size() == 1 && shaped_new().dims.size() == 0);

// ---- outputs -------------------------------------------------------------------------------------

constexpr Graph recurrence_new() {
    Input x{"x", Signal{64}};
    Output y{"y"};
    y[n] = x[n] + 0.5 * y[n - 1];
    return y;
}
constexpr Graph recurrence_old() {
    Expr x = detail::input("x", Shape{64});
    Variable y;
    V::is(y, x + 0.5 * V::previous(y, 1));
    return detail::finish("y", V::value(y));
}

// The past of an output assigned with `=` is its value's past.
constexpr Graph assigned_past_new() {
    Input x{"x", Signal{64}};
    Output y{"y"};
    y = x * 2.0;
    Output z{"z"};
    z = y[n - 1] + 1.0;
    return z;
}
constexpr Graph assigned_past_old() {
    Expr x = detail::input("x", Shape{64});
    Expr y = x * 2.0;
    return detail::finish("z", detail::delayed(y, 1) + 1.0);
}

static_assert(recurrence_new() == recurrence_old() && assigned_past_new() == assigned_past_old());

// `z = y` of a recurrence names the recurrence's value twice.
constexpr Graph copied_recurrence() {
    Input x{"x", Signal{64}};
    Output y{"y"};
    Output z{"z"};
    y[n] = x[n] + 0.5 * y[n - 1];
    z = y;
    return finish(y, z);
}
static_assert(copied_recurrence().outputs[0].value == copied_recurrence().outputs[1].value && recurrence_new().nodes.size() == copied_recurrence().nodes.size());

// Several outputs, in order, over one computation; `z = y` names y's value twice.
constexpr Graph three() {
    Input x{"x", Signal{64}};
    Output a{"a"};
    Output b{"b"};
    Output c{"c"};
    b = x * 2.0;
    a = b + 1.0;
    c = b;
    return finish(a, b, c);
}
static_assert(three().outputs.size() == 3 && three().outputs[0].name.view() == "a" && three().outputs[2].name.view() == "c");
static_assert(three().outputs[1].value == three().outputs[2].value && three().nodes.size() == 2);

// An input, or a variable read by name, as the output: its identity, one `_realize`.
constexpr Graph identity_of_an_input() {
    Input x{"x", Signal{64}};
    Output y{"y"};
    y = x;
    return y;
}
constexpr Graph identity_of_a_variable() {
    Input x{"x", Signal{64}};
    Variable v;
    Output y{"y"};
    y = v; // read by name before its definition: the variable itself
    v[n] = x[n] + 0.5 * v[n - 1];
    return y;
}
static_assert(identity_of_an_input().nodes.size() == 1 && identity_of_an_input().nodes[0].op == op::Realize);
static_assert(identity_of_an_input() == identity_of_an_input()); // the identity is made once, at the assignment
constexpr bool output_is_realize(const Graph& g) {
    for (const Node& node : g.nodes) {
        if (node.output == g.outputs[0].value) {
            return node.op == op::Realize && g.values[static_cast<std::size_t>(node.inputs[0])].category == ValueCategory::Variable;
        }
    }
    return false;
}
static_assert(output_is_realize(identity_of_a_variable()));

// An input no output reads is in the graph only when its arena joined the output's.
constexpr Graph unread_input() {
    Input a{"a", Signal{8}};
    Input b{"b", Signal{8}};
    Output y{"y"};
    y = a * 2.0;
    return y;
}
static_assert(unread_input().inputs.size() == 1 && unread_input().inputs[0].name.view() == "a");

// ---- settings ------------------------------------------------------------------------------------

constexpr Graph with_settings() {
    Graph g = tensor_new();
    set_default_precision(g, numerics::Precision::High);
    return g;
}
static_assert(with_settings().default_precision == numerics::Precision::High);
// ...which is what a `BuildContext` carried, with a sample rate that went with `Graph::sample_rate` (Decision 59): the
// setter replaces it (Plan/archive/14-dsl/02 §6). The context is deleted (L9); the graph it finished, `w * 2.0` at
// `High`, is pinned by its hash, recorded at 22b7c9ef, re-pinned when the hash stopped mixing a value's units and
// the sample rate (Decision 59), and again when `ValueCategory::Output` went (Decision 60), the graph field for field
// the same.
static_assert(hash_graph(without_timeless(with_settings())) == 0xa82191308d32317eull);

// ---- refusals ------------------------------------------------------------------------------------

int g_wrong = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        ++g_wrong;
        std::printf("  FAILED: %s\n", what);
    }
}

template <class Build>
bool refused(Build build, std::string_view text) {
    try {
        (void)build();
    } catch (const heinzel::error& e) {
        return e.message().find(text) != std::string_view::npos;
    }
    return false;
}

void input_refusals() {
    const std::string long_name(64, 'x');
    expect(refused([] { return Signal{Shape{}}; }, "heinzel: a signal has a length"), "Signal{Shape{}}");
    expect(refused([] { return Signal{1, 2, 3, 4, 5, 6, 7}; }, "more axes than kMaxRank"), "a signal of more than kMaxRank extents");
    expect(refused([] { return Input{"w", {1, 2, 3, 4, 5, 6}}; }, "heinzel: a tensor has at most kMaxRank - 1 axes"),
           "a tensor with no slot for its stream");
    expect(refused([] { return Input{"w", {-2, 4}}; }, "heinzel: a tensor's extent is a whole number") &&
               refused([] { return Input{"w", {4, -1}}; }, "heinzel: a tensor's extent is a whole number") &&
               refused([] { return Input{"w", {Dim{0}, 4}}; }, "heinzel: a tensor's extent is a whole number") &&
               refused([] { return Input{"w", {Dim{-3}, 4}}; }, "heinzel: a Dim's id is out of range") &&
               !refused([] { return Input{"w", {0, 4}}; }, "heinzel"),
           "a tensor's extents: a negative one or a symbol refused, an axis of none kept");
    expect(refused([] { return Signal{kFrames, 64}; }, "heinzel: a Length is a signal's last extent"), "a Length not last");
    expect(refused(
               [] {
                   Input a{"a", Signal{Length{.min = 8}}};
                   Input b{"b", Signal{Length{.min = 16}}};
                   Output y{"y"};
                   y = a + b;
                   return Graph(y);
               },
               "declared twice with different constraints"),
           "two Lengths that disagree");
    expect(refused([&] { return Input{long_name, Signal{8}}; }, "longer than kMaxNameLength"), "an input's name too long");
    expect(refused([&] { return Output{long_name}; }, "longer than kMaxNameLength"), "an output's name too long");
    // An extent is at least one: a negative integer would read as a symbol, a wide one would wrap to one.
    expect(refused([] { return Signal{4, -2}; }, "heinzel: a signal's extent is a whole number of at least one"), "a negative extent");
    expect(refused([] { return Signal{0}; }, "a whole number of at least one"), "an extent of zero");
    expect(refused([] { return Signal{std::size_t{4294967294u}}; }, "a whole number of at least one"), "an extent too wide");
    // A `Dim` or an `Extent` given as it is: an id the graph can record, and a literal of at least one — through
    // a `Shape` too.
    expect(refused([] { return Signal{Dim{-1}}; }, "heinzel: a Dim's id is from 0 to kMaxDimDecls - 1"), "a Dim of id -1");
    expect(refused([] { return Signal{Dim{static_cast<std::int32_t>(kMaxDimDecls)}}; },
                   "heinzel: a Dim's id is from 0 to kMaxDimDecls - 1; rebuild with -DHEINZEL_MAX_DIM_DECLS=N"),
           "a Dim past the ids");
    expect(refused([] { return Signal{Extent{0}}; }, "a whole number of at least one"), "an Extent of zero");
    expect(refused([] { return Signal{Shape{2, Dim{static_cast<std::int32_t>(kMaxDimDecls)}}}; }, "a Dim's id is from 0"),
           "a Shape carrying a Dim past the ids");
}

// Where an output is given its value: once, by one spelling, and not after it was read.
void assignment_refusals() {
    const auto read_then_assign = [] {
        Input x{"x", Signal{8}};
        Output y{"y"};
        Expr late = y[n - 1];
        y = x * 2.0;
        return 0;
    };
    const auto read_now_then_assign = [] {
        Input x{"x", Signal{8}};
        Output y{"y"};
        Expr now = y;
        y = x * 2.0;
        return 0;
    };
    const auto both = [] {
        Input x{"x", Signal{8}};
        Output y{"y"};
        y = x * 2.0;
        y[n] = x[n];
        return 0;
    };
    const auto defined_then_assigned = [] {
        Input x{"x", Signal{8}};
        Output y{"y"};
        y[n] = x[n];
        y = x * 2.0;
        return 0;
    };
    const auto twice = [](bool by_index) {
        Input x{"x", Signal{8}};
        Output y{"y"};
        by_index ? void(y[n] = x[n]) : void(y = x * 2.0);
        by_index ? void(y[n] = x[n] * 3.0) : void(y = x * 3.0);
        return 0;
    };
    expect(refused(read_then_assign, "heinzel: an Output was read before it was assigned; use [n] = for a recurrence"),
           "a read of the past before =");
    expect(refused(read_now_then_assign, "heinzel: an Output was read before it was assigned"), "a present read before =");
    expect(refused(both, "heinzel: an Output is assigned by = or by [n] =, not both"), "= then [n] =");
    expect(refused(defined_then_assigned, "heinzel: an Output is assigned by = or by [n] =, not both"), "[n] = then =");
    expect(refused([&] { return twice(true); }, "heinzel: an Output is assigned once"), "[n] = twice");
    expect(refused([&] { return twice(false); }, "heinzel: an Output is assigned once"), "= twice");
}

// What a finished graph must have: every output defined, every variable it reads defined, no literal output.
void definition_refusals() {
    const auto never = [] {
        Output y{"y"};
        return Graph(y);
    };
    // Read, never defined, and not among the outputs finished: an Output's past, a Variable by name.
    const auto past_never_defined = [] {
        Input x{"x", Signal{8}};
        Output a{"a"};
        Output b{"b"};
        b = a[n - 1] + x;
        return finish(b);
    };
    const auto variable_never_defined = [] {
        Variable v;
        Output y{"y"};
        y = v;
        return Graph(y);
    };
    expect(refused(never, "heinzel: an Output was never assigned"), "an output never assigned");
    expect(refused(past_never_defined, "heinzel: a Variable or an Output is read but never defined"), "an Output's past, never defined");
    expect(refused(variable_never_defined, "heinzel: a Variable or an Output is read but never defined"), "a Variable, never defined");
    expect(refused([] { Output y{"y"}; y = constant(1.0); return Graph(y); }, "heinzel: an output is computed or an input"),
           "a literal as an output");
}

void name_refusals() {
    const auto two_inputs = [] {
        Input a{"x", Signal{8}};
        Input b{"x", Signal{8}};
        Output y{"y"};
        y = a + b;
        return Graph(y);
    };
    const auto two_outputs = [] {
        Input x{"x", Signal{8}};
        Output a{"y"};
        Output b{"y"};
        a = x * 2.0;
        b = x * 3.0;
        return finish(a, b);
    };
    const auto in_and_out = [] {
        Input x{"x", Signal{8}};
        Output y{"x"};
        y = x * 2.0;
        return Graph(y);
    };
    expect(refused(two_inputs, "heinzel: two ports share a name"), "two inputs of one name");
    expect(refused(two_outputs, "heinzel: two ports share a name"), "two outputs of one name");
    expect(refused(in_and_out, "heinzel: two ports share a name"), "an input and an output of one name");
}

} // namespace

int main() {
    // The same functions at run time.
    expect(without_timeless(tensor_new()) == tensor_old() && without_timeless(number_new()) == number_old(), "tensors");
    expect(signal_new() == signal_old() && length_new() == length_old() && shaped_new() == shaped_old(), "signals");
    expect(without_timeless(param_new()) == param_old(), "a Param");
    expect(recurrence_new() == recurrence_old() && assigned_past_new() == assigned_past_old(), "outputs");
    expect(three() == three() && output_is_realize(identity_of_a_variable()), "several outputs, an identity");
    expect(copied_recurrence().outputs[0].value == copied_recurrence().outputs[1].value, "z = y of a recurrence");
    // A tensor is not the signal of the same shape: the artifact hash sees the declaration.
    expect(hash_graph(tensor_new()) != hash_graph(tensor_old()) && hash_graph(without_timeless(tensor_new())) == hash_graph(tensor_old()),
           "the hash sees timeless");
    Graph settled = with_settings();
    expect(infer(settled).ok && settled.values[static_cast<std::size_t>(settled.outputs[0].value)].precision == numerics::Precision::High,
           "set_default_precision reaches the analysis");
    input_refusals();
    assignment_refusals();
    definition_refusals();
    name_refusals();
    if (g_wrong != 0) {
        std::printf("declarations_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("declarations_test: each declaration records what the older spelling built\n");
    return 0;
}
