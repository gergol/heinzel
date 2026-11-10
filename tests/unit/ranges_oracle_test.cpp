// The proof's oracle (Plan/26-value-ranges/03): random small graphs, without promises, over random port ranges with
// holes, run on the reference walk (`tests/reference/`) from silence on inputs drawn from those ranges: at their ends,
// beside their holes and inside. Every number every value takes at every sample lies within the range the proof gives
// it, to a few ulp where a core, a blend or a sum's order rounds otherwise than the range's ends; and a graph the proof
// finds nothing in computes no NaN. The rows the plan's audit found unsound in its first draft are in the generator
// (`_dline`, `_tblread`, `_order`, `_ovadd` at its edges and past its window, `_matmul`), so are `abs` and `frac` with
// the facts they state, and the run counts how often each row was checked against a range narrower than any number.
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "heinzel/ranges/proven.hpp"

#include "interpret.hpp"
#include "ranged_graphs.hpp"

using namespace heinzel;
using namespace heinzel::testing;

namespace {

constexpr std::size_t kSamples = 32;
constexpr int kGraphs = 3000;
constexpr double kNice[] = {-4, -2, -1.5, -1, -0.5, -0.25, 0, 0.25, 0.5, 1, 1.5, 2, 3, 4};

std::string name_text(std::uint8_t code) {
    std::string s;
    for (std::size_t k = 0; k < 8 && kOpTable[code].name[k] != '\0'; ++k) {
        s += kOpTable[code].name[k];
    }
    return s;
}

struct Random {
    std::uint64_t state;
    std::uint64_t next() {
        std::uint64_t z = (state += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }
    int below(int n) { return static_cast<int>(next() % static_cast<std::uint64_t>(n)); }
    double unit() { return static_cast<double>(next() >> 11) * 0x1.0p-53; }
    bool chance(double p) { return unit() < p; }
    double nice() { return kNice[below(static_cast<int>(std::size(kNice)))]; }
};

// ---- random port ranges ---------------------------------------------------------------------------------------

Range hole_at(double h) { return Range{{Interval{.hi = h}, Interval{.lo = h}}, 2}; }

Range random_range(Random& r) {
    double a = r.nice();
    double b = r.nice();
    if (a > b) {
        std::swap(a, b);
    }
    const bool a_open = a == b ? false : r.chance(0.4);
    const bool b_open = a == b ? false : r.chance(0.4);
    switch (r.below(6)) {
    case 0:
        return ranges::span(a, r.chance(0.5) ? kInfinity : b, a_open, b_open); // one-sided, or both
    case 1:
        return ranges::span(-kInfinity, b, true, b_open);
    case 2: // a hole inside
        return a < b ? intersect(ranges::span(a, b, a_open, b_open), hole_at(a + (b - a) * 0.5)) : ranges::span(a, b);
    case 3: { // two pieces
        const double c = b + 0.5 + r.unit() * 2;
        const double d = c + r.unit() * 3;
        return ranges::unite(ranges::span(a, b, a_open, b_open), ranges::span(c, d, r.chance(0.4) && d > c, false));
    }
    default:
        return ranges::span(a, b, a_open, b_open);
    }
}

// A number of the range: at an end, beside one, or inside a piece.
double drawn(Random& r, const Range& range) {
    const Interval& p = range.pieces[r.below(range.count)];
    const double lo = p.lo == -kInfinity ? (p.hi == kInfinity ? -8.0 : p.hi - 8.0) : p.lo;
    const double hi = p.hi == kInfinity ? lo + 8.0 : p.hi;
    switch (r.below(5)) {
    case 0:
        return p.lo_open ? std::nextafter(lo, hi) : lo;
    case 1:
        return p.hi_open ? std::nextafter(hi, lo) : hi;
    case 2:
        return p.lo_open ? lo + (hi - lo) * 1e-9 : lo;
    default:
        return lo + (hi - lo) * r.unit();
    }
}

// ---- random graphs ---------------------------------------------------------------------------------------------

constexpr std::int32_t kB = static_cast<std::int32_t>(kSamples);
const Shape kScalar{kB};
const Shape kWide{4, kB};

class Generator {
public:
    explicit Generator(std::uint64_t seed) : r_{seed} {}

    Graph graph();

private:
    Random r_;
    Graph g_{};
    std::vector<std::int32_t> scalars_;
    std::vector<std::int32_t> wides_;
    std::vector<bool> wide_; // per value: four numbers a sample (a node's shape is known only once analysed)

