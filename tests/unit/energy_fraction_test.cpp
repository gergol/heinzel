// energy_fraction as a graph, checked against a hand-written windowed-RMS — MF.4 of the multi-rate /
// frame-stream model (Decision 44, representation A).
//
// The old tree's `examples/energy_fraction.cpp` frames the streamed axis into overlapping windows,
// takes each window's RMS, and reports the fraction of windows whose RMS clears a threshold. This
// builds the PER-BLOCK core of that — the windowed RMS and the 0/1 above-threshold mask — as one graph
// and checks it three ways:
//
//   x{N}  --unfold(axis0, win, hop)-->  {win, W}  (frame the STREAM, representation A)
//         --mul(self)-->  {win, W}               (square each windowed sample)
//         --reduce Mean over the window axis-->  {W}   (mean square per frame)
//         --sqrt-->  {W}                          (RMS per frame)
//   mask = Lt(threshold, rms)  ->  {W}            (1 where rms > threshold, else 0 — integer 0/1)
//
// The cross-block frame framing (constant memory, overlap `win-hop`) and the fraction reduction are the
// CLI's, ported at MF.6; here the graph runs one block of W frames and its RMS and mask are compared,
// element by element, to a reference computed straight from the definition. Checked at TWO distinct N
// (two frame counts W), on the walk (the loop tree does not run a framed stream yet). The threshold is
// chosen so the mask carries BOTH a 0 and a 1 — a threshold every frame cleared, or none did, would pass
// a mask that computed nothing.
#include <array>
#include <cmath>
#include <cstddef>

#include "heinzel/sema/analyze.hpp"

#include "hand_built_graph.hpp"
#include "interpret.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t kWin = 4;
constexpr std::int32_t kHop = 2;
constexpr double kThreshold = 6.0; // between the first and last frame's RMS of the ramp below

// x{N} framed to overlapping windows, each window's RMS, and a 1/0 above-threshold mask. Built by hand
// — the frontend spelling for an unfold of the streamed axis is minted at MF.6 with the CLI port.
constexpr Graph windowed_rms_mask(std::int32_t n) {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{n}, ValueCategory::Input);
    OpAttr frame{};
    frame.kind = AttrKind::View;
    frame.view.kind = ViewKind::Unfold;
    frame.view.axis = 0; // the streamed axis of a rank-1 value
    frame.view.window = kWin;
    frame.view.hop = kHop;
    const auto framed = add_node(g, op::Unfold, x, kNoValue, kNoValue, frame); // {win, W}
    const auto sq = add_node(g, op::Mul, framed, framed);                      // {win, W}
    OpAttr mean{};
    mean.kind = AttrKind::Reduce;
    mean.reduce.op = ReduceOp::Mean;
    mean.reduce.axes = Axes{0}; // the window axis; the streamed axis W is left whole
    const auto meansq = add_node(g, op::Reduce, sq, kNoValue, kNoValue, mean); // {W}
    const auto rms = add_node(g, op::Sqrt, meansq);                            // {W}
    const auto thr = add_value(g, ElemType::F64, Shape{}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(thr)].constant = kThreshold;
    const auto mask = add_node(g, op::Lt, thr, rms); // thr < rms  ->  1 where rms > threshold
    g.inputs.push_back(NamedValue{"x", x});
    g.outputs.push_back(NamedValue{"rms", rms});
    g.outputs.push_back(NamedValue{"mask", mask});
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the windowed-rms-mask graph did not resolve");
    }
    return g;
}

constexpr Graph kRms12 = windowed_rms_mask(12); // W = (12-4)/2 + 1 = 5
constexpr Graph kRms16 = windowed_rms_mask(16); // W = (16-4)/2 + 1 = 7

// x[n] = n + 1, fed wider than the block (N samples for W frames).
template <std::int32_t N>
constexpr std::array<double, static_cast<std::size_t>(N)> ramp() {
    std::array<double, static_cast<std::size_t>(N)> xs{};
    for (std::int32_t n = 0; n < N; ++n) {
        xs[static_cast<std::size_t>(n)] = static_cast<double>(n + 1);
    }
    return xs;
}

// The reference, straight from the definition: frame f's RMS is the root mean of its window's squares,
// and its mask is 1 exactly when that clears the threshold. `std::sqrt` (not a constexpr context) so a
// runtime function, matched against the graph's `op::Sqrt` at the SAME tier the default run uses.
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

// The graph's RMS and mask from the walk.
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

// The mask half is integer 0/1, and carries both — so the comparison above is not passing an all-0 or
// all-1 mask that computed nothing.
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
    // The walk lands on the reference at both N — the graph computes the windowed RMS and mask.
    if (!same<5>(walked<12, 5>(kRms12), reference<12, 5>())) {
        return false;
    }
    if (!same<7>(walked<16, 7>(kRms16), reference<16, 7>())) {
        return false;
    }
    // The mask really is a mixed 0/1 at this threshold.
    if (!mask_is_a_mixed_0_1<5>(reference<12, 5>()) || !mask_is_a_mixed_0_1<7>(reference<16, 7>())) {
        return false;
    }
    return true;
}

} // namespace

int main() {
    return check() ? 0 : 1;
}
