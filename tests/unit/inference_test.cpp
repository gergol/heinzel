// The one pass that works out every value's shape and element type.
//
// What is being tested is not really "does it get the shape right" — it is that ONE piece of code,
// which contains no branch asking which operation it is looking at, gets every operation right by
// reading a table. So the tests below deliberately cover operations whose rules differ as much as
// possible, and each says which rule it is exercising.
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"

// A graph built by hand, so a test can put a rule under pressure without needing a spelling for it
// in the language yet. Shared with symbolic_test.cpp, which needs the same thing.
#include "hand_built_graph.hpp"

#include <string_view>

using namespace heinzel;
constexpr Index n{};


// ---- the elementwise rule: shapes broadcast -----------------------------------------------------
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 8}, ValueCategory::Input);
    const auto row = add_value(g, ElemType::F32, Shape{8}, ValueCategory::Input);
    const auto sum = add_node(g, op::Add, x, row);
    const Diagnostic d = infer(g);
    return d.ok && g.values[static_cast<std::size_t>(sum)].shape == Shape{4, 8};
}(), "a shorter shape lines up against the innermost axes");

static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 1}, ValueCategory::Input);
    const auto y = add_value(g, ElemType::F32, Shape{1, 8}, ValueCategory::Input);
    const auto sum = add_node(g, op::Add, x, y);
    return infer(g).ok && g.values[static_cast<std::size_t>(sum)].shape == Shape{4, 8};
}(), "an axis of extent one stretches to meet the other");

// And an elementwise broadcast that CANNOT line up is refused, complaint naming the operation. This is
// the rejection milestone MB.S2 confirms directly on the op family channel_scale uses (`_add`/`_mul`),
// not only on `_contr` below: two non-1 unequal literals on the same axis are the plain disagreement
// `combine_extent` sets `agreed=false` on. A deliberately broken input, observed to object.
static_assert([] {
    Graph g{};
    const auto a = add_value(g, ElemType::F32, Shape{2, 8}, ValueCategory::Input);
    const auto b = add_value(g, ElemType::F32, Shape{3, 8}, ValueCategory::Input);
    const auto sum = add_node(g, op::Add, a, b);
    const Diagnostic d = infer(g);
    return !d.ok && d.message.view() == std::string_view{"shapes disagree at +: [2, 8] and [3, 8]"};
}(), "an outer axis that is neither equal nor one is refused, and the complaint names the add");

// ---- the same code, a completely different rule: a reduction drops axes ---------------------------
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 8}, ValueCategory::Input);
    OpAttr a{};
    a.kind = AttrKind::Reduce;
    a.reduce.axes = Axes{1};
    const auto total = add_node(g, op::Reduce, x, kNoValue, kNoValue, a);
    return infer(g).ok && g.values[static_cast<std::size_t>(total)].shape == Shape{4};
}(), "the reduced axis is gone");

static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 8}, ValueCategory::Input);
    OpAttr a{};
    a.kind = AttrKind::Reduce;
    a.reduce.axes = Axes{1};
    a.reduce.keep_axes = true;
    const auto total = add_node(g, op::Reduce, x, kNoValue, kNoValue, a);
    return infer(g).ok && g.values[static_cast<std::size_t>(total)].shape == Shape{4, 1};
}(), "asked to keep it, the reduced axis stays at extent one so it broadcasts back");