    std::int32_t input(Name name, Shape s) {
        const std::int32_t v = add_value(g_, ElemType::F64, s, ValueCategory::Input);
        g_.inputs.push_back(NamedValue{name, v});
        add_range(g_, v, random_range(r_));
        return v;
    }
    std::int32_t constant(double c, Shape s = Shape{}) {
        const std::int32_t v = add_value(g_, ElemType::F64, s.rank == 0 ? kScalar : s, ValueCategory::Const);
        g_.values[static_cast<std::size_t>(v)].constant = c;
        return v;
    }
    std::int32_t pick(const std::vector<std::int32_t>& from) {
        const int n = static_cast<int>(from.size());
        return from[static_cast<std::size_t>(r_.chance(0.6) ? n - 1 - r_.below(n < 3 ? n : 3) : r_.below(n))];
    }
    bool is_wide(std::int32_t v) const { return static_cast<std::size_t>(v) < wide_.size() && wide_[static_cast<std::size_t>(v)]; }
    std::int32_t any() { return !wides_.empty() && r_.chance(0.3) ? pick(wides_) : pick(scalars_); }
    std::int32_t keep(std::int32_t v, bool wide) {
        (wide ? wides_ : scalars_).push_back(v);
        wide_.resize(g_.values.size());
        wide_[static_cast<std::size_t>(v)] = wide;
        return v;
    }
    std::int32_t node(OpCode op, std::int32_t a, std::int32_t b = kNoValue, std::int32_t c = kNoValue, OpAttr attr = OpAttr{}) {
        return add_node(g_, op, a, b, c, attr);
    }
    std::int32_t reduced(std::int32_t v, Axes axes) {
        OpAttr a{};
        a.kind = AttrKind::Reduce;
        a.reduce.op = static_cast<ReduceOp>(r_.below(5));
        a.reduce.axes = axes;
        return keep(node(op::Reduce, v, kNoValue, kNoValue, a), false);
    }
    std::int32_t reshaped(std::int32_t v, Shape to) {
        OpAttr a{};
        a.kind = AttrKind::View;
        a.view.kind = ViewKind::Reshape;
        a.view.shape = to;
        return node(op::Reshape, v, kNoValue, kNoValue, a);
    }

