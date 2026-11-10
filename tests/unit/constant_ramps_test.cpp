// Ramps whose past nothing reads are constants (Plan/15-zero-overhead/03, step 5): one sample in read-only data, read
// where it lies, every read broadcast along time; no workspace bytes and no fill. Each case is a program that holds
// its ramp as a constant, run on `Executor` against the reference walk bit for bit (`executed`): a ramp as a
// contraction's operand; as one the planner relays (on tiny caches: its relaid copy is a constant too, laid out when the
// program compiles); as a
// table; and as an output over a `Length`, prepared at two lengths and run at several.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "heinzel/compile.hpp"
#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/notation/einsum.hpp"

#include "edited_schedules.hpp"
#include "executed.hpp"
#include "well_formed.hpp"

using namespace heinzel;
using namespace heinzel::testing;

namespace {

constexpr std::int32_t kCells = 16;

constexpr Graph contracted() { // (`nest_contract_test`'s tall shape: tiny caches relay its right operand)
    Input a{"a", {50, 21}};
    Output y{"y"};
    y = einsum("ik,kj->ij", a, iota({21, 19}));
    return y;
}

// `y = 2 · table[frac(p) · N]`, the table a ramp of N cells.
constexpr Graph table_of_a_ramp() {
    Input phase{"p", Signal{64}};
    Expr table = iota({kCells});
    return finish(Output{"y"} = wavetable(table, phase) * 2.0);
}

// The output is the ramp itself, over a `Length`, beside a signal that carries the input.
constexpr Graph ramp_output() {
    Input x{"x", Signal{4, Length{}}};
    Output y{"y"};
    Output r{"r"};
    y = x * 2.0;
    r = iota(Signal{4, Length{}});
    return finish(y, r);
}

// ...alone: a program that keeps nothing, so a prepared `Executor` has no arena (the review of 03).
constexpr Graph only_a_ramp() {
    Output r{"r"};
    r = iota(Signal{3, Length{}});
    return r;
}

// Ramps of no element (a tensor axis may be 0): a constant is one sample, so these stay workspace buffers of no bytes
// (the second review of 03: as constants, their null numbers crashed a run).
constexpr Graph no_ramp() {
    Output r{"r"};
    r = iota({0});
    return r;
}
constexpr Graph contracted_over_nothing() {
    Input x{"x", {3, 0}};
    Output y{"y"};
    y = einsum("ik,kj->ij", x, iota({0, 4}));
    return y;
}

// A ramp is a constant in `p` (and, with `relaid`, read through a relaid copy of itself).
constexpr bool holds_a_constant_ramp(const loops::Program& p, bool relaid) {
    std::int32_t ramp = kNoValue;
    for (const loops::Buffer& b : p.buffers) {
        ramp = b.ramp && b.home == loops::Home::Constant ? b.value : ramp;
    }
    bool copied = false;
    for (const loops::Buffer& b : p.buffers) {
        copied = copied || (b.value == ramp && b.blocked_axis >= 0);
    }
    return ramp != kNoValue && (!relaid || copied) && loops::well_formed(p).has_value() && loops::outputs_whole(p);
}
template <auto Build, const Machine& M = default_machine>
constexpr Compiled kCompiled = compile(at_high<Build>(), M);
static_assert(holds_a_constant_ramp(kCompiled<contracted>.program, false), "a contraction reads its ramp where it lies");
static_assert(holds_a_constant_ramp(kCompiled<contracted, edited_schedules::kTinyCaches>.program, true) &&
                  kCompiled<contracted, edited_schedules::kTinyCaches>.program.stages.size() == 1,
              "tiny caches relay the ramp, into a constant: no stage copies it");
static_assert(holds_a_constant_ramp(kCompiled<table_of_a_ramp>.program, false), "a table read reads its ramp where it lies");
static_assert(holds_a_constant_ramp(kCompiled<ramp_output>.program, false), "an output that is a ramp is a constant");
static_assert(holds_a_constant_ramp(kCompiled<only_a_ramp>.program, false) && loops::kPiecesOf<kCompiled<only_a_ramp>.program>.empty(),
              "a program whose one output is a ramp keeps nothing");
constexpr bool no_constant(const loops::Program& p) {
    for (const loops::Buffer& b : p.buffers) {
        if (b.home == loops::Home::Constant) {
            return false;
        }
    }
    return loops::well_formed(p).has_value();
}
static_assert(no_constant(kCompiled<no_ramp>.program) && no_constant(kCompiled<contracted_over_nothing>.program), "a ramp of no element is no constant");

int g_wrong = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        ++g_wrong;
        std::printf("  FAILED: %s\n", what);
    }
}

