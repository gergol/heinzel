// The lowering on stages walked as they stand (`Plan/archive/13-loop-tree/05` §7, M2.3): a stage of every kind — a map, a fold
// opening at its first element, a contraction and what follows it, sibling folds, an order statistic's network, a
// recurrence handing its state on, a cycle walked with time outermost, a feed's past with its head samples, a delay
// line, a value made in parts, a producer computed inside its reader's window, a ramp whose past is read — lowered from
// a schedule that walks each stage one serial loop an axis in memory order, well formed while the compiler evaluates
// (`lowered` holds every program to `well_formed`), printed and compared with its listing in
// `tests/goldens/loops_lower/`: checked once by eye against `02` §4.3 and, when pinned, equal to the old lowering's
// tree for the same nests. Then each rule of normalization that needs more of a schedule (M2.4), pinned the same way:
// lanes along a fold's own axis, a fold resuming — and the store it is given where it had none — state handed on under
// lanes across voices, a network under lanes across outputs, a fold inside a feedback stage's sample (Plan/23 10; its
// refusals are `fold_in_feedback_test`'s). Then every refusal of the lowering, by name, at compile time: a refusal is a
// value, so a `static_assert` gets its answer (what a schedule may not plan, and its loops' and choices' refusals, are
// `loops_primitives`').
//
// A listing that changes is printed; `HEINZEL_REPIN=1 test_loops_lower` rewrites the files, and the change is
// reviewed in `git diff` like any other — never accepted blind.
//
// Unreachable, kept as internal checks with no case: a lag reading what a LATER stage makes (that value has no
// buffer yet, so the refusal before it answers); a window's source computed in its reader's stage but made after it
// is read, or read after the fold (a source not inlined has the domain's or the fold's shape, and no window's source
// has); a read further back than its buffer's silence (`loops/validate.hpp`); a stage reading a feed's past along
// no loop (time is every domain's last axis); a value made in parts along an axis of symbolic extent, and a dimension
// id beyond `kMaxDimDecls` (the language refuses both first). And, refused as a value but with no case, `kMaxTreeNodes`:
// reaching it takes a program near `kMaxStmts` whose stages each copy long chains of enumerators into every nest.

#include "heinzel/loops/lower.hpp"
#include "heinzel/loops/print.hpp"

#include "corpus_graphs.hpp"
#include "hand_built_graph.hpp"
#include "loops_schedules.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace {

using namespace heinzel;
using namespace loops_schedules;
namespace loops = heinzel::loops;

constexpr Expr in(std::string_view name, Shape shape) { return Input{name, Signal{shape}}; } // a signal, of one sample or more

// ---- the graphs of the listings that the corpus does not have ------------------------------------------

constexpr Graph ramp_past() { // a ramp, filled once, whose past is read: nest_agreement_test's
    constexpr Index n{};
    return corpus::analysed(finish(Output{"y"} = in("x", Shape{4, 16}) + iota(Signal{4, 16})[n - 2]));
}

// Sixteen voices of y = x + .5y[n-1] - .25y[n-2] + .125y[n-4]: lags 1 and 2 a run, handed on in two slots, lag 4 past a
// gap, read from memory.
constexpr Graph two_lags_and_a_gap() {
    constexpr Index n{};
    Input x{"x", Signal{16, Length{.multiple_of = 1, .min = 1, .expect = 64}}};
    Output y{"y"};
    y[n] = x[n] + 0.5 * y[n - 1] - 0.25 * y[n - 2] + 0.125 * y[n - 4];
    return corpus::analysed(y);
}

// A feedback stage whose sample holds a fold (Plan/23 10): eight voices in double summed inside a loop,
// u = x − y[n−1]/8; v = c0·u + c1·u[n−1] + a1·v[n−1]; y = Σv + u/2 (`fold_in_feedback_test`'s plain bank, which also
// holds its refusals).
constexpr Graph bank_in_a_loop() {
    constexpr Index n{};
    constexpr std::array<double, 8> c0{0.0625, 0.0703125, 0.078125, 0.0859375, 0.09375, 0.1015625, 0.109375, 0.1171875};
    constexpr std::array<double, 8> c1{-0.03125, -0.02734375, -0.0234375, -0.01953125, -0.015625, -0.01171875, -0.0078125, -0.00390625};
    constexpr std::array<double, 8> a1{0.375, -0.2421875, 0.390625, -0.2265625, 0.40625, -0.2109375, 0.421875, -0.1953125};
    Input x{"x", Signal{Length{}}, ElemType::F64};
    Variable y;
    Variable v;
    const Expr u = x - 0.125 * y[n - 1];
    v[n] = constant(c0) * u + constant(c1) * u[n - 1] + constant(a1) * v[n - 1];
    y[n] = Expr(v).sum(0) + 0.5 * u;
    return corpus::analysed(finish(Output{"y"} = y));
}

// ---- the graphs of the refusals --------------------------------------------------------------------------

constexpr Graph contraction_then_sum() { // n0 the contraction, n1 a fold of its result
    return corpus::analysed(finish(Output{"y"} = einsum("ik,kj->ij", in("a", Shape{3, 4, 1}), in("b", Shape{4, 5, 1})).sum(0)));
}

