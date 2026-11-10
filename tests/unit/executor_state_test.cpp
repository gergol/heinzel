// `Executor`'s state across runs (Plan/archive/14-dsl/04 §7, 05): runs continue where the one before stopped, from
// silence at a fresh start (`prepare`, `reset`); F64 over two voices; one buffer as the input and the output, run twice;
// the refusals of a reset too early or after a move. Then the object itself (Plan/15-zero-overhead/03): float and double
// Params and outputs side by side, moves. (`set_initial` is gone, Plan/23 3.4: the state for the
// next block is the Executor's.)
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string_view>
#include <utility>
#include <vector>

#include "heinzel/compile.hpp"
#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"

#include "well_formed.hpp"

using namespace heinzel;

namespace {

constexpr Index n{};
constexpr Length kFrames{.multiple_of = 4, .min = 4, .expect = 64};

int g_wrong = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        ++g_wrong;
        std::printf("  FAILED: %s\n", what);
    }
}

template <class Run>
bool refused(Run run, std::string_view text) {
    try {
        run();
    } catch (const heinzel::error& e) {
        return e.message().find(text) != std::string_view::npos;
    }
    return false;
}

std::vector<float> ramp(std::size_t count) {
    std::vector<float> xs(count);
    for (std::size_t i = 0; i < count; ++i) {
        xs[i] = 0.125f * static_cast<float>(i % 17) - 1.0f;
    }
    return xs;
}

// y[n] = x[n] + y[n - 3]: a coefficient of one, so only the sums round and every expected number is exact.
constexpr Graph echo3() {
    Input x{"x", Signal{8}};
    Output y{"y"};
    y[n] = x[n] + y[n - 3];
    return y;
}
constexpr Graph long_echo3() { // (an output of 8 KiB: its arena on the heap)
    Input x{"x", Signal{2048}};
    Output y{"y"};
    y[n] = x[n] + y[n - 3];
    return y;
}
constexpr Graph streamed_echo3() {
    Input x{"x", Signal{kFrames}};
    Output y{"y"};
    y[n] = x[n] + y[n - 3];
    return y;
}