// ---- a contraction: the elementwise rule and the reduction rule, composed ---------------------------
// The two operands line up the way an addition's would, and then the axes the attribute names
// disappear into the sum. Asserted on a case where the two operands do NOT have the same shape, so
// the broadcast half is doing something rather than being the identity.
static_assert([] {
    Graph g{};
    const auto a = add_value(g, ElemType::F32, Shape{4, 8}, ValueCategory::Input);
    const auto b = add_value(g, ElemType::F32, Shape{8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::Contract;
    k.contract.axes = Axes{1};
    const auto out = add_node(g, op::Contract, a, b, kNoValue, k);
    return infer(g).ok && g.values[static_cast<std::size_t>(out)].shape == Shape{4};
}(), "the summed axis is gone, and the shorter operand broadcast against the longer first");

// Naming no axis contracts nothing, which is a product and not a contraction — and the shape says so
// rather than the pass having a special case for it.
static_assert([] {
    Graph g{};
    const auto a = add_value(g, ElemType::F32, Shape{4, 8}, ValueCategory::Input);
    const auto b = add_value(g, ElemType::F32, Shape{4, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::Contract;
    const auto out = add_node(g, op::Contract, a, b, kNoValue, k);
    return infer(g).ok && g.values[static_cast<std::size_t>(out)].shape == Shape{4, 8};
}(), "no axis named means nothing collapses, and the shape says so without a special case for it");

// And the operands still have to agree, because the broadcast half is the real one: a contraction of
// [4, 8] with [4, 3] is refused by the same complaint an addition of them would get.
static_assert([] {
    Graph g{};
    const auto a = add_value(g, ElemType::F32, Shape{4, 8}, ValueCategory::Input);
    const auto b = add_value(g, ElemType::F32, Shape{4, 3}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::Contract;
    k.contract.axes = Axes{1};
    const auto out = add_node(g, op::Contract, a, b, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok && d.message.view() == std::string_view{"shapes disagree at _contr: [4, 8] and [4, 3]"};
}(), "an axis that cannot broadcast is refused, and the complaint names the contraction");

// ---- the GENERAL contraction (`_matmul`, milestone MB): a different axis of each operand ------------
// `_contr` lines its two operands up elementwise and sums a shared axis. A matmul binds a DIFFERENT axis
// of each — `"ik,kj->ij"` reads a at (i,k) and b at (k,j) — and its shape is `[batch] ++ [a free] ++
// [b free] ++ [streamed]`, a case `ShapeRule::ContractGeneral` builds and `ContractAxes` cannot. Here the
// streamed innermost axis is 8: a `[I=4, K=3]` against a `[K=3, J=5]` gives `[I=4, J=5]`, the contracted
// K transposed between position 1 in a and position 0 in b.
static_assert([] {
    Graph g{};
    const auto a = add_value(g, ElemType::F32, Shape{4, 3, 8}, ValueCategory::Input);
    const auto b = add_value(g, ElemType::F32, Shape{3, 5, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::MatMul;
    k.matmul.a_contract = 1;
    k.matmul.b_contract = 0;
    const auto out = add_node(g, op::MatMul, a, b, kNoValue, k);
    return infer(g).ok && g.values[static_cast<std::size_t>(out)].shape == Shape{4, 5, 8};
}(), "a transposing contraction: the contracted axis is summed, a's and b's free axes sit side by side");

// A BATCHED matmul `"bik,bkj->bij"`: the batch axis is present in a, b and the output, iterated not
// summed, and appears once at the front. `[B=2, I=4, K=3]` against `[B=2, K=3, J=5]` gives `[B=2, I=4, J=5]`.
static_assert([] {
    Graph g{};
    const auto a = add_value(g, ElemType::F32, Shape{2, 4, 3, 8}, ValueCategory::Input);
    const auto b = add_value(g, ElemType::F32, Shape{2, 3, 5, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::MatMul;
    k.matmul.a_contract = 2;
    k.matmul.b_contract = 1;
    k.matmul.batch_a = Axes{0};
    k.matmul.batch_b = Axes{0};
    const auto out = add_node(g, op::MatMul, a, b, kNoValue, k);
    return infer(g).ok && g.values[static_cast<std::size_t>(out)].shape == Shape{2, 4, 5, 8};
}(), "a batched matmul: the batch axis is iterated and kept, at the front of the output");

// SHOWN REJECTING — a contracted axis of unequal extent, pointed at a deliberately broken binding and
// observed to object: a's K is 3, b's K is 5, so the sum has no shared length.
static_assert([] {
    Graph g{};
    const auto a = add_value(g, ElemType::F32, Shape{4, 3, 8}, ValueCategory::Input);
    const auto b = add_value(g, ElemType::F32, Shape{5, 5, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::MatMul;
    k.matmul.a_contract = 1; // K=3
    k.matmul.b_contract = 0; // K=5
    const auto out = add_node(g, op::MatMul, a, b, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok && d.message.view() == std::string_view{"einsum's contracted extents disagree: 3 and 5"};
}(), "a contracted axis whose two extents disagree is refused, and the complaint names the matmul");

// SHOWN REJECTING — a contraction that names the STREAMED (innermost) axis, which is time and passes
// through: axis 2 of a rank-3 operand is the streamed one, and a contraction may never sum it.
static_assert([] {
    Graph g{};
    const auto a = add_value(g, ElemType::F32, Shape{4, 3, 8}, ValueCategory::Input);
    const auto b = add_value(g, ElemType::F32, Shape{3, 5, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::MatMul;
    k.matmul.a_contract = 2; // the streamed innermost axis
    k.matmul.b_contract = 0;
    const auto out = add_node(g, op::MatMul, a, b, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok && d.message.view() ==
                        std::string_view{"einsum may not contract the streamed (innermost) axis 2; a "
                                         "contraction is over element axes"};
}(), "a contraction that names the streamed axis is refused by name — it is a different mistake");

// SHOWN REJECTING — a contracted axis that is not an axis of its operand at all.
static_assert([] {
    Graph g{};
    const auto a = add_value(g, ElemType::F32, Shape{4, 3, 8}, ValueCategory::Input);
    const auto b = add_value(g, ElemType::F32, Shape{3, 5, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::MatMul;
    k.matmul.a_contract = 5; // a rank-3 operand has no axis 5
    k.matmul.b_contract = 0;
    const auto out = add_node(g, op::MatMul, a, b, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok && d.message.view() ==
                        std::string_view{"einsum's first operand's contracted axis 5 is not an axis of a "
                                         "rank-3 operand"};
}(), "a contracted axis that does not exist is refused, naming the operand's rank");

// SHOWN REJECTING — the batch side of the binding: lists that do not pair up, a batch axis an operand does not
// have, and batch or streamed extents that disagree. `[B=2, I=4, K=3]` against `[B=2, K=3, J=5]` as above.
constexpr Diagnostic batched_matmul(Axes batch_a, Axes batch_b, std::int32_t b_batch, std::int32_t b_block) {
    Graph g{};
    const auto a = add_value(g, ElemType::F32, Shape{2, 4, 3, 8}, ValueCategory::Input);
    const auto b = add_value(g, ElemType::F32, Shape{b_batch, 3, 5, b_block}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::MatMul;
    k.matmul.a_contract = 2;
    k.matmul.b_contract = 1;
    k.matmul.batch_a = batch_a;
    k.matmul.batch_b = batch_b;
    (void)add_node(g, op::MatMul, a, b, kNoValue, k);
    return infer(g);
}
static_assert(batched_matmul(Axes{0}, Axes{}, 2, 8).message.view() ==
                  std::string_view{"einsum's batch axis lists must pair up, but name 1 and 0 axes"},
              "batch axis lists of different lengths are refused, naming both lengths");
static_assert(batched_matmul(Axes{0}, Axes{4}, 2, 8).message.view() ==
                  std::string_view{"einsum's second operand's batch axis 4 is not an axis of a rank-4 operand"},
              "a batch axis that does not exist is refused, naming the operand and its rank");
static_assert(batched_matmul(Axes{0}, Axes{0}, 3, 8).message.view() == std::string_view{"einsum's batch extents disagree: 2 and 3"},
              "a batch pair whose extents disagree is refused, naming both");
static_assert(batched_matmul(Axes{0}, Axes{0}, 2, 6).message.view() == std::string_view{"einsum's streamed extents disagree: 8 and 6"},
              "streamed axes of different lengths are refused, naming both");


// ---- a state depth against a scan length (milestone M4.6, risk R1-1) -------------------------------
//
// Two different things that share one integer type, which is why this needs a check and not a
// signature. How many samples a run walks may be symbolic — that is what the extent solver is for.
// How far back a lag reaches may not: it sizes the history a run keeps, and that is decided before a
// sample is computed. `Extent`'s encoding spends the values below zero on symbols (`e <= -2` is
// symbol `-e - 2`) and on its reserved "nothing said" (`-1`), so a symbol handed to a depth arrives
// as a NEGATIVE COUNT and nothing about its type says otherwise.
//
// All four cases: a real depth, a depth of zero (which reads the present and is deliberately legal),
// a symbolic depth on each of the two constant lags. The graphs below declare a symbolic axis extent
// as well, so what is refused is visibly the DEPTH and not the presence of a symbol.

static_assert([] {
    Graph g{};
    const Dim n{0};
    const auto x = add_value(g, ElemType::F32, Shape{n}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::Delay;
    k.delay.samples = 3;
    add_node(g, op::FixedDelay, x, kNoValue, kNoValue, k);
    const Bindings sizes = Bindings{}.bind(n, 16);
    return infer(g, sizes).ok;
}(), "a constant depth over a symbolic scan length is exactly the combination R1-1 says is fine");

static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{16}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::Delay;
    k.delay.samples = 0;
    add_node(g, op::FixedDelay, x, kNoValue, kNoValue, k);
    return infer(g).ok;
}(), "a depth of zero reads the present, which is a thing a graph may say");

static_assert([] {
    Graph g{};
    const Dim n{0};
    const auto x = add_value(g, ElemType::F32, Shape{n}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::Delay;
    // The symbol as it would arrive: `Dim{0}` encoded, which is what an `Extent` holding a symbol
    // stores and what a caller reaching for `x[n - N]` would pass.
    k.delay.samples = encode_symbol(0);
    add_node(g, op::FixedDelay, x, kNoValue, kNoValue, k);
    const Bindings sizes = Bindings{}.bind(n, 16);
    const Diagnostic d = infer(g, sizes);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"_fdelay depth -2: a state depth is a constant >= 0; only a scan length may be symbolic"};
}(), "SHOWN REJECTING: a symbolic state depth, named as such, on a graph whose scan length is symbolic and fine");


// ---- the tap delay line's ceiling, which is a distance ----------------------------------------------
// A negative ceiling is not a smaller clamp: the position would be clamped to a range with nothing
// in it and then handed to a cast. Refused so the clamp can stay the two comparisons it is.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{16}, ValueCategory::Input);
    const auto p = add_value(g, ElemType::F32, Shape{16}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::SignalledDelay;
    k.signalled_delay.max_delay = 4;
    add_node(g, op::DelayLine, x, p, kNoValue, k);
    return infer(g).ok;
}(), "a ceiling that is a distance is accepted");

static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{16}, ValueCategory::Input);
    const auto p = add_value(g, ElemType::F32, Shape{16}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::SignalledDelay;
    k.signalled_delay.max_delay = -4;
    add_node(g, op::DelayLine, x, p, kNoValue, k);
    const Diagnostic d = infer(g);
    return !d.ok &&
           d.message.view() ==
               std::string_view{"_dline may reach back at most -4 samples, which is not a distance"};
}(), "SHOWN REJECTING: a negative ceiling on a tap delay line");

// ---- and a rule that is chosen by the ATTRIBUTE, not the operation ---------------------------------
// Three rearrangements, one operation code. This is the case that makes looking a rule up by
// operation alone wrong, so the pass is put through all three.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 8}, ValueCategory::Input);
    OpAttr a{};
    a.kind = AttrKind::View;
    a.view.kind = ViewKind::Reshape;
    a.view.shape = Shape{2, 2, 8}; // a valid re-cut: 4 numbers per sample stay 4, the streamed axis (8) whole
    const auto split = add_node(g, op::Reshape, x, kNoValue, kNoValue, a);
    return infer(g).ok && g.values[static_cast<std::size_t>(split)].shape == Shape{2, 2, 8};
}());

static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 1, 8}, ValueCategory::Input);
    OpAttr a{};
    a.kind = AttrKind::View;
    a.view.kind = ViewKind::Squeeze;
    a.view.axis = 1;
    const auto out = add_node(g, op::Reshape, x, kNoValue, kNoValue, a);
    return infer(g).ok && g.values[static_cast<std::size_t>(out)].shape == Shape{4, 8};
}());

static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 8}, ValueCategory::Input);
    OpAttr a{};
    a.kind = AttrKind::View;
    a.view.kind = ViewKind::Unsqueeze;
    a.view.axis = 0;
    const auto out = add_node(g, op::Reshape, x, kNoValue, kNoValue, a);
    return infer(g).ok && g.values[static_cast<std::size_t>(out)].shape == Shape{1, 4, 8};
}());

// ---- element types, from four different rules ------------------------------------------------------
static_assert([] {
    Graph g{};
    const auto a = add_value(g, ElemType::F32, Shape{4}, ValueCategory::Input);
    const auto b = add_value(g, ElemType::F64, Shape{4}, ValueCategory::Input);
    const auto sum = add_node(g, op::Add, a, b);
    return infer(g).ok && g.values[static_cast<std::size_t>(sum)].type == ElemType::F64;
}(), "mixing precisions takes the wider one");

static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4}, ValueCategory::Input);
    OpAttr a{};
    a.kind = AttrKind::Cast;
    a.cast.to = ElemType::F64;
    const auto out = add_node(g, op::Cast, x, kNoValue, kNoValue, a);
    return infer(g).ok && g.values[static_cast<std::size_t>(out)].type == ElemType::F64;
}(), "a conversion says what it produces, and is believed");

static_assert([] {
    Graph g{};
    const auto cond = add_value(g, ElemType::F32, Shape{4}, ValueCategory::Input);
    const auto a = add_value(g, ElemType::F64, Shape{4}, ValueCategory::Input);
    const auto b = add_value(g, ElemType::F64, Shape{4}, ValueCategory::Input);
    const auto out = add_node(g, op::Select, cond, a, b);
    return infer(g).ok && g.values[static_cast<std::size_t>(out)].type == ElemType::F64;
}(), "the condition does not contribute its type to the answer");

// ---- a literal adapts, and does not drive ------------------------------------------------------------
// This is the rule that stops `0.5 * w` making a whole expression double precision. It is asserted
// on the value the literal ITSELF ends up with, because that is where the old tree's three separate
// mechanisms used to disagree.
static_assert([] {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4}, ValueCategory::Input);
    g.values.push_back(Value{ElemType::Unknown, Shape{}, ValueCategory::Const, 0.5});
    const auto half = static_cast<std::int32_t>(g.values.size() - 1);
    const auto scaled = add_node(g, op::Mul, x, half);
    return infer(g).ok && g.values[static_cast<std::size_t>(scaled)].type == ElemType::F32 &&
           g.values[static_cast<std::size_t>(half)].type == ElemType::F32;
}(), "a literal written as a double becomes what it is used with, and takes nothing with it");

