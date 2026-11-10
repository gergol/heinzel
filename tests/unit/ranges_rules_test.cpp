// The proof's rules (Plan/26-value-ranges/03), one column value at a time: for each range rule and each domain of the
// rule table, a small graph, its exact range and its findings, with open and closed ends, 0 times infinity and a NaN
// end; then ranges of several pieces, merged across the smallest gap where they outgrow `kRangePieces`. The walk's
// own rules (past reads, induction, promises) and the wording are `ranges_walk_test.cpp`; the oracle is
// `ranges_oracle_test.cpp`. Checked while compiling and again at run time.
#include <bit>
#include <cstdio>
#include <limits>

#include "heinzel/ranges/interval.hpp"
#include "heinzel/ranges/proven.hpp"

#include "ranged_graphs.hpp"

using namespace heinzel;
using namespace heinzel::testing;

namespace {

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
constexpr Range kUnit = made_of(closed(-1, 1));
constexpr Range kZeroOne = made_of(closed(0, 1));

constexpr OpAttr view(ViewKind kind) {
    OpAttr a{};
    a.kind = AttrKind::View;
    a.view.kind = kind;
    return a;
}

// ---- same, covering ------------------------------------------------------------------------------------------

constexpr bool same_and_covering() {
    OpAttr cast{};
    cast.kind = AttrKind::Cast;
    cast.cast.to = ElemType::F32;
    OpAttr tap{};
    tap.kind = AttrKind::SignalledDelay;
    tap.signalled_delay.max_delay = 4;
    OpAttr table{};
    table.kind = AttrKind::Table;
    table.table.size = 4;
    OpAttr median{};
    median.kind = AttrKind::Order;
    median.order.op = OrderOp::Quantile;
    median.order.q = 0.5;
    OpAttr sorted = median;
    sorted.order.op = OrderOp::Sort;
    const Range holed = made_of(closed(-1, -0.5), closed(0.5, 1));
    const Range wide = made_of(closed(0, 1), closed(2, 3));
    return result_of(op::Realize, made_of(open_closed(0, 1))) == made_of(open_closed(0, 1)) &&
           result_of(op::Cast, kUnit, Range{}, Range{}, cast) == kUnit &&
           // a delay line blends two taps of its past, silence among them: one interval over both
           result_of(op::DelayLine, holed, made_of(closed(0, 3)), Range{}, tap) == kUnit &&
           result_of(op::DelayLine, made_of(closed(1, 2)), made_of(closed(0, 3)), Range{}, tap) == made_of(closed(0, 2)) &&
           result_of(over_ports(op::TableRead, Port{ranges::whole()}, Port{wide, Shape{4, 8}}, Port{}, table)) == made_of(closed(0, 3)) &&
           result_of(one_operation(op::Order, holed, Range{}, Range{}, median, Shape{4, 8})) == kUnit &&
           result_of(one_operation(op::Order, holed, Range{}, Range{}, sorted, Shape{4, 8})) == kUnit &&
           result_of(one_operation(op::Slice, holed, Range{}, Range{}, [] {
               OpAttr a = view(ViewKind::Slice);
               a.view.length = 2;
               return a;
           }(), Shape{4, 8})) == holed;
}

// ---- sums and products -----------------------------------------------------------------------------------------

constexpr bool sums() {
    return result_of(op::Add, kZeroOne, made_of(open_closed(0, 1))) == made_of(open_closed(0, 2)) &&
           result_of(op::Add, made_of(closed_open(0, 1)), made_of(closed(1, 2))) == made_of(closed_open(1, 3)) &&
           result_of(op::Add, ranges::span(0, kInfinity, true), ranges::span(-kInfinity, 1)) == ranges::whole() &&
           result_of(op::Sub, kZeroOne, made_of(open_closed(0, 1))) == made_of(closed_open(-1, 1)) &&
           result_of(op::Neg, made_of(open_closed(0, 1))) == made_of(closed_open(-1, 0));
}

// One value on both sides of a product is a square.
constexpr Graph squared(OpCode op, Range x, OpAttr attr = OpAttr{}, Shape shape = Shape{8}) {
    Graph g{};
    const std::int32_t in = add_value(g, ElemType::F64, shape, ValueCategory::Input);
    g.inputs.push_back(NamedValue{Name{"x"}, in});
    add_range(g, in, x);
    std::int32_t out = kNoValue;
    if (rules_of(op).arity == 3) {
        const std::int32_t one = add_value(g, ElemType::F64, shape, ValueCategory::Const);
        g.values[static_cast<std::size_t>(one)].constant = 1.0;
        out = add_node(g, op, in, in, one, attr);
    } else {
        out = add_node(g, op, in, in, kNoValue, attr);
    }
    g.outputs.push_back(NamedValue{Name{"y"}, out});
    return analysed(g);
}

constexpr bool products() {
    const Range from_one = ranges::span(1, kInfinity);
    return result_of(op::Mul, kZeroOne, made_of(open_closed(0, 1))) == kZeroOne && // a closed 0 factor gives a closed 0
           result_of(op::Mul, made_of(open_closed(0, 1)), made_of(open_closed(0, 1))) == made_of(open_closed(0, 1)) &&
           result_of(op::Mul, made_of(closed(-1, 2)), made_of(closed(-3, 1))) == made_of(closed(-6, 3)) &&
           result_of(op::Mul, kZeroOne, from_one) == ranges::span(0, kInfinity) && // 0 times infinity counts as 0
           result_of(op::Mul, made_of(open_closed(0, 1)), from_one) == ranges::span(0, kInfinity, true) &&
           result_of(squared(op::Mul, made_of(closed(-1, 2)))) == made_of(closed(0, 4)) &&
           result_of(squared(op::Mul, made_of(open_closed(-2, -1)))) == made_of(closed_open(1, 4)) &&
           result_of(squared(op::Mul, made_of(closed_open(-1, 0), open_closed(0, 1)))) == made_of(open_closed(0, 1)) &&
           result_of(op::Fma, kZeroOne, made_of(closed(0, 2)), made_of(closed(1, 1))) == made_of(closed(1, 3)) &&
           result_of(op::MultiplyAdd, kZeroOne, made_of(closed(0, 2)), made_of(closed(1, 1))) == made_of(closed(1, 3)) &&
           result_of(squared(op::Fma, kUnit)) == made_of(closed(1, 2)) && result_of(squared(op::MultiplyAdd, kUnit)) == made_of(closed(1, 2));
}

constexpr bool quotients() {
    const Range one = made_of(closed(1, 1));
    return result_of(op::Div, one, made_of(open_closed(0, 2))) == ranges::span(0.5, kInfinity) &&
           result_of(op::Div, made_of(closed(1, 2)), made_of(closed(-2, -1))) == made_of(closed(-2, -0.5)) &&
           result_of(op::Div, kZeroOne, made_of(open_closed(0, 1))) == ranges::span(0, kInfinity) &&
           result_of(op::Div, one, made_of(closed_open(-1, 0), open_closed(0, 1))) ==
               made_of(Interval{-kInfinity, -1, true, false}, Interval{1, kInfinity, false, true}) &&
           // [0, 1]·(0, 1] holds 0, so 1/(a·b) divides by it
           findings_of(op::Div, one, made_of(closed(0, 1))) == 1;
}

// ---- monotone rules, and the limit rule -------------------------------------------------------------------------

constexpr bool monotone() {
    return result_of(op::Floor, made_of(open(0.5, 2.5))) == made_of(closed(0, 2)) &&
           result_of(op::Ceil, made_of(open(0.5, 2.5))) == made_of(closed(1, 3)) &&
           result_of(op::Round, made_of(open(0.5, 2.5))) == made_of(closed(0, 2)) && // ties to even
           result_of(op::Trunc, made_of(open(-1.5, 1.5))) == kUnit &&
           result_of(op::Floor, ranges::span(-kInfinity, 2)) == ranges::span(-kInfinity, 2) &&
           result_of(op::Sign, made_of(open_closed(0, 5))) == kZeroOne && // non-strict: both ends closed
           result_of(op::Sqrt, made_of(closed_open(0, 4))) == made_of(closed_open(0, 2)) &&
           result_of(op::Exp, ranges::span(-kInfinity, 0)) == made_of(open_closed(0, 1)) &&
           result_of(op::Log, made_of(open_closed(0, 1))) == ranges::span(-kInfinity, 0) &&
           result_of(op::Tanh, ranges::whole()) == made_of(open(-1, 1)) &&
           // a finite end whose image rounds onto the limit stays open: exp(-1e30) is 0.0, tanh(1000) is 1.0
           result_of(op::Exp, made_of(closed(-1e30, 0))) == made_of(open_closed(0, 1)) &&
           result_of(op::Tanh, made_of(closed(0, 1000))) == made_of(closed_open(0, 1)) &&
           result_of(op::Exp, made_of(closed(0, 1000))) == ranges::span(1, kInfinity) &&
           result_of(op::Rsqrt, made_of(open_closed(0, 4))) == ranges::span(0.5, kInfinity) &&
           result_of(op::Rsqrt, ranges::span(1, kInfinity)) == made_of(open_closed(0, 1));
}

// min and max take each end from the operand that decides it; on a tie, a maximum's lower end is reached only when both
// are, its upper end when either is, and a minimum is the mirror image.
constexpr bool minimum_and_maximum() {
    return result_of(op::Max, kZeroOne, made_of(open_closed(-1, 2))) == made_of(closed(0, 2)) &&
           result_of(op::Max, kZeroOne, made_of(open(0, 1))) == made_of(open_closed(0, 1)) &&
           result_of(op::Min, kZeroOne, made_of(open(0, 1))) == made_of(closed_open(0, 1)) &&
           result_of(op::Min, ranges::span(-kInfinity, 1), made_of(closed(0, 2))) == ranges::span(-kInfinity, 1) &&
           result_of(op::Max, made_of(closed(-2, -1), closed(1, 2)), made_of(closed(0, 0))) == made_of(closed(0, 0), closed(1, 2));
}

// abs: a piece without a negative number as it is, one without a positive number negated, one across 0 from a closed 0
// to the larger end's magnitude, open where that end is (on a tie, where both are); the pieces then joined.
constexpr bool magnitudes() {
    return result_of(op::Abs, made_of(closed(-3, 2))) == made_of(closed(0, 3)) &&
           result_of(op::Abs, made_of(open_closed(-3, 2))) == made_of(closed_open(0, 3)) &&
           result_of(op::Abs, made_of(closed_open(-1, 2))) == made_of(closed_open(0, 2)) &&
           result_of(op::Abs, made_of(open(-2, 2))) == made_of(closed_open(0, 2)) &&
           result_of(op::Abs, made_of(closed_open(-2, 2))) == made_of(closed(0, 2)) &&
           result_of(op::Abs, made_of(open_closed(-5, -1))) == made_of(closed_open(1, 5)) &&
           result_of(op::Abs, made_of(open(1, 4))) == made_of(open(1, 4)) &&
           result_of(op::Abs, made_of(closed(-4, -3), closed(1, 2))) == made_of(closed(1, 2), closed(3, 4)) &&
           result_of(op::Abs, made_of(closed(-2, -1), closed(1, 3))) == made_of(closed(1, 3)) &&
           result_of(op::Abs, made_of(closed_open(-1, 0), open_closed(0, 1))) == made_of(open_closed(0, 1)) &&
           result_of(op::Abs, ranges::whole()) == ranges::span(0, kInfinity) &&
           result_of(op::Abs, ranges::span(-kInfinity, -2)) == ranges::span(2, kInfinity);
}

constexpr bool fixed_sets() {
    const Range angle = made_of(closed(-ranges::kPiAbove, ranges::kPiAbove));
    return result_of(op::Sin, ranges::whole()) == kUnit && result_of(op::Cos, ranges::whole()) == kUnit &&
           result_of(op::SinPi, ranges::whole()) == kUnit && result_of(op::Lt, ranges::whole(), ranges::whole()) == kZeroOne &&
           result_of(op::Le, ranges::whole(), ranges::whole()) == kZeroOne && result_of(op::Eq, ranges::whole(), ranges::whole()) == kZeroOne &&
           result_of(op::Atan2, ranges::whole(), made_of(closed(1, 2))) == angle && result_of(op::Atan, ranges::whole()) == angle &&
           result_of(op::Asin, kUnit) == angle && result_of(op::Acos, kUnit) == angle &&
           result_of(op::Pow, kZeroOne, made_of(closed(1, 2))) == ranges::span(0, kInfinity) &&
           result_of(op::Exp2, ranges::whole()) == ranges::span(0, kInfinity, true) &&
           // no double writing, so no range; the domain is still checked
           result_of(op::Log2, made_of(open_closed(0, 1))) == ranges::whole() && findings_of(op::Log2, kZeroOne) == 1 &&
           result_of(op::Log10, made_of(open_closed(0, 1))) == ranges::whole() && result_of(op::Tan, kUnit) == ranges::whole();
}

// ---- choices, unions, sums of terms, generators ------------------------------------------------------------------

constexpr bool choices_and_unions() {
    OpAttr pad = view(ViewKind::Pad);
    pad.view.pad_before = 1;
    pad.view.pad_after = 1;
    pad.view.pad_bits = std::bit_cast<std::uint64_t>(5.0);
    OpAttr nan_pad = pad;
    nan_pad.view.pad_bits = std::bit_cast<std::uint64_t>(kNan);
    const Range low = kZeroOne;
    const Range high = made_of(closed(3, 4));
    return result_of(op::Select, kZeroOne, low, high) == made_of(closed(0, 1), closed(3, 4)) &&
           result_of(one_operation(op::Concat, low, high, Range{}, view(ViewKind::Concat), Shape{2, 8})) == made_of(closed(0, 1), closed(3, 4)) &&
           result_of(one_operation(op::Pad, low, Range{}, Range{}, pad, Shape{3, 8})) == made_of(closed(0, 1), closed(5, 5)) &&
           result_of(one_operation(op::Pad, low, Range{}, Range{}, nan_pad, Shape{3, 8})) == ranges::whole(); // a NaN end: any number
}

constexpr OpAttr folding(ReduceOp op) {
    OpAttr a{};
    a.kind = AttrKind::Reduce;
    a.reduce.op = op;
    a.reduce.axes = Axes{0};
    return a;
}

constexpr bool sums_of_terms() {
    OpAttr contract{};
    contract.kind = AttrKind::Contract;
    contract.contract.axes = Axes{0};
    OpAttr matmul{};
    matmul.kind = AttrKind::MatMul;
    matmul.matmul.a_contract = 1;
    OpAttr frames{};
    frames.kind = AttrKind::OverlapAdd;
    frames.overlap.window = 4;
    frames.overlap.hop = 2;
    const Shape wide{4, 8};
    const Range two_pieces = made_of(closed(0, 1), closed(2, 3));
    const auto folded = [&](ReduceOp op, Range r) { return result_of(one_operation(op::Reduce, r, Range{}, Range{}, folding(op), wide)); };
    return folded(ReduceOp::Sum, kZeroOne) == made_of(closed(0, 4)) && folded(ReduceOp::Sum, made_of(closed_open(-1, 0.5))) == made_of(closed_open(-4, 2)) &&
           folded(ReduceOp::Mean, two_pieces) == made_of(closed(0, 3)) && folded(ReduceOp::Max, two_pieces) == made_of(closed(0, 3)) &&
           folded(ReduceOp::Min, two_pieces) == made_of(closed(0, 3)) && folded(ReduceOp::Prod, kZeroOne) == ranges::whole() &&
           result_of(one_operation(op::Contract, kZeroOne, kUnit, Range{}, contract, wide)) == made_of(closed(-4, 4)) &&
           result_of(squared(op::Contract, made_of(closed(-1, 2)), contract, wide)) == made_of(closed(0, 16)) &&
           // a[i,k] times a[k,j] is no square
           result_of(squared(op::MatMul, made_of(closed(-1, 2)), matmul, Shape{3, 3, 8})) == made_of(closed(-6, 12)) &&
           result_of(over_ports(op::MatMul, Port{kUnit, Shape{2, 3, 8}}, Port{made_of(closed(0, 2)), Shape{3, 2, 8}}, Port{}, matmul)) ==
               made_of(closed(-6, 6)) &&
           // between none and two frames at an element: the edges have fewer
           result_of(one_operation(op::OverlapAdd, made_of(closed(1, 2)), Range{}, Range{}, frames, Shape{3, 4, 8})) == made_of(closed(0, 4)) &&
           result_of(one_operation(op::OverlapAdd, kUnit, Range{}, Range{}, frames, Shape{3, 4, 8})) == made_of(closed(-2, 2)) &&
           // a count known only later: the sign the terms share
           ranges::detail::terms(kZeroOne, -1) == ranges::span(0, kInfinity) &&
           ranges::detail::terms(made_of(open_closed(-1, 0)), -1) == ranges::span(-kInfinity, 0) && ranges::detail::terms(kUnit, -1) == ranges::whole();
}

constexpr Graph generator(AttrKind kind, double nan_at) {
    Graph g{};
    OpAttr a{};
    a.kind = kind;
    a.generator.shape = Shape{4, 8};
    a.generator.type = ElemType::F64;
    if (kind == AttrKind::Constant) {
        a.generator.first = 0;
        for (const double number : {0.5, -1.25, 2.0, 3.5}) {
            push_constant(g, number == nan_at ? kNan : number);
        }
    }
    g.outputs.push_back(NamedValue{Name{"y"}, add_node(g, kind == AttrKind::Iota ? op::Iota : op::Constant, kNoValue, kNoValue, kNoValue, a)});
    return analysed(g);
}

constexpr bool generators() {
    return result_of(generator(AttrKind::Iota, 0)) == made_of(closed(0, 3)) &&
           result_of(generator(AttrKind::Constant, 0)) == made_of(closed(-1.25, 3.5)) &&
           result_of(generator(AttrKind::Constant, 2.0)) == ranges::whole();
}

// ---- the domains ----------------------------------------------------------------------------------------------

constexpr bool domains() {
    const Range one = made_of(closed(1, 1));
    const Range two = made_of(closed(1, 2));
    return findings_of(op::Div, one, made_of(closed(0, 2))) == 1 && findings_of(op::Div, one, made_of(open_closed(0, 2))) == 0 &&
           findings_of(op::Div, one, made_of(closed_open(-1, 0), open_closed(0, 1))) == 0 &&
           findings_of(op::Sqrt, kUnit) == 1 && findings_of(op::Sqrt, kZeroOne) == 0 &&
           findings_of(op::Log, kZeroOne) == 1 && findings_of(op::Log, made_of(open_closed(0, 1))) == 0 &&
           findings_of(op::Rsqrt, kZeroOne) == 1 && findings_of(op::Log10, kZeroOne) == 1 &&
           findings_of(op::Asin, made_of(closed(-2, 2))) == 1 && findings_of(op::Asin, kUnit) == 0 &&
           findings_of(op::Acos, made_of(open_closed(-1.5, 0))) == 1 &&
           findings_of(op::Tan, kUnit) == 0 && findings_of(op::Tan, two) == 1 &&
           findings_of(op::Tan, made_of(closed(-1, -0.5), closed(3, 3.5))) == 0 &&
           findings_of(op::Tan, made_of(closed(-1, -0.5), closed(1, 2))) == 1 && // one piece of two leaves its branch
           findings_of(op::Atan2, kUnit, kUnit) == 1 && findings_of(op::Atan2, kUnit, two) == 0 &&
           findings_of(op::Atan2, made_of(open_closed(0, 1)), kUnit) == 0 &&
           findings_of(op::Pow, kZeroOne, two) == 0 && findings_of(op::Pow, kZeroOne, kUnit) == 1 &&
           findings_of(op::Pow, kUnit, two) == 1 && findings_of(op::Pow, made_of(open_closed(0, 1)), kUnit) == 0 &&
           findings_of(op::Log, made_of(closed(-2, -1), closed(1, 2))) == 1 && // a domain that fails on one piece only
           findings_of(op::Sqrt, ranges::point(kNan)) == 1;                    // a NaN is any number, inside no domain
}

// After a finding, the result is any number.
constexpr bool after_a_finding() {
    const Graph g = one_operation(op::Sqrt, kUnit);
    const ranges::Proof p = ranges::proof(g);
    return p.findings.size() == 1 && p.findings[0].operand == 0 && p.findings[0].range == kUnit &&
           p.values[static_cast<std::size_t>(g.outputs[0].value)] == ranges::whole();
}

// ---- several pieces -------------------------------------------------------------------------------------------

constexpr bool pieces() {
    ranges::Pieces five{};
    for (const Interval p : {closed(0, 1), closed(2, 3), closed(5, 6), closed(7, 8), closed(20, 21)}) {
        five.add(p);
    }
    ranges::Pieces rounding{};
    rounding.add(Interval{1, 1, true, true});            // rounding met the ends: the number between them
    rounding.add(Interval{kInfinity, kInfinity, false, true}); // overflow: past the largest double
    ranges::Pieces nan{};
    nan.add(Interval{0, kNan, false, false});
    ranges::Pieces zero{};
    zero.add(closed(-0.0, -0.0));
    const Range a = made_of(closed(0, 1), closed(3, 4));
    const Range b = made_of(closed(0, 1), closed(10, 11));
    return ranges::join(five) == made_of(closed(0, 3), closed(5, 6), closed(7, 8), closed(20, 21)) && // the smallest gap, first
           ranges::join(rounding) == made_of(closed(1, 1), Interval{ranges::kLargest, kInfinity, false, true}) &&
           ranges::join(nan) == ranges::whole() &&
           std::bit_cast<std::uint64_t>(ranges::join(zero).pieces[0].lo) == 0 &&
           result_of(op::Add, a, b) == made_of(closed(0, 2), closed(3, 5), closed(10, 12), closed(13, 15)) &&
           // a promise against a computed range may need a fifth piece: the proof widens where `intersect` refuses
           ranges::narrowed(made_of(closed(0, 1), closed(2, 3), closed(4, 5)), made_of(open(-kInfinity, 0.5), open(0.5, 2.5), open(2.5, kInfinity))) ==
               made_of(closed(0, 1), closed_open(2, 2.5), open_closed(2.5, 3), closed(4, 5));
}

static_assert(same_and_covering());
static_assert(sums());
static_assert(products());
static_assert(quotients());
static_assert(monotone());
static_assert(minimum_and_maximum());
static_assert(magnitudes());
static_assert(fixed_sets());
static_assert(choices_and_unions());
static_assert(sums_of_terms());
static_assert(generators());
static_assert(domains());
static_assert(after_a_finding());
static_assert(pieces());

int g_wrong = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        ++g_wrong;
        std::printf("  FAILED: %s\n", what);
    }
}

} // namespace

int main() {
    expect(same_and_covering(), "same and covering");
    expect(sums(), "sums");
    expect(products(), "products, 0 times infinity, squares");
    expect(quotients(), "quotients");
    expect(monotone(), "monotone rules and the limit rule");
    expect(minimum_and_maximum(), "min and max");
    expect(magnitudes(), "abs");
    expect(fixed_sets(), "fixed sets");
    expect(choices_and_unions(), "choices and unions");
    expect(sums_of_terms(), "sums of terms");
    expect(generators(), "generators");
    expect(domains(), "domains");
    expect(after_a_finding(), "after a finding");
    expect(pieces(), "several pieces");
    if (g_wrong != 0) {
        std::printf("ranges_rules_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("ranges_rules_test: every range rule and every domain gives its exact range and its findings\n");
    return 0;
}
