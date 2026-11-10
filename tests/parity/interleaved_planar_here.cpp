// The `interleaved_planar_audio` example's PLANAR core, built in THIS tree's language, run on `Executor`, and
// printed.
//
// The old tree's `examples/interleaved_planar_audio.cpp` makes two points. The one MB.S5 grants is a
// per-channel gain over a logical `[C, T]` value — `y[c,t] = gain[c]·x[c,t]`, gain `{0.5, 2.0}` — reached
// two ways: directly, and through a `rearrange("c t -> t c")` / `rearrange("t c -> c t")` round-trip that
// is a logical no-op. MB.S5a's notation lowers each `rearrange` to one `_transpose` of the two ELEMENT
// axes, the streamed axis passing through; the value is a tensor, `x{C,T}`, so both axes c,t are element
// axes and its length-1 stream is never named. The gain is a
// per-channel broadcast, already ported by channel_scale.
//
// So this program RUNS both the direct gain and the transpose-round-trip gain and prints each; the
// companion `_there` prints the old tree's planar gain TWICE, so line i compares the direct gain to the
// reference and line i+C*T compares the round-trip to it — showing the two inverse transposes compose to
// the identity NUMERICALLY (every number returns to where it started), the substance of S5's transpose.
//
// TWO of the old example's points are NOT ported and are recorded as reserved inventory in
// docs/decisions.md, not built: (1) `input_layout(... "(t c)")`, a physically INTERLEAVED input buffer —
// a run-surface memory-layout feature, not a shape-algebra rearrange; and (2) the "inverse pair cancels to
// ZERO nodes" claim — a graph-canonicalisation/optimiser property the capture layer (which emits the nodes
// it is told to, M8.2) does not have. This program asserts only what is TRUE: the numbers are right and the
// transposes compose. gains are exact powers of two, so ALLOWED is 0.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/notation/rearrange.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t C = 2;
constexpr std::int32_t T = 16;
constexpr std::int32_t kGrid = C * T;

// Logical x(c,t): channel 0 (L) is t, channel 1 (R) is 100+t — the old example's own values, carried
// through float so their bits match the old tree's f32.
constexpr double x_value(std::int32_t c, std::int32_t t) {
    return static_cast<double>(c == 0 ? static_cast<float>(t) : 100.0f + static_cast<float>(t));
}

// The direct per-channel gain: y = x{C,T} * gain{C,1}, gain broadcasting over t.
constexpr Graph build_direct() {
    Input x{"x", {C, T}};
    Input gain{"gain", {C, 1}};
    return finish(Output{"y"} = x * gain);
}

// The same gain reached through a transpose round-trip: `c t -> t c` then `t c -> c t` is a
// logical no-op (two inverse transposes of the element axes), so the numbers must be identical.
constexpr Graph build_roundtrip() {
    Input x{"x", {C, T}};
    Input gain{"gain", {C, 1}};
    Expr r = rearrange("t c -> c t", rearrange("c t -> t c", x));
    return finish(Output{"y"} = r * gain);
}

template <auto Build>
void run_and_print() {
    std::array<double, kGrid> x_buf{};
    std::array<double, C> gain_buf{static_cast<double>(0.5f), static_cast<double>(2.0f)};
    for (std::int32_t c = 0; c < C; ++c) {
        for (std::int32_t t = 0; t < T; ++t) {
            x_buf[static_cast<std::size_t>(c * T + t)] = x_value(c, t);
        }
    }
    const testing::Outputs y = testing::executed<Build>({{"x", x_buf.data(), kGrid}, {"gain", gain_buf.data(), C}});
    for (const double v : y[0]) { // y[c,t] at element c*T + t: (c,t) row-major
        std::printf("%.9e\n", v);
    }
}

} // namespace

int main() {
    run_and_print<build_direct>();    // y[c,t] over (c,t) row-major
    run_and_print<build_roundtrip>(); // the same, reached through the transpose round-trip
    return 0;
}
