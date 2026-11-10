// What a debug build checks (Plan/26-value-ranges/04): a Param when it is set, an input's numbers before a run, and
// after it each promise the proof did not prove, which the program writes as an output `#k` of its own. Each check
// fails where it should, in its words, each number in the shortest digits of the double it was compared as, and passes
// where it should: a Param inside, outside and NaN; a signal input, one of a `Length` and a tensor; a promise on a
// computed value, one on a feedback value the proof cannot prove, one on a tensor's or a Param's value (named by
// element, where a run of one sample names its sample), and one whose long range a message cuts after the number; a
// port's long name cut before it. A proven promise makes no output, in either build, nor does a fact (`abs`); `#0` is
// no output of the caller's (`gates/refusals/executor.cpp`), though an input or a Param named `#x` binds; the checks'
// buffers survive a move and a second `prepare`. In release, the program is that of the graph written without ranges,
// for these graphs and `both_ways.hpp`'s corpus (a cascade's: `ranges_cascade_test.cpp`). The debug and release
// Executors are spelled `Executor<g, M, true>` and `<g, M, false>`, so that the test means the same with and without
// `NDEBUG`.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>

#include "heinzel/compile.hpp"
#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"

#include "both_ways.hpp"

using namespace heinzel;

namespace {

constexpr Index n{};

int g_wrong = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        ++g_wrong;
        std::printf("  FAILED: %s\n", what);
    }
}

// The words a run fails with, empty where it does not fail.
template <class Run>
fixed_string<kMaxMessageLength> failure(Run run) {
    try {
        run();
    } catch (const heinzel::error& e) {
        return e.text;
    }
    return {};
}
template <class Run>
void expect_failure(Run run, std::string_view words, const char* what) {
    const fixed_string<kMaxMessageLength> said = failure(run);
    if (said.view() != words) {
        std::printf("  said \"%s\"\n  want \"%.*s\"\n", said.data_, static_cast<int>(words.size()), words.data());
    }
    expect(said.view() == words, what);
}

std::unique_ptr<const CompileResult> compiled(const Graph& g, bool checked, const Machine& m = default_machine) {
    return std::unique_ptr<const CompileResult>(new auto(try_compile(g, m, Bindings{}, checked)));
}

// ---- the graphs ------------------------------------------------------------------------------------------------

constexpr Graph gain() {
    Input x{"x", Signal{8}};
    Param g{"gain", ge(0), le(1)};
    Output y{"y"};
    y = x * g;
    return y;
}

constexpr Graph bounded() {
    Input x{"x", Signal{8}, ge(-1), le(1)};
    Output y{"y"};
    y = x * 0.5;
    return y;
}

// A float input at a decimal bound: 0.1f is above 0.1.
constexpr Graph tenth() {
    Input x{"x", Signal{8}, ge(0), le(0.1)};
    Output y{"y"};
    y = x * 2.0;
    return y;
}

// Two numbers a sample, the samples bound at run time.
constexpr Graph bounded_frames() {
    Input x{"x", Signal{2, Length{}}, ge(-1), le(1)};
    Output y{"y"};
    y = x * 0.5;
    return y;
}

constexpr Graph weighted() {
    Input x{"x", Signal{4, 8}};
    Input w{"w", {4}, gt(0)};
    Output y{"y"};
    y = x * w;
    return y;
}

// The host keeps a + b above 0; the proof sees [0, 2].
constexpr Graph divided() {
    Input x{"x", Signal{8}, ge(-1), le(1)};
    Input a{"a", Signal{8}, ge(0), le(1)};
    Input b{"b", Signal{8}, ge(0), le(1)};
    Output q{"q"};
    q = x / (a + b).promise(gt(0), le(2));
    return q;
}

constexpr Graph divided_frames() {
    Input a{"a", Signal{Length{}}};
    Output q{"q"};
    q = 1.0 / (a * 2.0).promise(gt(0));
    return q;
}

