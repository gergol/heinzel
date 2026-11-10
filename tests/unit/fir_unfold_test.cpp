// FIR-via-unfold, checked against a summed-tap convolution — MF.5 of the multi-rate / frame-stream
// model (Decision 44, representation A). This lifts tutorial 05's reserved FIR item (Decision 40).
//
// tutorial 05 spells an FIR filter as `(x.unfold(K, 1) * h).sum(1)`: frame the signal into every length-K
// window at hop 1, multiply each window by the taps, sum the window. Under representation A an unfold of
// the streamed axis gives `{K, W}` (window tap K an element axis, frame count W the new block), so:
//
//   x{N}  --unfold(axis0, win=K, hop=1)-->  {K, W}
//         --mul(h)-->  {K, W}         (h{K}: the taps, broadcast over the W frames)
//         --reduce Sum over the window axis-->  {W}   ==   y[f] = sum_k x[f+k]*h[k]
//
// checked element by element against that convolution reference at TWO distinct N (W = N-K+1), on the
// walk (the loop tree does not run a framed stream yet). The taps are DISTINCT and asymmetric
// (0.5, -1, 0.25), so a run that summed the window with the taps in the wrong order, or dropped one,
// diverges from the reference — which a uniform boxcar could not catch.
//
// The taps ride a wide INPUT of K numbers per frame rather than a graph constant: a value's constant is
// one scalar (`ir::Value`), so a K-wide constant vector has no spelling here yet — the frontend growth
// that gives it one, and the real tap constants, arrive with the CLI port at MF.6. This is the unit
// parity of the unfold*taps->sum path, exactly as MF.4 is energy_fraction's.
#include <array>
#include <cstddef>

#include "heinzel/sema/analyze.hpp"

#include "hand_built_graph.hpp"
#include "interpret.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t kTaps = 3;
constexpr std::array<double, kTaps> kH = {0.5, -1.0, 0.25};

// x{N} framed to length-K windows at hop 1, each window dotted with the taps — the FIR of tutorial 05.
// x and h are named inputs; the frame count W is the run's block. Built by hand (the frontend spelling
// is MF.6).
constexpr Graph fir_via_unfold(std::int32_t n, std::int32_t w) {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{n}, ValueCategory::Input);
    const auto h = add_value(g, ElemType::F64, Shape{kTaps, w}, ValueCategory::Input);
    OpAttr frame{};
    frame.kind = AttrKind::View;
    frame.view.kind = ViewKind::Unfold;
    frame.view.axis = 0; // the streamed axis of a rank-1 value
    frame.view.window = kTaps;
    frame.view.hop = 1;
    const auto framed = add_node(g, op::Unfold, x, kNoValue, kNoValue, frame); // {K, W}
    const auto prod = add_node(g, op::Mul, framed, h);                         // {K, W}
    OpAttr sum{};
    sum.kind = AttrKind::Reduce;
    sum.reduce.op = ReduceOp::Sum;
    sum.reduce.axes = Axes{0}; // the window axis; the streamed axis W is left whole
    const auto y = add_node(g, op::Reduce, prod, kNoValue, kNoValue, sum); // {W}
    g.inputs.push_back(NamedValue{"x", x});
    g.inputs.push_back(NamedValue{"h", h});
    g.outputs.push_back(NamedValue{"y", y});
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the FIR-via-unfold graph did not resolve");
    }
    return g;
}

constexpr Graph kFir8 = fir_via_unfold(8, 6);   // W = 8 - 3 + 1 = 6
constexpr Graph kFir12 = fir_via_unfold(12, 10); // W = 12 - 3 + 1 = 10

// x[n] = n + 1.
template <std::int32_t N>
constexpr std::array<double, static_cast<std::size_t>(N)> ramp() {
    std::array<double, static_cast<std::size_t>(N)> xs{};
    for (std::int32_t n = 0; n < N; ++n) {
        xs[static_cast<std::size_t>(n)] = static_cast<double>(n + 1);
    }
    return xs;
}

// The taps, laid out sample-major for a width-K feed: the same K taps at every one of the W frames.
template <std::size_t W>
constexpr std::array<double, kTaps * W> tap_feed() {
    std::array<double, kTaps * W> h{};
    for (std::size_t f = 0; f < W; ++f) {
        for (std::int32_t k = 0; k < kTaps; ++k) {
            h[f * kTaps + static_cast<std::size_t>(k)] = kH[static_cast<std::size_t>(k)];
        }
    }
    return h;
}

// The convolution reference, straight from the definition: y[f] = sum_k x[f+k]*h[k]. The sum runs the
// taps in k order, matching the graph's fold over the window axis; `+`/`*` are exact, so `==` is valid.
template <std::int32_t N, std::size_t W>
constexpr std::array<double, W> reference() {
    const std::array<double, static_cast<std::size_t>(N)> xs = ramp<N>();
    std::array<double, W> y{};
    for (std::size_t f = 0; f < W; ++f) {
        double acc = 0.0;
        for (std::int32_t k = 0; k < kTaps; ++k) {
            acc += xs[static_cast<std::size_t>(static_cast<std::int32_t>(f) + k)] * kH[static_cast<std::size_t>(k)];
        }
        y[f] = acc;
    }
    return y;
}

template <std::int32_t N, std::size_t W>
std::array<double, W> walked(const Graph& g) {
    const std::array<double, static_cast<std::size_t>(N)> xs = ramp<N>();
    const std::array<double, kTaps * W> h = tap_feed<W>();
    Inputs feeds{};
    feeds.push_back(InputFeed{xs.data(), 1, N});
    feeds.push_back(InputFeed{h.data(), kTaps});
    Signals<W> out{};
    run(g, feeds, out);
    std::array<double, W> got{};
    for (std::size_t f = 0; f < W; ++f) {
        got[f] = sample_at(out, g.outputs[0].value, 0, f);
    }
    return got;
}

template <std::size_t W>
constexpr bool same(const std::array<double, W>& a, const std::array<double, W>& b) {
    for (std::size_t i = 0; i < W; ++i) {
        if (!(a[i] == b[i])) {
            return false;
        }
    }
    return true;
}

// The reference at compile time carries a non-trivial answer, so the runtime comparison below is not
// matching all-zero: y[0] = 1*0.5 + 2*(-1) + 3*0.25 = -0.75.
static_assert(reference<8, 6>()[0] == -0.75, "the FIR reference's first output is wrong");

bool check() {
    if (!same<6>(walked<8, 6>(kFir8), reference<8, 6>()) ||
        !same<10>(walked<12, 10>(kFir12), reference<12, 10>())) {
        return false;
    }
    return true;
}

} // namespace

int main() {
    return check() ? 0 : 1;
}