constexpr Graph folded_two_ways() { // n0 folds axis 0, n1 axis 1, n2 their sum
    Expr x = in("x", Shape{4, 3, 8});
    return corpus::analysed(finish(Output{"y"} = x.sum(0, keepdims) + x.sum(1, keepdims)));
}

constexpr Graph other_rank_siblings() { // n0 x {4, 8} folded over axis 0, kept; n1 z {2, 4, 8} folded over axis 0; n2 their sum
    Expr x = in("x", Shape{4, 8});
    return corpus::analysed(finish(Output{"y"} = x.sum(0, keepdims) + in("z", Shape{2, 4, 8}).sum(0)));
}

constexpr Graph widened_after_the_fold() { // n0 the fold {1, 8}, n1 its product with w {3, 8}
    Expr x = in("x", Shape{4, 8});
    return corpus::analysed(finish(Output{"y"} = x.sum(0, keepdims) * in("w", Shape{3, 8})));
}

constexpr Graph blur_and_its_square() { // `corpus::blur`, the square an output too: n0 x*x, n1 the window, n2 *w, n3 the fold
    Expr x = in("x", Shape{8, 6, 1});
    Expr w = in("w", Shape{1, 3, 1, 1});
    Expr square = x * x;
    Expr y = (square.unfold(0, 3) * w).sum(1);
    return corpus::analysed(finish(Output{"y"} = y, Output{"square"} = square));
}

constexpr Graph strict_rows() { // `corpus::rowsum<4>` at High precision
    Graph g = finish(Output{"y"} = in("x", Shape{4, 8}).sum(0));
    set_default_precision(g, numerics::Precision::High);
    return corpus::analysed(g);
}

constexpr Graph two_windows() { // n0 x*x, n1 and n4 two windows over it, n2/n5 their products, n3/n6 their folds, n7 the sum
    Expr x = in("x", Shape{8, 6, 1});
    Expr square = x * x;
    Expr w = in("w", Shape{1, 3, 1, 1});
    const auto blurred = [&](Expr v) { return (v.unfold(0, 3) * w).sum(1); };
    return corpus::analysed(finish(Output{"y"} = blurred(square) + blurred(square)));
}

constexpr Graph after_reads_inside() { // n0 m = x*2, n1 its fold (domain and result both {1, 8}), n2 fold + m
    Expr m = in("x", Shape{1, 8}) * 2.0;
    return corpus::analysed(finish(Output{"y"} = m.sum(0, keepdims) + m));
}

constexpr Graph table_of_three_axes() { // n0 a table read whose table has two axes besides time
    Graph g{};
    const auto phase = add_value(g, ElemType::F32, Shape{64}, ValueCategory::Input);
    const auto cells = add_value(g, ElemType::F32, Shape{3, 3, 64}, ValueCategory::Input);
    OpAttr read{};
    read.kind = AttrKind::Table;
    read.table.size = 9;
    const auto y = add_node(g, op::TableRead, phase, cells, kNoValue, read);
    g.inputs.push_back(NamedValue{"phase", phase});
    g.inputs.push_back(NamedValue{"x", cells});
    g.outputs.push_back(NamedValue{"y", y});
    return corpus::analysed(g);
}

constexpr Graph transposed_product() { // n0 x*2, n1 its transpose — a view with a statement, of the same shape
    return corpus::analysed(finish(Output{"y"} = (in("x", Shape{4, 4, 1}) * 2.0).transpose({1, 0})));
}

constexpr Graph contraction_of_a_product() { // n0 a*2, n1 its contraction with b (every shape {4, 4, 1})
    return corpus::analysed(finish(Output{"y"} = einsum("ik,kj->ij", in("a", Shape{4, 4, 1}) * 2.0, in("b", Shape{4, 4, 1}))));
}

constexpr Graph quantile_between_ranks() { // n0 the 0.3 quantile of six values, n1 + 0
    return corpus::analysed(finish(Output{"y"} = in("x", Shape{6, 8}).quantile(0, 0.3) + 0.0));
}

constexpr Graph broadcast_in_a_map() { // n0 v*2 {1, 8}, n1 x + it {4, 8}
    Expr v = in("v", Shape{1, 8});
    return corpus::analysed(finish(Output{"y"} = in("x", Shape{4, 8}) + v * 2.0));
}

constexpr Expr padded_along(Expr x, std::int32_t axis) {
    OpAttr attr{};
    attr.kind = AttrKind::View;
    attr.view.kind = ViewKind::Pad;
    attr.view.axis = axis;
    attr.view.pad_before = 2;
    attr.view.pad_after = 3;
    attr.view.pad_bits = std::bit_cast<std::uint64_t>(0.5);
    return detail::append_unary(x, op::Pad, attr);
}

constexpr Graph padded_product() { // n0 x*2, n1 it padded, n2 *2
    return corpus::analysed(finish(Output{"y"} = padded_along(in("x", Shape{13, 7, 24}) * 2.0, 1) * 2.0));
}

constexpr Graph median_and_sum() { // n0 the window, n1 its median, n2 its sum, n3 their sum
    Expr w = in("x", Shape{16, 1}).unfold(0, 5);
    return corpus::analysed(finish(Output{"y"} = w.median(1) + w.sum(1)));
}