// A resonant biquad: its interval bound grows by a step, so the induction cannot prove the promise.
constexpr Graph resonant() {
    Input x{"x", Signal{8}, ge(-1), le(1)};
    Output y{"y"};
    y[n] = (x[n] + 1.8 * y[n - 1] - 0.9 * y[n - 2]).promise(ge(-4), le(4));
    return y;
}

// A phase stepping down, promised below 1: `frac` of a number just below 0 is 1 (`-1e-9f + 1` in float), which its
// fact [0, 1] allows and the promise does not.
constexpr Graph falling_phase() {
    Input inc{"inc", Signal{8}, ge(-0.5), le(0)};
    Variable p;
    p[n] = frac(p[n - 1] + inc[n]).promise(lt(1));
    Output y{"y"};
    y = 1.0 / (1.0 - p[n - 1]);
    return y;
}

// A promise of a long range: the sample and the number come before it, so that a message's 95 characters cut the range.
constexpr Graph angle() {
    Input a{"a", Signal{8}};
    Output y{"y"};
    y = (a * 2.0).promise(ge(-3.141592653589793), le(3.141592653589793));
    return y;
}

// A promise on a tensor's value, the same at every sample: its check names an element.
constexpr Graph weights() {
    Input x{"x", Signal{4, 8}};
    Input w{"w", {4}};
    Output y{"y"};
    y = x * (w * 2.0).promise(gt(0));
    return y;
}

// A value with a time axis names its sample, also where a run holds one: a static `Signal{1}`, and a `Signal{4, 1}`
// whose four numbers are one sample. A value with none names its element: a Param's, as a tensor's (`weights`).
constexpr Graph one_sample() {
    Input x{"x", Signal{1}};
    Output y{"y"};
    y = (x * 2.0).promise(gt(0));
    return y;
}
constexpr Graph four_once() {
    Input x{"x", Signal{4, 1}};
    Output y{"y"};
    y = (x * 2.0).promise(gt(0));
    return y;
}
constexpr Graph param_only() {
    Input x{"x", Signal{8}};
    Param g{"g"};
    Output y{"y"};
    y = x * (g * 2.0).promise(gt(0));
    return y;
}

// A soft limiter: `abs` states its result at least 0 as a fact, which nothing checks.
constexpr Graph soft() {
    Input x{"x", Signal{8}};
    Output y{"y"};
    y = x / (1.0 + abs(x));
    return y;
}

// A Param and an input named in 63 characters, the most a name holds: a message cuts the name, so that the index and
// the number always fit (the review of Plan 26, round 2: "... = 1.00" read as inside `le(1)`).
constexpr std::string_view kLongParam = "a_parameter_name_of_sixty_three_characters_for_the_cutoff_in_hz";
constexpr std::string_view kLongInput = "an_input_name_of_sixty_three_characters_for_a_carrier_at_192khz";
constexpr Graph long_names() {
    Param p{kLongParam, ge(0), le(1)};
    Input x{kLongInput, Signal{8}, f64, ge(0)};
    Output y{"y"};
    y = x * p;
    return y;
}

// An input and a Param named `#...`: only an output's name is kept for the checks.
constexpr Graph hashed() {
    Input x{"#x", Signal{8}};
    Param g{"#gain"};
    Output y{"y"};
    y = x * g;
    return y;
}

// [1, 2] lies inside the promise.
constexpr Graph proven() {
    Input x{"x", Signal{8}, ge(-1), le(1)};
    Input a{"a", Signal{8}, ge(0), le(1)};
    Output r{"r"};
    r = x / (a + 1.0).promise(gt(0), le(3));
    return r;
}

// ---- each check, where it fails and where it does not ----------------------------------------------------------

