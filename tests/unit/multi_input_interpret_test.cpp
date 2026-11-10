// The walk, fed MORE THAN ONE input and an input WIDER than one number per sample.
//
// Milestone MW's first step. The scalar `run(g, std::array<double,BlockSize>, out)` — one input, one
// number per sample — is untouched and its own tests still stand; this file is about the wide sibling
// `run(g, const Inputs&, out)` that the operator decision of 2026-09-04 authorised. A graph may
// declare N named inputs (the frontend always allowed it) and any of them may be several numbers per
// sample; this checks that each is bound, that every element of a wide one is fed, and that a feed of
// the wrong shape is refused rather than arriving half-filled.
//
// WHY THE NUMBERS ARE CHOSEN TO DIFFER PER INPUT AND PER ELEMENT. A run that fed only the first input,
// or only element zero of a wide one, or read the feed element-major instead of sample-major, would
// each land on a plausible wrong number. So the two inputs carry different signals and the two
// channels of the wide input carry different signals, and the goldens below are hand-computed from
// the layout `run_inputs.hpp` documents (`data[n*width + e]`).
#include <array>
#include <cstddef>
#include <string_view>

#include "heinzel/sema/analyze.hpp"

#include "hand_built_graph.hpp"
#include "interpret.hpp"

using namespace heinzel;

namespace {

constexpr std::size_t kBlock = 8;
constexpr std::int32_t kBlockI = static_cast<std::int32_t>(kBlock);

// ---- two named scalar inputs, summed -------------------------------------------------------------

constexpr Graph two_input_sum() {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{kBlockI}, ValueCategory::Input);
    const auto y = add_value(g, ElemType::F64, Shape{kBlockI}, ValueCategory::Input);
    const auto z = add_node(g, op::Add, x, y);
    g.inputs.push_back(NamedValue{"x", x});
    g.inputs.push_back(NamedValue{"y", y});
    g.outputs.push_back(NamedValue{"z", z});
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the two-input sum did not resolve");
    }
    return g;
}
constexpr Graph kSum = two_input_sum();

// The graph really does take two inputs, so nothing below is one input fed twice.
static_assert(kSum.inputs.size() == 2, "the two-input sum did not declare two inputs");

// x[n] = n + 1, y[n] = 10*(n + 1) — different per input and per sample. z[n] = 11*(n + 1).
constexpr std::array<double, kBlock> summed() {
    std::array<double, kBlock> xs{};
    std::array<double, kBlock> ys{};
    for (std::size_t n = 0; n < kBlock; ++n) {
        xs[n] = static_cast<double>(n + 1);
        ys[n] = 10.0 * static_cast<double>(n + 1);
    }
    Inputs feeds{};
    feeds.push_back(InputFeed{xs.data(), 1});
    feeds.push_back(InputFeed{ys.data(), 1});
    Signals<kBlock> out{};
    run(kSum, feeds, out);
    std::array<double, kBlock> got{};
    for (std::size_t n = 0; n < kBlock; ++n) {
        got[n] = sample_at(out, kSum.outputs[0].value, 0, n);
    }
    return got;
}
constexpr bool the_sum_is_both_inputs() {
    const std::array<double, kBlock> got = summed();
    for (std::size_t n = 0; n < kBlock; ++n) {
        if (!(got[n] == 11.0 * static_cast<double>(n + 1))) {
            return false;
        }
    }
    return true;
}
static_assert(the_sum_is_both_inputs(),
              "the two-input walk did not add both inputs, or fed only the first");

// ---- one WIDE named input, negated ---------------------------------------------------------------

constexpr Graph wide_input_neg() {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{2, kBlockI}, ValueCategory::Input);
    const auto y = add_node(g, op::Neg, x);
    g.inputs.push_back(NamedValue{"x", x});
    g.outputs.push_back(NamedValue{"y", y});
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the wide-input negate did not resolve");
    }
    return g;
}
constexpr Graph kWide = wide_input_neg();

// The input really is two numbers per sample, so a run of it is not a scalar run in disguise.
static_assert(numbers_per_sample(kWide.values[static_cast<std::size_t>(kWide.inputs[0].value)].shape) == 2,
              "the wide input is not two numbers per sample");

// Channel 0 is n + 1, channel 1 is -(n + 1) * 100 — different per channel and per sample, laid out
// sample-major: in[n*2 + 0], in[n*2 + 1]. Output is the negation of each.
constexpr std::array<std::array<double, kBlock>, 2> negated_wide() {
    std::array<double, 2 * kBlock> in{};
    for (std::size_t n = 0; n < kBlock; ++n) {
        in[n * 2 + 0] = static_cast<double>(n + 1);
        in[n * 2 + 1] = -100.0 * static_cast<double>(n + 1);
    }
    Inputs feeds{};
    feeds.push_back(InputFeed{in.data(), 2});
    Signals<kBlock> out{};
    run(kWide, feeds, out);
    std::array<std::array<double, kBlock>, 2> got{};
    for (std::int32_t e = 0; e < 2; ++e) {
        for (std::size_t n = 0; n < kBlock; ++n) {
            got[static_cast<std::size_t>(e)][n] = sample_at(out, kWide.outputs[0].value, e, n);
        }
    }
    return got;
}
constexpr bool both_channels_negated() {
    const std::array<std::array<double, kBlock>, 2> got = negated_wide();
    for (std::size_t n = 0; n < kBlock; ++n) {
        // A run that filled only element zero would leave channel 1 at zero; one that read the feed
        // element-major would swap the two channels' samples. Both are caught here.
        if (!(got[0][n] == -static_cast<double>(n + 1))) {
            return false;
        }
        if (!(got[1][n] == 100.0 * static_cast<double>(n + 1))) {
            return false;
        }
    }
    return true;
}
static_assert(both_channels_negated(),
              "the wide-input walk did not fill every channel, or read the feed element-major");

// ---- the refusals, each observed objecting ------------------------------------------------------
//
// In `main` rather than as `static_assert`s: under constant evaluation a `fail()` IS the compile
// error, so a refusal can only be watched from a run (the idiom `width_test.cpp` uses).

bool refused(std::string_view expected, void (*attempt)()) {
    try {
        attempt();
        return false;
    } catch (const error& e) {
        return e.message() == expected;
    }
}

// Two named inputs, one feed: the count is wrong and the second input would never be bound.
bool a_feed_count_that_is_not_the_input_count_is_refused() {
    return refused("heinzel::feeds_are_this_graph_s: one feed per named input", [] {
        std::array<double, kBlock> xs{};
        Inputs feeds{};
        feeds.push_back(InputFeed{xs.data(), 1});
        Signals<kBlock> out{};
        run(kSum, feeds, out);
    });
}

// One input two numbers wide, a feed one number wide: the feed cannot fill the input, and saying so
// is what keeps the wide input from arriving with its second channel silently at zero.
bool a_feed_width_that_is_not_the_input_width_is_refused() {
    return refused("heinzel::feeds_are_this_graph_s: a feed's width is not this input's element count", [] {
        std::array<double, kBlock> narrow{};
        Inputs feeds{};
        feeds.push_back(InputFeed{narrow.data(), 1});
        Signals<kBlock> out{};
        run(kWide, feeds, out);
    });
}

} // namespace

int main() {
    if (!the_sum_is_both_inputs()) {
        return 1;
    }
    if (!both_channels_negated()) {
        return 2;
    }
    if (!a_feed_count_that_is_not_the_input_count_is_refused()) {
        return 3;
    }
    if (!a_feed_width_that_is_not_the_input_width_is_refused()) {
        return 4;
    }
    return 0;
}