constexpr Graph delay_line_over_a_feed() { // n0 a delay line reading the input itself, which keeps no past
    Expr x = in("x", Shape{21});
    Expr p = in("p", Shape{21});
    Arena* arena = bring_together(x, p);
    BuildNode n{};
    n.op = op::DelayLine;
    n.inputs[0] = x.position();
    n.inputs[1] = p.position();
    n.attr.kind = AttrKind::SignalledDelay;
    n.attr.signalled_delay.max_delay = 5;
    arena->nodes.push_back(n);
    return corpus::analysed(finish(Output{"y"} = Expr{arena, static_cast<std::int32_t>(arena->nodes.size() - 1)}));
}

constexpr Graph window_reshaped() { // a window given new extents: no address
    return corpus::analysed(finish(Output{"y"} = in("x", Shape{16, 1}).unfold(0, 4).reshape({13, 2, 2}) * 2.0));
}

constexpr Graph chain(std::int32_t length, bool padded) { // n0 … n(length-1): x*1.5, each the last one's *1.5 — or n0 x padded
    Expr y = padded ? padded_along(in("x", Shape{13, 7, 24}), 1) : in("x", Shape{8});
    for (std::int32_t k = padded ? 1 : 0; k < length; ++k) {
        y = y * 1.5;
    }
    return corpus::analysed(finish(Output{"y"} = y));
}

constexpr Graph fan(std::int32_t width) { // n0 … n(width-1): x*k, then their sum, left to right
    Expr x = in("x", Shape{8});
    Expr sum = x * 1.0;
    for (std::int32_t k = 1; k < width; ++k) {
        sum = sum + x * static_cast<double>(k + 1);
    }
    return corpus::analysed(finish(Output{"y"} = sum));
}

// ---- the listings --------------------------------------------------------------------------------------

constexpr Graph kElem = corpus::elem();                          // n0 a*b, n1 +b
constexpr Graph kRowsum = corpus::rowsum<4>();                   // n0 a fold of four columns
constexpr Graph kMatmul = corpus::matmul<3, 4, 5>();             // n0 a contraction
constexpr Graph kTall = corpus::tall(true);                      // n0 a contraction, n1 +bias, n2 relu
constexpr Graph kSoftmax = corpus::softmax();                    // n0 max, n1 x-max, n2 exp, n3 sum, n4 /sum
constexpr Graph kRmsnorm = corpus::rmsnorm();                    // n0 x*x, n1 mean, n2 +eps, n3 sqrt, n4 x/it
constexpr Graph kRowMeans = [] {                                  // [64 d1]: n0 the mean of 64 columns a row
    // its length is symbol 1, which no declaration spells (DS-18)
    const DimDecl r{1, DimConstraints{1, 1}};
    Expr x = detail::input("x", Shape{64, Extent{Dim{r.id}}}, ElemType::F32, r);
    return corpus::analysed(finish(Output{"y"} = x.mean(0)));
}();
constexpr Graph kMedian = corpus::sliding_median(64, 5);         // n0 the window, n1 the median
constexpr Graph kOscillator = corpus::fm_oscillator();           // n0-n4 the phase, handing its state on; n5-n7 the sine
constexpr Graph kLagOfALag = corpus::lag_of_a_lag();             // n0-n6 a cycle of nine voices
constexpr Graph kFir = corpus::fir_over_a_feed();                // n0-n5 the past of a feed
constexpr Graph kDelayLine = corpus::delay_line();               // n0 a = x/2, n1 the delay line
constexpr Graph kPadded = corpus::padded();                      // n0 the padded value, n1 *2
constexpr Graph kBlur = corpus::blur<8, 6, 3>();                 // n0 x*x, n1 the window, n2 *w, n3 the fold
constexpr Graph kBlurxy = corpus::blurxy<8, 6, 3>();             // n0/n3 the windows, n1-n2 and n4-n5 their folds
constexpr Graph kRampPast = ramp_past();                         // n0 the ramp, n1 its past, n2 +x
constexpr Graph kBank = corpus::one_pole_bank(16, 0.5, 64);      // [16 d1]: n0-n3 a one-pole per voice
constexpr Graph kTwoLags = two_lags_and_a_gap();                 // [16 d1]: n0-n9 three lags of one value a voice
constexpr Graph kBankInALoop = bank_in_a_loop();                 // [8 d0]: n0 y[n-1], n2 u, n4 c0·u, n5 u[n-1], n14 the sum

template <const Graph& G, auto Plans>
inline constexpr std::expected<loops::Program, loops::Refusal> kLowered = lowered(G, Plans());