// ---- numbers that a reader types (Plan/28-dsl-proposals/03 B2, Decision 76) -----------------------------------------
// Where the sweep left them untyped: a value made of numbers alone takes the type of the reader that types it, as a
// literal does, whatever its condition's; a `where` that nothing types takes its condition's; with neither, refused.
template <ElemType Condition, ElemType Reader>
constexpr Graph picked_and_read() {
    Input c{"c", Signal{8}, Condition};
    Input x{"x", Signal{8}, Reader};
    Expr picked = where(c > 0.0, 0.877, 0.9987);
    Expr scaled = 0.5 * picked + 0.25; // numbers alone too
    Output y{"y"};
    y = scaled * x;
    return y;
}
template <ElemType Condition>
constexpr Graph sign_of() {
    Input c{"c", Signal{8}, Condition};
    Output y{"y"};
    y = where(c > 0.0, 1.0, -1.0);
    return y;
}
// The `where`, its two numbers and the output of the type wanted; analysed twice, equal.
template <Graph (*Build)(), ElemType Wanted>
constexpr bool where_typed() {
    Graph g = Build();
    bool typed = infer(g).ok && g.values[static_cast<std::size_t>(g.outputs[0].value)].type == Wanted;
    for (const Node& nd : g.nodes) {
        if (nd.op == op::Select) {
            for (const std::int32_t v : {nd.output, nd.inputs[1], nd.inputs[2]}) {
                typed = typed && g.values[static_cast<std::size_t>(v)].type == Wanted;
            }
        }
    }
    Graph again = g;
    return typed && infer(again).ok && again == g;
}
static_assert(where_typed<picked_and_read<ElemType::F64, ElemType::F32>, ElemType::F32>(), "* x32: f32, not the mask's f64");
static_assert(where_typed<picked_and_read<ElemType::F32, ElemType::F64>, ElemType::F64>(), "* x64: f64, not the mask's f32");
static_assert(where_typed<sign_of<ElemType::F32>, ElemType::F32>() && where_typed<sign_of<ElemType::F64>, ElemType::F64>(),
              "a `where` that is the output takes its condition's type");
