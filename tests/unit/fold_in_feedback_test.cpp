// A fold inside a feedback stage's sample (Plan/23 10): a bank of voices in lanes inside a loop that folds them every
// sample and feeds the result back.
//   the walk    the reference walk takes a fold in a cycle: eight voices in double summed inside a loop, against the
//               loop by hand, bit for bit
//   the trees   the shape built by hand (`fold_in_feedback.hpp`): the plain bank over 8 doubles (two lane bodies), 3 (a
//               masked tail), 16 (four bodies) and 5 floats (one body of eight), the loop read in the lanes directly, a
//               lag of the fold's own result, TDF2's lanes after the fold — each against the walk of its graph at High,
//               bit for bit: in one run, in blocks of 1, 7, 8 and 256 and of uneven lengths one after another, and run
//               again after `reset`, every run canaried; and three at Normal (the lanes added pairwise, a masked
//               tail's missing lanes left out) within rounding of the walk
//   lowered     the same graphs at High, lowered from a schedule written as the planner lays such a stage out
//               (`loops_schedules::in_one_sample`), run as the trees are: the lowering builds the shape; and what it
//               refuses by name
//   planned     in `fold_in_feedback_planned_test.cpp`

#include "fold_in_feedback_graphs.hpp"

#include "canaried.hpp"
#include "executed.hpp"
#include "fold_in_feedback.hpp"
#include "interpret.hpp"
#include "loops_schedules.hpp"
#include "planned.hpp"

namespace {

// ---- the walk takes a fold in a cycle --------------------------------------------------------------------------

// The plain bank of eight voices in double, by hand in the walk's order: every operation in double, the sum from its
// first voice.
std::vector<double> plain_by_hand(const std::vector<double>& x) {
    std::array<double, 8> v{};
    double y = 0;
    double u1 = 0;
    std::vector<double> out;
    for (const double in : x) {
        const double u = in - ff::kGain * y;
        double sum = 0;
        for (std::int32_t k = 0; k < 8; ++k) {
            const std::size_t at = static_cast<std::size_t>(k);
            v[at] = ff::c0(k) * u + ff::c1(k) * u1 + ff::a1(k) * v[at];
            sum = k == 0 ? v[at] : sum + v[at];
        }
        y = sum + ff::kDirect * u, u1 = u;
        out.push_back(y);
    }
    return out;
}

void check_the_walk() {
    const std::vector<double> x = noise();
    const std::vector<double> want = plain_by_hand(x);
    const std::vector<double> got = walked(plain<double, 8>(), x);
    check(std::equal(want.begin(), want.end(), got.begin(), [](double a, double b) { return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b); }),
          "the walk of eight voices summed in a loop is the loop by hand, bit for bit");
}

// ---- the trees against the walk ----------------------------------------------------------------------------------

// `P` run over `x` in blocks of `blocks` (cycled) one after another, reset before the run once `reset_at` samples are
// done (a run of the whole again): its output, every run canaried.
template <const loops::Program& P, class T>
std::vector<double> ran(const std::vector<T>& x, const std::vector<std::int64_t>& blocks, std::size_t reset_at = kSamples) {
    const std::int64_t most = *std::max_element(blocks.begin(), blocks.end());
    canaried::Workspace<P> work(loops::sizes_of_length<P>(most));
    std::vector<double> y;
    for (std::size_t at = 0, b = 0; at < x.size(); ++b) {
        if (at == reset_at) {
            work.reset();
        }
        const auto m = static_cast<std::size_t>(std::min<std::int64_t>(blocks[b % blocks.size()], static_cast<std::int64_t>(x.size() - at)));
        work.set_length(static_cast<std::int64_t>(m));
        loops::Feeds feeds{};
        feeds[0] = x.data() + at;
        canaried::run<P>(feeds, work);
        const T* out = work.template data<T>(static_cast<std::size_t>(P.outputs[0]));
        y.insert(y.end(), out, out + m);
        at += m;
    }
    return y;
}

template <Bank B, class T, std::size_t V, std::int32_t Lanes, bool High = true>
constexpr ff::Voices kVoices{.bank = B, .voices = static_cast<std::int32_t>(V), .lanes = Lanes,
                             .bodies = (static_cast<std::int32_t>(V) + Lanes - 1) / Lanes, .type = kType<T>, .high = High};
template <Bank B, class T, std::size_t V, std::int32_t Lanes, bool High = true>
constexpr loops::Program kTree = ff::bank(kVoices<B, T, V, Lanes, High>);

