// A run's first sample (Plan/33 S3): `_first`, 1 at the first sample of a run and 0 at every other, a run being one
// call of a program over its block. The walk walks a signal as the runs of a program called on it one after another
// (`run(g, feeds, out, run_length)`), its state crossing from one run to the next, `_first` 1 at each run's start. The
// stage that reads it is peeled (`Schedule::Stage::peeled`), its time loop running that sample apart
// (`Node::peels_start`); `Executor` in runs at `High` gives the walk's numbers bit for bit, and the lowering refuses a
// schedule that reads it unpeeled, or peels a stage in lanes, a band or a worker's split, by name; a lag of it, which
// the planner refuses as a lag of a value of no axes, the lowering too, by name, as a schedule written by hand.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#include <string>
#include <string_view>

#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/print.hpp"
#include "heinzel/sema/analyze.hpp"

#include "executed.hpp"
#include "interpret.hpp"
#include "loops_schedules.hpp"
#include "planned.hpp"

using namespace heinzel;

namespace {

constexpr std::size_t kSamples = 12;

// A counter that a run's start resets: the sample's place in its run, plus the input.
constexpr Graph counted() {
    constexpr Index n{};
    Input x{"x", Signal{Length{}}};
    Variable count;
    count[n] = where(detail::first_sample(), 0.0, count[n - 1] + 1.0);
    Output y{"y"};
    y = x + count;
    return finish(y);
}

// `counted` walked over kSamples samples in runs of `run_length`, fed x[n] = 100 n.
std::vector<double> counted_walk(std::size_t run_length) {
    const Graph g = testing::analysed<counted>(kSamples);
    std::vector<double> x(kSamples);
    for (std::size_t i = 0; i < kSamples; ++i) {
        x[i] = 100.0 * static_cast<double>(i);
    }
    Inputs feeds{};
    feeds.push_back(InputFeed{x.data(), 1});
    constexpr std::size_t planes = testing::store_of<counted, kSamples>().planes;
    const auto out = std::make_unique<Signals<kSamples, planes>>();
    run(g, feeds, *out, run_length);
    std::vector<double> y(kSamples);
    for (std::size_t i = 0; i < kSamples; ++i) {
        y[i] = sample_at(*out, g.outputs[0].value, 0, i);
    }
    return y;
}

bool counts_within_its_runs(std::size_t run_length) {
    const std::vector<double> y = counted_walk(run_length);
    for (std::size_t i = 0; i < kSamples; ++i) {
        if (y[i] != 100.0 * static_cast<double>(i) + static_cast<double>(i % run_length)) {
            std::fprintf(stderr, "runs of %zu: sample %zu is %g\n", run_length, i, y[i]);
            return false;
        }
    }
    return true;
}

// `counted` at `High` on `Executor`, prepared for `run_length` and run over kSamples in runs of it, the last what is
// left, x[n] = 100 n: y, required to be the walk's in runs bit for bit.
bool counted_runs(std::size_t run_length) {
    using E = Executor<testing::at_high<counted>>;
    testing::well_formed_once<testing::at_high<counted>>();
    std::vector<float> x(kSamples);
    for (std::size_t i = 0; i < kSamples; ++i) {
        x[i] = 100.0f * static_cast<float>(i);
    }
    std::vector<float> y(kSamples);
    const auto ex = std::make_unique<E>();
    ex->prepare(static_cast<std::int64_t>(run_length));
    for (std::size_t at = 0; at < kSamples; at += run_length) {
        ex->input("x") = x.data() + at;
        ex->output("y") = y.data() + at;
        ex->run(static_cast<std::int64_t>(at + run_length <= kSamples ? run_length : kSamples - at));
    }
    testing::require_walked("y", y, counted_walk(run_length));
    return true;
}

// A map that reads a run's start: `y = where(_first, 2 x, x)`. Its stage runs in lanes, which a peeled stage may not
// until a benchmark asks.
constexpr Graph doubled_at_the_start() {
    Input x{"x", Signal{Length{}}};
    Output y{"y"};
    const Expr twice = x * 2.0;
    y = where(detail::first_sample(), twice, x);
    return finish(y);
}

constexpr Graph kCounted = testing::analysed<counted>(0);

// `y = x * _first[n - 1]`, reachable only through `detail::first_sample()` or a rewrite: `_first`, a lag of it, the product.
constexpr Graph lagged_start() {
    constexpr Index n{};
    Input x{"x", Signal{Length{}}};
    const Expr first = detail::first_sample();
    Output y{"y"};
    y = x * first[n - 1];
    return finish(y);
}
constexpr Graph kLaggedStart = testing::analysed<lagged_start>(0);

constexpr Graph doubled() {
    Input x{"x", Signal{Length{}}};
    Output y{"y"};
    y = x * 2.0;
    return finish(y);
}
constexpr Graph kPlain = testing::analysed<doubled>(0);

// The planner's schedule of `counted` with one decision taken back.
template <class Edit>
std::string_view refused(Edit edit) {
    static loops::Refusal why{};
    loops::Schedule d = planned::schedule_of(kCounted, zen2);
    edit(d);
    why = planned::refusal_of(kCounted, d);
    return why.view();
}

bool said(std::string_view what, std::string_view got, std::string_view wanted) {
    if (got != wanted) {
        std::fprintf(stderr, "%.*s: \"%.*s\"\n", static_cast<int>(what.size()), what.data(), static_cast<int>(got.size()), got.data());
        return false;
    }
    return true;
}

// The counter's stage is peeled, its time loop peels the start, and the counter reads it: what the prints show.
bool planned_apart() {
    const loops::Schedule d = planned::schedule_of(kCounted, zen2);
    const loops::Program p = planned::program_of(kCounted, d);
    std::size_t peeled = 0;
    for (const loops::Schedule::Stage& stage : d.stages) {
        peeled += stage.peeled ? 1 : 0;
    }
    std::size_t starts = 0;
    for (const loops::Node& node : p.nodes) {
        starts += node.peels_start ? 1 : 0;
    }
    const std::string schedule = loops::print(d, kCounted);
    const std::string program = loops::print(p);
    const bool shown = schedule.find(" peeled") != std::string::npos && program.find(" start") != std::string::npos;
    if (peeled != 1 || starts != 1 || !shown) {
        std::fprintf(stderr, "planned apart: %zu peeled, %zu starts\n%s%s", peeled, starts, schedule.c_str(), program.c_str());
        return false;
    }
    return true;
}

std::string_view band_refusal() {
    static loops::Refusal why{};
    const loops::Schedule d = planned::schedule_of(kCounted, zen2);
    for (const loops::Schedule::Stage& stage : d.stages) {
        why = stage.peeled ? loops::why_not_in_any_band(kCounted, loops::graph::facts_of(kCounted), stage) : why;
    }
    return why.view();
}

bool refusals() {
    const auto unpeeled = [](loops::Schedule& d) {
        for (loops::Schedule::Stage& stage : d.stages) {
            stage.peeled = false;
        }
    };
    const auto split = [](loops::Schedule& d) {
        d.parallel_work = 1;
        for (loops::Schedule::Stage& stage : d.stages) {
            stage.parallel = stage.peeled ? 0 : stage.parallel;
        }
    };
    static loops::Refusal pointless{};
    loops::Schedule plain = planned::schedule_of(kPlain, zen2);
    plain.stages[0].peeled = true;
    pointless = planned::refusal_of(kPlain, plain);
    static loops::Refusal lanes{};
    lanes = planned::refusal_of(testing::analysed<doubled_at_the_start>(0), zen2);
    static loops::Refusal lag_planned{};
    lag_planned = planned::refusal_of(kLaggedStart, zen2);
    static loops::Refusal lag_lowered{}; // (the lag and the product in one peeled stage, `_first` computed where read)
    loops::Schedule by_hand = loops_schedules::schedule({loops_schedules::walked(kLaggedStart, loops_schedules::stage({1, 2}))});
    by_hand.stages[0].peeled = true;
    lag_lowered = planned::refusal_of(kLaggedStart, by_hand);
    return said("unpeeled", refused(unpeeled), "heinzel::compile: a stage reads a run's first sample but is not peeled") &&
           // (the counter's one loop walks time, so the split rule of a feedback stage refuses it first)
           said("split", refused(split), "heinzel::compile: a split group reads what it writes only at the point it makes") &&
           // (the counter is one stage: asked of the band rule the planner and the lowering share)
           said("banded", band_refusal(), "heinzel::compile: a band holds no peeled stage (each chunk would start a run)") &&
           said("reads no start", pointless.view(), "heinzel::compile: a peeled stage is one stage reading a run's start, in no band, no split") &&
           said("in lanes", lanes.view(), "heinzel::compile: a peeled stage runs in no lanes, until a benchmark asks") &&
           said("a lag planned", lag_planned.view(), "heinzel::compile: a value of no axes has no time to be read back along") &&
           said("a lag lowered", lag_lowered.view(), "heinzel::compile: a run's first sample has no past for a lag to read");
}

} // namespace

int main() {
    // `_first` is one number of no axis, typed by its reader as a literal is; the counter takes the graph's length.
    static_assert([] {
        const Graph g = testing::analysed<counted>(kSamples);
        for (const Node& node : g.nodes) {
            const Value& made = g.values[static_cast<std::size_t>(node.output)];
            if (node.op == op::First) {
                return made.shape.rank == 0 && made.type == ElemType::F32 && !made.timeless;
            }
        }
        return false;
    }(), "`_first`: one number, f32 where an f32 reads it, no tensor");
    // One run (the block is the run), runs of 5 (the last 2 samples), of 4, of 1 (every sample a start).
    bool ok = counts_within_its_runs(kSamples) && counts_within_its_runs(5) && counts_within_its_runs(4) &&
              counts_within_its_runs(1);
    // The Executor in the same runs: `_first` fires at each call's first sample, and nowhere else.
    ok = ok && counted_runs(kSamples) && counted_runs(5) && counted_runs(4) && counted_runs(1);
    ok = ok && planned_apart() && refusals();
    std::printf("first_sample: %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