constexpr Schedule elem() { return stages(kElem, {stage({0, 1})}); }
constexpr Schedule rowsum() { return stages(kRowsum, {stage({0})}); }
constexpr Schedule matmul() { return stages(kMatmul, {stage({0})}); }
constexpr Schedule tall() { return stages(kTall, {stage({0, 1, 2})}); }
constexpr Schedule softmax() { return stages(kSoftmax, {stage({0}), stage({1, 2, 3}), stage({4})}); }
constexpr Schedule rmsnorm() { return stages(kRmsnorm, {stage({0, 1, 2, 3}), stage({4})}); }
constexpr Schedule median() { return stages(kMedian, {stage({1})}); }
constexpr Schedule oscillator() { return stages(kOscillator, {stage({0, 1, 2, 3, 4}), stage({5, 6, 7})}); }
constexpr Schedule lag_of_a_lag() { return stages(kLagOfALag, {stage({0, 1, 2, 3, 4, 5, 6})}); }
constexpr Schedule fir() { return stages(kFir, {stage({0, 1, 2, 3, 4, 5})}); }
constexpr Schedule delay_line() { return stages(kDelayLine, {stage({0}), stage({1})}); }
constexpr Schedule padded() { return stages(kPadded, {stage({0}), stage({1})}); }
constexpr Schedule blur() { return stages(kBlur, {stage({0, 2, 3}, 1, 1)}); }
constexpr Schedule blurxy() { return stages(kBlurxy, {stage({1, 2}), stage({4, 5})}); }
constexpr Schedule ramp() { return stages(kRampPast, {stage({1, 2})}); }

constexpr Schedule fold_lanes() { return schedule({planned(stage({0}), {serial(1), lanes(0, 4)}, 1)}); }
constexpr Schedule resume() { return schedule({planned(stage({0}), {serial(1), tile(0, 2), serial(0)}, 2)}); }
constexpr Schedule forced_store() { return schedule({planned(stage({0, 1, 2}), {serial(2), serial(0), serial(1), tile(3, 8), serial(3)}, 4)}); }
constexpr Schedule banked_state() { return schedule({planned(stage({0, 1, 2, 3}), {lanes(0, 8, 2), serial(1)}, 1)}); }
constexpr Schedule state_in_slots() { return schedule({planned(stage({0, 1, 2, 3, 4, 5, 6, 7, 8, 9}), {lanes(0, 8, 2), serial(1)}, 1)}); }
constexpr Schedule network_lanes() { return schedule({planned(stage({1}), {serial(2), lanes(0, 8), serial(1)}, 2)}); }
constexpr Schedule fold_in_feedback() { return in_one_sample(kBankInALoop, 4); }

// Every listing: its name, and the program lowered while the compiler evaluates.
template <class Visit>
void each(const Visit& visit) {
    visit("elem", kLowered<kElem, elem>);
    visit("rowsum", kLowered<kRowsum, rowsum>);
    visit("matmul", kLowered<kMatmul, matmul>);
    visit("tall", kLowered<kTall, tall>);
    visit("softmax", kLowered<kSoftmax, softmax>);
    visit("rmsnorm", kLowered<kRmsnorm, rmsnorm>);
    visit("median", kLowered<kMedian, median>);
    visit("oscillator", kLowered<kOscillator, oscillator>);
    visit("lag_of_a_lag", kLowered<kLagOfALag, lag_of_a_lag>);
    visit("fir", kLowered<kFir, fir>);
    visit("delay_line", kLowered<kDelayLine, delay_line>);
    visit("padded", kLowered<kPadded, padded>);
    visit("blur", kLowered<kBlur, blur>);
    visit("blurxy", kLowered<kBlurxy, blurxy>);
    visit("ramp", kLowered<kRampPast, ramp>);
    visit("fold_lanes", kLowered<kRowsum, fold_lanes>);
    visit("resume", kLowered<kRowsum, resume>);
    visit("forced_store", kLowered<kTall, forced_store>);
    visit("banked_state", kLowered<kBank, banked_state>);
    visit("state_in_slots", kLowered<kTwoLags, state_in_slots>);
    visit("network_lanes", kLowered<kMedian, network_lanes>);
    visit("fold_in_feedback", kLowered<kBankInALoop, fold_in_feedback>);
}

static_assert(kLowered<kElem, elem>.has_value() && kLowered<kRowsum, rowsum>.has_value() && kLowered<kMatmul, matmul>.has_value());
static_assert(kLowered<kTall, tall>.has_value() && kLowered<kSoftmax, softmax>.has_value() && kLowered<kRmsnorm, rmsnorm>.has_value());
static_assert(kLowered<kMedian, median>.has_value() && kLowered<kOscillator, oscillator>.has_value() && kLowered<kLagOfALag, lag_of_a_lag>.has_value());
static_assert(kLowered<kFir, fir>.has_value() && kLowered<kDelayLine, delay_line>.has_value() && kLowered<kPadded, padded>.has_value());
static_assert(kLowered<kBlur, blur>.has_value() && kLowered<kBlurxy, blurxy>.has_value() && kLowered<kRampPast, ramp>.has_value());
static_assert(kLowered<kRowsum, fold_lanes>.has_value() && kLowered<kRowsum, resume>.has_value() && kLowered<kTall, forced_store>.has_value());
static_assert(kLowered<kBank, banked_state>.has_value() && kLowered<kMedian, network_lanes>.has_value() && kLowered<kTwoLags, state_in_slots>.has_value());
static_assert(kLowered<kBankInALoop, fold_in_feedback>.has_value());
// The product a resuming fold makes is stored between chunks: a buffer made after formation, in its place (`02` §2) —
// after the feeds, as the stage's first store, before the relu's.
static_assert(kLowered<kTall, forced_store>->buffers.size() == 5 && kLowered<kTall, forced_store>->buffers[3].value == kTall.nodes[0].output &&
              kLowered<kTall, forced_store>->buffers[4].value == kTall.nodes[2].output);