constexpr Graph numbers_only() {
    Input x{"x", Signal{8}};
    Output y{"y"};
    Output z{"z"};
    Expr on = constant(1.0) > 0.5;
    y = x * 2.0;
    z = where(on, 1.0, 2.0);
    return finish(y, z);
}
static_assert([] {
    Graph g = numbers_only();
    return infer(g).message.view() ==
           std::string_view{"heinzel: where: its element type could not be worked out; type one of its values"};
}(), "numbers only, nothing to type the `where`: refused, naming it");

// ---- a value defined by its own past learns from its definition -----------------------------------------
// The delay tap is read before the recurrence exists, so this only works because the pass sweeps
// until nothing more is learned.
static_assert([] {
    Input x{"x", Signal{64}};
    Variable w;
    w[n] = x - 0.5 * w[n - 1];
    Graph g = finish(Output{"y"} = w);

    if (!infer(g).ok) {
        return false;
    }
    for (const Value& v : g.values) {
        if (v.type == ElemType::Unknown) {
            return false;
        }
        // A literal is a scalar and stays one; it broadcasts where it is used. Everything the
        // signal flows through carries the signal's shape, the variable and its tap included.
        if (v.category != ValueCategory::Const && !(v.shape == Shape{64})) {
            return false;
        }
    }
    return true;
}(), "a one-pole filter resolves completely, taps and variable included");

