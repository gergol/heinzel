// `loops::streams`, the clause that refuses a program whose state does not run along the stream (Plan/archive/14-dsl/05,
// DS-14): state one sample long broadcast over a longer signal — a recurrence, a lagged input — and two literal
// signals of different lengths that both carry state (refused conservatively); then the band clause, for state in a
// stream band along an axis that is not the stream's. All of it at compile time.
#include <cstdint>
#include <cstdio>

#include "heinzel/compile.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/streams.hpp"
#include "heinzel/sema/analyze.hpp"

#include "planned.hpp"
#include "well_formed.hpp"

using namespace heinzel;

namespace {

constexpr Index n{};

constexpr Machine kUnbanded = [] {
    Machine m = zen2;
    m.band_bytes = 0; // no band the planner makes: a band only by hand
    return m;
}();

template <Graph (*Build)()>
constexpr Graph kAnalysed = [] {
    Graph g = Build();
    set_default_precision(g, numerics::Precision::Normal);
    if (!infer(g).ok) {
        fail("streams_test: a graph did not resolve");
    }
    return g;
}();

// What cannot stream: state one sample long broadcast over a longer signal — a recurrence, a lagged input — and
// two literal signals of different lengths that both carry state (refused conservatively, DS-14).
constexpr Graph one_sample_recurrence() {
    Input w{"w", Signal{4, 1}};
    Input x{"x", Signal{4, 16}};
    Variable s;
    s[n] = w[n] + 0.5 * s[n - 1];
    Output y{"y"};
    y = x * s;
    return y;
}
constexpr Graph lagged_one_sample_input() {
    Input w{"w", Signal{4, 1}};
    Input x{"x", Signal{4, 16}};
    Output y{"y"};
    y = x * w[n - 1];
    return y;
}
constexpr Graph two_literal_streams() {
    Input a{"a", Signal{16}};
    Input b{"b", Signal{32}};
    Output y{"y"};
    Output z{"z"};
    y[n] = a[n] + 0.5 * y[n - 1];
    z[n] = b[n] + 0.5 * z[n - 1];
    return finish(y, z);
}
template <Graph (*Build)()>
constexpr Compiled kCompiledOf = compile(Build());
template <Graph (*Build)()>
constexpr bool does_not_stream() {
    static_assert(loops::accepted<kCompiledOf<Build>.program>());
    return !loops::streams(kCompiledOf<Build>.program);
}
static_assert(does_not_stream<one_sample_recurrence>() && does_not_stream<lagged_one_sample_input>() && does_not_stream<two_literal_streams>());

// ...and the band clause: state in a stream band along an axis that is not the stream's. The band buffers are then
// all a run carries, so no other clause sees them — a one-sample recurrence put in a band by hand against 16
// samples, and a 16-sample oscillator's band beside a stateless 32-sample signal (refused conservatively).
constexpr Graph one_sample_state_in_two_groups() {
    Input w{"w", Signal{4, 1}};
    Input x{"x", Signal{4, 16}};
    Variable s;
    s[n] = w[n] + 0.5 * s[n - 1];
    Output y{"y"};
    y = x * (sinpi(s * 0.25) + s);
    return y;
}
constexpr Graph literal_oscillator_beside_a_longer_signal() {
    Input inc{"inc", Signal{16}};
    Input w{"w", Signal{32}};
    Variable phase;
    phase[n] = frac(phase[n - 1] + inc[n]);
    Output y{"y"};
    Output z{"z"};
    y = sinpi(phase) * 0.5; // (read by a core, which runs in lanes: the phase is the band's; a cheap follower would join its stage)
    z = w * 2.0;
    return finish(y, z);
}
template <Graph (*Build)(), std::int32_t Axis>
constexpr planned::Held kBandedByHandHeld{[] {
    loops::Schedule d = planned::schedule_of(kAnalysed<Build>, kUnbanded);
    d.bands.push_back(loops::Schedule::Band{.first = 0, .count = 2, .axis = Axis, .step = 4, .parallel = false});
    for (loops::Schedule::Stage& stage : d.stages) {
        stage.parallel = -1;
    }
    return planned::program_of(kAnalysed<Build>, d);
}()};
template <Graph (*Build)(), std::int32_t Axis>
constexpr const loops::Program& kBandedByHand = kBandedByHandHeld<Build, Axis>.program;
template <const loops::Program& P>
constexpr bool only_the_band_clause_refuses() {
    bool banded = false;
    for (const loops::Buffer& b : P.buffers) {
        if (b.home == loops::Home::Workspace && b.band < 0 && b.prefix > 0) {
            return false;
        }
        banded = banded || (b.band >= 0 && b.prefix > 0);
    }
    for (const std::int32_t depth : loops::feed_depths(P)) {
        banded = banded && depth == 0;
    }
    return banded && !loops::streams(P);
}
static_assert(only_the_band_clause_refuses<kBandedByHand<one_sample_state_in_two_groups, 1>>() &&
              only_the_band_clause_refuses<kBandedByHand<literal_oscillator_beside_a_longer_signal, 0>>());

} // namespace

int main() {
    std::printf("streams_test: what does not run along the stream is refused, at compile time\n");
    return 0;
}
