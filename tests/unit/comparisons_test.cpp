// The comparisons, `where` with a number first and `clamp` with an expression for a bound
// (Plan/28-dsl-proposals/02 A1). Each builds the graph of the operations it names, its operands in the order it needs
// and a number as the `constant` it is, so the graphs are equal; the four comparisons run on `Executor` at Normal and
// High, below, equal and above, the zeros of either sign, a NaN on either side, as C++ answers. Two numbers the wrong
// way round in `clamp` are refused while compiling: the build_refuses_clamp_bounds_crossed fixture. A `where` of two
// numbers (Plan/28-dsl-proposals/03 B2) runs at f32 and f64 as the walk runs it; its types are `inference_test`'s.
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

constexpr Index n{};

// Between two values a comparison is one node: `<` and `<=` read their operands as written, `>` and `>=` turned round.
template <bool Spelled>
constexpr Graph between_values() {
    Input a{"a", Signal{8}};
    Input b{"b", Signal{8}};
    Output lt{"lt"};
    Output gt{"gt"};
    Output le{"le"};
    Output ge{"ge"};
    if constexpr (Spelled) {
        lt = a < b;
        gt = a > b;
        le = a <= b;
        ge = a >= b;
    } else {
        lt = apply(op::Lt, a, b);
        gt = apply(op::Lt, b, a);
        le = apply(op::Le, a, b);
        ge = apply(op::Le, b, a);
    }
    return finish(lt, gt, le, ge);
}
static_assert(between_values<true>() == between_values<false>());

// A number on either side is a `constant`, as `max(x, 0.5)` makes one; an integer is a number.
template <bool Spelled>
constexpr Graph against_numbers() {
    Input x{"x", Signal{8}};
    Output lt{"lt"};
    Output gt{"gt"};
    Output le{"le"};
    Output ge{"ge"};
    Output lt_first{"lt_first"};
    Output gt_first{"gt_first"};
    Output le_first{"le_first"};
    Output ge_first{"ge_first"};
    Output whole{"whole"};
    if constexpr (Spelled) {
        lt = x < 0.5;
        gt = x > 1.5;
        le = x <= 2.5;
        ge = x >= 3.5;
        lt_first = 4.5 < x;
        gt_first = 5.5 > x;
        le_first = 6.5 <= x;
        ge_first = 7.5 >= x;
        whole = x > 0;
    } else {
        lt = apply(op::Lt, x, constant(0.5));
        gt = apply(op::Lt, constant(1.5), x);
        le = apply(op::Le, x, constant(2.5));
        ge = apply(op::Le, constant(3.5), x);
        lt_first = apply(op::Lt, constant(4.5), x);
        gt_first = apply(op::Lt, x, constant(5.5));
        le_first = apply(op::Le, constant(6.5), x);
        ge_first = apply(op::Le, x, constant(7.5));
        whole = apply(op::Lt, constant(0.0), x);
    }
    return finish(lt, gt, le, ge, lt_first, gt_first, le_first, ge_first, whole);
}
static_assert(against_numbers<true>() == against_numbers<false>());

// What converts to an expression compares as one: a Param, a past read, a Variable by name and by `v[n]`, an
// assigned Output.
template <bool Spelled>
constexpr Graph conversions() {
    Input x{"x", Signal{8}};
    Param p{"p"};
    Variable v;
    Output y{"y"};
    Output z{"z"};
    v[n] = x[n] + 0.5 * v[n - 1];
    y = x * 2.0;
    if constexpr (Spelled) {
        Expr small = p < 0.5;
        Expr rising = x[n - 1] > 0.0;
        Expr caught = v >= x;
        Expr now = v[n] <= y;
        z = small + rising + caught + now;
    } else {
        Expr small = apply(op::Lt, p, constant(0.5));
        Expr rising = apply(op::Lt, constant(0.0), x[n - 1]);
        Expr caught = apply(op::Le, x, v);
        Expr now = apply(op::Le, v, Expr(y));
        z = small + rising + caught + now;
    }
    return finish(y, z);
}
static_assert(conversions<true>() == conversions<false>());

// `where` with a number first or two; `clamp` with an expression for either bound or both, `min(max(x, lo), hi)` as
// with two numbers.
template <bool Spelled>
constexpr Graph where_and_clamp() {
    Input x{"x", Signal{8}};
    Input lo{"lo", Signal{8}};
    Input hi{"hi", Signal{8}};
    Output picked{"picked"};
    Output two{"two"};
    Output both{"both"};
    Output low{"low"};
    Output high{"high"};
    Output numbers{"numbers"};
    if constexpr (Spelled) {
        Expr positive = x > 0.0;
        picked = where(positive, 1.0, x);
        two = where(positive, 1.0, -1.0);
        both = clamp(x, lo, hi);
        low = clamp(x, lo, 1.0);
        high = clamp(x, -1.0, hi);
        numbers = clamp(x, -1.0, 1.0);
    } else {
        Expr positive = apply(op::Lt, constant(0.0), x);
        picked = apply(op::Select, positive, constant(1.0), x);
        Expr taken = constant(1.0);
        Expr not_taken = constant(-1.0);
        two = apply(op::Select, positive, taken, not_taken);
        Expr above_lo = apply(op::Max, x, lo);
        both = apply(op::Min, above_lo, hi);
        Expr above_lo_too = apply(op::Max, x, lo);
        low = apply(op::Min, above_lo_too, constant(1.0));
        Expr above_minus_one = apply(op::Max, x, constant(-1.0));
        high = apply(op::Min, above_minus_one, hi);
        Expr above_minus_one_too = apply(op::Max, x, constant(-1.0));
        numbers = apply(op::Min, above_minus_one_too, constant(1.0));
    }
    return finish(picked, two, both, low, high, numbers);
}
static_assert(where_and_clamp<true>() == where_and_clamp<false>());