void params() {
    std::vector<float> x(8, 1.0f);
    std::vector<float> y(8);
    const auto set = [&](double v) {
        Executor<gain, default_machine, true> ex;
        ex.input("x") = x.data(), ex.output("y") = y.data();
        ex.param("gain") = v;
        ex.run();
    };
    expect_failure([&] { set(0.5); }, "", "a Param inside its range");
    expect_failure([&] { set(1.0); }, "", "a Param at its closed end");
    expect_failure([&] { set(2.0); }, "heinzel::Executor: param gain = 2 is outside ge(0), le(1)", "a Param outside its range");
    expect_failure([&] { set(std::numeric_limits<double>::quiet_NaN()); }, "heinzel::Executor: param gain = nan is outside ge(0), le(1)",
                   "a Param set to NaN");
    // The number as given, before it is rounded to the port's float: 1 + 1e-12 is outside, its float is 1. Its digits are
    // the shortest that read back as it (six would write the bound).
    expect_failure([&] { set(1.0 + 1e-12); }, "heinzel::Executor: param gain = 1.000000000001 is outside ge(0), le(1)",
                   "a Param before it is rounded");
    expect_failure([&] { Executor<gain, default_machine, false> ex; ex.param("gain") = 2.0; }, "", "release checks no Param");
    expect_failure([] { Executor<long_names, default_machine, true> ex; ex.param(kLongParam.data()) = 1.0000000000000002; },
                   "heinzel::Executor: param a_parameter_name_of_sixty_three_characters_ = 1.0000000000000002 is ou",
                   "a long Param name cut, not the number");
}

void inputs() {
    std::vector<float> x(8, 0.5f);
    std::vector<float> y(8);
    const auto run = [&](auto ex) {
        ex.input("x") = x.data(), ex.output("y") = y.data();
        ex.run();
    };
    expect_failure([&] { run(Executor<bounded, default_machine, true>{}); }, "", "a signal inside its range");
    x[5] = 2.0f;
    expect_failure([&] { run(Executor<bounded, default_machine, true>{}); }, "heinzel::Executor: input x[5] = 2 is outside ge(-1), le(1)",
                   "a signal outside its range");
    expect_failure([&] { run(Executor<bounded, default_machine, false>{}); }, "", "release checks no input");
    x[5] = 1.0000001f; // the double it is compared as, not a float's shortest digits (1.0000001)
    expect_failure([&] { run(Executor<bounded, default_machine, true>{}); },
                   "heinzel::Executor: input x[5] = 1.0000001192092896 is outside ge(-1), le(1)", "a float as its double");
    std::vector<float> small(8, 0.05f);
    std::vector<float> twice(8);
    small[3] = 0.1f; // above 0.1: a float's own digits would read "0.1 is outside ge(0), le(0.1)"
    expect_failure(
        [&] {
            Executor<tenth, default_machine, true> ex;
            ex.input("x") = small.data(), ex.output("y") = twice.data();
            ex.run();
        },
        "heinzel::Executor: input x[3] = 0.10000000149011612 is outside ge(0), le(0.1)", "a float at a decimal bound, as its double");
    x[5] = 0.5f;

    // A `Length`'s run reads its frames: two numbers each, and nothing past them.
    std::vector<float> wide(16, 0.5f);
    std::vector<float> out(16);
    wide[9] = 2.0f;
    const auto frames = [&](std::int64_t count) {
        Executor<bounded_frames, default_machine, true> ex;
        ex.input("x") = wide.data(), ex.output("y") = out.data();
        ex.prepare(8);
        ex.run(count);
    };
    expect_failure([&] { frames(4); }, "", "a Length's run reads its frames alone");
    expect_failure([&] { frames(5); }, "heinzel::Executor: input x[9] = 2 is outside ge(-1), le(1)", "a Length's frames, two numbers each");
}

