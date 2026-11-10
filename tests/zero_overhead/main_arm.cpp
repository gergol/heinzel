// One workload on `main`'s `executor` (`build/main-tree`, `c4012b82`), the fifth arm of `Plan/15-zero-overhead/01`,
// built by `measure.sh` against `main`'s headers: -DWORKLOAD=w1, w2, w3, w6, w7 or w8, and one of
//   (neither)  the size build: make, bind and run; its text + data + bss is B;
//   -DCOUNT    I, C and M, as `arm.cpp` prints them (`count.hpp`);
//   -DCHECK    `main` against the hand loop over three runs, each output within the hand loop's bound where it has
//              one, else within 1e-5 of the larger of its magnitude and 1: `main` owes the hand loop no bits (D-7).
// Each graph is the workload's computation in `main`'s DSL, at `main`'s default target, backend and tier (`Precise`,
// which is this tree's `Normal`). W4, W5 and W9 have none: `main`'s executor does not carry their past from one run to
// the next (the plan says how). W12 has none either: it has no hand loop to check `main` against.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "heinzel/frontend/builder.hpp"
#include "heinzel/frontend/einsum.hpp"
#include "heinzel/runtime/executor.hpp"

#include "harness.hpp"

namespace zo {

using namespace heinzel;

inline constexpr Index n{};

} // namespace zo

namespace zo::w1 { // gain

constexpr Graph build() {
    GraphBuilder g;
    Input x{g, "x", {64}};
    Param gain{g, "g"};
    Output y{g, "y", {64}};
    y = x * gain;
    return g;
}

} // namespace zo::w1

namespace zo::w2 { // elem: the length a dim, which `main` fixes at prepare()

constexpr Graph build() {
    GraphBuilder g;
    const Dim samples = g.dim("N", {.multiple_of = 1, .min = 1, .expect = 262144});
    Input a{g, "a", {samples}};
    Input b{g, "b", {samples}};
    Output y{g, "y", {samples}};
    y = a * b + b;
    return g;
}

} // namespace zo::w2

namespace zo::w3 { // biquad

constexpr Graph build() {
    GraphBuilder g;
    Input x{g, "x", {64}};
    Output y{g, "y", {64}};
    y[n] = x[n] + kC1 * y[n - 1] + kC2 * y[n - 2];
    return g;
}

} // namespace zo::w3

namespace zo::w6 { // rowsum

constexpr Graph build() {
    GraphBuilder g;
    Input x{g, "x", {64, 64}};
    Output y{g, "y", {64}};
    y = x.sum(1);
    return g;
}

} // namespace zo::w6

namespace zo::w7 { // matmul

constexpr Graph build() {
    GraphBuilder g;
    Input a{g, "a", {64, 64}};
    Input b{g, "b", {64, 64}};
    Output y{g, "y", {64, 64}};
    y = einsum("ik,kj->ij", a, b);
    return g;
}

} // namespace zo::w7

namespace zo::w8 { // blur: `main`'s `g_blurxy` (`benchmarks/image_pipeline_bench.cpp`), its weights {3} for {1, 1, 3}, {1, 3, 1}

constexpr Graph build() {
    GraphBuilder g;
    Input x{g, "x", {1024, 2048}};
    Input wh{g, "wh", {3}};
    Input wv{g, "wv", {3}};
    Output y{g, "y", {1022, 2046}};
    Expr bx = sum(unfold(x, 3, 1) * wh, Axes{2});
    y = sum(unfold_rows(bx, 3, 1) * wv, Axes{2});
    return g;
}

} // namespace zo::w8

namespace {

using Ex = heinzel::executor<w::build()>;

template <std::size_t I>
void bind_input(Ex& ex, const float* data) {
    if constexpr (w::kIn[I].param) {
        ex.param(w::kIn[I].name) = *data;
    } else {
        ex.input(w::kIn[I].name) = data;
    }
}

struct Arm {
    Ex ex;
    std::array<float*, kOuts> out{};
    static constexpr std::size_t kObject = sizeof(Ex);

    explicit Arm(std::int64_t frames) {
        if constexpr (Ex::NDims > 0) {
            ex.dim("N") = static_cast<std::int32_t>(frames);
            ex.prepare();
        }
        if constexpr (Ex::has_recurrence) {
            ex.set_streaming(true); // carry the past from run to run, as the hand loop and this tree's `Executor` do
        }
    }
    void bind(const Buffers& b) {
        out = b.outs();
        [&]<std::size_t... I, std::size_t... O>(std::index_sequence<I...>, std::index_sequence<O...>) {
            const std::array<const float*, kIns> in = b.ins();
            (bind_input<I>(ex, in[I]), ...);
            ((ex.output(w::kOut[O].name) = out[O]), ...);
        }(std::make_index_sequence<kIns>{}, std::make_index_sequence<kOuts>{});
    }
    [[gnu::noinline]] void run(std::int64_t) { ex.run(); }
    const float* result(std::size_t o) { return out[o]; }
    std::size_t kept() { return 0; }
};

double bound(zo::In in, std::size_t e, float want) {
    const double hand = w::hand_bound(in, e);
    return hand > 0.0 ? hand : 1e-5 * std::max(std::fabs(static_cast<double>(want)), 1.0);
}

} // namespace

#if defined(COUNT)
constexpr bool kZeroCopy = false; // (`count.hpp` prints it; `main`'s executor wrote in place, but no gate reads this arm)
#include "count.hpp"
#endif

int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
#if defined(COUNT)
    return argc > 1 ? faults(launder(argv[1][0] == 't')) : count();
#elif defined(CHECK)
    return check<Arm, HandArm<w::Hand>>(bound);
#else
    Buffers buffers;
    Arm arm(launder(w::kFrames));
    arm.bind(buffers);
    for (int r = 0; r < 4; ++r) {
        arm.run(launder(w::kFrames));
    }
    return 0;
#endif
}