// ---- what must fail, fails — and says what disagreed --------------------------------------------------
static_assert([] {
    Graph g{};
    const auto a = add_value(g, ElemType::F32, Shape{4}, ValueCategory::Input);
    const auto b = add_value(g, ElemType::F32, Shape{8}, ValueCategory::Input);
    add_node(g, op::Add, a, b);
    return !infer(g).ok;
}(), "four and eight do not broadcast, and the pass says so instead of picking one");

static_assert([] {
    Graph g{};
    const auto a = add_value(g, ElemType::F32, Shape{4}, ValueCategory::Input);
    const auto b = add_value(g, ElemType::F32, Shape{8}, ValueCategory::Input);
    add_node(g, op::Add, a, b);
    // The message names the operation and both shapes. That is the requirement — a shape conflict
    // must be readable, not a template cascade.
    return infer(g).message.view() == std::string_view{"shapes disagree at +: [4] and [8]"};
}(), "the complaint names the operation and both shapes");

// ---- symbolic extents: the pass SOLVES them rather than carrying them around --------------------
//
// Milestone M2.1. Two claims, and the second is the one that needs a solver rather than a
// bookkeeping table.

// A size arriving later flows through the graph like any other.
static_assert([] {
    Graph g{};
    constexpr Dim n{0};
    const auto x = add_value(g, ElemType::F32, Shape{n}, ValueCategory::Input);
    const auto y = add_value(g, ElemType::F32, Shape{n}, ValueCategory::Input);
    const auto sum = add_node(g, op::Add, x, y);
    return infer(g).ok && g.values[static_cast<std::size_t>(sum)].shape == Shape{n};
}(), "one symbol against itself needs no solving, and must not be broken by having one");

