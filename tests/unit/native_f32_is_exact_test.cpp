// May an F32 value be computed in `float`? Today every operation runs in double and rounds its result
// to float once (`as_type`). For ONE correctly rounded step that is the float operation itself —
// 53 >= 2*24 + 2, the double-rounding theorem — and this file states that instead of trusting it: for
// every `Exact` elementwise operation, the library's via-double answer against the library's own
// writing of it at `V = float`, over random f32 bit patterns and the awkward values.
//
// It also states where the theorem STOPS. A sequence of steps rounds once via double and at every step
// in float, and those are different numbers: `_rsqrt` (a root, then a division), `_fma` (float's
// fused multiply-add is not double's, rounded). The rule table marks exactly these `ExactInDouble`,
// which is what tells a float lane to widen — and this file is what keeps that column honest, in both
// directions.

#include "heinzel/ir/op_rules.hpp"
#include "heinzel/runtime/compute.hpp"

#include "one_operation.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>

using namespace heinzel;
using heinzel::testing::apply;

namespace {

float via_double(OpCode op, float a, float b = 0.0f, float c = 0.0f) {
    return static_cast<float>(as_type(ElemType::F32, apply(op, a, b, c)));
}

// The same bits, or both a NaN (`runtime/hardware_twins.hpp`).
bool same(float x, float y) {
    return std::bit_cast<std::uint32_t>(x) == std::bit_cast<std::uint32_t>(y) || (x != x && y != y);
}

struct Row {
    OpCode op;
    float (*in_float)(float, float, float);
    float (*in_double)(float, float, float) = nullptr; // the library's, when it is not one `compute` call
};

// The float side is the library's own writing at `V = float` (`compute_as`), not a second spelling.
template <OpCode Op>
float in_float(float a, float b, float c) { return compute_as<float>(Op, FnAccuracy::Exact, a, b, c); }

// One correctly rounded step each.
constexpr Row kOneStep[] = {
    {op::Add, in_float<op::Add>},     {op::Sub, in_float<op::Sub>},     {op::Mul, in_float<op::Mul>},
    {op::Div, in_float<op::Div>},     {op::Neg, in_float<op::Neg>},     {op::Min, in_float<op::Min>},
    {op::Max, in_float<op::Max>},     {op::Sqrt, in_float<op::Sqrt>},   {op::Floor, in_float<op::Floor>},
    {op::Ceil, in_float<op::Ceil>},   {op::Trunc, in_float<op::Trunc>}, {op::Round, in_float<op::Round>},
    {op::Sign, in_float<op::Sign>},   {op::Lt, in_float<op::Lt>},       {op::Le, in_float<op::Le>},
    {op::Eq, in_float<op::Eq>},       {op::Select, in_float<op::Select>}, {op::Abs, in_float<op::Abs>},
};

// A sequence of steps: the float spelling is a different number.
constexpr Row kSequence[] = {
    {op::Rsqrt, in_float<op::Rsqrt>},
    {op::Fma, [](float a, float b, float c) { return __builtin_fmaf(a, b, c); }},
    // The tap blend both interpolating reads share (`near + f*(far - near)`), with f kept in [0, 1).
    {op::DelayLine, [](float a, float b, float f) { f = f - __builtin_floorf(f); return f != f ? f : a + f * (b - a); },
     [](float a, float b, float f) {
         f = f - __builtin_floorf(f);
         return f != f ? f : static_cast<float>(as_type(ElemType::F32, lerp2(a, b, f)));
     }},
    {op::TableRead, [](float a, float b, float f) { f = f - __builtin_floorf(f); return f != f ? f : a + f * (b - a); },
     [](float a, float b, float f) {
         f = f - __builtin_floorf(f);
         return f != f ? f : static_cast<float>(as_type(ElemType::F32, lerp2(a, b, f)));
     }},
};

std::uint64_t next(std::uint64_t& state) { // splitmix64
    std::uint64_t z = (state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

float pattern(std::uint64_t& state, int round) {
    constexpr float table[] = {0.0f,  -0.0f, 1.0f,  -1.0f,   0.5f,     -0.5f, 1.5f, 2.5f, -2.5f,
                               3.0f,  0.1f,  1e-40f, 3e38f,  -7.75f,   1.0000001f, 16777216.0f};
    return round % 4 == 0 ? table[next(state) % 16] : std::bit_cast<float>(static_cast<std::uint32_t>(next(state)));
}

// An operation's name as text: eight characters packed in a word, not all of them used.
const char* text_of(OpCode op) {
    static char text[9];
    for (std::size_t i = 0; i < 8; ++i) {
        text[i] = name_of(op)[i];
    }
    return text;
}

long disagreements(const Row& row) {
    std::uint64_t state = 1;
    long count = 0;
    for (int round = 0; round < 1000000; ++round) {
        const float a = pattern(state, round);
        const float b = pattern(state, round + 1);
        const float c = pattern(state, round + 2);
        const float library = row.in_double != nullptr ? row.in_double(a, b, c) : via_double(row.op, a, b, c);
        count += same(library, row.in_float(a, b, c)) ? 0 : 1;
    }
    return count;
}

} // namespace

template <std::size_t N>
bool lists(const Row (&rows)[N], OpCode op) {
    for (const Row& row : rows) {
        if (row.op == op) {
            return true;
        }
    }
    return false;
}

// No row escapes: every plain elementwise operation without a freedom is measured above, on the side
// its column claims. (The rest of the `Exact` rows do no arithmetic: views, delays, a cast, an index, a
// link, which moves numbers between elements and has no per-sample meaning.)
int unmeasured_rows() {
    int missing = 0;
    for (std::size_t i = 0; i < kOpCount; ++i) {
        const OpRules& r = kOpTable[i].rules;
        const OpCode op{static_cast<std::uint8_t>(i)};
        const bool plain = r.shape == ShapeRule::Elementwise && r.attr == AttrKind::None && r.meaning == MeaningRule::PerSample;
        if ((plain && r.numeric == NumericContract::Exact && !lists(kOneStep, op)) ||
            (r.numeric == NumericContract::ExactInDouble && !lists(kSequence, op))) {
            std::printf("  %s is not measured by this file\n", text_of(op));
            ++missing;
        }
    }
    return missing;
}

int main() {
    int wrong = unmeasured_rows();
    for (const Row& row : kOneStep) {
        const long differ = disagreements(row);
        if (differ != 0 || rules_of(row.op).numeric != NumericContract::Exact) {
            std::printf("  %s: %ld of 1000000 differ in float, and the table says it is %s\n", text_of(row.op), differ,
                        rules_of(row.op).numeric == NumericContract::Exact ? "Exact" : "not Exact");
            ++wrong;
        }
    }
    for (const Row& row : kSequence) {
        const long differ = disagreements(row);
        if (differ == 0 || rules_of(row.op).numeric != NumericContract::ExactInDouble) {
            std::printf("  %s: %ld of 1000000 differ in float, and the table says it is %s\n", text_of(row.op), differ,
                        rules_of(row.op).numeric == NumericContract::ExactInDouble ? "ExactInDouble" : "not ExactInDouble");
            ++wrong;
        }
    }
    return wrong == 0 ? 0 : 1;
}
