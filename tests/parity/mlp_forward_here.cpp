// The `mlp_train` example's FORWARD pass, built in THIS tree's language, run on `Executor`, and its output
// printed.
//
// WHAT IS PORTED AND WHAT IS NOT. The old tree's `examples/mlp_train.cpp` trains a 2-layer ReLU MLP: it
// builds the forward `y = matmul(relu(matmul(x,w1)+b1), w2)+b2`, then calls `gradients(loss, {w1..b2})`
// to append a whole backward pass, and the host runs SGD between `run()` calls. Milestone MB's `_matmul`
// makes the FORWARD expressible — two contractions `"bi,ih->bh"` and `"bh,ho->bo"`, a broadcast
// bias add, and a ReLU (`max(_,0)`), every op already tested. This program is the forward, output-compared
// to the old tree's own forward; the training, through `gradients(…)`, is `mlp_train_here.cpp`.
//
// THE MAPPING. `x{B,IN}`, `w1{IN,H}`, `w2{H,OUT}` and the biases `{1,H}`/`{1,OUT}` are tensors, the biases
// broadcasting over the batch. `b` is a FREE axis of x (a per-row output), not a shared batch axis —
// `"bi,ih->bh"` contracts `i`, keeps `b` and `h`. Inputs are the example's own deterministic init, carried
// through float so their bits match.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/notation/einsum.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t B = 16;
constexpr std::int32_t IN = 4;
constexpr std::int32_t H = 8;
constexpr std::int32_t OUT = 2;

// y = matmul(relu(matmul(x,w1)+b1), w2)+b2 — the old example's forward, over tensors.
constexpr Graph build() {
    Input x{"x", {B, IN}};
    Input w1{"w1", {IN, H}};
    Input b1{"b1", {1, H}};
    Input w2{"w2", {H, OUT}};
    Input b2{"b2", {1, OUT}};
    Expr h = max(einsum("bi,ih->bh", x, w1) + b1, 0.0); // relu hidden layer
    Expr y = einsum("bh,ho->bo", h, w2) + b2;
    return finish(Output{"y"} = y);
}

// The example's deterministic init, in float so the bits match the old tree exactly.
float det(float seed, int i) { return std::sin(seed * static_cast<float>(i + 1) * 12.9898f) * 0.5f; }

} // namespace

int main() {
    std::array<double, B * IN> x{};
    std::array<double, IN * H> w1{};
    std::array<double, H> b1{}; // zero, as the example leaves it
    std::array<double, H * OUT> w2{};
    std::array<double, OUT> b2{}; // zero
    for (int i = 0; i < B * IN; ++i) {
        x[static_cast<std::size_t>(i)] = static_cast<double>(det(0.7f, i));
    }
    for (int i = 0; i < IN * H; ++i) {
        w1[static_cast<std::size_t>(i)] = static_cast<double>(det(1.3f, i));
    }
    for (int i = 0; i < H * OUT; ++i) {
        w2[static_cast<std::size_t>(i)] = static_cast<double>(det(2.1f, i));
    }

    const testing::Outputs y = testing::executed<build>({{"x", x.data(), B * IN},
                                                        {"w1", w1.data(), IN * H},
                                                        {"b1", b1.data(), H},
                                                        {"w2", w2.data(), H * OUT},
                                                        {"b2", b2.data(), OUT}});

    // y{B,OUT}: element e = b*OUT + o, printed row-major — the same order the there side prints.
    for (const double v : y[0]) {
        std::printf("%a\n", v);
    }
    return 0;
}