void tensor_inputs_and_long_names() {
    std::vector<float> signal(32, 1.0f);
    std::vector<float> w{1, 2, 0, 4};
    std::vector<float> z(32);
    const auto tensor = [&] {
        Executor<weighted, default_machine, true> ex;
        ex.input("x") = signal.data(), ex.input("w") = w.data(), ex.output("y") = z.data();
        ex.run();
    };
    expect_failure(tensor, "heinzel::Executor: input w[2] = 0 is outside gt(0)", "a tensor outside its range");
    w[2] = 3;
    expect_failure(tensor, "", "a tensor inside its range");

    std::vector<double> least(8, 1.0);
    std::vector<double> scaled(8);
    least[3] = -std::numeric_limits<double>::min(); // 24 characters, the longest a double's shortest digits take
    expect_failure(
        [&] {
            Executor<long_names, default_machine, true> ex;
            ex.input(kLongInput.data()) = least.data(), ex.param(kLongParam.data()) = 0.5, ex.output("y") = scaled.data();
            ex.run();
        },
        "heinzel::Executor: input an_input_name_of_sixty[3] = -2.2250738585072014e-308 is outside ge(0)", "a long input name cut, not the number");
}

void promises() {
    std::vector<float> x(8, 0.5f);
    std::vector<float> a(8, 0.5f);
    std::vector<float> b(8, 0.5f);
    std::vector<float> q(8);
    const auto run = [&](auto ex) {
        ex.input("x") = x.data(), ex.input("a") = a.data(), ex.input("b") = b.data(), ex.output("q") = q.data();
        ex.run();
    };
    expect_failure([&] { run(Executor<divided, default_machine, true>{}); }, "", "a promise kept");
    a[3] = b[3] = 0;
    expect_failure([&] { run(Executor<divided, default_machine, true>{}); }, "heinzel::Executor: promise #0 broken at sample 3: 0 is outside gt(0), le(2)",
                   "a promise broken");
    expect_failure([&] { run(Executor<divided, default_machine, false>{}); }, "", "release checks no promise");

    std::vector<float> one(8, 1.0f);
    std::vector<float> y(8);
    const auto feedback = [&] {
        Executor<resonant, default_machine, true> ex;
        ex.input("x") = one.data(), ex.output("y") = y.data();
        ex.run();
    };
    // 1, 2.8, 5.14: the filter rings past the promise, in float.
    expect_failure(feedback, "heinzel::Executor: promise #0 broken at sample 2: 5.139999866485596 is outside ge(-4), le(4)",
                   "a feedback value's promise broken");
    one.assign(8, 0.1f);
    expect_failure(feedback, "", "a feedback value's promise kept");

    std::vector<float> inc(8, -0.25f);
    std::vector<float> z(8);
    const auto phase = [&] {
        Executor<falling_phase, default_machine, true> ex;
        ex.input("inc") = inc.data(), ex.output("y") = z.data();
        ex.run();
    };
    expect_failure(phase, "", "a falling phase kept below 1");
    inc.assign(8, 0.0f);
    inc[3] = -1e-9f; // p[2] = 0, so p[3] = frac(-1e-9f) = 1
    expect_failure(phase, "heinzel::Executor: promise #0 broken at sample 3: 1 is outside lt(1)", "a falling phase at 1");

    std::vector<float> two(8, 0.5f);
    two[4] = 5.0f;
    expect_failure(
        [&] {
            Executor<angle, default_machine, true> ex;
            ex.input("a") = two.data(), ex.output("y") = q.data();
            ex.run();
        },
        "heinzel::Executor: promise #0 broken at sample 4: 10 is outside ge(-3.141592653589793), le(3.14", "a long range cut, not the number");
}