// TRANSITIVE EQUALITY. `b` meets `c` at one operation and `a` meets `b` at another. NOTHING relates
// `a` and `c`: no operation takes both, and neither ever meets the other's shape. They are one axis
// all the same, and the pass reports `c`'s extent AS `a`'s — not merely "equal to", but written
// into the resolved shape, which is what makes it visible to everything downstream.
constexpr Graph three_axes_two_meetings() {
    Graph g{};
    constexpr Dim a{0};
    constexpr Dim b{1};
    constexpr Dim c{2};
    add_value(g, ElemType::F32, Shape{a}, ValueCategory::Input); // value 0
    add_value(g, ElemType::F32, Shape{b}, ValueCategory::Input); // value 1
    add_value(g, ElemType::F32, Shape{c}, ValueCategory::Input); // value 2
    add_node(g, op::Add, 1, 2);                                  // b meets c
    add_node(g, op::Add, 0, 1);                                  // a meets b
    return g;
}

static_assert([] {
    Graph g = three_axes_two_meetings();
    const Diagnostic d = infer(g);
    // All three come back as ONE extent, named by the lowest symbol in the class.
    return d.ok && g.values[0].shape == Shape{Dim{0}} && g.values[1].shape == Shape{Dim{0}} &&
           g.values[2].shape == Shape{Dim{0}};
}(), "a and c are one axis though no operation ever related them");

