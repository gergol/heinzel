// The work gathered ahead of every unit with a loop (`docs/knob-math.md`; `loops/planner/stages.hpp` `next_unit`): work
// of no axis made from timeless values alone, as a filter's coefficients worked out from its knobs. A cascade of RBJ
// peaks, three Params a band (`freq`, `gain`, `q`, as the knob-math probe wrote them), of two, three
// and four bands, planned for zen2:
//
//   every stage with no loop comes before the first with a loop, each band's knob math a stage of its own;
//   as written and as rewritten, the stages with loops are those of the same cascade with its five coefficients a band
//   set as Params: their loops alike, and the sections' feedback chained alike, where a knob stage between two sections
//   kept them apart (four bands: and where the knob math, counted as work leading into the next section's cycle, priced
//   a fourth section out of the stage; as rewritten: and where the rewrite's mirror of that chaining counted it so, and
//   two bands took the feedback sums' other order; and from three bands on, where sections in lanes links both
//   cascades' sections into one cycle);
//   on `Executor<…, zen2>` at High, in blocks of 1, 32 and 256, bit for bit the walk.
//
// And timeless work with a loop keeps its schedule: a map over a declared tensor between two one-poles stays in its
// stage between theirs.
//
// Which stages are kept (`loops/planner/kept.hpp`), run only when a Param they watch changed: every
// knob stage of the cascades, as written and as rewritten, each watching its own band's three knobs, in the schedule
// and in the program; a chain of knob math longer than a stage holds, two stages, the second watching the first's
// Param and its own. Not kept: a stage with a loop (rule 1), so a map over a declared tensor (rule 3: no value of no
// axis is made from one by maps alone, so rules 1 and 2 refuse it first); a stage that makes an output (rule 5), and one
// that reads what such a stage made (rule 4).

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <initializer_list>
#include <memory>
#include <numbers>
#include <string_view>
#include <utility>
#include <vector>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/loops/planner/plan.hpp"
#include "heinzel/rewrite/rewritten.hpp"
#include "heinzel/sema/analyze.hpp"

#include "executed.hpp"
#include "planned.hpp"

using namespace heinzel;

