// The seam's first half: an operation's meaning is written once, and the table knows which
// operations owe one.
//
// The checks themselves fire while this file is compiled — `tests/reference/meaning_coverage.hpp`
// asserts both of them over the real table, so including the header at all is part of the evidence.
// What is left here is the half a passing assertion cannot show: that each check would OBJECT if the
// tree were wrong, which is asked by handing it a deliberately broken input.
//
// The other half of M3.4 — that the build actually refuses — is not here, and cannot be: a test that
// fails to compile is not a test. It is `build_refuses_row_with_no_meaning` and
// `build_refuses_meaning_with_no_row`, which build the two fixtures under `gates/fixtures/` and
// require the attempt to fail.
#include "compute_reference.hpp"
#include "incomplete_operation.hpp"
#include "iota_element.hpp"
#include "meaning_coverage.hpp"

using namespace heinzel;

// ---- the column discriminates -------------------------------------------------------------------
// A column that read the same in every row would prove nothing about being a column.
static_assert(rules_of(op::Add).meaning == MeaningRule::PerSample);
static_assert(rules_of(op::FixedDelay).meaning == MeaningRule::PerSample,
              "a delay hands back the sample it was given; WHICH sample is the caller's business");
static_assert(rules_of(op::Reduce).meaning == MeaningRule::AxisChanging);
static_assert(rules_of(op::Reshape).meaning == MeaningRule::AxisChanging);
static_assert(rules_of(op::Contract).meaning == MeaningRule::AxisChanging,
              "a contraction sums over axes, so a sample-at-a-time walk has nothing to say about it");

// It is not the shape column under another name. A delay is Passthrough-shaped and has a per-sample
// meaning; a rearrangement is ByViewKind-shaped and has none. Deriving one from the other happens to
// work for these thirteen operations, and that is exactly why it is worth not doing.
static_assert(rules_of(op::FixedDelay).shape == ShapeRule::Passthrough &&
                  rules_of(op::FixedDelay).meaning == MeaningRule::PerSample);

// ---- the writing answers for every row that says it should ---------------------------------------
static_assert(every_per_sample_operation_has_a_meaning(kOpTable, kOpCount));
static_assert(no_meaning_beyond(kOpCount));

// ---- and both are shown REJECTING ----------------------------------------------------------------
// A row that says it has a per-sample meaning and has none written.
static_assert(!every_per_sample_operation_has_a_meaning(kRowSaysItHasAMeaning.data(), kOpCount),
              "the coverage check must object to a row that owes a meaning nobody wrote");

// A meaning written for an operation code past the end of the table. Ten rows, eleven meanings.
static_assert(!no_meaning_beyond(10),
              "the coverage check must object to arithmetic written for an operation with no row");

// The two are independent: a table one row short still has every row it DOES have answered for, so
// the first check stays happy while the second objects. That is what makes them two conditions.
static_assert(every_per_sample_operation_has_a_meaning(kOpTable, 10) && !no_meaning_beyond(10));

// ---- an operation with no per-sample meaning says so rather than answering ------------------------
// `compute_if_written` reports that nothing was written; `compute` refuses. Both directions, because
// a runner that got a plausible number here would be wrong in a way no comparison could see: every
// runner would agree on it.
constexpr Node reduction = [] {
    Node n{};
    n.op = op::Reduce;
    n.attr.kind = AttrKind::Reduce;
    return n;
}();

static_assert(!compute_if_written(reduction, FnAccuracy::Exact, 2.0, 3.0, 4.0).written);
static_assert(compute_if_written(reduction, FnAccuracy::Exact, 2.0, 3.0, 4.0).value == 0.0,
              "and it does not hand back a number that looks like an answer");

// The same, for the operation M4 added to that class. Two rows classified `AxisChanging` and both
// refusing is what makes the column a column rather than a fact about `_reduce`.
constexpr Node contraction = [] {
    Node n{};
    n.op = op::Contract;
    n.attr.kind = AttrKind::Contract;
    return n;
}();

static_assert(!compute_if_written(contraction, FnAccuracy::Exact, 2.0, 3.0, 4.0).written);

// `_fft` (milestone M5, S4a) is `AxisChanging` too, but its element meaning is `Boundary`, not one of
// the runnable kinds: a DECISION that the element comes from an intrinsic backend deferred to M13, the
// opposite of the half-done `None`. So it satisfies the axis-changing coverage gate (`Boundary != None`)
// yet has NO per-sample arithmetic written — which is what makes the runners' `Boundary` arm the only
// path a run of it can take, a clear refusal rather than a plausible wrong number.
static_assert(rules_of(op::Fft).meaning == MeaningRule::AxisChanging);
static_assert(element_kind_of(op::Fft) == ElementKind::Boundary,
              "a boundary is a decision about the element meaning, distinct from None's absence of one");