// Below, equal, above; the zeros of either sign, equal; a NaN on the left, on the right, on both; an infinity above.
// Five times over, so that a run fills whole vectors and leaves a ragged end.
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInfinity = std::numeric_limits<float>::infinity();
constexpr std::array<float, 8> kLeft{1.0f, 2.0f, 3.0f, -0.0f, kNaN, 2.0f, kNaN, kInfinity};
constexpr std::array<float, 8> kRight{2.0f, 2.0f, 2.0f, 0.0f, 2.0f, kNaN, kNaN, 2.0f};
constexpr std::size_t kSamples = 5 * kLeft.size();
constexpr std::array<const char*, 4> kMasks{"<", ">", "<=", ">="};

template <numerics::Precision Level>
constexpr Graph masks() {
    Input a{"a", Signal{kSamples}};
    Input b{"b", Signal{kSamples}};
    Output lt{"lt"};
    Output gt{"gt"};
    Output le{"le"};
    Output ge{"ge"};
    lt = a < b;
    gt = a > b;
    le = a <= b;
    ge = a >= b;
    Graph g = finish(lt, gt, le, ge);
    set_default_precision(g, Level);
    return g;
}

// How many numbers of the four masks differ from C++'s own comparison, bit for bit.
template <numerics::Precision Level>
int wrong_masks(const char* level) {
    std::array<float, kSamples> a{};
    std::array<float, kSamples> b{};
    for (std::size_t i = 0; i < kSamples; ++i) {
        a[i] = kLeft[i % kLeft.size()];
        b[i] = kRight[i % kRight.size()];
    }
    std::array<std::array<float, kSamples>, 4> got{};
    Executor<masks<Level>> ex;
    testing::held(ex);
    ex.input("a") = a.data();
    ex.input("b") = b.data();
    ex.output("lt") = got[0].data();
    ex.output("gt") = got[1].data();
    ex.output("le") = got[2].data();
    ex.output("ge") = got[3].data();
    ex.run();
    int wrong = 0;
    for (std::size_t i = 0; i < kSamples; ++i) {
        const std::array<bool, 4> holds{a[i] < b[i], a[i] > b[i], a[i] <= b[i], a[i] >= b[i]};
        for (std::size_t k = 0; k < holds.size(); ++k) {
            const float want = holds[k] ? 1.0f : 0.0f;
            if (std::bit_cast<std::uint32_t>(got[k][i]) != std::bit_cast<std::uint32_t>(want)) {
                std::printf("comparisons: at %s, %g %s %g gives %g at sample %zu\n", level, static_cast<double>(a[i]),
                            kMasks[k], static_cast<double>(b[i]), static_cast<double>(got[k][i]), i);
                ++wrong;
            }
        }
    }
    return wrong;
}

// JUCE's peak follower: its coefficient a `where` of two numbers in the recurrence, typed by the multiply that reads it;
// beside it a sign, typed by its condition. On `Executor` at High, bit for bit as the walk runs it, or the test exits.
constexpr std::size_t kFollowed = 67; // whole vectors and a ragged end

template <ElemType T>
constexpr Graph follower() {
    Input x{"x", Signal{kFollowed}, T};
    Variable env;
    Expr rectified = abs(x);
    Expr rising = rectified > env[n - 1];
    Expr coefficient = where(rising, 0.877, 0.9987);
    env[n] = rectified + coefficient * (env[n - 1] - rectified);
    Output y{"y"};
    Output sign{"sign"};
    y = env;
    sign = where(x > 0.0, 1.0, -1.0);
    return finish(y, sign);
}

void followed() {
    std::vector<float> x(kFollowed);
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] = static_cast<float>(std::sin(0.37 * static_cast<double>(i)) * static_cast<double>(1 + i % 5));
    }
    const std::vector<double> x64(x.begin(), x.end());
    testing::executed<follower<ElemType::F32>>({{"x", x.data()}});
    testing::executed<follower<ElemType::F64>>({{"x", x64.data()}});
}

} // namespace

int main() {
    const int wrong_at_normal = wrong_masks<numerics::Precision::Normal>("Normal");
    const int wrong_at_high = wrong_masks<numerics::Precision::High>("High");
    if (wrong_at_normal + wrong_at_high != 0) {
        return 1;
    }
    followed();
    std::printf("comparisons: <, >, <=, >= as C++ answers them at Normal and High, a NaN included; the graphs equal\n");
    std::printf("comparisons: a where of two numbers, typed by its reader or its condition, runs as the walk runs it\n");
    return 0;
}
