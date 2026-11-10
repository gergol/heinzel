// Index notation (Plan/archive/14-dsl/02 §2): `x[n]` is x, `x[n - k]` is x k samples ago, `x[n - d]` the same
// for a duration folded to whole samples, and a `Variable` is defined by `v[n] = …` from its own past.
// Each spelling builds exactly the graph the older one builds (`delayed`, `previous`, `is`, `value`),
// node for node — checked while compiling and again at run time, where the same functions take the
// other evaluation path. Also: a duration's refusals and a second definition refused. A present read of
// a `Variable` before its definition (backlog B-18) is put in reading order when the graph is finished
// (Plan/28-dsl-proposals/03 B4): it runs at `High` as the walk does and as the graph written in that order
// does, the benchmark's delay effect written in either order is one program, a division by such a read is
// proven as when it is written after the definition, and a graph that reads nothing ahead is left as built.
// A lag before its definition that no loop reads is moved after it, and a loop through an `Expr`'s past is
// refused by name and walked as the same loop through a `Variable` runs (the review of package B). A chain of such
// lags, one needed only through another, is not moved yet: its refusal is pinned (the second review).
// What must not compile is in `gates/refusals/index_notation.cpp`.
#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>

#include "heinzel/compile.hpp"
#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

constexpr Index n{48'000};
using V = detail::variable_access; // the old spelling of a Variable's steps, kept underneath

constexpr Expr signal_x() { return detail::input("x", Shape{64}); }

// ---- each spelling, and the one it replaces ------------------------------------------------------

constexpr Graph now_new() { Expr x = signal_x(); return detail::finish("y", x[n] * 2.0); }
constexpr Graph now_old() { Expr x = signal_x(); return detail::finish("y", x * 2.0); }

constexpr Graph lag_new() {
    Expr x = signal_x();
    Expr late = x[n - 3];
    return detail::finish("y", x[n] + 0.5 * late);
}
constexpr Graph lag_old() {
    Expr x = signal_x();
    Expr late = detail::delayed(x, 3);
    return detail::finish("y", x + 0.5 * late);
}

// 3 ms at 48 kHz is 144 samples.
constexpr Graph duration_new() { Expr x = signal_x(); return detail::finish("y", x[n - 3.0 * ms]); }
constexpr Graph duration_old() { Expr x = signal_x(); return detail::finish("y", detail::delayed(x, 144)); }

constexpr Graph recurrence_new() {
    Expr x = signal_x();
    Variable v;
    v[n] = x[n] + 0.5 * v[n - 1];
    return detail::finish("y", v);
}
constexpr Graph recurrence_old() {
    Expr x = signal_x();
    Variable v;
    V::is(v, x + 0.5 * V::previous(v, 1));
    return detail::finish("y", V::value(v));
}

// Two recurrences that read each other, one read in the present (`low[n]`) inside the other's
// definition: the state-variable filter of `examples/synth_voice.cpp`.
constexpr Graph coupled_new() {
    Expr x = signal_x();
    Expr f = constant(0.25);
    Variable low;
    Variable band;
    low[n] = low[n - 1] + f * band[n - 1];
    band[n] = band[n - 1] + f * (x[n] - low[n] - 0.3 * band[n - 1]);
    return detail::finish("y", low);
}
constexpr Graph coupled_old() {
    Expr x = signal_x();
    Expr f = constant(0.25);
    Variable low;
    Variable band;
    V::is(low, V::previous(low, 1) + f * V::previous(band, 1));
    V::is(band, V::previous(band, 1) + f * (x - V::value(low) - 0.3 * V::previous(band, 1)));
    return detail::finish("y", V::value(low));
}

// `main`'s comb: an `Output` defined by its own past, three milliseconds back.
constexpr Graph comb_new() {
    Input x{"x", Signal{64}};
    Output y{"y"};
    y[n] = x[n] + 0.5 * y[n - 3.0 * ms];
    return y;
}
constexpr Graph comb_old() {
    Expr x = signal_x();
    Variable y;
    V::is(y, x + 0.5 * V::previous(y, 144));
    return detail::finish("y", V::value(y));
}

// `v[n] = w[n]` defines v by w's value (its own assignment, not the proxy's copy).
constexpr Graph alias_new() {
    Expr x = signal_x();
    Variable w;
    Variable v;
    w[n] = x[n] + 0.5 * w[n - 1];
    v[n] = w[n];
    return detail::finish("y", v[n] * 2.0);
}
constexpr Graph alias_old() {
    Expr x = signal_x();
    Variable w;
    Variable v;
    V::is(w, x + 0.5 * V::previous(w, 1));
    V::is(v, V::value(w));
    return detail::finish("y", V::value(v) * 2.0);
}

static_assert(now_new() == now_old());
static_assert(lag_new() == lag_old());
static_assert(duration_new() == duration_old());
static_assert(recurrence_new() == recurrence_old());
static_assert(coupled_new() == coupled_old());
static_assert(comb_new() == comb_old());
static_assert(alias_new() == alias_old());

// `x[n]` creates nothing; a lag creates exactly one node.
static_assert(now_new().nodes.size() == 1 && lag_new().nodes.size() == 3);

// ---- durations -----------------------------------------------------------------------------------

static_assert(samples(n, 5.0 * ms) == 240);
static_assert(samples(Index{2'000}, 3.0 * ms) == 6);

template <class Build>
bool refused(Build build, std::string_view text) {
    try {
        (void)build();
    } catch (const heinzel::error& e) {
        return e.message().find(text) != std::string_view::npos;
    }
    return false;
}

// ---- a present read before the definition (B-18, B4) --------------------------------------------

// `b[n] = a[n] * 2` before `a[n] = …`: `a[n]` is still the variable itself, so `b`'s read stands
// before the recurrence that defines `a`. Finishing moves it after, reading the definition.
constexpr Graph read_before_definition() {
    Expr x = signal_x();
    Variable a;
    Variable b;
    b[n] = a[n] * 2.0 + 0.5 * b[n - 1];
    a[n] = x[n] + 0.5 * a[n - 1];
    return detail::finish("y", b);
}
constexpr Graph read_after_definition() {
    Expr x = signal_x();
    Variable a;
    Variable b;
    a[n] = x[n] + 0.5 * a[n - 1];
    b[n] = a[n] * 2.0 + 0.5 * b[n - 1];
    return detail::finish("y", b);
}

// Each node's value numbered after the one before it: the nodes stand as they were made.
constexpr bool as_built(const Graph& g) {
    for (std::size_t i = 1; i < g.nodes.size(); ++i) {
        if (g.nodes[i].output <= g.nodes[i - 1].output) {
            return false;
        }
    }
    return true;
}

// A lag of a variable before its definition, which does not read it, and nothing read ahead: finishing moves it after
// the definition, as written there (the review of package B). Where it stood, the loop tree refuses it as a lag of a
// later value (`loops_plan_test`); before, only an unrelated read ahead elsewhere in the graph moved it.
constexpr Graph lag_before_definition() {
    Expr x = signal_x();
    Variable v;
    Expr late = v[n - 1];
    v[n] = x[n] * 0.5;
    Expr both = v + late;
    return detail::finish("y", both);
}
// Its reader before the definition too, waiting for nothing else: the lag waits for the definition, and so does it.
constexpr Graph lag_read_before_definition() {
    Expr x = signal_x();
    Variable v;
    Expr late = v[n - 1];
    Expr twice = late * 2.0;
    v[n] = x[n] * 0.5;
    Expr both = v + twice;
    return detail::finish("y", both);
}
// Read by another loop only, `u`'s, which its own definition does not read: moved as well.
constexpr Graph lag_another_loop_reads() {
    Expr x = signal_x();
    Variable y;
    Variable u;
    Expr prev = y[n - 1];
    y[n] = x[n] * 2.0;
    u[n] = prev + 0.5 * u[n - 1];
    return detail::finish("y", u);
}

static_assert(!as_built(read_before_definition()) && as_built(read_after_definition()));
static_assert(!as_built(lag_before_definition()) && !as_built(lag_read_before_definition()) && !as_built(lag_another_loop_reads()));
static_assert(as_built(recurrence_new()) && as_built(coupled_new()) && as_built(comb_new()) && as_built(alias_new()));

// A lag and the present of a variable, both before its definition: the lag stands just before its reader, after the
// definition its reader waits for too, not before it, where the loop tree would take it for a lag of a later value.
constexpr Graph lag_and_present_before() {
    Expr x = signal_x();
    Variable hp;
    Expr late = hp[n - 1];
    Expr both = late + hp;
    Expr fed = x * 0.3;
    Expr kept = 0.5 * hp[n - 1];
    hp[n] = fed + kept;
    return detail::finish("y", both);
}

// A lag of a computed value made after the read ahead, inside the definition read ahead: it waits for that value,
// as the loop tree reads a computed value's past only after it is made (a variable's past alone is feedback).
constexpr Graph lag_of_a_later_value() {
    Expr x = signal_x();
    Variable fb;
    Expr scaled = 0.3 * fb;
    Expr wet = x + scaled;
    Expr dry = x * 0.5;
    Expr old = dry[n - 5];
    fb[n] = old * 0.7;
    return detail::finish("y", wet);
}

// A loop through a computed value's past: `r` waits for `v`, read ahead, whose definition reads `r`'s past. Finishing
// takes that past for feedback, as a variable's, so the lag stands before `r` is made: the walk computes it, and the
// loop tree, which reads a computed value's past only after it is made, refuses it in words that say what to write.
constexpr Graph loop_through_an_expr() {
    Expr x = signal_x();
    Variable v;
    Expr fed = 0.5 * v;
    Expr r = x + fed;
    Expr old = r[n - 1];
    v[n] = 0.3 * old;
    return detail::finish("y", r);
}
// What it says to write: `r` a Variable.
constexpr Graph loop_through_a_variable() {
    Expr x = signal_x();
    Variable v;
    Variable r;
    Expr fed = 0.5 * v;
    r[n] = x + fed;
    Expr old = r[n - 1];
    v[n] = 0.3 * old;
    return detail::finish("y", r);
}
// Through a computed value's present it is an algebraic loop, as before.
constexpr Graph loop_through_the_present() {
    Expr x = signal_x();
    Variable v;
    Expr fed = 0.5 * v;
    Expr r = x + fed;
    v[n] = 0.3 * r;
    return detail::finish("y", r);
}
static_assert(try_compile(loop_through_an_expr()).refusal.view() == "heinzel::compile: feedback reads the past of an Expr; make it a Variable");
static_assert(try_compile(loop_through_the_present()).refusal.view() == "heinzel::compile: a value defined in terms of its own PRESENT is an algebraic loop");

// A chain of lags before their definitions, not lowered yet (`docs/limitations.md`, the second review of package B):
// `prev` is needed there only by `v`'s definition, which only the lag `late` needs there. Finishing moves one lag a
// pass, `late`, and the loop tree refuses `prev` where it was written; the day finishing moves the chain, this fails.
// Written after `v`'s definition, `late` compiles, and runs as the walk runs it.
constexpr Graph chain_of_late_lags() {
    Expr x = signal_x();
    Variable v;
    Variable w;
    Expr late = v[n - 1];
    Expr prev = w[n - 1];
    Expr halved = x[n] * 0.5;
    v[n] = halved + prev;
    w[n] = x[n] * 2.0;
    Expr sum = v + late;
    Expr y = sum + w;
    return detail::finish("y", y);
}
constexpr Graph chain_with_the_lag_after() {
    Expr x = signal_x();
    Variable v;
    Variable w;
    Expr prev = w[n - 1];
    Expr halved = x[n] * 0.5;
    v[n] = halved + prev;
    Expr late = v[n - 1];
    w[n] = x[n] * 2.0;
    Expr sum = v + late;
    Expr y = sum + w;
    return detail::finish("y", y);
}
static_assert(try_compile(chain_of_late_lags()).refusal.view() ==
              "heinzel::compile: a lag of a later value its definition does not read is not lowered yet");
static_assert(try_compile(chain_with_the_lag_after()).refusal.view().empty());

bool same_bits(const std::vector<double>& a, const std::vector<double>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(double)) == 0;
}

// The same run at High, bit for bit: the walk's, `Executor`'s, and the graph's written in reading order.
bool runs_as_written_after() {
    std::vector<float> x(64);
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] = static_cast<float>(i % 7) - 3.0f;
    }
    const testing::Outputs before = testing::executed<read_before_definition>({{"x", x.data()}});
    const testing::Outputs after = testing::executed<read_after_definition>({{"x", x.data()}});
    testing::executed<lag_and_present_before>({{"x", x.data()}});
    testing::executed<lag_of_a_later_value>({{"x", x.data()}});
    testing::executed<lag_before_definition>({{"x", x.data()}});
    testing::executed<lag_read_before_definition>({{"x", x.data()}});
    testing::executed<lag_another_loop_reads>({{"x", x.data()}});
    testing::executed<chain_with_the_lag_after>({{"x", x.data()}});
    const testing::Outputs through_an_expr = testing::walked<loop_through_an_expr>({{"x", x.data()}});
    const testing::Outputs through_a_variable = testing::executed<loop_through_a_variable>({{"x", x.data()}});
    return same_bits(before[0], after[0]) && same_bits(through_an_expr[0], through_a_variable[0]);
}

// The proof of ranges reads the definition too: `3 + g` is at least 3.25 whichever statement comes first.
template <bool Before>
constexpr Graph divided() {
    Input x{"x", Signal{64}};
    Variable g;
    Output y{"y"};
    if constexpr (Before) {
        Expr shifted = 3.0 + g;
        y = x / shifted;
    }
    Expr half = 0.5 * g[n - 1];
    Expr next = half + 0.25;
    g[n] = next.promise(ge(0), le(1));
    if constexpr (!Before) {
        Expr shifted = 3.0 + g;
        y = x / shifted;
    }
    return y;
}

// ---- the delay effect of bench_compare_juce_heinzel.cpp: its high-pass first, as it was, or its line first ------

constexpr std::int32_t kBlock = 1 << 20;
constexpr std::int32_t kDelay = 24000;
constexpr double kB0 = static_cast<double>(0x1.e0818ap-1f); // JUCE's first-order high-pass at 1 kHz
constexpr double kB1 = static_cast<double>(-0x1.e0818ap-1f);
constexpr double kA1 = static_cast<double>(-0x1.c10312p-1f);
constexpr double kFeedback = static_cast<double>(0.5f);
constexpr double kWet = static_cast<double>(0.8f);

constexpr Graph delay_filter_first() {
    Input x{"x", Signal{kBlock}};
    Variable hp;
    Variable line;
    hp[n] = kB0 * line[n - kDelay] + kB1 * line[n - (kDelay + 1)] - kA1 * hp[n - 1];
    line[n] = x + kFeedback * hp;
    return finish(Output{"y"} = x + kWet * hp);
}
constexpr Graph delay_line_first() {
    Input x{"x", Signal{kBlock}};
    Variable hp;
    Variable line;
    line[n] = x + kFeedback * hp;
    hp[n] = kB0 * line[n - kDelay] + kB1 * line[n - (kDelay + 1)] - kA1 * hp[n - 1];
    return finish(Output{"y"} = x + kWet * hp);
}

template <auto Build>
inline constexpr Compiled kDelayCompiled = compile(Build(), zen2);

// One program but for the graph value each buffer holds: the two orders number their values differently, so
// `Program ==` cannot hold; the stages, statements, loops and buffers are the same.
constexpr bool one_program_but_values(const loops::Program& a, const loops::Program& b) {
    loops::Program renamed = b;
    for (std::size_t i = 0; i < renamed.buffers.size() && i < a.buffers.size(); ++i) {
        renamed.buffers[i].value = a.buffers[i].value;
    }
    return renamed == a;
}
static_assert(one_program_but_values(kDelayCompiled<delay_filter_first>.program, kDelayCompiled<delay_line_first>.program));
static_assert(!as_built(delay_line_first()) && as_built(delay_filter_first()));

// The benchmark's machine and level, over its block: the same numbers, bit for bit.
template <auto Build>
std::vector<float> delayed_by(const std::vector<float>& x) {
    std::vector<float> y(x.size());
    Executor<Build, zen2> ex;
    ex.input("x") = x.data();
    ex.output("y") = y.data();
    ex.run();
    return y;
}
bool delays_in_either_order() {
    std::vector<float> x(static_cast<std::size_t>(kBlock));
    std::uint32_t state = 1;
    for (float& sample : x) {
        state = state * 1664525u + 1013904223u;
        sample = static_cast<float>(state >> 8) / 16777216.0f - 0.5f;
    }
    const std::vector<float> filter_first = delayed_by<delay_filter_first>(x);
    const std::vector<float> line_first = delayed_by<delay_line_first>(x);
    return std::memcmp(filter_first.data(), line_first.data(), x.size() * sizeof(float)) == 0;
}

int g_wrong = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        ++g_wrong;
        std::printf("  FAILED: %s\n", what);
    }
}

} // namespace

