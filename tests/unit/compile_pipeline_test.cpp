// The pipeline entry `compile(graph, machine, sizes)`: analysis, the planner and the lowering in that
// order, the same value at compile time and at run time, a graph that does not analyse stopped with
// the analysis's own message — and `try_compile`, which hands each refusal back as text instead: the
// analysis's, the planner's, and the lowering's of a schedule the planner made.

#include "heinzel/compile.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/notation/einsum.hpp"

#include "canaried.hpp"
#include "hand_built_graph.hpp" // add_value / add_node
#include "interpret.hpp"
#include "planned.hpp"
#include "run_inputs.hpp"
#include "well_formed.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t kBlock = 64;
constexpr Dim kLength{2};

// y = x*x + x, NOT analysed: the node outputs are still typeless and shapeless, so a program for it exists
// only if `compile` ran `infer`.
constexpr Graph raw_graph(Extent block) {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{block}, ValueCategory::Input);
    const auto sq = add_node(g, op::Mul, x, x);
    const auto y = add_node(g, op::Add, sq, x);
    g.inputs.push_back(NamedValue{Name{"x"}, x});
    g.outputs.push_back(NamedValue{Name{"y"}, y});
    return g;
}

constexpr Graph analysed(Graph g, const Bindings& sizes = Bindings{}) {
    if (!infer(g, sizes).ok) {
        fail("the test graph did not resolve");
    }
    return g;
}

constexpr Graph mismatched() {
    Input a{"a", Signal{4}};
    Input b{"b", Signal{8}};
    return finish(Output{"z"} = a + b);
}

// x*x followed by three chained +x: a group whose lanes follow the machine it is planned for.
constexpr Graph chain() {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{literal_extent(kBlock)}, ValueCategory::Input);
    auto y = add_node(g, op::Mul, x, x);
    for (int i = 0; i < 3; ++i) {
        y = add_node(g, op::Add, y, x);
    }
    g.inputs.push_back(NamedValue{Name{"x"}, x});
    g.outputs.push_back(NamedValue{Name{"y"}, y});
    return g;
}

// A window over the STREAMED axis — a framed stream, which the planner refuses by name.
constexpr Graph framed() {
    Input x{"x", Signal{kBlock}};
    return finish(Output{"y"} = x.unfold(0, 4, 2).sum(0));
}

// Lanes across a product's outputs with a transpose read after it: the planner once said yes and the lowering refused
// (`loops_plan_test`'s case 13). The product's lanes now stay off its outputs, and it compiles.
constexpr Graph transposed_after_a_product() {
    Input a{"a", {64, 32}};
    Input b{"b", {32, 48}};
    Input c{"c", {48, 64}};
    return finish(Output{"y"} = einsum("ik,kj->ij", a, b) + c.transpose({1, 0}));
}

// New extents with a symbolic one inside: refused with a text longer than `fail` keeps (105 characters, its shape cut).
constexpr Graph reshaped_around_a_symbol() {
    const DimDecl s{0, DimConstraints{1, 1}};
    return finish(Output{"y"} = detail::input("x", Shape{2, 3, Extent{Dim{s.id}}, 1}, ElemType::F32, s).reshape({6, Extent{Dim{s.id}}}) * 2.0);
}
constexpr std::string_view kLongRefusal = "heinzel::compile: _reshape: new extents, a symbolic one not outermost, have no address yet: [2, 3, ?0, 1]";

// Graphs edited by hand into what the frontend never builds, which `infer` accepts: the outer of two flips moved before
// the inner one it reads, and a value made by two nodes (`loops_lower_test` holds the lowering to the same refusals).
constexpr Expr flip(Expr x, std::int32_t axis) {
    OpAttr attr{};
    attr.kind = AttrKind::View;
    attr.view.kind = ViewKind::Flip;
    attr.view.axis = axis;
    return detail::append_unary(x, op::Flip, attr);
}
constexpr Graph out_of_order() {
    Input x{"x", {8, 6}};
    Expr e = x * 2.0;
    Graph g = finish(Output{"c"} = e + 1.0, Output{"v"} = flip(flip(e, 0), 0) * 3.0);
    std::swap(g.nodes[2], g.nodes[3]);
    return g;
}
constexpr Graph reads_itself() { // y = x*2, edited to read y
    Input x{"x", {8, 6}};
    Graph g = finish(Output{"y"} = x * 2.0);
    (void)infer(g); // (the edit keeps the shapes the analysis gave)
    g.nodes[0].inputs[0] = g.nodes[0].output;
    return g;
}
constexpr Graph made_twice() {
    Input x{"x", Signal{4, 19}};
    Expr a = x * 2.0;
    Expr b = x * 3.0;
    Graph g = finish(Output{"y"} = a + 1.0, Output{"z"} = b);
    (void)infer(g); // (the edit keeps the shapes the analysis gave)
    g.nodes[1].output = g.nodes[0].output;
    g.outputs[1].value = g.nodes[0].output;
    return g;
}

constexpr fixed_string<kMaxMessageLength> analysis_says(Graph g) { return infer(g).message; }

constexpr Graph kRaw = raw_graph(literal_extent(kBlock));
constexpr Compiled kCompiled = compile(kRaw);

} // namespace

