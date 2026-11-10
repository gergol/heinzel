// A fold inside a feedback stage's sample (Plan/23 10), planned: the planner makes that stage (the voices in lanes, time the
// inner loop, never split), and the graphs run on `Executor<…, zen2>` at High as the trees of `fold_in_feedback_test.cpp`
// do, at Normal (rewritten) within rounding of the walk; refused by name: 17 double voices, a symbolic count, two folds
// in one loop; a fold that keeps its axis never split along it, on zen2 and the M1 Pro against the walk; cheap maps
// leading into such a cycle never join an earlier feedback stage for it (15 maps after a one-pole keep their lanes).

#include "fold_in_feedback_graphs.hpp"

#include "executed.hpp"
#include "planned.hpp"

namespace {

// ---- planned: the graphs through `compile()` and `Executor<…, zen2>` (10.3) --------------------------------------

// `Build`'s Executor for `M` over `x` in blocks of `blocks` (cycled), reset before the run once `reset_at` samples are
// done: its output.
template <auto Build, const Machine& M = zen2, class T>
std::vector<double> executed(const std::vector<T>& x, const std::vector<std::int64_t>& blocks, std::size_t reset_at = kSamples) {
    const auto run = std::make_unique<Executor<Build, M>>();
    run->prepare(*std::max_element(blocks.begin(), blocks.end()));
    std::vector<T> y(x.size());
    for (std::size_t at = 0, b = 0; at < x.size(); ++b) {
        if (at == reset_at) {
            run->reset();
        }
        const auto m = static_cast<std::size_t>(std::min<std::int64_t>(blocks[b % blocks.size()], static_cast<std::int64_t>(x.size() - at)));
        run->input("x") = x.data() + at;
        run->output("y") = y.data() + at;
        run->run(static_cast<std::int64_t>(m));
        at += m;
    }
    return std::vector<double>(y.begin(), y.end());
}

// The planner's schedule of the graph at High for zen2: the stage holding its fold (its cycle; a map of the feed alone
// and what reads the loop's result outside it have stages of their own), its voices in lanes outside the time loop, the
// inner loop, and never split among workers (its one-voice nodes do not move along the voices, `planner/parallel.hpp`).
template <Bank B, class T, std::size_t V>
constexpr bool planned_as_one_sample() {
    const std::expected<loops::Schedule, loops::Refusal> d = loops::planner::schedule_of(kAtHigh<B, T, V>, zen2);
    std::int32_t samples = 0;
    for (std::size_t s = 0; d && s < d->stages.size(); ++s) {
        const loops::Schedule::Stage& st = d->stages[s];
        bool folds = false;
        for (const std::int32_t ni : st.nodes) {
            folds = folds || loops::graph::is_a_fold(kAtHigh<B, T, V>.nodes[static_cast<std::size_t>(ni)]);
        }
        samples += folds && st.loops.size() == 2 && st.loops[0].kind == loops::LoopKind::Vector && st.loops[0].axis == 0 && st.inner_from == 1 &&
                           st.parallel == -1 ? 1 : 0;
    }
    return samples == 1;
}

// The graph at High on `Executor<…, zen2>` against its walk, bit for bit: one run, blocks, uneven blocks, a run after
// reset.
template <Bank B, class T, std::size_t V>
void check_planned(const char* what) {
    static_assert(planned_as_one_sample<B, T, V>());
    constexpr auto build = testing::at_high<graph_of<B, T, V>>;
    const std::vector<double> x = noise();
    const std::vector<double> want = walked(graph_of<B, T, V>(), x);
    const std::vector<T> fed(x.begin(), x.end());
    std::printf("%s\n", what);
    check(same_bits(executed<build>(fed, {static_cast<std::int64_t>(kSamples)}), want), "one run on Executor");
    for (const std::int64_t block : {1, 7, 256}) {
        check(same_bits(executed<build>(fed, {block}), want), "in blocks on Executor");
    }
    check(same_bits(executed<build>(fed, {5, 1, 13, 2, 64, 3, 30, 1, 9}), want), "in blocks of uneven lengths on Executor");
    std::vector<T> twice = fed;
    twice.insert(twice.end(), fed.begin(), fed.end());
    check(same_bits(executed<build>(twice, {static_cast<std::int64_t>(kSamples)}, kSamples), want, kSamples), "after reset on Executor");
}

// At Normal, rewritten (the feedback sums in ready order, fused) and its lanes added pairwise: within `bound` of the walk
// of the graph as written, in one run and in blocks of 7.
template <Bank B, class T, std::size_t V>
void check_planned_normal(const char* what, double bound) {
    const std::vector<double> x = noise();
    const std::vector<double> want = walked(graph_of<B, T, V>(), x, false);
    const std::vector<T> fed(x.begin(), x.end());
    for (const std::int64_t block : {static_cast<std::int64_t>(kSamples), std::int64_t{7}}) {
        const std::vector<double> got = executed<graph_of<B, T, V>>(fed, {block});
        double worst = 0;
        for (std::size_t i = 0; i < kSamples; ++i) {
            worst = std::max(worst, std::abs(got[i] - want[i]));
        }
        std::printf("%s on Executor at Normal, blocks of %lld: %.3g from the walk\n", what, static_cast<long long>(block), worst);
        check(worst <= bound, "on Executor at Normal, within rounding of the walk");
    }
}

// Refused by name, and so planned as written, which is refused too: more voices than four lane bodies hold (17 doubles),
// two folds in one sample (the graph facts), a symbolic count of voices, a cycle reading its past two samples back
// only (no state of one sample in registers), a machine without lanes (a vector of one double).
constexpr Graph symbolic_voices() {
    Input x{"x", Signal{Dim{1}, Length{}}, f64};
    Variable y;
    Variable v;
    const Expr u = x - ff::kGain * y[n - 1];
    v[n] = 0.5 * u + 0.25 * v[n - 1];
    y[n] = Expr(v).sum(0);
    return at_high(finish(Output{"y"} = y));
}
constexpr std::string_view kTooManyVoices = "heinzel::compile: a sample folding its voices takes a literal count, four lane bodies at most";
static_assert(try_compile(kAtHigh<Bank::Plain, double, 17>, zen2).refusal.view() == kTooManyVoices);
static_assert(try_compile(symbolic_voices(), zen2).refusal.view() == kTooManyVoices);
static_assert(try_compile(two_folds(), zen2).refusal.view() == "heinzel::compile: feedback through anything but maps of one shape is not lowered yet");
constexpr Graph two_samples_back() {
    Input x{"x", Signal{Length{}}, f64};
    Variable y;
    Variable v;
    const Expr u = x - ff::kGain * y[n - 2];
    v[n] = constant(each_voice<double, 8>(ff::c0)) * u + constant(each_voice<double, 8>(ff::a1)) * v[n - 2];
    y[n] = Expr(v).sum(0);
    return at_high(finish(Output{"y"} = y));
}
constexpr Machine kNoLanes = [] {
    Machine m = zen2;
    m.vector_bytes = 8;
    return m;
}();
static_assert(try_compile(two_samples_back(), zen2).refusal.view() ==
              "heinzel::compile: a sample folding its voices needs its state of one sample back, in registers");
static_assert(try_compile(kAtHigh<Bank::Plain, double, 8>, kNoLanes).refusal.view() ==
              "heinzel::compile: a sample folding its voices takes them in lanes, which this machine has not");

// A fold that keeps its axis (keepdims) in a loop: its one-voice values are [1, time], made along the voices at extent
// one, and on a machine of many cores the planner offered a split along them, the axis its fold collapses (the review
// of P10). Never split; on zen2 and the M1 Pro at High against the walk, bit for bit.
constexpr Graph kept_axis() {
    Input x{"x", Signal{Length{}}, f64};
    Variable y;
    Variable v;
    const Expr u = x - ff::kGain * y[n - 1];
    v[n] = constant(each_voice<double, 5>(ff::c0)) * u + constant(each_voice<double, 5>(ff::a1)) * v[n - 1];
    y[n] = Expr(v).sum(0, keepdims) + ff::kDirect * u;
    return finish(Output{"y"} = y);
}
template <const Machine& M>
constexpr bool never_split() {
    const std::expected<loops::Schedule, loops::Refusal> d = loops::planner::schedule_of(at_high(kept_axis()), M);
    bool split = !d.has_value();
    for (std::size_t s = 0; d && s < d->stages.size(); ++s) {
        split = split || d->stages[s].parallel >= 0;
    }
    return !split;
}
static_assert(never_split<zen2>() && never_split<m1_pro>());
template <const Machine& M>
void check_keepdims(const char* what) {
    const std::vector<double> x = noise();
    const std::vector<double> want = walked(kept_axis(), x);
    std::printf("%s\n", what);
    check(same_bits(executed<testing::at_high<kept_axis>, M>(x, {static_cast<std::int64_t>(kSamples)}), want), "a fold that keeps its axis, one run");
    check(same_bits(executed<testing::at_high<kept_axis>, M>(x, {7}), want), "a fold that keeps its axis, in blocks of 7");
}

// A one-pole, `Maps` cheap maps, then the plain bank in its loop, at Normal (the second review of P10): the maps lead
// into a cycle that folds every sample, which joins nothing, so they never join the one-pole's stage for it and keep
// their lanes. Joined, 13 to 17 of them cost 1.25-1.39x (15: 12.3 against 10.2 ns a sample in blocks of 256).
template <int Maps>
constexpr Graph pole_then_bank() {
    Input x{"x", Signal{Length{}}, f64};
    Variable p;
    Variable y;
    Variable v;
    p[n] = 0.125 * x + 0.875 * p[n - 1];
    Expr s = p;
    for (int k = 0; k < Maps; ++k) {
        s = k % 2 == 0 ? s * 0.75 + 0.125 : max(s, -2.0);
    }
    const Expr u = s - ff::kGain * y[n - 1];
    v[n] = constant(each_voice<double, 8>(ff::c0)) * u + constant(each_voice<double, 8>(ff::c1)) * u[n - 1] + constant(each_voice<double, 8>(ff::a1)) * v[n - 1];
    y[n] = Expr(v).sum(0) + ff::kDirect * u;
    return finish(Output{"y"} = y);
}
// The one-pole's stage, the one holding the first recurrence, holds fewer nodes than the maps.
template <int Maps>
constexpr bool maps_apart() {
    Graph g = pole_then_bank<Maps>();
    if (!infer(g).ok) {
        return false;
    }
    const Graph h = rewrite::rewritten(g, zen2);
    const std::expected<loops::Schedule, loops::Refusal> d = loops::planner::schedule_of(h, zen2);
    std::size_t pole = 0;
    while (pole < h.nodes.size() && h.nodes[pole].attr.kind != AttrKind::Recurrence) {
        ++pole;
    }
    bool apart = d.has_value();
    for (std::size_t s = 0; d && s < d->stages.size(); ++s) {
        for (const std::int32_t ni : d->stages[s].nodes) {
            apart = apart && (static_cast<std::size_t>(ni) != pole || d->stages[s].nodes.size() < static_cast<std::size_t>(Maps));
        }
    }
    return apart;
}
static_assert(maps_apart<15>(), "cheap maps leading into a cycle that folds every sample keep their lanes");

} // namespace

int main() {
    check_planned<Bank::Plain, double, 8>("planned: eight double voices");
    check_planned<Bank::Plain, double, 3>("planned: three double voices, a masked tail");
    check_planned<Bank::Plain, double, 16>("planned: sixteen double voices");
    check_planned<Bank::Plain, float, 5>("planned: five float voices, a masked tail");
    check_planned<Bank::Direct, double, 8>("planned: the loop read in the lanes directly");
    check_planned<Bank::OwnLag, double, 3>("planned: a lag of the fold's own result, a masked tail");
    check_planned<Bank::Tdf2, double, 8>("planned: TDF2, lanes after the fold");
    check_planned_normal<Bank::Plain, double, 8>("planned: eight double voices", 1e-12);
    check_planned_normal<Bank::OwnLag, double, 3>("planned: a lag of the fold's own result, a masked tail", 1e-12);
    check_planned_normal<Bank::Plain, float, 5>("planned: five float voices, a masked tail", 1e-5);
    check_keepdims<zen2>("planned: a fold that keeps its axis, on zen2's 16 cores");
    check_keepdims<m1_pro>("planned: a fold that keeps its axis, on the M1 Pro");
    if (g_wrong != 0) {
        std::printf("%d checks failed\n", g_wrong);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