// A run continues from the one before; `reset` starts over from silence.
bool runs_continue() {
    const std::vector<float> x{1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<float> y(8);
    Executor<echo3> ex;
    ex.input("x") = x.data();
    ex.output("y") = y.data();
    ex.run();
    const bool first = y == std::vector<float>{1, 2, 3, 5, 7, 9, 12, 15};
    ex.run(); // y[-1..-3] = 15, 12, 9
    const bool continued = y[0] == 1 + 9.0f && y[1] == 2 + 12.0f && y[2] == 3 + 15.0f;
    ex.reset();
    ex.run();
    return first && continued && y == std::vector<float>{1, 2, 3, 5, 7, 9, 12, 15};
}

// Blocks of a `Length` give the numbers of one run over both; `prepare` again, or `reset`, starts from silence.
bool blocks_on_a_length() {
    const std::vector<float> x{1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<float> y(8);
    Executor<streamed_echo3> ex;
    ex.input("x") = x.data();
    ex.output("y") = y.data();
    ex.prepare(8);
    ex.run(4);
    const bool first = y[0] == 1 && y[1] == 2 && y[2] == 3 && y[3] == 5;
    ex.input("x") = x.data() + 4;
    ex.run(4); // x[4..7], after y[1..3] = 2, 3, 5
    const bool second = y[0] == 5 + 2.0f && y[1] == 6 + 3.0f && y[2] == 7 + 5.0f && y[3] == 8 + 7.0f;
    ex.input("x") = x.data();
    ex.prepare(4);
    ex.run(4);
    const bool prepared = y[0] == 1 && y[1] == 2 && y[2] == 3 && y[3] == 5;
    ex.run(4);
    ex.reset();
    ex.run(4);
    return first && second && prepared && y[0] == 1 && y[1] == 2 && y[2] == 3 && y[3] == 5;
}

// F64 over two voices, sample-major, from silence at every fresh start: `prepare`, `prepare` again, `reset` of a moved
// Executor.
constexpr Graph echo2_f64() {
    Input x{"x", Signal{2, kFrames}, f64};
    Output y{"y"};
    y[n] = x[n] + y[n - 2];
    return y;
}
bool f64_fresh_starts() {
    const std::vector<double> x{1, 2, 3, 4, 5, 6, 7, 8};
    const std::vector<double> want{1, 2, 3, 4, 6, 8, 10, 12}; // 4 samples of 2 voices
    std::vector<double> y(8);
    Executor<echo2_f64> ex;
    ex.input("x") = x.data();
    ex.output("y") = y.data();
    ex.prepare(4);
    ex.run(4);
    const bool first = y == want;
    ex.prepare(4);
    ex.run(4);
    const bool again = y == want;
    Executor<echo2_f64> moved = std::move(ex);
    moved.run(4);
    const bool continued = y != want;
    moved.reset();
    moved.run(4);
    return first && again && continued && y == want;
}

// One buffer as the input and the output, run twice over consecutive blocks: the numbers of one run over both —
// the feed's tail is kept before the output is copied over it.
constexpr Graph fir_of(std::int32_t samples) {
    Input x{"x", Signal{samples}};
    Output y{"y"};
    y = 0.5 * x[n] + 0.25 * x[n - 1] + 0.25 * x[n - 2];
    return y;
}
bool two_runs_in_place() {
    const std::vector<float> x = ramp(128);
    std::vector<float> whole(128);
    std::vector<float> shared(x.begin(), x.begin() + 64);
    std::vector<float> got;
    Executor<[] { return fir_of(128); }> once;
    Executor<[] { return fir_of(64); }> twice;
    once.input("x") = x.data();
    once.output("y") = whole.data();
    once.run();
    twice.input("x") = shared.data();
    twice.output("y") = shared.data();
    twice.run();
    got.insert(got.end(), shared.begin(), shared.end());
    std::copy(x.begin() + 64, x.end(), shared.begin());
    twice.run();
    got.insert(got.end(), shared.begin(), shared.end());
    return got == whole;
}

// A reset before prepare and after a move: checked in debug builds only (OQ-1).
void state_refusals() {
#ifndef NDEBUG
    expect(refused([] { Executor<streamed_echo3> ex; ex.reset(); }, "heinzel::Executor: prepare first"), "reset before prepare");
    expect(refused(
               [] {
                   Executor<long_echo3> ex;
                   Executor<long_echo3> moved = std::move(ex);
                   ex.reset();
               },
               "heinzel::Executor: this one was moved from"),
           "a moved-from Executor reset");
#endif
}

// ---- the object itself: its Params and its moves (Plan/15-zero-overhead/03) ----------------------------------------

// A float Param before a double one, and a float output before a double one, each keeping a past: the class keeps the
// doubles first, so that each stands aligned. Every number is exact.
constexpr Graph mixed() {
    Input x{"x", Signal{4}};
    Input w{"w", Signal{4}, f64};
    Param g{"g"};
    Param h{"h", f64};
    Output y{"y"};
    Output z{"z"};
    y[n] = x[n] * g + 0.5 * y[n - 1];
    z[n] = w[n] * h + 0.25 * z[n - 2];
    return finish(y, z);
}
struct Mixed {
    std::vector<float> x{1, 2, 3, 4};
    std::vector<float> y = std::vector<float>(4);
    std::vector<double> w{1, 2, 3, 4};
    std::vector<double> z = std::vector<double>(4);
    void bind(Executor<mixed>& ex, double g) {
        ex.input("x") = x.data(), ex.input("w") = w.data();
        ex.param("g") = g, ex.param("h") = 3.0;
        ex.output("y") = y.data(), ex.output("z") = z.data();
    }
    bool same(const Mixed& o) const { return y == o.y && z == o.z; }
};
bool float_before_double() {
    Mixed m;
    Executor<mixed> ex;
    m.bind(ex, 2.0);
    ex.run();
    return m.y == std::vector<float>{2, 5, 8.5f, 12.25f} && m.z == std::vector<double>{3, 6, 9.75, 13.5};
}

// A move onto a live Executor takes the stream it was in; a self-move changes nothing; a Param set after a move is the
// moved-to one's alone; an output bound to null is not written, and the stream runs on. Each against one Executor
// given the same calls, unmoved.
bool moves() {
    Mixed got;
    Mixed want;
    Mixed other;
    Executor<mixed> a;
    Executor<mixed> b;
    Executor<mixed> ref;
    got.bind(a, 2.0), other.bind(b, 5.0), want.bind(ref, 2.0);
    a.run(), ref.run(), b.run();
    b = std::move(a); // (b's own stream is dropped)
    b.param("g") = 1.0, ref.param("g") = 1.0;
    a.param("g") = 7.0; // the moved-from one's own
    b.run(), ref.run();
    bool same = got.same(want);
    Executor<mixed>& itself = b;
    b = std::move(itself);
    b.output("y") = static_cast<float*>(nullptr), ref.output("y") = static_cast<float*>(nullptr);
    b.run(), ref.run();
    b.output("y") = got.y.data(), ref.output("y") = want.y.data();
    b.run(), ref.run();
    return same && got.same(want);
}

// A moved-from Executor whose arena lies inside it keeps a copy of it, so it runs on from where the move left both
// (OQ-1: a moved-from Executor that can run, runs).
bool moved_from_inline_runs() {
    const std::vector<float> x{1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<float> a(8);
    std::vector<float> b(8);
    Executor<echo3> ex;
    ex.input("x") = x.data();
    ex.run();
    Executor<echo3> moved = std::move(ex);
    ex.output("y") = a.data(), moved.output("y") = b.data();
    ex.run(), moved.run();
    return a == b && a[0] == 1 + 9; // x[0] and the first run's y[5]
}

// A move takes the stream; a moved-from Executor prepared again starts from silence.
constexpr Graph deep_echo() {
    Input x{"x", Signal{kFrames}};
    Output y{"y"};
    y[n] = x[n] + y[n - 20];
    return y;
}
bool heap_arena_moves() {
    const std::vector<float> x = ramp(32);
    std::vector<float> got(32);
    std::vector<float> want(32);
    std::vector<float> fresh(32);
    std::vector<float> silent(32);
    Executor<deep_echo> ex;
    Executor<deep_echo> ref;
    Executor<deep_echo> unrun;
    for (auto* e : {&ex, &ref, &unrun}) {
        e->input("x") = x.data();
    }
    ex.output("y") = got.data(), ref.output("y") = want.data(), unrun.output("y") = silent.data();
    ex.prepare(32), ref.prepare(32), unrun.prepare(32);
    ex.run(32), ref.run(32);
    Executor<deep_echo> moved = std::move(ex);
    moved.run(32), ref.run(32), unrun.run(32);
    ex.output("y") = fresh.data();
    ex.prepare(32);
    ex.run(32);
    return got == want && fresh == silent && got != silent;
}

// The programs these Executors run, held to what the runner accepts.
template <auto Build>
constexpr Compiled kCompiled = compile(Build());
template <auto... Build>
constexpr bool kAccepted = ((loops::accepted<kCompiled<Build>.program>() && loops::outputs_whole(kCompiled<Build>.program)) && ...);
static_assert(kAccepted<[] { return fir_of(64); }, [] { return fir_of(128); }, echo3, streamed_echo3, echo2_f64, mixed, deep_echo>);

} // namespace

int main() {
    expect(runs_continue(), "a run continues from the one before; reset starts from silence");
    expect(blocks_on_a_length(), "blocks of a Length as one run; prepare again and reset start from silence");
    expect(f64_fresh_starts(), "F64 over two voices from silence at each fresh start");
    expect(two_runs_in_place(), "two runs in place equal one over both blocks");
    state_refusals();
    expect(float_before_double(), "a float Param and output before double ones");
    expect(moves(), "moves onto a live Executor and onto itself, a Param after a move, an output bound to null");
    expect(heap_arena_moves(), "an arena on the heap moved; a moved-from Executor prepared again starts from silence");
    expect(moved_from_inline_runs(), "a moved-from Executor with its arena inside it runs on");
    if (g_wrong != 0) {
        std::printf("executor_state_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("executor_state_test: runs, resets and runs in place carry state as one run does\n");
    return 0;
}
