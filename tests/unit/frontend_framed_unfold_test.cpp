// The FRONTEND spells an unfold of the streamed axis — MF.6 s2, the frontend-growth item (a) of the
// multi-rate / frame-stream model (Decision 44, representation A; operator "grow the frontend",
// 2026-09-05 10:55 / reopen 19:01).
//
// MF.4 built energy_fraction's per-block core — unfold(streamed axis) -> square -> mean over the window
// -> sqrt = windowed RMS, and Lt(threshold, rms) = a 0/1 mask — by hand, because "the frontend spelling
// for an unfold of the streamed axis is minted at MF.6." This mints it: the SAME graph written in the
// user-facing DSL. `x.unfold(0, win, hop)` on a rank-1 {N} value windows the STREAMED axis
// (axis 0 is the innermost of a rank-1 value); MF.2 lifted the analysis pass's streamed-axis refusal
// into representation A, and the builder appends exactly the op::Unfold node MF.4 hand-built (its
// `frames` field defaults to -1, which is the value the builder writes), so no new op and no new
// semantics — only that the spelling now reaches the streamed axis.
//
// Proof that the spelling lowers to the right thing, not merely that it compiles: the captured graph is
// walked (the loop tree does not run a framed stream yet) and its RMS and mask are matched, element by
// element, to a reference computed straight from the definition — the same reference and the same ramp
// MF.4's hand-built graph is held to — at TWO distinct N (two frame counts W). The mask is checked to
// carry BOTH a 0 and a 1, so a threshold that computed nothing would fail.
#include <array>
#include <cmath>
#include <cstddef>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"

#include "interpret.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t kWin = 4;
constexpr std::int32_t kHop = 2;
constexpr double kThreshold = 6.0; // between the first and last frame's RMS of the ramp below

// energy_fraction's windowed-RMS-mask core, written in the user-facing DSL. The streamed-axis unfold is
// the spelling this file exists to prove; the rest — square, mean over the window, sqrt, greater-than —
// are spellings closed earlier (MB / the 09-04 frontend-growth decisions).
constexpr Graph frontend_windowed_rms_mask(std::int32_t n) {
    Input x{"x", Signal{n}, ElemType::F64};
    Expr framed = x.unfold(/*axis*/ 0, /*window*/ kWin, /*hop*/ kHop); // {win, W}, rep. A
    Expr sq = framed * framed;                                                    // square each tap
    Expr meansq = sq.mean(0); // mean over the window axis -> {W}
    Expr rms = sqrt(meansq);                                     // RMS per frame -> {W}
    Expr mask = rms > constant(kThreshold);                      // Lt(threshold, rms): 1 where rms>thr
    Graph g = finish(Output{"rms"} = rms, Output{"mask"} = mask);
    if (!infer(g).ok) {
        fail("the frontend windowed-rms-mask graph did not resolve");
    }
    return g;
}

constexpr Graph kF12 = frontend_windowed_rms_mask(12); // W = (12-4)/2 + 1 = 5
constexpr Graph kF16 = frontend_windowed_rms_mask(16); // W = (16-4)/2 + 1 = 7

// x[n] = n + 1, fed wider than the block (N samples for W frames), exactly MF.4's ramp.
template <std::int32_t N>
constexpr std::array<double, static_cast<std::size_t>(N)> ramp() {
    std::array<double, static_cast<std::size_t>(N)> xs{};
    for (std::int32_t n = 0; n < N; ++n) {
        xs[static_cast<std::size_t>(n)] = static_cast<double>(n + 1);
    }
    return xs;
}

// The reference straight from the definition: frame f's RMS is the root mean of its window's squares,
// and its mask is 1 exactly when that clears the threshold. `std::sqrt` at the same tier the default run
// uses, so an exact `==` compare is valid.
template <std::int32_t N, std::size_t W>
std::array<double, 2 * W> reference() {
    const std::array<double, static_cast<std::size_t>(N)> xs = ramp<N>();
    std::array<double, 2 * W> got{}; // [rms_0..rms_{W-1}, mask_0..mask_{W-1}]
    for (std::size_t f = 0; f < W; ++f) {
        double sum = 0.0;
        for (std::int32_t k = 0; k < kWin; ++k) {
            const double v = xs[static_cast<std::size_t>(static_cast<std::int32_t>(f) * kHop + k)];
            sum += v * v;
        }
        const double rms = std::sqrt(sum / static_cast<double>(kWin));
        got[f] = rms;
        got[W + f] = rms > kThreshold ? 1.0 : 0.0;
    }
    return got;
}

template <std::int32_t N, std::size_t W>
std::array<double, 2 * W> walked(const Graph& g) {
    const std::array<double, static_cast<std::size_t>(N)> xs = ramp<N>();
    Inputs feeds{};
    feeds.push_back(InputFeed{xs.data(), 1, N});
    Signals<W> out{};
    run(g, feeds, out);
    std::array<double, 2 * W> got{};
    for (std::size_t f = 0; f < W; ++f) {
        got[f] = sample_at(out, g.outputs[0].value, 0, f);     // rms
        got[W + f] = sample_at(out, g.outputs[1].value, 0, f); // mask
    }
    return got;
}

template <std::size_t W>
bool same(const std::array<double, 2 * W>& a, const std::array<double, 2 * W>& b) {
    for (std::size_t i = 0; i < 2 * W; ++i) {
        if (!(a[i] == b[i])) {
            return false;
        }
    }
    return true;
}

template <std::size_t W>
bool mask_is_a_mixed_0_1(const std::array<double, 2 * W>& r) {
    bool saw0 = false;
    bool saw1 = false;
    for (std::size_t f = 0; f < W; ++f) {
        const double m = r[W + f];
        if (m != 0.0 && m != 1.0) {
            return false;
        }
        saw0 = saw0 || m == 0.0;
        saw1 = saw1 || m == 1.0;
    }
    return saw0 && saw1;
}

bool check() {
    // The frontend-captured graph's walk matches the from-definition reference at two N.
    if (!same<5>(walked<12, 5>(kF12), reference<12, 5>()) ||
        !same<7>(walked<16, 7>(kF16), reference<16, 7>())) {
        return false;
    }
    // Its mask carries both a 0 and a 1.
    if (!mask_is_a_mixed_0_1<5>(reference<12, 5>()) || !mask_is_a_mixed_0_1<7>(reference<16, 7>())) {
        return false;
    }
    return true;
}

} // namespace

int main() {
    return check() ? 0 : 1;
}