int main() {
    // The same functions at run time: the other evaluation path builds the same graphs.
    expect(now_new() == now_old() && lag_new() == lag_old() && duration_new() == duration_old(), "x[n], x[n - k], x[n - d]");
    expect(recurrence_new() == recurrence_old() && coupled_new() == coupled_old() && alias_new() == alias_old(),
           "v[n] = … over its own past, two coupled, v[n] = w[n]");
    expect(comb_new() == comb_old(), "an Output's recurrence, a duration back");
    expect(refused([] { return samples(Index{}, 1.0 * ms); }, "sample rate is unset"), "a duration with no rate");
    expect(refused([] { return samples(n, 0.01 * ms); }, "not an integer number of samples"), "a fraction of a sample");
    expect(refused([] { return n - (-1); }, "heinzel: n - k reaches back, so k is at least 0"), "a lag into the future");
    expect(refused(
               [] {
                   Expr x = signal_x();
                   Variable v;
                   v[n] = x[n];
                   v[n] = x[n] * 2.0;
                   return 0;
               },
               "heinzel::Variable: defined twice"),
           "a variable is defined once");
    expect(runs_as_written_after(), "a present read or a lag before the definition runs as written after it (B-18), and a loop "
                                    "through an Expr's past as through a Variable's");
    expect(try_compile(divided<true>()).refusal.view().empty() && try_compile(divided<false>()).refusal.view().empty(),
           "a division by a variable read before its definition is proven as when it is read after");
    expect(delays_in_either_order(), "the benchmark's delay gives one output in either order");
    if (g_wrong != 0) {
        std::printf("index_notation_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("index_notation_test: each spelling builds the graph of the one it replaces\n");
    return 0;
}
