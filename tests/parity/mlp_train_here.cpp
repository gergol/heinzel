// The `mlp_train` example — a 2-layer ReLU MLP trained by SGD through the compiled forward+backward
// graph — built in THIS tree's language, run, and its loss trajectory printed.
//
// `mlp_forward_here` could port only the FORWARD, because the backward needed a frontend autodiff
// spelling and there was none. MG S2's gradients, today `gradients(loss, {…})` (leaf-composing the closed M7
// `heinzel::autodiff::gradients`) and MG S1's multi-output capture now make the whole example — loss
// AND four gradients as named outputs of one forward — a frontend spelling. This is MG.4.
//
// THE MATMULS, and why NOT a matmul. `mlp_forward_here` spelled the two layers with `einsum("bi,ih->bh")`,
// which lowers to the general `_matmul` (MB) whose ADJOINT is not written (a backend-shaped item). This
// port spells each layer as an einsum whose two operands carry the SAME labels (`"bih,bih->bh"`) — the
// restricted contraction that lines its two operands up elementwise (size-1 axes broadcast) and sums the
// named axis — which is built as a multiply and a sum (Decision 78), two operations the M7 autodiff
// differentiates. No new op and no new numeric meaning: a contraction is a sum of products either way.
//
// THE MAPPING. The inputs are tensors. Layer 1 contracts IN: `x{B,IN,1}` against `w1{1,IN,H}` broadcast into
// `{B,IN,H}` (`"bih,bih->bh"`), summed over IN → `{B,H}`; `+ b1{1,H}`; relu. Layer 2 contracts H: the hidden
// `{B,H}` reshaped to `{B,H,1}` against `w2{1,H,OUT}` (`"bho,bho->bo"`) → `{B,OUT}`; `+ b2{1,OUT}`. Loss is the
// mean of `(y-t)^2` over B and OUT. Biases start at zero; a gradient has its target's shape, so `dw1` is
// `{1,IN,H}` etc., read element-for-element against the same buffer
// the host mutates. Init and data are the example's own deterministic `det()` in float, as the old side's are.
//
// ON EXECUTOR AT HIGH. The graph runs as compiled, at `High`, where the loop tree computes as the reference walk
// does, and every run is checked against the walk on the same parameters, bit for bit (as `fir_fit_here`). The
// inputs are F32, so the host keeps the parameters in float and updates them in float, as the old side's host
// loop does. Until the aligned einsum was a multiply and a sum, the loop tree refused the graph and it ran on the
// walk alone.
//
// THE COMPARISON. Both trees run the SAME deterministic 600-step SGD from the SAME init, so the loss
// falls the same way; each prints the loss at the six sampled steps (0,120,…,600). The residual gap is how
// the two round a contraction (there an f32 fused multiply-add per term; here each product rounded to f32,
// their sum in double) and the mean — largest at step 0, so it is the forward graph, not the descent. The
// parity ALLOWED is that gap, measured.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/notation/einsum.hpp"

#include "executed.hpp"
#include "gradients_capture.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t B = 16;
constexpr std::int32_t IN = 4;
constexpr std::int32_t H = 8;
constexpr std::int32_t OUT = 2;

// y = contract(relu(contract(x,w1)+b1), w2)+b2 ; loss = mean_(B,OUT) (y-t)^2 ; + the four gradients.
constexpr Graph build() {
    Input x{"x", {B, IN, 1}};
    Input w1{"w1", {1, IN, H}};
    Input b1{"b1", {1, H}};
    Input w2{"w2", {1, H, OUT}};
    Input b2{"b2", {1, OUT}};
    Input t{"t", {B, OUT}};

    Expr h = max(einsum("bih,bih->bh", x, w1) + b1, 0.0); // relu hidden {B,H}
    Expr h4 = h.reshape({B, H, 1});                       // add the OUT-slot axis
    Expr y = einsum("bho,bho->bo", h4, w2) + b2;          // {B,OUT}
    Expr d = y - t;                                       // {B,OUT}
    Output loss{"loss"};
    loss = (d * d).mean({0, 1}); // scalar over B and OUT
    return gradients(loss, {Grad{"dw1", w1}, Grad{"db1", b1}, Grad{"dw2", w2}, Grad{"db2", b2}},
                     numerics::Precision::High);
}