// A broken promise names its sample where its value has a time axis, though a run holds one sample, and its element
// where it has none.
void samples_and_elements() {
    std::vector<float> signal(32, 1.0f);
    std::vector<float> w{1, 2, 0, 4};
    std::vector<float> weighted_y(32);
    expect_failure(
        [&] {
            Executor<weights, default_machine, true> ex;
            ex.input("x") = signal.data(), ex.input("w") = w.data(), ex.output("y") = weighted_y.data();
            ex.run();
        },
        "heinzel::Executor: promise #0 broken at element 2: 0 is outside gt(0)", "a tensor's promise names its element");

    std::vector<float> single{-1};
    std::vector<float> four{1, 1, -1, 1};
    std::vector<float> y4(4);
    expect_failure(
        [&] {
            Executor<one_sample, default_machine, true> ex;
            ex.input("x") = single.data(), ex.output("y") = y4.data();
            ex.run();
        },
        "heinzel::Executor: promise #0 broken at sample 0: -2 is outside gt(0)", "a run of one sample names it");
    expect_failure(
        [&] {
            Executor<four_once, default_machine, true> ex;
            ex.input("x") = four.data(), ex.output("y") = y4.data();
            ex.run();
        },
        "heinzel::Executor: promise #0 broken at sample 0: -2 is outside gt(0)", "a run of one sample of four numbers names it");
    expect_failure(
        [&] {
            Executor<param_only, default_machine, true> ex;
            ex.input("x") = signal.data(), ex.param("g") = -1.0, ex.output("y") = weighted_y.data();
            ex.run();
        },
        "heinzel::Executor: promise #0 broken at element 0: -2 is outside gt(0)", "a Param's value names its element");
}

// What the compile result holds: each input's declared range and each check's promise, the check an output `#k` after
// the user's that names the promised value (a recurrence's by its `_recur`); a proven promise none, in either build.
void outputs() {
    const auto g = compiled(gain(), true);
    const auto d = compiled(divided(), true);
    const auto r = compiled(resonant(), true);
    const auto p = compiled(proven(), true);
    const auto p_release = compiled(proven(), false);
    expect(g->ranges.inputs.size() == 2 && ranges::is_whole(g->ranges.inputs[0]) && g->ranges.inputs[1] == intersect(ge(0), le(1)) &&
               g->ranges.promises.empty(),
           "a declared range, and the whole line where none is");
    const Graph& dg = d->compiled.graph;
    expect(dg.outputs.size() == 2 && dg.outputs[1].name.view() == "#0" && d->ranges.promises.size() == 1 &&
               d->ranges.promises[0] == intersect(gt(0), le(2)) && dg.ranges.empty() && compiled(divided(), false)->compiled.graph.outputs.size() == 1,
           "a promise checked by #0, in debug alone");
    Graph feedback = resonant();
    detail::StatedRanges stated{};
    const bool taken = infer(feedback).ok && detail::take_ranges(feedback, true, stated).ok;
    bool names_recur = false;
    for (const Node& node : feedback.nodes) {
        names_recur = names_recur || (node.attr.kind == AttrKind::Recurrence && node.output == feedback.outputs[1].value);
    }
    expect(taken && names_recur && feedback.outputs[1].value == feedback.outputs[0].value && r->compiled.graph.outputs.size() == 2,
           "a feedback value's check names its recurrence's value, as y does");
    expect(p->compiled.graph.outputs.size() == 1 && p->compiled == p_release->compiled && p->ranges.promises.empty(),
           "a proven promise makes no output, in either build");
    const auto s = compiled(soft(), true);
    expect(s->refusal.view().empty() && s->compiled.graph.outputs.size() == 1 && s->ranges.promises.empty() &&
               s->compiled == compiled(soft(), false)->compiled,
           "a fact makes no output: debug's program is release's");
}

// An input and a Param named `#...` bind, in either build: only among the outputs is `#0` no port of the caller's.
template <bool Checked>
bool hashed_bind() {
    std::vector<float> x(8, 2.0f);
    std::vector<float> y(8);
    Executor<hashed, default_machine, Checked> ex;
    ex.input("#x") = x.data();
    ex.param("#gain") = 0.5;
    ex.output("y") = y.data();
    ex.run();
    return y == std::vector<float>(8, 1.0f);
}