// What the listings are meant to show, read off the programs: state handed on (the phase: time its one loop), a
// cycle with time outermost handing nothing on, head samples, a relay of parts, buffers in the order of `02` §2.
static_assert(kLowered<kOscillator, oscillator>->nodes[1].kind == loops::NodeKind::Registers &&
              kLowered<kOscillator, oscillator>->nodes[1].role == loops::Role::State, "the phase hands its state on in a register");
static_assert(kLowered<kLagOfALag, lag_of_a_lag>->nodes[3].kind == loops::NodeKind::Compute, "a cycle walked with time outermost has no register point");
static_assert(kLowered<kFir, fir>->stages[0].head == 3, "a feed read three samples back runs its first three apart");
static_assert(kLowered<kPadded, padded>->stages.size() == 4, "a value padded on both sides is made in three parts, then read");
static_assert(kLowered<kRampPast, ramp>->buffers[1].ramp && kLowered<kRampPast, ramp>->buffers[1].prefix == 2, "the ramp after the feed, with its past");
static_assert(kLowered<kTwoLags, state_in_slots>->nodes[2].kind == loops::NodeKind::Registers && kLowered<kTwoLags, state_in_slots>->nodes[2].slots == 2,
              "lags 1 and 2 of the voice handed on in two slots, lag 4 past the gap from memory");

// ---- the refusals ------------------------------------------------------------------------------------------

// Formation (`loops/lower_form.hpp`).
static_assert(refusal(kElem, stages(kElem, {stage({0, 1}, 2)})) == "heinzel::compile: a group inlines a producer and holds nothing of its own to read it");
static_assert(refusal(kElem, stages(kElem, {stage({0, 1}, 1)})) == "heinzel::compile: a group inlines a producer through no window");
static_assert(refusal(kBlur, stages(kBlur, {stage({0, 2, 3}, 1, 2)})) ==
              "heinzel::compile: a group computes a producer through something that is not a window over its own domain");
static_assert(refusal(contraction_then_sum(), stages(contraction_then_sum(), {stage({0, 1})})) == "heinzel::compile: a contraction's group holds no second fold");
static_assert(refusal(folded_two_ways(), stages(folded_two_ways(), {stage({0, 1, 2})})) ==
              "heinzel::compile: sibling folds in one group must collapse the same axes of the same domain");
static_assert(refusal(other_rank_siblings(), stages(other_rank_siblings(), {stage({0, 1, 2})})) ==
              "heinzel::compile: a fold's result does not have the axes its operand keeps");
static_assert(refusal(widened_after_the_fold(), stages(widened_after_the_fold(), {stage({0, 1})})) ==
              "heinzel::compile: a node of a fold group runs over neither the fold's domain nor its result");
static_assert(refusal(blur_and_its_square(), stages(blur_and_its_square(), {stage({0, 2, 3}, 1, 1)})) ==
              "heinzel::compile: a producer computed inside its reader's window is never stored, and something outside the group reads this one");
static_assert(refusal(two_windows(), stages(two_windows(), {stage({0, 2, 3, 5, 6, 7}, 1, 1)})) ==
              "heinzel::compile: a node reads a value of its own group through a window the group does not compute it through");
static_assert(refusal(padded_product(), stages(padded_product(), {stage({1}), stage({0}), stage({2})})) ==
              "heinzel::compile: a part of a padded or joined value copies storage: an input, or what an earlier group made");
static_assert(refusal(kElem, stages(kElem, {stage({0})})) == "heinzel::compile: an output is not the result of a lowered node");

// Operands and statements (`loops/lower_stmts.hpp`).
static_assert(refusal(kElem, stages(kElem, {stage({1, 0})})) ==
              "heinzel::compile: a node reads a value its group has not made yet — the schedule's order is not the graph's");
static_assert(refusal(after_reads_inside(), stages(after_reads_inside(), {stage({0, 1, 2})})) ==
              "heinzel::compile: a node after a fold reads a value that only exists inside the fold's loops");
static_assert(refusal(kBlur, stages(kBlur, {stage({2, 3}), stage({0})})) ==
              "heinzel::compile: a windowed value's source must be stored — an input, or an earlier group's result");
static_assert(refusal(kElem, stages(kElem, {stage({1}), stage({0})})) ==
              "heinzel::compile: a node reads a value that has no place — not an input, a constant, or an earlier node's");
constexpr Graph kBankOfOne = corpus::one_pole_bank(1, 0.5, 64);
static_assert(refusal(kBankOfOne, stages(kBankOfOne, {stage({0}), stage({1, 2, 3})})) ==
              "heinzel::compile: the past of a value that is neither fed, a ramp, nor made by a group has no address");
static_assert(refusal(kDelayLine, stages(kDelayLine, {stage({0, 1})})) ==
              "heinzel::compile: a read whose address is a signal reads storage: an input, or what an EARLIER group made");
