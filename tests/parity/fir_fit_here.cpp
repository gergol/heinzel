// The `fir_fit` example — differentiable FIR coefficient fit — built in THIS tree's language, run, and
// its result printed.
//
// The old tree's `examples/fir_fit.cpp` fits K FIR taps by gradient descent THROUGH the compiled graph:
// a valid FIR convolution (unfold→einsum), an MSE loss against a hidden reference kernel's response, and
// — via `gradients(loss, {w, gain})` — the loss gradients w.r.t. the taps and one gain knob. Forward and
// backward compile into one run; the host runs SGD between `run()` calls. The fit recovers the reference:
// the effective kernel `gain*w` converges to `1.5*kRef`.
//
// WHAT MG ADDS TO MAKE THIS EXPRESSIBLE. `mlp_forward_here` could port only the forward, because the
// backward needed a frontend autodiff SPELLING and there was none. MG S2 added one, today
// `gradients(loss, {…})` (composing the frontend graph with the closed M7 `heinzel::autodiff::gradients`),
// and MG S1 the multi-output capture it rests on. With those, this example's WHOLE graph — loss AND both
// gradients as named outputs of one forward — is a frontend spelling that lowers to already-tested ops
// (unfold = `_unfold`/View, einsum = `_contract`, `*`/`-`, square = `*`, mean = `_reduce`), no new
// numeric meaning.
//
// THE MAPPING. Each array is a tensor, recorded with a length-1 stream as the shapes below are: `x{L}`,
// `w{1,K}` (taps, broadcast over the frame axis), `t{F}`, and `gain{1}` (the old tree's
// `Param gain` is a differentiation TARGET, and a target must be an input here, so gain is a width-1
// input the host mutates — the numbers are the same as a param's). `unfold(x, axis 0, K, 1)` windows the
// outer signal axis into `{F,K,1}`; the FIR is then the old tree's own `heinzel::dsp::fir` lowering —
// `reduce(unfold(x,K,1) * taps, Sum, tap axis)` — an elementwise weight and a sum over the tap axis, not
// an einsum. That is deliberate: `einsum("fk,k->f", ...)` lowers to the general `_matmul` (MB), whose
// ADJOINT is not written yet (a backend-shaped item), whereas the weight-and-reduce lowers to `_unfold`
// (View), `*` and `_reduce` (Sum) — every op the M7 autodiff already differentiates and MG requires no
// new numeric meaning. The rest is elementwise and a mean over the frame axis. Inputs are the example's
// own deterministic init carried through float so their bits match the old tree.
//
// THE COMPARISON. `fir_fit_there` runs the old example's own graph+loop and prints the same K effective
// taps. Both trees do gradient descent to the SAME reference kernel (the fixed point the loss defines),
// so they converge to the same effective taps — bit for bit, with the host update in float on both
// sides (measured; tests/CMakeLists.txt, BECAUSE).
//
// ON EXECUTOR AT HIGH. The graph runs as compiled, at `High`, where the loop tree computes as the
// reference walk does, and every run is checked against the walk on the same parameters, bit for bit.
// The inputs are F32, so the host keeps the taps and the gain in float and updates them in float, as the
// old side's host loop does.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "heinzel/frontend/graph_builder.hpp"

#include "executed.hpp"
#include "gradients_capture.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t L = 64;
constexpr std::int32_t K = 5;
constexpr std::int32_t F = L - K + 1;

// loss = mean_f (gain * reduce(unfold(x,K,1) * w, Sum, tap axis) - t)^2, with dloss/dw and dloss/dgain.
constexpr Graph build() {
    Input x{"x", {L}};
    Input w{"w", {1, K}}; // taps, broadcast over the frame axis
    Input t{"t", {F}};
    Input gain{"gain", {1}};

    Expr frames = x.unfold(0, K);                                                 // {F, K, 1}
    Expr conv = (frames * w).sum(1);                                              // {F, 1}
    Expr y = conv * gain;                                                         // {F, 1}
    Expr d = y - t;                                                               // {F, 1}
    Output loss{"loss"};
    loss = (d * d).mean(0);                                                       // {1}
    return gradients(loss, {Grad{"dw", w}, Grad{"dgain", gain}}, numerics::Precision::High);
}

// One SGD update, in float as the old side's host loop does it.
constexpr float kLr = 0.2f;
void step_down(float& p, float grad) { p = p - kLr * grad; }

} // namespace

int main() {
    // probe signal + target = reference kernel at gain 1.5, computed through float so the fixed point
    // matches the old tree's.
    constexpr std::array<float, K> kRef = {0.20f, -0.35f, 0.55f, -0.15f, 0.40f};
    std::array<float, L> x{};
    for (int i = 0; i < L; ++i) {
        const float v = std::sin(static_cast<float>(i + 1)) * 43758.5453f;
        x[static_cast<std::size_t>(i)] = (v - std::floor(v)) - 0.5f;
    }
    std::array<float, F> t{};
    for (int f = 0; f < F; ++f) {
        float acc = 0;
        for (int k = 0; k < K; ++k) {
            acc += x[static_cast<std::size_t>(f + k)] * kRef[static_cast<std::size_t>(k)];
        }
        t[static_cast<std::size_t>(f)] = 1.5f * acc;
    }

    std::array<float, K> w{}; // zero-initialized taps, mutated in place across runs
    std::array<float, 1> gain{1.0f};
    std::array<float, K> dw{};
    std::array<float, 1> dgain{};

    // The inputs are views over the host storage above; w and gain are updated between runs, so each
    // run reads the current parameters.
    Executor<build> ex;
    ex.input("x") = x.data();
    ex.input("w") = w.data();
    ex.input("t") = t.data();
    ex.input("gain") = gain.data();
    ex.output("dw") = dw.data();
    ex.output("dgain") = dgain.data();
    for (int step = 0; step <= 400; ++step) {
        ex.run(); // recomputes from the current w/gain
        const testing::Outputs walk =
            testing::walked<build>({{"x", x.data(), L}, {"w", w.data(), K}, {"t", t.data(), F}, {"gain", gain.data()}});
        testing::require_walked("dw", dw, walk[1]); // the outputs: loss, dw, dgain
        testing::require_walked("dgain", dgain, walk[2]);
        for (int k = 0; k < K; ++k) {
            step_down(w[static_cast<std::size_t>(k)], dw[static_cast<std::size_t>(k)]);
        }
        step_down(gain[0], dgain[0]);
    }

    // gain·w is what the loss constrains — print the effective kernel, one tap per line, nothing else.
    for (int k = 0; k < K; ++k) {
        std::printf("%.9e\n", static_cast<double>(gain[0] * w[static_cast<std::size_t>(k)]));
    }
    return 0;
}