// The checks' buffers made by the constructor or `prepare`, moved with the `Executor`, and made again by `prepare`.
void buffers() {
    std::vector<float> a(16, 1.0f);
    std::vector<float> q(16);
    using E = Executor<divided_frames, default_machine, true>;
    const auto bind = [&](E& ex) { ex.input("a") = a.data(), ex.output("q") = q.data(); };
    expect_failure(
        [&] {
            E ex;
            bind(ex);
            ex.prepare(4);
            ex.run(4);
            E moved = static_cast<E&&>(ex);
            a[2] = -1;
            moved.run(4);
        },
        "heinzel::Executor: promise #0 broken at sample 2: -2 is outside gt(0)", "a check after a move");
    a.assign(16, 1.0f);
    expect_failure(
        [&] {
            E ex;
            bind(ex);
            ex.prepare(4);
            E moved = static_cast<E&&>(ex);
            ex.prepare(8); // the moved-from one, made again
            a[6] = -1;
            ex.run(8);
        },
        "heinzel::Executor: promise #0 broken at sample 6: -2 is outside gt(0)", "a moved-from Executor prepared again");
    a.assign(16, 1.0f);
    expect_failure(
        [&] {
            E ex;
            bind(ex);
            ex.prepare(4);
            ex.run(4);
            ex.prepare(16);
            a[12] = -1;
            ex.run(16);
        },
        "heinzel::Executor: promise #0 broken at sample 12: -2 is outside gt(0)", "a second prepare");
    std::vector<float> x(8, 0.5f);
    std::vector<float> b(8, 0.5f);
    std::vector<float> c(8, 0.5f);
    std::vector<float> out(8);
    expect_failure(
        [&] {
            using S = Executor<divided, default_machine, true>;
            S first;
            S second;
            first.input("x") = x.data(), first.input("a") = b.data(), first.input("b") = c.data(), first.output("q") = out.data();
            second = static_cast<S&&>(first);
            b[7] = c[7] = 0;
            second.run();
        },
        "heinzel::Executor: promise #0 broken at sample 7: 0 is outside gt(0), le(2)", "a static program's check after a move");
}

// ---- release ---------------------------------------------------------------------------------------------------

// Release's program is the graph's written without ranges, or the planner's refusal of it: planned as `compile` plans
// it, past the proof, which would refuse it without them.
bool as_written(const Graph& g) {
    Graph without = g;
    if (!infer(without).ok) {
        return false;
    }
    without.ranges.clear();
    const auto with = compiled(g, false);
    const std::unique_ptr<const Planning> plain(new auto(planned_as_rewritten(without, default_machine)));
    if (!plain->planned) {
        return with->refusal == detail::held_refusal(plain->planned.error().view());
    }
    return with->refusal.view().empty() && with->compiled == Compiled{plain->graph, plain->planned->program};
}

void release() {
    expect(as_written(gain()) && as_written(bounded()) && as_written(weighted()) && as_written(divided()) && as_written(resonant()) &&
               as_written(proven()) && as_written(soft()),
           "release's program is that of the graph written without ranges");
    bool corpus = true;
    for (std::size_t i = 0; i < testing::kCorpusSize; ++i) {
        corpus = corpus && as_written(testing::corpus_graph(i));
    }
    expect(corpus, "and so is the both-ways corpus's");
    // Declared ranges alone check no promise: debug's program is release's.
    expect(compiled(gain(), true)->compiled == compiled(gain(), false)->compiled &&
               compiled(bounded(), true)->compiled == compiled(bounded(), false)->compiled,
           "declared ranges alone: one program in both builds");
}

} // namespace

int main() {
    params();
    inputs();
    tensor_inputs_and_long_names();
    promises();
    samples_and_elements();
    outputs();
    expect(hashed_bind<true>() && hashed_bind<false>(), "an input and a Param named # bind");
    buffers();
    release();
    if (g_wrong != 0) {
        std::printf("ranges_checked_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("ranges_checked_test: a debug build checks Params, inputs and unproven promises; release plans as without ranges\n");
    return 0;
}