static_assert(refusal(table_of_three_axes(), stages(table_of_three_axes(), {stage({0})})) ==
              "heinzel::compile: a table with axes besides its cells and time is not lowered yet");
static_assert(refusal(transposed_product(), stages(transposed_product(), {stage({0, 1})})) ==
              "heinzel::compile: a view reads storage: an input, or what an EARLIER group made");
static_assert(refusal(contraction_of_a_product(), stages(contraction_of_a_product(), {stage({0, 1})})) ==
              "heinzel::compile: a contraction's operand must be stored — an input, or an earlier group's result");
static_assert(refusal(quantile_between_ranks(), stages(quantile_between_ranks(), {stage({0}), stage({1})})) ==
              "heinzel::compile: an order statistic that is not one exact rank of a literal axis is not lowered yet");
static_assert(refusal(broadcast_in_a_map(), stages(broadcast_in_a_map(), {stage({0, 1})})) ==
              "heinzel::compile: a group holds a node that is neither its fold nor a map over the shape it runs at");

// Validation (`loops/validate.hpp`) and the normal form (`02` §4.3 points 7–8), each beside its legal twin.
static_assert(refusal(kRowsum, schedule({planned(stage({0}), {lanes(1, 8), serial(0)}, 1)})) ==
              "heinzel::compile: a Vector loop runs along an axis some buffer is not contiguous in");
static_assert(refusal(strict_rows(), schedule({planned(stage({0}), {serial(1), lanes(0, 4)}, 1)})) ==
              "heinzel::compile: a fold owed its strict order (High precision) may not run in lanes");
static_assert(refusal(strict_rows(), stages(strict_rows(), {stage({0})})) == "lowered");
static_assert(refusal(kRowsum, schedule({planned(stage({0}), {serial(1), unrolled(0, 4)}, 1)})) ==
              "heinzel::compile: only a loop outside a fold's may be unrolled into bodies");
static_assert(refusal(kRowsum, schedule({planned(stage({0}), {serial(1), lanes(0, 2, 2)}, 1)})) == "lowered");
constexpr Schedule::Stage kPaddedParts = walked(kPadded, stage({0}));
static_assert(refusal(kPadded, schedule({kPaddedParts, planned(stage({1}), {serial(2), lanes(1, 4), serial(0)}, 3)})) ==
              "heinzel::compile: a Vector loop is a group's innermost, or outside its fold's or its feedback's loops");
static_assert(refusal(kPadded, schedule({kPaddedParts, planned(stage({1}), {serial(2), serial(0), lanes(1, 4)}, 3)})) == "lowered");
static_assert(refusal(kRowsum, schedule({planned(stage({0}), {unrolled(1, 2), lanes(0, 4)}, 1)})) ==
              "heinzel::compile: lanes along a fold's own axis with rows or lanes outside it");
static_assert(refusal(kRowsum, fold_lanes()) == "lowered");
static_assert(refusal(kRowsum, schedule({planned(stage({0}), {serial(1), tile(0, 2), lanes(0, 2)}, 2)})) ==
              "heinzel::compile: a fold chunked along its own axis resumes per output, so its lanes must run across outputs");
constexpr Schedule::Stage kPhase = walked(kOscillator, stage({0, 1, 2, 3, 4}));
constexpr Schedule::Stage kSine = walked(kOscillator, stage({5, 6, 7}));
static_assert(refusal(kOscillator, schedule({planned(stage({0, 1, 2, 3, 4}), {lanes(0, 8)}, 1), kSine})) ==
              "heinzel::compile: a group that reads its own past walks time one sample at a time, in order");
static_assert(refusal(kOscillator, schedule({kPhase, planned(stage({5, 6, 7}), {lanes(0, 8)}, 1)})) == "lowered");
static_assert(refusal(kFir, schedule({planned(stage({0, 1, 2, 3, 4, 5}), {tile(0, 8), serial(0)}, 2)})) ==
              "heinzel::compile: a group that reads a feed's past may not walk time in chunks");
static_assert(refusal(kFir, schedule({planned(stage({0, 1, 2, 3, 4, 5}), {lanes(0, 8)}, 1)})) == "lowered");
static_assert(refusal(kMedian, schedule({planned(stage({1}), {serial(2), serial(0), tile(1, 2), serial(1)}, 2)})) ==
              "heinzel::compile: an order statistic's group has one inner loop, serial, over exactly the values it gathers");
static_assert(refusal(kRowMeans, schedule({planned(stage({0}), {serial(1), tile(0, 16), serial(0)}, 2)})) ==
              "heinzel::compile: a fold owed its strict order, or one that is finished (a mean), may not be chunked along its own axis");
// A fold that keeps its axes, chunked along one: each chunk's partial result would be kept one chunk further along an
// axis of one (review of M3; softmax's sum, its partial sums stored for the next stage, was lowered so before).
static_assert(refusal(kRmsnorm, schedule({planned(stage({0, 1, 2, 3}), {serial(2), serial(0), tile(1, 16), serial(1)}, 3), walked(kRmsnorm, stage({4}))})) ==
              "heinzel::compile: a fold that keeps its axes is not chunked along one it collapses");