// `P`, the program of graph B over V voices of T, against the walk of that graph at High, bit for bit: in one run, in
// blocks of 1, 7, 8 and 256 and of uneven lengths one after another, and in a run after `reset`.
template <const loops::Program& P, Bank B, class T, std::size_t V>
void check_runs(const char* what) {
    static_assert(loops::accepted<P>());
    const std::vector<double> x = noise();
    const std::vector<double> want = walked(graph_of<B, T, V>(), x);
    const std::vector<T> fed(x.begin(), x.end());
    double largest = 0;
    for (const double y : want) {
        largest = std::max(largest, std::abs(y));
    }
    std::printf("%s (the walk's largest |y| %.3g)\n", what, largest);
    check(largest > 0.1 && largest < 16, "the loop stays bounded and moves");
    check(same_bits(ran<P>(fed, {static_cast<std::int64_t>(kSamples)}), want), "one run");
    for (const std::int64_t block : {1, 7, 8, 256}) {
        check(same_bits(ran<P>(fed, {block}), want), "in blocks");
    }
    check(same_bits(ran<P>(fed, {5, 1, 13, 2, 64, 3, 30, 1, 9}), want), "in blocks of uneven lengths");
    std::vector<T> twice = fed;
    twice.insert(twice.end(), fed.begin(), fed.end());
    check(same_bits(ran<P>(twice, {static_cast<std::int64_t>(kSamples)}, kSamples), want, kSamples), "after reset");
}

template <Bank B, class T, std::size_t V, std::int32_t Lanes>
void check_tree(const char* what) {
    static constexpr const loops::Program& p = kTree<B, T, V, Lanes>;
    check_runs<p, B, T, V>(what);
}

template <Bank B, class T, std::size_t V, std::int32_t Lanes>
constexpr planned::Held kLowered{planned::program_of(kAtHigh<B, T, V>, loops_schedules::in_one_sample(kAtHigh<B, T, V>, Lanes))};

// ---- refused by name -------------------------------------------------------------------------------------------

// Schedule `d` with node `node` of its stage moved to stand right after node `after`.
constexpr loops::Schedule moved_after(loops::Schedule d, std::int32_t node, std::int32_t after) {
    decltype(loops::Schedule::Stage::nodes) order{};
    for (const std::int32_t m : d.stages[0].nodes) {
        if (m != node) {
            order.push_back(m);
        }
        if (m == after) {
            order.push_back(node);
        }
    }
    d.stages[0].nodes = order;
    return d;
}

constexpr Graph lanes_reading_their_sum(bool the_fold = false) { // (`the_fold`: the sum itself, not y, its hand-on)
    Input x{"x", Signal{Length{}}, f64};
    Variable y;
    Variable w;
    const Expr u = x - ff::kGain * y[n - 1];
    const Expr o = constant(each_voice<double, 8>(ff::c0)) * u + w[n - 1];
    const Expr sum = o.sum(0);
    y[n] = sum;
    w[n] = constant(each_voice<double, 8>(ff::c1)) * (the_fold ? sum : Expr(y)) - constant(each_voice<double, 8>(ff::a1)) * o;
    return at_high(finish(Output{"y"} = y));
}

// The lowering refuses what the shape has no place for (`loops/validate.hpp` `check_sample`): voices in two chunks of
// their strip; a second fold; a node over the fold's result among the lanes (u[n−1], n5, after c0·u, n4); a lane reading
// the part after the fold (planned in order it is unmade, `y` moved right after the sum its lanes read), or the fold
// itself (`loops/lower_stmts.hpp`: inside its loops it is no value yet; the review of P10); time not innermost, or cut
// into chunks outside the voices (as a plain voice bank's is: the inner loop walks no axis of the fold).
using loops_schedules::in_one_sample, loops_schedules::lanes, loops_schedules::planned, loops_schedules::refusal, loops_schedules::schedule,
    loops_schedules::serial;
constexpr const Graph& kPlain = kAtHigh<Bank::Plain, double, 8>;
constexpr Graph kTwoFolds = two_folds();
constexpr Graph kReadingTheSum = lanes_reading_their_sum();
constexpr Graph kReadingTheFold = lanes_reading_their_sum(true);
static_assert(refusal(kPlain, in_one_sample(kPlain, 4)) == "lowered");
static_assert(refusal(kPlain, schedule({planned(in_one_sample(kPlain, 4).stages[0], {lanes(0, 4), serial(1)}, 1)})) ==
              "heinzel::compile: a fold in a feedback stage's sample takes its voices in lanes, all in one chunk of their strip");