    void per_sample();
    void lag();
    void recurrence();
    void widened();
    void folded();
    void contracted();
    void ordered();
    void overlapped();
    void tabled();
    void viewed();
    void composed();
};

constexpr OpCode kOneOperand[] = {op::Neg, op::Sqrt, op::Rsqrt, op::Exp, op::Log, op::Tanh, op::Floor, op::Ceil, op::Round, op::Trunc,
                                  op::Sign, op::Sin, op::Cos, op::SinPi, op::Exp2, op::Log2, op::Log10, op::Atan, op::Asin,
                                  op::Acos, op::Tan, op::Realize, op::Abs};
constexpr OpCode kTwoOperands[] = {op::Add, op::Sub, op::Mul, op::Div, op::Min, op::Max, op::Lt, op::Le, op::Eq, op::Atan2, op::Pow};
constexpr OpCode kThreeOperands[] = {op::Fma, op::MultiplyAdd, op::Select};

void Generator::per_sample() {
    const int kind = r_.below(10);
    if (kind < 4) {
        const std::int32_t x = any();
        keep(node(kOneOperand[r_.below(static_cast<int>(std::size(kOneOperand)))], x), is_wide(x));
        return;
    }
    const std::int32_t a = any();
    const std::int32_t b = r_.chance(0.25) ? a : (r_.chance(0.2) ? constant(r_.nice()) : any());
    if (kind < 8) {
        keep(node(kTwoOperands[r_.below(static_cast<int>(std::size(kTwoOperands)))], a, b), is_wide(a) || is_wide(b));
        return;
    }
    const std::int32_t c = any();
    const OpCode op = kThreeOperands[r_.below(static_cast<int>(std::size(kThreeOperands)))];
    const std::int32_t out = node(op, a, b, c);
    if (op == op::MultiplyAdd) { // rounded as the machine rounds: a freedom only a granted precision takes
        g_.values[static_cast<std::size_t>(out)].precision = numerics::Precision::Normal;
    }
    keep(out, is_wide(a) || is_wide(b) || is_wide(c));
}

void Generator::lag() {
    if (r_.chance(0.5)) {
        OpAttr a{};
        a.kind = AttrKind::Delay;
        a.delay.samples = 1 + r_.below(3);
        const std::int32_t x = any();
        keep(node(op::FixedDelay, x, kNoValue, kNoValue, a), is_wide(x));
        return;
    }
    OpAttr a{};
    a.kind = AttrKind::SignalledDelay;
    a.signalled_delay.max_delay = 4;
    keep(node(op::DelayLine, pick(scalars_), pick(scalars_), kNoValue, a), false);
}

// y = x + k·y[n - d], |k| < 1: a feedback value, which the proof (with no promise) leaves any number.
void Generator::recurrence() {
    const std::int32_t state = add_value(g_, ElemType::F64, kScalar, ValueCategory::Variable);
    OpAttr back{};
    back.kind = AttrKind::Delay;
    back.delay.samples = 1 + r_.below(3);
    const std::int32_t past = node(op::FixedDelay, state, kNoValue, kNoValue, back);
    const std::int32_t next = node(op::Add, pick(scalars_), node(op::Mul, past, constant(r_.nice() * 0.2)));
    OpAttr defines{};
    defines.kind = AttrKind::Recurrence;
    defines.recurrence.defines = state;
    keep(node(op::Recurrence, next, kNoValue, kNoValue, defines), false);
}

void Generator::widened() {
    OpAttr a{};
    switch (r_.below(3)) {
    case 0:
        keep(node(op::Mul, pick(scalars_), constant(r_.nice(), kWide)), true);
        return;
    case 1:
        a.kind = AttrKind::Iota;
        break;
    default:
        a.kind = AttrKind::Constant;
        a.generator.first = static_cast<std::int32_t>(g_.constants.size());
        for (int e = 0; e < 4; ++e) {
            push_constant(g_, r_.nice());
        }
    }
    a.generator.shape = kWide;
    a.generator.type = ElemType::F64;
    keep(node(a.kind == AttrKind::Iota ? op::Iota : op::Constant, kNoValue, kNoValue, kNoValue, a), true);
}

void Generator::folded() {
    if (wides_.empty()) {
        return widened();
    }
    reduced(pick(wides_), Axes{0});
}

// An elementwise contraction, and a general one of [2, 2] by [2, 2] (one value on both sides is no square there).
void Generator::contracted() {
    if (wides_.empty()) {
        return widened();
    }
    const std::int32_t a = pick(wides_);
    const std::int32_t b = r_.chance(0.3) ? a : pick(wides_);
    if (r_.chance(0.5)) {
        OpAttr c{};
        c.kind = AttrKind::Contract;
        c.contract.axes = Axes{0};
        keep(node(op::Contract, a, b, kNoValue, c), false);
        return;
    }
    const std::int32_t left = reshaped(a, Shape{2, 2, kB});
    const std::int32_t right = a == b ? left : reshaped(b, Shape{2, 2, kB});
    OpAttr m{};
    m.kind = AttrKind::MatMul;
    m.matmul.a_contract = 1;
    m.matmul.b_contract = 0;
    reduced(node(op::MatMul, left, right, kNoValue, m), Axes{0, 1});
}

void Generator::ordered() {
    if (wides_.empty()) {
        return widened();
    }
    OpAttr a{};
    a.kind = AttrKind::Order;
    a.order.op = static_cast<OrderOp>(r_.below(3));
    a.order.k = 2;
    a.order.q = r_.below(5) * 0.25;
    const std::int32_t out = node(op::Order, pick(wides_), kNoValue, kNoValue, a);
    if (a.order.op == OrderOp::Quantile) {
        keep(out, false);
    } else if (a.order.op == OrderOp::Sort) {
        keep(out, true);
    } else {
        reduced(out, Axes{0});
    }
}

// Two frames of two taps, at a hop of 1, 2 or 3: past the window, an element no frame reaches holds 0.
void Generator::overlapped() {
    if (wides_.empty()) {
        return widened();
    }
    OpAttr a{};
    a.kind = AttrKind::OverlapAdd;
    a.overlap.window = 2;
    a.overlap.hop = 1 + r_.below(3);
    reduced(node(op::OverlapAdd, reshaped(pick(wides_), Shape{2, 2, kB}), kNoValue, kNoValue, a), Axes{0});
}

void Generator::tabled() {
    if (wides_.empty()) {
        return widened();
    }
    OpAttr a{};
    a.kind = AttrKind::Table;
    a.table.size = 4;
    // The phase is a port, as when a NaN or infinite phase made `table_tap` index nowhere; it reads a NaN since Plan 28
    // D3 (`runtime/table_gather_element.hpp`), and the draw stays, so the oracle's graphs are the ones it always drew.
    keep(node(op::TableRead, scalars_[static_cast<std::size_t>(r_.below(2))], pick(wides_), kNoValue, a), false);
}

void Generator::viewed() {
    if (wides_.empty()) {
        return widened();
    }
    OpAttr a{};
    a.kind = AttrKind::View;
    switch (r_.below(4)) {
    case 0:
        a.view.kind = ViewKind::Concat;
        reduced(node(op::Concat, pick(wides_), pick(wides_), kNoValue, a), Axes{0});
        return;
    case 1:
        a.view.kind = ViewKind::Pad;
        a.view.pad_before = r_.below(2);
        a.view.pad_after = 1 + r_.below(2);
        a.view.pad_bits = std::bit_cast<std::uint64_t>(r_.nice());
        reduced(node(op::Pad, pick(wides_), kNoValue, kNoValue, a), Axes{0});
        return;
    case 2:
        a.view.kind = ViewKind::Slice;
        a.view.offset = 1;
        a.view.length = 2;
        reduced(node(op::Slice, pick(wides_), kNoValue, kNoValue, a), Axes{0});
        return;
    default:
        a.view.kind = ViewKind::Flip;
        keep(node(op::Flip, pick(wides_), kNoValue, kNoValue, a), true);
    }
}

// `abs` and `frac` as the frontend spells them (`_abs`, `x - floor(x)`), each with the fact it states: what the
// proof trusts must hold of every number. A third of the fracs take a number just below 0 (x · 1e-18 - 1e-17, below 0
// wherever |x| < 10), where x - floor(x) rounds to 1: the open fact [0, 1) left no number there (the review of Plan 26).
void Generator::composed() {
    std::int32_t x = any();
    const bool frac = r_.chance(0.5);
    const bool wide = is_wide(x);
    if (frac && r_.chance(0.3)) {
        x = node(op::Sub, node(op::Mul, x, constant(1e-18)), constant(1e-17));
    }
    const std::int32_t out = frac ? node(op::Sub, x, node(op::Floor, x)) : node(op::Abs, x);
    add_range(g_, out, frac ? ranges::span(0, 1) : ranges::span(0, kInfinity));
    keep(out, wide);
}

Graph Generator::graph() {
    keep(input(Name{"a"}, kScalar), false);
    keep(input(Name{"b"}, kScalar), false);
    keep(input(Name{"w"}, kWide), true);
    const int steps = 6 + r_.below(10);
    for (int s = 0; s < steps; ++s) {
        switch (r_.below(13)) {
        case 0: lag(); break;
        case 1: recurrence(); break;
        case 2: widened(); break;
        case 3: folded(); break;
        case 4: contracted(); break;
        case 5: ordered(); break;
        case 6: overlapped(); break;
        case 7: tabled(); break;
        case 8: viewed(); break;
        case 9: composed(); break;
        default: per_sample(); break;
        }
    }
    g_.outputs.push_back(NamedValue{Name{"y"}, scalars_.back()});
    return analysed(g_);
}

// ---- the check -------------------------------------------------------------------------------------------------

// 16 ulp of the end, or of 1 near 0: what a core, a blend or a sum's order may round past it. (Measured: only `_fma`
// needs any, 1 ulp, since it rounds once where its range's ends round its product and its sum.)
bool within(const Range& r, double x) {
    for (std::int8_t i = 0; i < r.count; ++i) {
        const Interval& p = r.pieces[i];
        const auto slack = [](double end) { return 16 * 0x1.0p-52 * std::max(1.0, std::fabs(end)); };
        if ((p.lo == -kInfinity || x >= p.lo - slack(p.lo)) && (p.hi == kInfinity || x <= p.hi + slack(p.hi))) {
            return true;
        }
    }
    return false;
}

struct Tally {
    long graphs{0};
    long samples{0};
    long checked{0};
    long nan_free_graphs{0};
    long narrow[256]{}; // per row: numbers checked against a range narrower than any number
    long facts{0};      // numbers of `abs` and `frac`, checked against the range their fact narrowed
    int wrong{0};
};

// A mark on a value a node makes: a composition's fact (the generator promises nothing).
bool marked(const Graph& g, std::size_t v) {
    for (const RangeMark& m : g.ranges) {
        if (static_cast<std::size_t>(m.value) == v) {
            return true;
        }
    }
    return false;
}

void run_one(std::uint64_t seed, Tally& t) {
    const Graph g = Generator{seed}.graph();
    const ranges::Proof p = ranges::proof(g);
    Random r{seed ^ 0x5bd1e995ull};
    std::vector<std::vector<double>> data(g.inputs.size());
    Inputs feeds{};
    for (std::size_t i = 0; i < g.inputs.size(); ++i) {
        const std::int32_t v = g.inputs[i].value;
        const std::int32_t width = elements_of(g.values[static_cast<std::size_t>(v)].shape).count;
        const Range& range = g.ranges[i].range;
        for (std::size_t k = 0; k < kSamples * static_cast<std::size_t>(width); ++k) {
            data[i].push_back(drawn(r, range));
        }
        feeds.push_back(InputFeed{data[i].data(), width, 0});
    }
    auto out = std::make_unique<Signals<kSamples, 1024>>();
    run(g, feeds, *out);
    std::vector<int> made_by(g.values.size(), -1);
    for (const Node& n : g.nodes) {
        made_by[static_cast<std::size_t>(n.output)] = n.op.index;
    }
    bool nan = false;
    bool infinite = false;
    for (std::size_t v = 0; v < g.values.size(); ++v) {
        const std::int32_t width = elements_of(g.values[v].shape).count;
        const bool narrow = !ranges::is_whole(p.values[v]);
        const bool stated = made_by[v] >= 0 && marked(g, v);
        for (std::int32_t e = 0; e < width; ++e) {
            for (std::size_t s = 0; s < kSamples; ++s) {
                const double x = sample_at(*out, static_cast<std::int32_t>(v), e, s);
                nan = nan || x != x;
                infinite = infinite || std::isinf(x);
                if (x != x || std::isinf(x)) {
                    continue; // outside what the proof is about: overflow, or an operation outside its domain
                }
                ++t.checked;
                t.narrow[made_by[v] < 0 ? 255 : made_by[v]] += narrow ? 1 : 0;
                t.facts += stated ? 1 : 0;
                if (!within(p.values[v], x)) {
                    ++t.wrong;
                    MessageWriter w;
                    ranges::write_range(w, p.values[v]);
                    std::printf("  FAILED: seed %llu value %zu (%s) element %d sample %zu: %.17g outside %s\n",
                                static_cast<unsigned long long>(seed), v,
                                made_by[v] < 0 ? "no node" : name_text(static_cast<std::uint8_t>(made_by[v])).c_str(), e, s, x,
                                w.problem().message.view().data());
                }
            }
        }
    }
    if (nan && p.findings.empty() && !infinite) {
        ++t.wrong;
        std::printf("  FAILED: seed %llu computed a NaN, and the proof found nothing\n", static_cast<unsigned long long>(seed));
    }
    ++t.graphs;
    t.samples += static_cast<long>(kSamples);
    t.nan_free_graphs += nan ? 0 : 1;
}

} // namespace

int main() {
    Tally t{};
    for (int i = 0; i < kGraphs; ++i) {
        const std::uint64_t seed = 0x2600 + static_cast<std::uint64_t>(i);
        try {
            run_one(seed, t);
        } catch (const heinzel::error& e) { // a graph the generator should not have made
            ++t.wrong;
            std::printf("  FAILED: seed %llu: %s\n", static_cast<unsigned long long>(seed), e.message().data());
        }
    }
    // The rows the first draft had unsound, each checked against a narrow range many times.
    for (const OpCode must : {op::DelayLine, op::TableRead, op::Order, op::OverlapAdd, op::MatMul}) {
        if (t.narrow[must.index] < 100) {
            ++t.wrong;
            std::printf("  FAILED: %s was checked against a narrow range only %ld times\n", name_text(must.index).c_str(), t.narrow[must.index]);
        }
    }
    if (t.facts < 100) {
        ++t.wrong;
        std::printf("  FAILED: abs and frac were checked against their facts only %ld times\n", t.facts);
    }
    std::printf("ranges_oracle_test: %ld graphs, %ld samples each, %ld numbers checked, %ld of them abs's and frac's\n", t.graphs,
                static_cast<long>(kSamples), t.checked, t.facts);
    for (std::uint8_t i = 0; i < kOpCount; ++i) {
        if (t.narrow[i] > 0) {
            std::printf("  %-9s %ld\n", name_text(i).c_str(), t.narrow[i]);
        }
    }
    if (t.wrong != 0) {
        std::printf("ranges_oracle_test: %d wrong\n", t.wrong);
        return 1;
    }
    std::printf("ranges_oracle_test: every number lies within its proven range\n");
    return 0;
}