static_assert(refusal(kSoftmax, schedule({walked(kSoftmax, stage({0})), planned(stage({1, 2, 3}), {serial(2), serial(0), tile(1, 16), serial(1)}, 3),
                                       walked(kSoftmax, stage({4}))})) == "heinzel::compile: a fold that keeps its axes is not chunked along one it collapses");
static_assert(refusal(kOscillator, schedule({planned(stage({0, 1, 2, 3, 4}), {tile(0, 1), serial(0)}, 1), kSine})) ==
              "heinzel::compile: a Tile walks chunks of two or more: a chunk of one is a Serial loop");
static_assert(refusal(kOscillator, schedule({planned(stage({0, 1, 2, 3, 4}), {tile(0, 8), serial(0)}, 1), kSine})) ==
              "heinzel::compile: a loop outside the inner loops over an axis they walk chunks a fold, by more than one");
static_assert(refusal(kOscillator, schedule({kPhase, planned(stage({5, 6, 7}), {tile(0, 8), serial(0)}, 2)})) == "lowered");
static_assert(refusal(kRowsum, schedule({planned(stage({0}), {serial(0), serial(1)}, 0)})) ==
              "heinzel::compile: a fold's inner loops walk exactly the axes it collapses");
static_assert(refusal(kPadded, schedule({kPaddedParts, planned(stage({1}), {serial(2), lanes(0, 4), lanes(1, 4)}, 3)})) ==
              "heinzel::compile: a stage's lanes run along one axis");
static_assert(refusal(delay_line_over_a_feed(), stages(delay_line_over_a_feed(), {stage({0})})) ==
              "heinzel::compile: a delay line reads further into the past than its buffer keeps silence for");
static_assert(refusal(median_and_sum(), stages(median_and_sum(), {stage({1, 2, 3})})) == "heinzel::compile: a group holds both a network and a fold");

// A view without an address, before any stage is formed.
static_assert(refusal(window_reshaped(), stages(window_reshaped(), {stage({2})})) ==
              "heinzel::compile: new extents over a sliding window have no address here yet");

// Graphs edited by hand into what the frontend never builds, refused before anything is read off them: the outer of two
// flips moved before the inner one it reads (stored whole, or with x*2 banded, which once cut what the inner flip reads
// to a chunk and computed v wrong), and a value made by two nodes. `compile_pipeline_test` holds the planner to the same.
constexpr Expr flip(Expr x, std::int32_t axis) {
    OpAttr attr{};
    attr.kind = AttrKind::View;
    attr.view.kind = ViewKind::Flip;
    attr.view.axis = axis;
    return detail::append_unary(x, op::Flip, attr);
}
constexpr Graph out_of_order() { // n0 e = x*2, n1 c = e + 1, n2 the outer flip reading n3 the inner one, n4 v = 3 * the flips
    Input x{"x", {8, 6}};
    Expr e = x * 2.0;
    Graph g = finish(Output{"c"} = e + 1.0, Output{"v"} = flip(flip(e, 0), 0) * 3.0);
    std::swap(g.nodes[2], g.nodes[3]);
    return corpus::analysed(g);
}
constexpr Graph made_twice() { // n0 x*2 and n1 x*3 both make a; n2 y = a + 1
    Input x{"x", Signal{4, 19}};
    Expr a = x * 2.0;
    Expr b = x * 3.0;
    Graph g = finish(Output{"y"} = a + 1.0, Output{"z"} = b);
    (void)infer(g); // (the edit keeps the shapes the analysis gave)
    g.nodes[1].output = g.nodes[0].output;
    g.outputs[1].value = g.nodes[0].output;
    return corpus::analysed(g);
}
constexpr Graph reads_itself() { // n0 y = x*2, edited to read y
    Input x{"x", {8, 6}};
    Graph g = finish(Output{"y"} = x * 2.0);
    (void)infer(g); // (the edit keeps the shapes the analysis gave)
    g.nodes[0].inputs[0] = g.nodes[0].output;
    return corpus::analysed(g);
}
constexpr Graph kOutOfOrder = out_of_order();
constexpr Graph kMadeTwice = made_twice();
constexpr Graph kReadsItself = reads_itself();
constexpr Schedule kOutOfOrderStages = stages(kOutOfOrder, {stage({0}), stage({1}), stage({2}), stage({4})});
constexpr std::string_view kLater = "heinzel::compile: a node reads what it or a later node makes";
static_assert(refusal(kOutOfOrder, kOutOfOrderStages) == kLater && refusal(kOutOfOrder, [] {
                  Schedule d = kOutOfOrderStages;
                  d.bands.push_back(band(0, 2, 0, 2));
                  return d;
              }()) == kLater);
static_assert(refusal(kReadsItself, stages(kReadsItself, {stage({0})})) == kLater);
static_assert(refusal(kMadeTwice, stages(kMadeTwice, {stage({0, 1, 2})})) == "heinzel::compile: a value is made by two nodes");