// And binding any ONE of the three gives all of them the size — including the one that was only
// ever related transitively. The symbol bound here is `c`, the far end of the chain.
static_assert([] {
    Graph g = three_axes_two_meetings();
    const Diagnostic d = infer(g, Bindings{}.bind(Dim{2}, 64));
    return d.ok && g.values[0].shape == Shape{64} && g.values[1].shape == Shape{64} &&
           g.values[2].shape == Shape{64};
}(), "binding the far end of the chain sizes the whole class");

// A bound graph is fully determined, which is the property an operation demanding every extent known
// now (`_fft`) will ask about — and the reason that check runs after the fixpoint rather than inside it.
static_assert([] {
    Graph g = three_axes_two_meetings();
    (void)infer(g, Bindings{}.bind(Dim{2}, 64));
    return is_static(g);
}(), "a symbolic graph with its symbols bound has no symbolic extent left in it");

// A CONTRADICTION IS NAMED. One axis constrained to two different sizes, by two different
// operations. The message names both sizes AND the operation that introduced each — asserted
// literally, in the same style as the shape complaint above, because a test that only checked
// `!ok` would pass on "something went wrong".
constexpr Graph one_axis_two_sizes() {
    Graph g{};
    constexpr Dim n{0};
    add_value(g, ElemType::F32, Shape{n}, ValueCategory::Input); // value 0
    add_value(g, ElemType::F32, Shape{4}, ValueCategory::Input); // value 1
    add_value(g, ElemType::F32, Shape{8}, ValueCategory::Input); // value 2
    add_node(g, op::Add, 0, 1);                                  // pins the axis to 4
    add_node(g, op::Mul, 0, 2);                                  // and then to 8
    return g;
}

static_assert([] {
    Graph g = one_axis_two_sizes();
    return !infer(g).ok;
}(), "one axis cannot be two sizes");

static_assert([] {
    Graph g = one_axis_two_sizes();
    return infer(g).message.view() == std::string_view{"one axis is given two sizes: 4 at + and 8 at *"};
}(), "both sizes, and the operation that introduced each");

// A binding that contradicts what the graph already says is refused the same way, and names the
// binding as the source of one of the two sizes rather than blaming an operation for it.
static_assert([] {
    Graph g{};
    constexpr Dim n{0};
    add_value(g, ElemType::F32, Shape{n}, ValueCategory::Input);
    add_value(g, ElemType::F32, Shape{4}, ValueCategory::Input);
    add_node(g, op::Add, 0, 1);
    const Diagnostic d = infer(g, Bindings{}.bind(n, 8));
    return !d.ok && d.message.view() == std::string_view{"one axis is given two sizes: 8 at binding and 4 at +"};
}(), "a binding that disagrees with the graph is a contradiction like any other");

// An axis of extent one still stretches, symbol or not: broadcasting against a literal 1 is not a
// claim that the two axes are the same axis, so it must not unify them.
static_assert([] {
    Graph g{};
    constexpr Dim n{0};
    const auto x = add_value(g, ElemType::F32, Shape{n}, ValueCategory::Input);
    const auto one = add_value(g, ElemType::F32, Shape{1}, ValueCategory::Input);
    const auto sum = add_node(g, op::Add, x, one);
    const Diagnostic d = infer(g, Bindings{}.bind(n, 64));
    return d.ok && g.values[static_cast<std::size_t>(sum)].shape == Shape{64} &&
           g.values[static_cast<std::size_t>(one)].shape == Shape{1};
}(), "a scalar-ish axis broadcasts against a symbolic one without being pinned to it");

int main() { return 0; }