std::vector<double> numbers(std::size_t count, double scale) {
    std::vector<double> v(count);
    for (std::size_t i = 0; i < count; ++i) {
        v[i] = scale * static_cast<double>(static_cast<std::int32_t>((i * 37 + 11) % 61) - 30);
    }
    return v;
}

// The relaid copy gives the walk's numbers too: the same graph at `High`, its operand relaid, against `executed`.
bool relaid_agrees(const Outputs& walked) {
    const std::vector<double> a = numbers(50 * 21, 0.125);
    const std::vector<float> narrow(a.begin(), a.end());
    std::vector<float> y(50 * 19);
    Executor<at_high<contracted>, edited_schedules::kTinyCaches> ex;
    ex.input("a") = narrow.data();
    ex.output("y") = y.data();
    ex.run();
    ex.run(); // (twice: the relaid constant is read again, not copied again)
    return std::vector<double>(y.begin(), y.end()) == walked[0];
}

// The ramp out of an `Executor`: element e of every sample is e, whatever the length prepared and run.
bool ramp_out() {
    const std::vector<float> x(4 * 16, 1.0f);
    std::vector<float> y(4 * 16);
    std::vector<float> r(4 * 16, -1.0f);
    Executor<ramp_output> ex;
    ex.input("x") = x.data();
    ex.output("y") = y.data(), ex.output("r") = r.data();
    bool ok = true;
    for (const auto [prepared, frames] : {std::pair{16, 16}, std::pair{16, 5}, std::pair{8, 8}, std::pair{8, 3}}) {
        ex.prepare(prepared);
        ex.run(frames);
        for (std::int32_t t = 0; t < frames; ++t) {
            for (std::int32_t e = 0; e < 4; ++e) {
                ok = ok && r[static_cast<std::size_t>(t * 4 + e)] == static_cast<float>(e);
            }
        }
    }
    return ok;
}

// Prepared and run, though it has no arena: a run is refused only before `prepare`.
bool only_a_ramp_runs() {
    std::vector<float> r(3 * 8, -1.0f);
    Executor<only_a_ramp> ex;
    ex.output("r") = r.data();
    ex.prepare(8);
    ex.run(8);
    ex.reset();
    ex.run(5);
    bool ok = true;
    for (std::size_t k = 0; k < r.size(); ++k) {
        ok = ok && r[k] == static_cast<float>(k % 3);
    }
    return ok;
}

// They run: nothing to copy out, a sum over nothing.
bool nothing_runs() {
    float none[1]{-1.0f};
    Executor<no_ramp> empty;
    empty.output("r") = none;
    empty.run();
    std::vector<float> y(3 * 4, -1.0f);
    const float x[1]{};
    Executor<contracted_over_nothing> sum;
    sum.input("x") = x;
    sum.output("y") = y.data();
    sum.run();
    return none[0] == -1.0f && y == std::vector<float>(3 * 4, 0.0f);
}

} // namespace

int main() {
    const std::vector<double> a = numbers(50 * 21, 0.125);
    const std::vector<double> p = numbers(64, 0.03125);
    const std::vector<double> x = numbers(4 * 16, 0.5);
    const Outputs contraction = executed<contracted>({{"a", a.data(), 50 * 21}});
    executed<table_of_a_ramp>({{"p", p.data()}});
    executed<ramp_output, 16>({{"x", x.data(), 4}});
    expect(relaid_agrees(contraction), "a relaid ramp, against the walk");
    expect(ramp_out(), "a ramp output over a Length, prepared twice, run at four lengths");
    expect(only_a_ramp_runs(), "a Length graph that keeps nothing, prepared, run and reset");
    expect(nothing_runs(), "ramps of no element: an output of none, a sum over none");
    if (g_wrong != 0) {
        std::printf("constant_ramps_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("constant_ramps_test: ramps read where they lie give the walk's numbers\n");
    return 0;
}
