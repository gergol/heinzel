// A contraction operand read from a copy turned around (`Plan/20-score-product`): `Q . K^T`'s K strides along the
// output's lane axis `k`, so a stage that runs its lanes across the outputs reads K from a copy with `k` its contiguous
// axis, filled by a stage of its own one element at a time, and relaid in blocks after it where the schedule asks. The
// planner's schedule is edited here to that register tile, and each program runs against the reference walk, bit for bit
// at `High`: shapes whose blocks do not divide, a contracted axis below and
// above a lane, a batch axis, on zen2, tiny caches and the default machine; K a constant, turned (and relaid) when the
// program compiles, with no stage. A schedule that turns an operand carrying no lanes is refused by name. Then the
// planner's own programs, which turn K where each of its numbers is read 32 times or more, on `Executor` against the
// walk: attention whole; on tiny caches turned and relaid at `High`, and turned, relaid and tiled at `Normal`.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <vector>

#include "heinzel/compile.hpp"
#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/notation/einsum.hpp"
#include "heinzel/sema/analyze.hpp"

#include "canaried.hpp"
#include "edited_schedules.hpp"
#include "executed.hpp"
#include "planned.hpp"

using namespace heinzel;
using namespace heinzel::testing;

namespace {

namespace loops = heinzel::loops;

constexpr float kSmall[19 * 5] = {1, -2, 3, 0.5, -1.5, 2, 4, -3, 1, 0, -1, 2.5, 3, -4, 1, 0.25, 2, -2, 1, 3, -1, 1, 2, -0.5, 4,
                                   1, -2, 3, 0.5, -1.5, 2, 4, -3, 1, 0, -1, 2.5, 3, -4, 1, 0.25, 2, -2, 1, 3, -1, 1, 2, -0.5, 4,
                                   1, -2, 3, 0.5, -1.5, 2, 4, -3, 1, 0, -1, 2.5, 3, -4, 1, 0.25, 2, -2, 1, 3, -1, 1, 2, -0.5, 4,
                                   1, -2, 3, 0.5, -1.5, 2, 4, -3, 1, 0, -1, 2.5, 3, -4, 1, 0.25, 2, -2, 1, 3};

constexpr Graph scores_37() {
    Input q{"Q", {13, 5}};
    Input k{"K", {37, 5}};
    Output y{"y"};
    y = einsum("qd,kd->qk", q, k);
    return y;
}
constexpr Graph scores_wide() { // (a contracted axis of 64: eight lanes' worth)
    Input q{"Q", {7, 64}};
    Input k{"K", {19, 64}};
    Output y{"y"};
    y = einsum("qd,kd->qk", q, k);
    return y;
}
constexpr Graph scores_batched() {
    Input q{"Q", {3, 7, 5}};
    Input k{"K", {3, 19, 5}};
    Output y{"y"};
    y = einsum("bqd,bkd->bqk", q, k);
    return y;
}
constexpr Graph scores_constant() { // K a constant {19, 5}
    Input q{"Q", {13, 5}};
    Output y{"y"};
    y = einsum("qd,kd->qk", q, constant(kSmall, {19, 5}));
    return y;
}

// The planner's schedule of `G`, its one stage made the register tile with operand 1 turned (and relaid, `relay`):
// the result's axes in memory order, the rows unrolled, the columns in lanes of the machine's width, unrolled twice.
template <auto G>
constexpr loops::Schedule turned(const Machine& m, bool relay, std::int32_t which = 1) {
    const Graph g = analysed<G>(0);
    loops::Schedule d = planned::schedule_of(g, m);
    loops::Schedule::Stage& s = d.stages[0];
    const std::int32_t rank = g.values[static_cast<std::size_t>(g.outputs[0].value)].shape.rank;
    const std::int32_t lanes = m.vector_bytes / 4;
    s.loops = {};
    s.loops.push_back(loops::Schedule::Loop{.axis = rank - 1});
    for (std::int32_t a = 0; a + 3 < rank; ++a) {
        s.loops.push_back(loops::Schedule::Loop{.axis = a});
    }
    s.loops.push_back(loops::Schedule::Loop{.axis = rank - 3, .step = (m.registers - 3) / 2, .kind = loops::LoopKind::Unroll});
    s.loops.push_back(loops::Schedule::Loop{.axis = rank - 2, .step = lanes, .kind = loops::LoopKind::Vector, .unroll = 2});
    s.inner_from = static_cast<std::int32_t>(s.loops.size());
    s.loops.push_back(loops::Schedule::Loop{.axis = rank});
    s.turned = which;
    s.relaid = relay ? which : -1, s.relaid_block = relay ? lanes * 2 : 0;
    s.parallel = -1;
    return d;
}

template <auto G, const Machine& M, bool Relay>
struct Turned {
    static constexpr loops::Schedule kSchedule = turned<G>(M, Relay);
    static constexpr planned::Held kHeld{planned::program_of(analysed<G>(0), kSchedule)};
};

// Its program holds a turned copy; a stage fills it unless it is a constant; a relay copies it where asked.
constexpr bool holds_a_turned_copy(const loops::Program& p, std::size_t stages) {
    bool turned = false;
    for (const loops::Buffer& b : p.buffers) {
        turned = turned || b.turned >= 0;
    }
    return turned && p.stages.size() == stages;
}
static_assert(holds_a_turned_copy(Turned<scores_37, zen2, false>::kHeld.program, 2), "a turned copy, filled by a stage of its own");
static_assert(holds_a_turned_copy(Turned<scores_37, zen2, true>::kHeld.program, 3), "...then relaid by another");
static_assert(holds_a_turned_copy(Turned<scores_constant, zen2, false>::kHeld.program, 1) &&
                  holds_a_turned_copy(Turned<scores_constant, zen2, true>::kHeld.program, 1),
              "a constant is turned (and relaid) when the program compiles, by no stage");
static_assert(planned::refusal_of(analysed<scores_37>(0), turned<scores_37>(zen2, false, 0)) ==
                  std::string_view{"heinzel::compile: a turned operand carries its stage's lanes, off its contiguous axis"},
              "turning an operand that carries no lanes is refused by name");

// The planner turns K itself where the product reads each of its numbers `turn_pays_after` (32) times or more: Q of 40
// rows, not of 13.
constexpr Graph planned_40() {
    Input q{"Q", {40, 5}};
    Input k{"K", {37, 5}};
    Output y{"y"};
    y = einsum("qd,kd->qk", q, k);
    return y;
}
constexpr Graph planned_batched() {
    Input q{"Q", {3, 40, 5}};
    Input k{"K", {3, 19, 5}};
    Output y{"y"};
    y = einsum("bqd,bkd->bqk", q, k);
    return y;
}
constexpr Graph planned_constant() {
    Input q{"Q", {40, 5}};
    Output y{"y"};
    y = einsum("qd,kd->qk", q, constant(kSmall, {19, 5}));
    return y;
}
template <auto G, const Machine& M>
inline constexpr planned::Held kPlannedHeld{planned::program_of(analysed<G>(0), M)};
static_assert(holds_a_turned_copy(kPlannedHeld<planned_40, zen2>.program, 2) && holds_a_turned_copy(kPlannedHeld<planned_batched, zen2>.program, 2) &&
                  holds_a_turned_copy(kPlannedHeld<planned_constant, zen2>.program, 1),
              "the planner reads an operand striding along the outputs' lanes, read 40 times, from a copy turned around");
static_assert(!holds_a_turned_copy(kPlannedHeld<scores_37, zen2>.program, 2) && kPlannedHeld<scores_37, zen2>.program.stages.size() == 1,
              "...but not one read 13 times: the copy would cost more than it saves");

// Attention whole, its score product reading K 40 times: turned.
constexpr Graph attention_40() { // (`corpus::attention<40, 16>`, unanalysed: `analysed<G>` analyses it at `High`)
    Input q{"Q", {40, 16}};
    Input k{"K", {40, 16}};
    Input v{"V", {40, 16}};
    Expr scores = einsum("qd,kd->qk", q, k) * (1.0 / 4.0);
    Expr e = exp(scores - scores.max(1, keepdims));
    Output out{"out"};
    out = einsum("qk,kd->qd", e / e.sum(1, keepdims), v);
    return out;
}
static_assert(holds_a_turned_copy(kPlannedHeld<attention_40, default_machine>.program, kPlannedHeld<attention_40, default_machine>.program.stages.size()),
              "attention's score product reads K turned");

// On tiny caches the planner also relays the turned copy, and at `Normal` tiles the contracted axis too.
constexpr Graph tiled_64() {
    Input q{"Q", {40, 64}};
    Input k{"K", {37, 64}};
    Output y{"y"};
    y = einsum("qd,kd->qk", q, k);
    return y;
}
constexpr bool relays_the_turned_copy(const loops::Program& p) {
    bool both = false;
    for (const loops::Buffer& b : p.buffers) {
        both = both || (b.turned >= 0 && b.blocked_axis >= 0);
    }
    return both;
}
static_assert(relays_the_turned_copy(compile(at_high<tiled_64>(), edited_schedules::kTinyCaches).program) &&
                  relays_the_turned_copy(compile(tiled_64(), edited_schedules::kTinyCaches).program),
              "tiny caches relay the turned copy, at High and at Normal");

int g_wrong = 0;

std::vector<double> numbers(std::size_t count, unsigned seed) {
    std::vector<double> v(count);
    for (std::size_t i = 0; i < count; ++i) {
        v[i] = 0.125 * static_cast<double>(static_cast<int>((i * 37 + seed * 11) % 61) - 30);
    }
    return v;
}

// `G`'s program as the planner makes it on `M` at `Precision`, on `Executor`, against the walk: bit for bit, since the
// numbers are multiples of 1/8 whose sums are exact in float in any order (so `Normal`'s reordered, tiled and fused sums
// too).
template <auto G, const Machine& M>
void planned_on(const char* what) {
    const Graph g = analysed<G>(0);
    const std::vector<double> q = numbers(static_cast<std::size_t>(numbers_per_sample(g.values[static_cast<std::size_t>(g.inputs[0].value)].shape)), 1);
    const std::vector<double> k = numbers(static_cast<std::size_t>(numbers_per_sample(g.values[static_cast<std::size_t>(g.inputs[1].value)].shape)), 2);
    const std::vector<float> qf(q.begin(), q.end());
    const std::vector<float> kf(k.begin(), k.end());
    const Outputs walk = walked<G>({{"Q", q.data(), static_cast<std::int32_t>(q.size())}, {"K", k.data(), static_cast<std::int32_t>(k.size())}});
    std::vector<float> y(walk[0].size());
    Executor<G, M> ex;
    ex.input("Q") = qf.data(), ex.input("K") = kf.data();
    ex.output("y") = y.data();
    ex.run();
    if (std::vector<double>(y.begin(), y.end()) != walk[0]) {
        ++g_wrong;
        std::printf("  FAILED: %s\n", what);
    }
}

// The program of `G` on `M`, run under canaries, against the walk bit for bit.
template <auto G, const Machine& M, bool Relay>
void check(const char* what) {
    constexpr const loops::Program& p = Turned<G, M, Relay>::kHeld.program;
    const Graph g = analysed<G>(0);
    std::vector<std::vector<double>> in;
    std::vector<std::vector<float>> narrow;
    loops::Feeds feeds{};
    for (std::size_t i = 0; i < g.inputs.size(); ++i) {
        in.push_back(numbers(static_cast<std::size_t>(numbers_per_sample(g.values[static_cast<std::size_t>(g.inputs[i].value)].shape)), static_cast<unsigned>(i + 1)));
        narrow.emplace_back(in.back().begin(), in.back().end());
    }
    for (std::size_t i = 0; i < narrow.size(); ++i) {
        feeds[i] = narrow[i].data();
    }
    const Outputs walk = in.size() == 2 ? walked<G>({{"Q", in[0].data(), static_cast<std::int32_t>(in[0].size())}, {"K", in[1].data(), static_cast<std::int32_t>(in[1].size())}})
                                        : walked<G>({{"Q", in[0].data(), static_cast<std::int32_t>(in[0].size())}});
    canaried::Workspace<p> work(loops::Sizes{});
    canaried::run<p>(feeds, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(p.outputs[0]));
    for (std::size_t e = 0; e < walk[0].size(); ++e) {
        if (static_cast<double>(y[e]) != walk[0][e]) {
            ++g_wrong;
            std::printf("  FAILED: %s, number %zu: %.9g, the walk %.9g\n", what, e, static_cast<double>(y[e]), walk[0][e]);
            return;
        }
    }
}

} // namespace