static_assert(kRaw.values[2].type == ElemType::Unknown, "the raw graph really is unanalysed");
static_assert(kCompiled.graph == analysed(kRaw), "compile analyses, and plans the graph as analysed");
static_assert(kCompiled.program == planned::program_of(kCompiled.graph, default_machine),
              "the program is the planner's and the lowering's, of the graph compile returns");
static_assert(compile(chain(), zen2).program == planned::program_of(compile(chain(), zen2).graph, zen2) &&
                  compile(chain(), zen2).program != compile(chain()).program && loops::well_formed(compile(chain()).program),
              "the target reaches the planner, on a graph where it changes the lanes");
static_assert(!(compile(chain(), zen2).graph == compile(chain()).graph),
              "and the rewrite: zen2 has the multiply-add, so x*x + x is one (`rewrite/product_adds.hpp`), the default machine not");

// Bindings reach the analysis: a symbolic block length bound at compile() is the literal graph, and
// unbound it is not.
static_assert(compile(raw_graph(Extent{kLength}), default_machine, Bindings{}.bind(kLength, kBlock)).graph ==
                  kCompiled.graph,
              "a bound symbolic extent analyses to the literal graph");
static_assert(compile(raw_graph(Extent{kLength})).graph != kCompiled.graph, "and an unbound one does not");

// try_compile: the same pipeline, the reason handed back.
static_assert(try_compile(kRaw).refusal.view().empty() && try_compile(kRaw).compiled == kCompiled,
              "a graph that compiles: no refusal, and the same result compile gives");
static_assert(try_compile(mismatched()).refusal == analysis_says(mismatched()),
              "a graph that does not analyse: the analysis's own message");
static_assert(try_compile(framed()).refusal.view().starts_with("heinzel::compile: ") &&
                  try_compile(framed()).refusal.view().find("window") != std::string_view::npos,
              "a graph the planner refuses: its refusal, by name");
static_assert(try_compile(chain(), Machine{.vector_bytes = 24}).refusal.view() == "heinzel::compile: a schedule's lanes are a power of two, two or more",
              "a schedule the lowering refuses (six lanes of a 24-byte register): the lowering's refusal, by name, handed back too");
static_assert(analysis_says(out_of_order()).view().empty() &&
                  try_compile(out_of_order()).refusal.view() == "heinzel::compile: a node reads what it or a later node makes" &&
                  analysis_says(reads_itself()).view().empty() &&
                  try_compile(reads_itself()).refusal.view() == "heinzel::compile: a node reads what it or a later node makes" &&
                  analysis_says(made_twice()).view().empty() && try_compile(made_twice()).refusal.view() == "heinzel::compile: a value is made by two nodes",
              "a graph the frontend never builds: the planner's refusal");
static_assert([] {
    const CompileResult r = try_compile(transposed_after_a_product());
    return r.refusal.view().empty() && loops::well_formed(r.compiled.program);
}(), "a schedule the lowering once refused now compiles");
static_assert(kLongRefusal.size() > kMaxMessageLength - 1 &&
                  try_compile(reshaped_around_a_symbol()).refusal.view() == kLongRefusal.substr(0, kMaxMessageLength - 1),
              "a refusal longer than fail keeps is cut where fail cuts");

int main() {
    if (compile(kRaw) != kCompiled) {
        return 1; // run time and compile time disagree
    }

    // Compile moved no number: x*x + x over a ramp of quarters is two exact operations in float, and
    // the default target grants no contraction, so the program lands on the walk bit for bit.
    static constexpr const loops::Program& program = kCompiled.program;
    std::array<double, kBlock> ramp{};
    std::array<float, kBlock> feed{};
    for (std::size_t n = 0; n < kBlock; ++n) {
        ramp[n] = 0.25 * (static_cast<double>(n) - 20.0);
        feed[n] = static_cast<float>(ramp[n]);
    }
    Inputs walked{};
    walked.push_back(InputFeed{ramp.data(), 1});
    const auto signals = std::make_unique<Signals<kBlock>>();
    run(analysed(kRaw), walked, *signals);
    canaried::Workspace<program> work(loops::Sizes{});
    loops::Feeds feeds{};
    feeds[0] = feed.data();
    canaried::run<program>(feeds, work);
    const float* y = work.data<float>(static_cast<std::size_t>(program.outputs[0]));
    for (std::size_t n = 0; n < kBlock; ++n) {
        if (static_cast<double>(y[n]) != sample_at(*signals, kCompiled.graph.outputs[0].value, 0, n)) {
            return 2;
        }
    }

    // A refusal longer than `fail` keeps: what compile throws is what try_compile hands back.
    try {
        (void)compile(reshaped_around_a_symbol());
        return 6;
    } catch (const error& e) {
        if (e.message() != kLongRefusal.substr(0, kMaxMessageLength - 1)) {
            return 7;
        }
    }

    // A graph that does not analyse: compile stops with the message infer itself gives.
    Graph broken = mismatched();
    const Diagnostic why = infer(broken);
    if (why.ok) {
        return 3;
    }
    try {
        (void)compile(mismatched());
        return 4;
    } catch (const error& e) {
        if (e.message() != why.message.view() || e.message().find("at +:") == std::string_view::npos) {
            return 5;
        }
    }
    return 0;
}