namespace {

namespace loops = heinzel::loops;

constexpr Index n{};
constexpr double kRate = 48'000;
constexpr std::size_t kMostBands = 4;
constexpr Length kSamples{.multiple_of = 1, .min = 1, .expect = 64};

inline constexpr const char* kKnob[kMostBands][3] = {{"freq0", "gain0", "q0"}, {"freq1", "gain1", "q1"}, {"freq2", "gain2", "q2"}, {"freq3", "gain3", "q3"}};
inline constexpr const char* kCoefficient[kMostBands][5] = {{"b0_0", "b1_0", "b2_0", "a1_0", "a2_0"},
                                                            {"b0_1", "b1_1", "b2_1", "a1_1", "a2_1"},
                                                            {"b0_2", "b1_2", "b2_2", "a1_2", "a2_2"},
                                                            {"b0_3", "b1_3", "b2_3", "a1_3", "a2_3"}};

// An RBJ peak over `x`, its coefficients from its three knobs.
constexpr Expr peak(Expr x, const char* const* name) {
    Param freq{name[0], ge(20), le(20000)};
    Param gain{name[1], ge(-24), le(24)};
    Param q{name[2], ge(0.1), le(10)};
    const Expr turns = freq * (2.0 / kRate);
    const Expr c = cospi(turns);
    const Expr s = sinpi(turns);
    const Expr big = exp(gain * (std::numbers::ln10 / 40.0));
    const Expr alpha = s / (2.0 * q);
    const Expr a0 = (1.0 + alpha / big).promise(gt(0));
    const Expr b0 = (1.0 + alpha * big) / a0;
    const Expr b1 = -2.0 * c / a0;
    const Expr b2 = (1.0 - alpha * big) / a0;
    const Expr a1 = -2.0 * c / a0;
    const Expr a2 = (1.0 - alpha / big) / a0;
    Variable y;
    y[n] = b0 * x + b1 * x[n - 1] + b2 * x[n - 2] - a1 * y[n - 1] - a2 * y[n - 2];
    return y;
}

// The same biquad, its five coefficients Params.
constexpr Expr section(Expr x, const char* const* name) {
    Param b0{name[0]};
    Param b1{name[1]};
    Param b2{name[2]};
    Param a1{name[3]};
    Param a2{name[4]};
    Variable y;
    y[n] = b0 * x + b1 * x[n - 1] + b2 * x[n - 2] - a1 * y[n - 1] - a2 * y[n - 2];
    return y;
}

// `Bands` peaks in series over a block given at run time; `Coefficients`: the sections with their coefficients Params.
template <std::size_t Bands, bool Coefficients>
constexpr Graph cascade() {
    Input x{"x", Signal{kSamples}};
    Expr s = x;
    for (std::size_t k = 0; k < Bands; ++k) {
        s = Coefficients ? section(s, kCoefficient[k]) : peak(s, kKnob[k]);
    }
    Output y{"y"};
    y = s;
    return y;
}

// Timeless work with a loop between two one-poles: `w * 2`, over a declared tensor of four numbers.
constexpr Graph tensor_between() {
    Input x{"x", Signal{4, kSamples}};
    Input w{"w", {4}};
    Variable a;
    Variable b;
    a[n] = x + 0.5 * a[n - 1];
    const Expr gain = w * 2.0;
    b[n] = a * gain + 0.25 * b[n - 1];
    Output y{"y"};
    y = b;
    return y;
}

// A chain of 100 maps of Param `p`, its last plus Param `q`, times a signal: past a stage's room (96 nodes), two stages
// with no loop, the second reading the first's value. `Shown`: the first's last value an output too.
template <bool Shown>
constexpr Graph long_knob_chain() {
    Input x{"x", Signal{kSamples}};
    Param p{"p"};
    Param q{"q"};
    Expr t = p;
    for (int k = 0; k < 96; ++k) {
        t = t * 1.0001;
    }
    const Expr shown = t;
    for (int k = 0; k < 3; ++k) {
        t = t * 1.0001;
    }
    const Expr gain = t + q;
    Output y{"y"};
    y = x * gain;
    if constexpr (Shown) {
        Output o{"o"};
        o = shown;
        return finish(y, o);
    }
    return y;
}

// A gain from a knob, also an output: a stage with no loop that makes an output.
constexpr Graph shown_gain() {
    Input x{"x", Signal{kSamples}};
    Param level{"level"};
    const Expr gain = exp(level * 0.1);
    Output y{"y"};
    y = x * gain;
    Output g{"g"};
    g = gain;
    return finish(y, g);
}

// `Build`'s graph analysed, without the ranges the proof would take (as `compile` hands it on); `rewritten`: as the
// rewrites hand it to the planner for zen2.
template <auto Build>
constexpr Graph analysed(bool rewritten) {
    Graph g = Build();
    if (!infer(g).ok) {
        fail("knob_stages_test: a graph did not resolve");
    }
    g.ranges.clear();
    return rewritten ? rewrite::rewritten(g, zen2) : g;
}

constexpr bool loops_in(const loops::Schedule::Stage& stage) { return !stage.loops.empty(); }

// Every stage of `s` with no loop comes before its first stage with a loop; and how many there are.
constexpr std::int32_t stages_ahead(const loops::Schedule& s) {
    std::int32_t ahead = 0;
    bool looped = false;
    for (const loops::Schedule::Stage& stage : s.stages) {
        if (!loops_in(stage) && looped) {
            return -1;
        }
        looped = looped || loops_in(stage);
        ahead += loops_in(stage) ? 0 : 1;
    }
    return ahead;
}

// The stages of `s` with a loop as their loops and the cycles of `g` they hold: per stage with a loop, its loops, then,
// per cycle in the order the stages run them, the place among those stages of the stage that holds it.
struct LoopStages {
    std::vector<fixed_vector<loops::Schedule::Loop, loops::kMaxStageLoops>> nests{};
    std::vector<std::int32_t> cycles{};