static_assert(refusal(kTwoFolds, in_one_sample(kTwoFolds, 4)) == "heinzel::compile: a feedback stage's sample holds one fold, and fetches nothing ahead");
static_assert(refusal(kPlain, moved_after(in_one_sample(kPlain, 4), 5, 4)) == "heinzel::compile: a node over a fold's result stands among the lanes of its sample");
static_assert(refusal(kReadingTheSum, in_one_sample(kReadingTheSum, 4)) ==
              "heinzel::compile: a node reads a value its group has not made yet — the schedule's order is not the graph's");
static_assert(refusal(kReadingTheSum, moved_after(in_one_sample(kReadingTheSum, 4), 8, 7)) ==
              "heinzel::compile: a lane of a feedback stage's sample reads what its part after the fold makes");
static_assert(refusal(kReadingTheFold, in_one_sample(kReadingTheFold, 4)) ==
              "heinzel::compile: a node inside a fold's loops reads the fold, which is made after them");
static_assert(refusal(kPlain, schedule({planned(in_one_sample(kPlain, 4).stages[0], {serial(1), lanes(0, 4, 2)}, 2)})) ==
              "heinzel::compile: a fold's group has inner loops: a feedback stage's sample walks time innermost");
static_assert(refusal(kPlain, schedule({planned(in_one_sample(kPlain, 4).stages[0], {loops_schedules::tile(1, 16), lanes(0, 4, 2), serial(1)}, 2)})) ==
              "heinzel::compile: a loop outside the inner loops over an axis they walk chunks a fold, by more than one");

template <Bank B, class T, std::size_t V, std::int32_t Lanes>
void check_lowered(const char* what) {
    static constexpr const loops::Program& p = kLowered<B, T, V, Lanes>.program;
    check_runs<p, B, T, V>(what);
}

// At Normal the fold adds its lanes pairwise, which rounds otherwise than the walk's order: within `bound` of it, in
// one run and in blocks of 7.
template <Bank B, class T, std::size_t V, std::int32_t Lanes>
void check_normal(const char* what, double bound) {
    static constexpr const loops::Program& p = kTree<B, T, V, Lanes, false>;
    static_assert(loops::accepted<p>());
    const std::vector<double> x = noise();
    const std::vector<double> want = walked(graph_of<B, T, V>(), x, false);
    const std::vector<T> fed(x.begin(), x.end());
    for (const std::int64_t block : {static_cast<std::int64_t>(kSamples), std::int64_t{7}}) {
        const std::vector<double> got = ran<p>(fed, {block});
        double worst = 0;
        for (std::size_t i = 0; i < kSamples; ++i) {
            worst = std::max(worst, std::abs(got[i] - want[i]));
        }
        std::printf("%s at Normal, blocks of %lld: %.3g from the walk\n", what, static_cast<long long>(block), worst);
        check(worst <= bound, "within rounding of the walk at Normal");
    }
}

} // namespace

int main() {
    check_the_walk();
    check_tree<Bank::Plain, double, 8, 4>("eight double voices, two lane bodies");
    check_tree<Bank::Plain, double, 3, 4>("three double voices, a masked tail");
    check_tree<Bank::Plain, double, 16, 4>("sixteen double voices, four lane bodies");
    check_tree<Bank::Plain, float, 5, 8>("five float voices, one body of eight, a masked tail");
    check_tree<Bank::Direct, double, 8, 4>("the loop read in the lanes directly");
    check_tree<Bank::OwnLag, double, 3, 4>("a lag of the fold's own result, a masked tail");
    check_tree<Bank::Tdf2, double, 8, 4>("TDF2: lanes after the fold");
    check_lowered<Bank::Plain, double, 8, 4>("lowered: eight double voices, two lane bodies");
    check_lowered<Bank::Plain, double, 3, 4>("lowered: three double voices, a masked tail");
    check_lowered<Bank::Plain, double, 16, 4>("lowered: sixteen double voices, four lane bodies");
    check_lowered<Bank::Plain, float, 5, 8>("lowered: five float voices, a masked tail");
    check_lowered<Bank::Direct, double, 8, 4>("lowered: the loop read in the lanes directly");
    check_lowered<Bank::OwnLag, double, 3, 4>("lowered: a lag of the fold's own result, a masked tail");
    check_lowered<Bank::Tdf2, double, 8, 4>("lowered: TDF2, lanes after the fold");
    check_normal<Bank::OwnLag, double, 3, 4>("a lag of the fold's own result, a masked tail", 1e-12);
    check_normal<Bank::Plain, double, 16, 4>("sixteen double voices", 1e-12);
    check_normal<Bank::Plain, float, 5, 8>("five float voices, a masked tail", 1e-5);
    if (g_wrong != 0) {
        std::printf("%d checks failed\n", g_wrong);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