// The example's deterministic init/data, in float so the bits match the old tree exactly.
float det(float seed, int i) { return std::sin(seed * static_cast<float>(i + 1) * 12.9898f) * 0.5f; }
constexpr float kLr = 0.1f; // the update runs in float, as the old side's does

// The example's probe x and its smooth nonlinear target t, both in float, as the old tree computes them.
void init_data(std::array<float, B * IN>& x, std::array<float, B * OUT>& t) {
    for (int i = 0; i < B * IN; ++i) {
        x[static_cast<std::size_t>(i)] = det(0.7f, i);
    }
    for (int b = 0; b < B; ++b) {
        float s = 0;
        for (int i = 0; i < IN; ++i) {
            s += x[static_cast<std::size_t>(b * IN + i)];
        }
        t[static_cast<std::size_t>(b * OUT + 0)] = std::tanh(s);
        t[static_cast<std::size_t>(b * OUT + 1)] = 0.5f * s * s - 0.2f;
    }
}

// One SGD step for one parameter: p[e] -= lr * grad[e]. A gradient shares its target's shape, so element
// e of the gradient is element e of the parameter — the same buffer the input reads.
template <std::size_t N>
void sgd_step(std::array<float, N>& p, const std::array<float, N>& grad) {
    for (std::size_t e = 0; e < N; ++e) {
        p[e] = p[e] - kLr * grad[e];
    }
}

} // namespace

int main() {
    std::array<float, B * IN> x{};
    std::array<float, B * OUT> t{};
    init_data(x, t);
    std::array<float, IN * H> w1{};
    std::array<float, H> b1{};   // zero, as the example leaves it
    std::array<float, H * OUT> w2{};
    std::array<float, OUT> b2{}; // zero
    for (int i = 0; i < IN * H; ++i) {
        w1[static_cast<std::size_t>(i)] = det(1.3f, i);
    }
    for (int i = 0; i < H * OUT; ++i) {
        w2[static_cast<std::size_t>(i)] = det(2.1f, i);
    }
    std::array<float, 1> loss{};
    std::array<float, IN * H> dw1{};
    std::array<float, H> db1{};
    std::array<float, H * OUT> dw2{};
    std::array<float, OUT> db2{};

    // The inputs are views over the host storage above; the four parameters are updated between runs, so each run
    // reads the current ones.
    Executor<build> ex;
    ex.input("x") = x.data();
    ex.input("w1") = w1.data();
    ex.input("b1") = b1.data();
    ex.input("w2") = w2.data();
    ex.input("b2") = b2.data();
    ex.input("t") = t.data();
    ex.output("loss") = loss.data();
    ex.output("dw1") = dw1.data();
    ex.output("db1") = db1.data();
    ex.output("dw2") = dw2.data();
    ex.output("db2") = db2.data();
    for (int step = 0; step <= 600; ++step) {
        ex.run();
        const testing::Outputs walk = testing::walked<build>({{"x", x.data(), B * IN},
                                                              {"w1", w1.data(), IN * H},
                                                              {"b1", b1.data(), H},
                                                              {"w2", w2.data(), H * OUT},
                                                              {"b2", b2.data(), OUT},
                                                              {"t", t.data(), B * OUT}});
        testing::require_walked("loss", loss, walk[0]); // the outputs: loss, dw1, db1, dw2, db2
        testing::require_walked("dw1", dw1, walk[1]);
        testing::require_walked("db1", db1, walk[2]);
        testing::require_walked("dw2", dw2, walk[3]);
        testing::require_walked("db2", db2, walk[4]);
        if (step % 120 == 0) { // the example's own sampling: 0,120,240,360,480,600
            std::printf("%.9e\n", static_cast<double>(loss[0]));
        }
        sgd_step(w1, dw1);
        sgd_step(b1, db1);
        sgd_step(w2, dw2);
        sgd_step(b2, db2);
    }
    return 0;
}