    constexpr bool operator==(const LoopStages&) const = default;
};
constexpr LoopStages loop_stages(const Graph& g, const loops::Schedule& s) {
    const loops::graph::Facts facts = loops::graph::facts_of(g);
    const loops::graph::Feedback feedback = loops::graph::find_feedback(g, facts);
    LoopStages out{};
    std::vector<std::int32_t> place(s.stages.size(), -1);
    for (std::size_t k = 0; k < s.stages.size(); ++k) {
        if (loops_in(s.stages[k])) {
            place[k] = static_cast<std::int32_t>(out.nests.size());
            out.nests.push_back(s.stages[k].loops);
        }
    }
    for (std::size_t k = 0; k < s.stages.size(); ++k) {
        for (const std::int32_t ni : s.stages[k].nodes) {
            if (feedback.cycle[static_cast<std::size_t>(ni)] == ni) { // (the node that opens its cycle)
                out.cycles.push_back(place[k]);
            }
        }
    }
    return out;
}

// The cascade analysed (`Rewritten`: and rewritten) and its schedule for zen2, a constant each, so that the asserts below
// rewrite and plan each graph once.
template <std::size_t Bands, bool Coefficients, bool Rewritten>
constexpr Graph kCascade = analysed<cascade<Bands, Coefficients>>(Rewritten);
template <std::size_t Bands, bool Coefficients, bool Rewritten>
constexpr loops::Schedule kSchedule = planned::schedule_of(kCascade<Bands, Coefficients, Rewritten>, zen2);

template <std::size_t Bands, bool Rewritten>
constexpr bool knobs_first() {
    return stages_ahead(kSchedule<Bands, false, Rewritten>) == static_cast<std::int32_t>(Bands);
}
// `cycles`: how many the stages hold.
template <std::size_t Bands, bool Rewritten>
constexpr bool chained_alike(std::size_t cycles = Bands) {
    const LoopStages knobs = loop_stages(kCascade<Bands, false, Rewritten>, kSchedule<Bands, false, Rewritten>);
    const LoopStages coefficients = loop_stages(kCascade<Bands, true, Rewritten>, kSchedule<Bands, true, Rewritten>);
    return knobs == coefficients && knobs.cycles.size() == cycles;
}

static_assert(knobs_first<2, false>() && knobs_first<3, false>() && knobs_first<4, false>() && knobs_first<2, true>() &&
                  knobs_first<3, true>() && knobs_first<4, true>(),
              "every band's knob math a stage of its own with no loop, before the first stage with a loop");
static_assert(chained_alike<2, false>() && chained_alike<3, false>() && chained_alike<4, false>(),
              "as written, the stages with loops of the knob cascade are those of the cascade with its coefficients Params");
static_assert(chained_alike<2, true>() && chained_alike<3, true>(1) && chained_alike<4, true>(1),
              "as rewritten, the stages with loops of the knob cascade are those of the cascade with its coefficients Params; "
              "from three bands on, the sections one linked cycle");

// The stage of `s` that holds node `ni` (-1: none).
constexpr std::int32_t stage_of(const loops::Schedule& s, std::int32_t ni) {
    for (std::size_t k = 0; k < s.stages.size(); ++k) {
        for (const std::int32_t m : s.stages[k].nodes) {
            if (m == ni) {
                return static_cast<std::int32_t>(k);
            }
        }
    }
    return -1;
}
// The map over the declared tensor has a loop of its own and stands between the two one-poles' stages, as it did before
// the work ahead was gathered first.
constexpr bool tensor_stays_between() {
    const Graph g = analysed<tensor_between>(false);
    const loops::Schedule s = planned::schedule_of(g, zen2);
    const loops::graph::Facts facts = loops::graph::facts_of(g);
    const loops::graph::Feedback feedback = loops::graph::find_feedback(g, facts);
    const std::vector<char> timeless = loops::graph::timeless_of(g);
    std::vector<std::int32_t> one_poles{};
    std::int32_t map = -1;
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        const std::int32_t at = stage_of(s, static_cast<std::int32_t>(i));
        if (feedback.cycle[i] == static_cast<std::int32_t>(i)) {
            one_poles.push_back(at);
        }
        map = timeless[static_cast<std::size_t>(g.nodes[i].output)] != 0 ? at : map;
    }
    return one_poles.size() == 2 && map >= 0 && loops_in(s.stages[static_cast<std::size_t>(map)]) && one_poles[0] < map && map < one_poles[1];
}
static_assert(tensor_stays_between(), "timeless work with a loop keeps its stage between the one-poles");

int g_wrong = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        ++g_wrong;
        std::printf("  not as planned: %s\n", what);
    }
}