// Capacities.
// n0-n83 slices of x at 84 offsets, each stored by the first stage, then `blocks` stages of 42 selects, each reading
// two slices through Loads and the select before it: 84 statements, then 126 or 127 a stage — four blocks, 591.
constexpr Graph selects_of_slices(std::int32_t blocks) {
    Expr x = in("x", Shape{92, 1});
    fixed_vector<Expr, 84> slices{};
    for (std::int32_t k = 0; k < 84; ++k) {
        slices.push_back(x.slice(0, k, 8));
    }
    Expr last = slices[0];
    for (std::int32_t b = 0; b < blocks; ++b) {
        for (std::size_t j = 0; j < 42; ++j) {
            last = where(slices[2 * j], slices[2 * j + 1], last);
        }
    }
    return corpus::analysed(finish(Output{"y"} = last));
}
constexpr Schedule blocks_of_selects(const Graph& g, std::int32_t blocks) {
    const loops::graph::Facts facts = loops::graph::facts_of(g);
    Schedule d{};
    for (std::int32_t b = -1; b < blocks; ++b) {
        fixed_vector<std::int32_t, loops::kMaxStageStmts> members{};
        for (std::int32_t j = 0; j < (b < 0 ? 84 : 42); ++j) {
            members.push_back(b < 0 ? j : 84 + 42 * b + j);
        }
        d.stages.push_back(walked(g, facts, Schedule::Stage{.nodes = members}));
    }
    return d;
}
constexpr Graph kSelects4 = selects_of_slices(4);
constexpr Graph kSelects3 = selects_of_slices(3);
static_assert(refusal(kSelects4, blocks_of_selects(kSelects4, 4)) == "heinzel::compile: more statements than kMaxStmts; rebuild with -DHEINZEL_MAX_STMTS=N");
static_assert(refusal(kSelects3, blocks_of_selects(kSelects3, 3)) == "lowered");

constexpr Schedule fan_stages(const Graph& g, std::int32_t width) { // the products, then the sums
    fixed_vector<std::int32_t, loops::kMaxStageStmts> products{};
    fixed_vector<std::int32_t, loops::kMaxStageStmts> sums{};
    for (std::int32_t k = 0; k < 2 * width - 1; ++k) {
        (k == 0 || k % 2 == 1 ? products : sums).push_back(k);
    }
    const loops::graph::Facts facts = loops::graph::facts_of(g);
    return schedule({walked(g, facts, Schedule::Stage{.nodes = products}), walked(g, facts, Schedule::Stage{.nodes = sums})});
}
constexpr Schedule one_stage_each(const Graph& g, std::int32_t count) {
    const loops::graph::Facts facts = loops::graph::facts_of(g);
    Schedule d{};
    for (std::int32_t k = 0; k < count; ++k) {
        d.stages.push_back(walked(g, facts, stage({k})));
    }
    return d;
}
constexpr Schedule all_in_one(const Graph& g, std::int32_t count) {
    fixed_vector<std::int32_t, loops::kMaxStageStmts> members{};
    for (std::int32_t k = 0; k < count; ++k) {
        members.push_back(k);
    }
    return schedule({walked(g, Schedule::Stage{.nodes = members})});
}
constexpr Graph kFan96 = fan(96);
constexpr Graph kFan95 = fan(95);
constexpr Graph kChain128 = chain(128, false);
constexpr Graph kChain127 = chain(127, false);
constexpr Graph kPadded16 = chain(16, true);
constexpr Graph kPadded14 = chain(14, true);
static_assert(refusal(kFan96, fan_stages(kFan96, 96)) == "heinzel::compile: more buffers than kMaxBuffers; rebuild with -DHEINZEL_MAX_BUFFERS=N");
static_assert(refusal(kFan95, fan_stages(kFan95, 95)) == "heinzel::compile: a stage over kMaxStageStmts; rebuild with -DHEINZEL_MAX_STAGE_STMTS=N"); // the buffers fit: the sums do not
static_assert(refusal(kChain128, all_in_one(kChain128, 128)) == "heinzel::compile: a stage over kMaxStageStmts; rebuild with -DHEINZEL_MAX_STAGE_STMTS=N");
static_assert(refusal(kChain127, all_in_one(kChain127, 127)) == "lowered");
static_assert(refusal(kPadded16, one_stage_each(kPadded16, 16)) == "heinzel::compile: more stages than kMaxStages; rebuild with -DHEINZEL_MAX_STAGES=N");
static_assert(refusal(kPadded14, one_stage_each(kPadded14, 14)) == "lowered");

std::string pinned(const std::string& path) {
    std::ifstream in(path);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

} // namespace

int main() {
    const bool repin = std::getenv("HEINZEL_REPIN") != nullptr;
    int wrong = 0;
    each([&](const char* name, const std::expected<loops::Program, loops::Refusal>& p) {
        const std::string path = std::string{HEINZEL_LOOPS_LOWER_GOLDENS} + "/" + name + ".txt";
        const std::string got = loops::print(*p);
        if (repin) {
            std::ofstream(path) << got;
        } else if (got != pinned(path)) {
            ++wrong;
            std::printf("%s: the tree differs from %s:\n%s\n", name, path.c_str(), got.c_str());
        }
    });
    if (wrong != 0) {
        std::printf("%d trees differ from their listings\n", wrong);
        return 1;
    }
    return 0;
}