constexpr Node transform = [] {
    Node n{};
    n.op = op::Fft;
    n.attr.kind = AttrKind::Fft;
    return n;
}();
static_assert(!compute_if_written(transform, FnAccuracy::Exact, 2.0, 3.0, 4.0).written,
              "a boundary op has no per-sample meaning, so a runner cannot reach a number for it");


// `_ovadd` (milestone M5, S4c) is `AxisChanging` too, and its element meaning is the FIFTH kind — a
// RUNNABLE one, `Scatter`: one output element is the SUM of the operand's elements a sliding window places
// onto it, the adjoint of the rearrangement `_unfold` is. It is not `Fold` (a fixed-fan-in collapse over
// named axes), not `Joined`, not `Boundary` — a windowed scatter-add is its own shape of meaning, so it
// got its own kind, exactly as `_fft` got `Boundary`. Like a fold and a rearrangement it has no
// sample-at-a-time arithmetic — its meaning is an INDEXING, in `tests/reference/overlap_add_element.hpp`.
static_assert(rules_of(op::OverlapAdd).meaning == MeaningRule::AxisChanging);
static_assert(element_kind_of(op::OverlapAdd) == ElementKind::Scatter,
              "an overlap-add sums the frames a sliding window scatters onto each output element");
constexpr Node scatter = [] {
    Node n{};
    n.op = op::OverlapAdd;
    n.attr.kind = AttrKind::OverlapAdd;
    return n;
}();
static_assert(!compute_if_written(scatter, FnAccuracy::Exact, 2.0, 3.0, 4.0).written,
              "an overlap-add changes axes, so a sample-at-a-time walk has no number for it");

// `_order` (milestone M5, S5) is `AxisChanging` too, and its element meaning is the SIXTH kind — a RUNNABLE
// one, `Ordered`: one output element is an ordered rank of the operand's elements along one axis, produced
// by a compare-exchange network. It is not `Fold` (a monoid collapse over named axes), not `Scatter`, not
// `Boundary` — a whole-axis order statistic is its own shape of meaning, so it got its own kind, exactly as
// `_ovadd` got `Scatter`. Like a fold it has no sample-at-a-time arithmetic — its meaning is an INDEXING and
// a network, in `tests/reference/order_element.hpp`.
static_assert(rules_of(op::Order).meaning == MeaningRule::AxisChanging);
static_assert(element_kind_of(op::Order) == ElementKind::Ordered,
              "an order statistic sorts, selects or interpolates the operand's elements along one axis");
constexpr Node ordered = [] {
    Node n{};
    n.op = op::Order;
    n.attr.kind = AttrKind::Order;
    return n;
}();
static_assert(!compute_if_written(ordered, FnAccuracy::Exact, 2.0, 3.0, 4.0).written,
              "an order statistic changes axes, so a sample-at-a-time walk has no number for it");

// `_tblread` (S6) is `AxisChanging` too, and its element meaning is the fourth RUNNABLE one, `Gathered`: one
// output element is a blend of TWO of the table's elements at a data-dependent index. It is not `Fold`, not
// `Scatter`, not `Ordered` — a wavetable gather is its own shape of meaning, so it got its own kind, exactly
// as `_order` got `Ordered`. `compute()` has no number for it because the two table elements sit at a
// data-dependent index the caller must resolve — its meaning is an INDEXING (`table_tap`) and a blend
// (`lerp2`), in `runtime/table_gather_element.hpp`.
static_assert(rules_of(op::TableRead).meaning == MeaningRule::AxisChanging);
static_assert(element_kind_of(op::TableRead) == ElementKind::Gathered,
              "a table read blends two of the table's elements at a data-dependent index");
constexpr Node gathered = [] {
    Node n{};
    n.op = op::TableRead;
    n.attr.kind = AttrKind::Table;
    return n;
}();
static_assert(!compute_if_written(gathered, FnAccuracy::Exact, 2.0, 3.0, 4.0).written,
              "a table read gathers a data-dependent index, so a sample-at-a-time walk has no number for it");