// ---- kept stages ----------------------------------------------------------------------------------------------

using Watched = loops::planner::Watched;
using PlannedOnHeap = std::unique_ptr<const std::expected<loops::Planned, loops::Refusal>>;

// `g` planned and lowered for zen2, on the heap, its program held to what the runner accepts (`well_formed`, whose rules
// for a kept stage hold the program's `watched` to what each stage reads); null where either fails.
PlannedOnHeap planned_for_zen2(const Graph& g) {
    PlannedOnHeap r(new auto(loops::plan_and_lower(g, zen2)));
    if (!r->has_value() || !loops::well_formed((*r)->program).has_value()) {
        return nullptr;
    }
    return r;
}

// The watch bits of `names` among the inputs of `g`.
Watched bits_of(const Graph& g, std::initializer_list<std::string_view> names) {
    Watched bits{};
    for (std::size_t i = 0; i < g.inputs.size(); ++i) {
        for (const std::string_view name : names) {
            if (g.inputs[i].name.view() == name) {
                bits[i / 64] |= std::uint64_t{1} << (i % 64);
            }
        }
    }
    return bits;
}

// The cascade of `Bands`: every stage with no loop kept, each watching one band's three knobs, every band's in one stage;
// no stage with a loop kept; the program's stages kept as the schedule's, watching the same.
template <std::size_t Bands>
bool kept_by_band(bool rewritten) {
    const Graph g = analysed<cascade<Bands, false>>(rewritten);
    const PlannedOnHeap r = planned_for_zen2(g);
    if (r == nullptr || (*r)->schedule.stages.size() != (*r)->program.stages.size()) {
        return false;
    }
    std::array<int, Bands> seen{};
    bool ok = true;
    for (std::size_t k = 0; k < (*r)->schedule.stages.size(); ++k) {
        const loops::Schedule::Stage& stage = (*r)->schedule.stages[k];
        const loops::Stage& lowered = (*r)->program.stages[k];
        std::size_t band = Bands;
        for (std::size_t b = 0; b < Bands; ++b) {
            band = stage.watched == bits_of(g, {kKnob[b][0], kKnob[b][1], kKnob[b][2]}) ? b : band;
        }
        const bool knobs = !loops_in(stage);
        ok = ok && stage.kept == knobs && (!knobs || (band < Bands && ++seen[band] == 1));
        ok = ok && lowered.kept == stage.kept && lowered.watched == stage.watched;
    }
    return ok && std::count(seen.begin(), seen.end(), 1) == static_cast<std::ptrdiff_t>(Bands);
}

// The long chain's two stages with no loop: kept, the first watching `p`, the second `p` and `q`; with the first's last
// value shown, neither (rule 5, then rule 4).
template <bool Shown>
bool chain_kept() {
    const Graph g = analysed<long_knob_chain<Shown>>(false);
    const PlannedOnHeap r = planned_for_zen2(g);
    if (r == nullptr) {
        return false;
    }
    std::vector<loops::Schedule::Stage> knobs{};
    for (const loops::Schedule::Stage& stage : (*r)->schedule.stages) {
        if (!loops_in(stage)) {
            knobs.push_back(stage);
        }
    }
    if (knobs.size() != 2) {
        return false;
    }
    if (Shown) {
        return !knobs[0].kept && !knobs[1].kept;
    }
    return knobs[0].kept && knobs[0].watched == bits_of(g, {"p"}) && knobs[1].kept && knobs[1].watched == bits_of(g, {"p", "q"});
}

// No stage of `Build`'s plan is kept, and one has no loop where `ahead` says so.
template <auto Build>
bool none_kept(bool ahead) {
    const Graph g = analysed<Build>(false);
    const PlannedOnHeap r = planned_for_zen2(g);
    if (r == nullptr) {
        return false;
    }
    bool none = true;
    bool loopless = false;
    for (const loops::Schedule::Stage& stage : (*r)->schedule.stages) {
        none = none && !stage.kept;
        loopless = loopless || !loops_in(stage);
    }
    return none && loopless == ahead;
}