int main() {
    check<scores_37, zen2, false>("13 x 37 over 5, turned, zen2");
    check<scores_37, zen2, true>("13 x 37 over 5, turned and relaid, zen2");
    check<scores_37, edited_schedules::kTinyCaches, true>("13 x 37 over 5, turned and relaid, tiny caches");
    check<scores_37, default_machine, true>("13 x 37 over 5, turned and relaid, the default machine");
    check<scores_wide, zen2, false>("7 x 19 over 64, turned, zen2");
    check<scores_wide, zen2, true>("7 x 19 over 64, turned and relaid, zen2");
    check<scores_batched, zen2, false>("3 x 7 x 19 over 5, batched, turned, zen2");
    check<scores_batched, zen2, true>("3 x 7 x 19 over 5, batched, turned and relaid, zen2");
    check<scores_constant, zen2, false>("K a constant, turned when compiled, zen2");
    check<scores_constant, zen2, true>("K a constant, turned and relaid when compiled, zen2");
    const std::vector<double> q = numbers(40 * 5, 1);
    const std::vector<double> k = numbers(37 * 5, 2);
    const std::vector<double> qb = numbers(3 * 40 * 5, 1);
    const std::vector<double> kb = numbers(3 * 19 * 5, 2);
    executed<planned_40>({{"Q", q.data(), 40 * 5}, {"K", k.data(), 37 * 5}}); // (the planner's programs on Executor, against the walk)
    executed<planned_batched>({{"Q", qb.data(), 3 * 40 * 5}, {"K", kb.data(), 3 * 19 * 5}});
    executed<planned_constant>({{"Q", q.data(), 40 * 5}});
    const std::vector<double> v = numbers(40 * 16, 3);
    const std::vector<double> qa = numbers(40 * 16, 4);
    const std::vector<double> ka = numbers(40 * 16, 5);
    executed<attention_40>({{"Q", qa.data(), 40 * 16}, {"K", ka.data(), 40 * 16}, {"V", v.data(), 40 * 16}});
    planned_on<at_high<tiled_64>, edited_schedules::kTinyCaches>("the planner's turned and relaid program, tiny caches, High");
    planned_on<tiled_64, edited_schedules::kTinyCaches>("the planner's turned, relaid and tiled program, tiny caches, Normal");
    if (g_wrong != 0) {
        std::printf("turned_operand_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("turned_operand_test: operands read from turned copies give the walk's numbers\n");
    return 0;
}