// `_iota` (S6) is `AxisChanging` too, and its element meaning is the sixth RUNNABLE one, `Indexed`: one
// output element IS its own index. It is not `Fold`, not `Rearrange`, not `Gathered` — a generator produces
// its value from the POSITION rather than from any operand, so it got its own kind. `compute()` has no number
// for it because it is handed operand values and no index — its meaning is `iota_value`, in
// `tests/reference/iota_element.hpp`, and the value at element e is e.
static_assert(rules_of(op::Iota).meaning == MeaningRule::AxisChanging);
static_assert(element_kind_of(op::Iota) == ElementKind::Indexed,
              "an index ramp's element is its own position, not a fold or a rearrangement of an operand");
// `_const`, the other generator, is `Pooled`: its element is a number of the graph's pool.
static_assert(rules_of(op::Constant).meaning == MeaningRule::AxisChanging && element_kind_of(op::Constant) == ElementKind::Pooled,
              "a constant's element is a number of the pool");
// A link (`_chain`) is `Linked`: element j is operand 1's element j - 1, so it reads across elements, which a
// sample-at-a-time computation is handed no number for.
static_assert(rules_of(op::Chain).meaning == MeaningRule::AxisChanging && element_kind_of(op::Chain) == ElementKind::Linked,
              "a link's element is the section before's");
constexpr Node linked = [] {
    Node n{};
    n.op = op::Chain;
    return n;
}();
static_assert(!compute_if_written(linked, FnAccuracy::Exact, 2.0, 3.0, 4.0).written,
              "a link reads the element before, which a sample-at-a-time walk has no number for");
// A run's start (`_first`) is `Started`: its value is where the run stands, which no operand carries, so a
// sample-at-a-time computation has no number for it; the walk is told which samples start a run (`interpret.hpp`).
static_assert(rules_of(op::First).meaning == MeaningRule::AxisChanging && element_kind_of(op::First) == ElementKind::Started,
              "a run start's element is whether the sample starts a run");
constexpr Node started = [] {
    Node n{};
    n.op = op::First;
    return n;
}();
static_assert(!compute_if_written(started, FnAccuracy::Exact, 2.0, 3.0, 4.0).written,
              "a run's start is no number of its operands");
constexpr Node indexed = [] {
    Node n{};
    n.op = op::Iota;
    n.attr.kind = AttrKind::Iota;
    return n;
}();
static_assert(!compute_if_written(indexed, FnAccuracy::Exact, 2.0, 3.0, 4.0).written,
              "an index ramp's value is its position, which a sample-at-a-time walk is handed no number for");
// The one writing, checked directly: the value at element e is e. The runners round it to the result's type.
static_assert(iota_value(0) == 0.0 && iota_value(1) == 1.0 && iota_value(2) == 2.0 && iota_value(3) == 3.0,
              "the ramp is out[e] = e");

// The two lags share one line of the writing and hand back what they were given, which is what makes
// them one meaning rather than two: what separates them is where the operand was fetched from, and
// that is the caller's business rather than the arithmetic's.
constexpr Node lag(OpCode which, AttrKind kind) {
    Node n{};
    n.op = which;
    n.attr.kind = kind;
    return n;
}

static_assert(compute(lag(op::FixedDelay, AttrKind::Delay), FnAccuracy::Exact, 7.0, 0.0, 0.0) == 7.0);

// `_realize` shares that hand-back line and carries no attribute: value for value it is the identity, a
// materialisation barrier whose copy the scheduler reads at M8 and whose arithmetic here is `out == in`.
constexpr Node realized = [] {
    Node n{};
    n.op = op::Realize;
    return n;
}();
static_assert(compute(realized, FnAccuracy::Exact, 7.0, 0.0, 0.0) == 7.0);
static_assert(rules_of(op::Realize).meaning == MeaningRule::PerSample,
              "a realize has a sample-at-a-time meaning: it hands its operand straight back");

// The ordinary case, so the assertions above are not passing because the probe is broken.
constexpr Node addition = [] {
    Node n{};
    n.op = op::Add;
    return n;
}();

static_assert(compute_if_written(addition, FnAccuracy::Exact, 2.0, 3.0, 4.0) == Computed{5.0, true});
static_assert(compute(addition, FnAccuracy::Exact, 2.0, 3.0, 4.0) == 5.0);

int main() {
    // Repeated while the program runs, which is the half a static_assert cannot show: one body,
    // giving the same answer in both worlds.
    return (compute(addition, FnAccuracy::Exact, 2.0, 3.0, 4.0) == 5.0 && !compute_if_written(reduction, FnAccuracy::Exact, 2.0, 3.0, 4.0).written) ? 0 : 1;
}