void check_what_is_kept() {
    expect(kept_by_band<2>(false) && kept_by_band<3>(false) && kept_by_band<4>(false), "as written, each band's knob stage kept, watching its knobs");
    expect(kept_by_band<2>(true) && kept_by_band<3>(true) && kept_by_band<4>(true), "as rewritten, each band's knob stage kept, watching its knobs");
    expect(chain_kept<false>(), "a knob stage reading another's value watches both stages' Params");
    expect(chain_kept<true>(), "a knob stage that makes an output is not kept, nor one that reads its value");
    expect(none_kept<shown_gain>(true), "a knob stage that makes an output is not kept");
    expect(none_kept<tensor_between>(false), "a map over a declared tensor has a loop, and is not kept");
}

// Band k's knobs: 125 Hz times 4^k; its gain in dB; its Q.
constexpr std::array<double, 3> knobs_of(std::size_t k) {
    const double gains[] = {6.0, -3.0, 9.0, -12.0};
    const double qs[] = {1.0, 2.0, 0.7, 4.0};
    return {125.0 * static_cast<double>(1u << (2 * k)), gains[k], qs[k]};
}

constexpr std::size_t kWalked = 512;

std::vector<double> noise() {
    std::vector<double> x(kWalked);
    std::uint32_t state = 7;
    for (double& v : x) {
        state = state * 1664525u + 1013904223u;
        v = static_cast<double>(static_cast<float>(static_cast<std::int32_t>(state >> 8) % 2001 - 1000) * 0.001f);
    }
    return x;
}

// The number the walk feeds input `name`: the noise, or a knob's value at every sample.
std::vector<double> fed_to(std::string_view name, const std::vector<double>& x) {
    for (std::size_t p = 0; p < 3 * kMostBands; ++p) {
        if (name == kKnob[p / 3][p % 3]) {
            return std::vector<double>(kWalked, knobs_of(p / 3)[p % 3]);
        }
    }
    return x;
}

// The knob cascade of `Bands` at High on `Executor<…, zen2>`, run in blocks of `block`, against its walk, bit for bit.
template <std::size_t Bands>
void check_against_the_walk(std::int64_t block) {
    static constexpr auto build = cascade<Bands, false>;
    static constexpr testing::Store store = testing::store_of<build, kWalked>();
    const Graph g = testing::analysed<build>(kWalked);
    const std::vector<double> x = noise();
    std::vector<std::vector<double>> fed{};
    for (const NamedValue& in : g.inputs) {
        fed.push_back(fed_to(in.name.view(), x));
    }
    Inputs feeds{};
    for (const std::vector<double>& f : fed) {
        feeds.push_back(InputFeed{f.data(), 1});
    }
    const auto walk = std::make_unique<Signals<store.samples, store.planes>>();
    run(g, feeds, *walk);

    const auto ex = std::make_unique<Executor<testing::at_high<build>, zen2, false>>();
    ex->prepare(256);
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        ((ex->param(kKnob[I / 3][I % 3]) = knobs_of(I / 3)[I % 3]), ...);
    }(std::make_index_sequence<3 * Bands>{});
    const std::vector<float> in(x.begin(), x.end());
    std::vector<float> y(kWalked);
    for (std::size_t at = 0; at < kWalked; at += static_cast<std::size_t>(block)) {
        ex->input("x") = in.data() + at;
        ex->output("y") = y.data() + at;
        ex->run(block);
    }
    for (std::size_t t = 0; t < kWalked; ++t) {
        const double got = static_cast<double>(y[t]);
        const double want = sample_at(*walk, g.outputs[0].value, 0, t);
        if (std::bit_cast<std::uint64_t>(got) != std::bit_cast<std::uint64_t>(want) && ++g_wrong <= 10) {
            std::printf("  %zu bands in blocks of %lld [%zu]: got %a, walked %a\n", Bands, static_cast<long long>(block), t, got, want);
        }
    }
}

} // namespace

int main() {
    for (const std::int64_t block : {1, 32, 256}) {
        check_against_the_walk<2>(block);
        check_against_the_walk<3>(block);
        check_against_the_walk<4>(block);
    }
    check_what_is_kept();
    if (g_wrong != 0) {
        std::printf("knob_stages_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("knob_stages_test: knob stages first, the cascade chained as with its coefficients Params, bit for bit the walk; each "
                "knob stage kept, watching its Params\n");
    return 0;
}
