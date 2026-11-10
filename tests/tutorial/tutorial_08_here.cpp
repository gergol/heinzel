// Tutorial chapter 08 — "autoscheduling and deployment"
// (tutorial/08-autoscheduling-and-deployment.md), built in THIS tree's language, run on `Executor`, and
// its samples printed. Grown one step at a time alongside `tutorial_08_there.cpp` (M8.3, S5c), each
// output-compared to the same graph on the old tree at c4012b8.
//
// WHAT THIS CHAPTER IS, AND WHY THE OLD SPELLING DOES NOT PORT LINE-FOR-LINE. The old chapter shows
// (1) one graph scheduled for two machine models (a cost model that reads TargetInfo{cores, caches,
// SIMD width} and decides tiles/fusion/threading), (2) a parallel executor byte-identical to serial,
// (3) the pinned `compile<G,T>()` + `Executor<>` deployment spelling. This tree's planner reads a
// `Machine` (vector width, caches, cores, a fused multiply-add) and decides lanes, unrolling, bands and
// splits from it. So the SUBJECT re-expresses in this tree's own spelling (Decision 27): "the same
// graph, two machine models, same numbers" becomes two Machines giving two programs with the same
// numbers, and the deployment path is `Executor<graph, target>`, whose program's facts are compile-time
// constants. The numeric spine is the row RMS-normalize graph, its output compared to the old tree's; the
// two-machine-models and pinned-deployment demonstrations are internal checks that do not change it.
//
// The graph: row RMS-normalize, y = x * rsqrt(mean(x*x over the columns, keepdims) + 1e-6f). The old
// chapter's x{R,C} reduces over the columns; here x is the tensor {R,C} (recorded {R,C,1}), the columns
// are its axis 1, and the keepdims mean-square is {R,1,1} and broadcasts back over the columns for the
// multiply (the ch03-cent broadcast path, MB). rsqrt names op::Rsqrt (M4, closed); mean lowers to
// op::Reduce (M5, closed); nothing new is spelled.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "heinzel/compile.hpp"
#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t R = 6; // the chapter's normalize rows/cols, kept small for the port
constexpr std::int32_t C = 8;
constexpr std::int32_t kIn = R * C; // the numbers of x and of y

// y = x * rsqrt(mean(x*x, columns, keepdims) + 1e-6f). The mean-square is {R,1,1} and broadcasts back
// over the columns (MB operand broadcast, the same path ch03's `cent` used for its keepdims mean). At
// `High`, the reference walk's arithmetic, so each run is checked against the walk bit for bit.
constexpr Graph build_norm() {
    Input x{"x", {R, C}};
    Expr ms = (x * x).mean(1, keepdims);
    Expr rn = rsqrt(ms + 1e-6f);
    return finish(High, Output{"y"} = x * rn);
}

// ---- the chapter's subject, re-expressed in this tree's spelling (S5c c2) -------------------------
//
// The old chapter's parts (1) "one graph, two machine models, same numbers" and (2) "byte-identical
// across runners" have no cost model to compare across the two trees. In THIS tree they are properties
// it can actually run: two Machines (the default and zen2) give two genuinely different programs, and
// the graph run on both — at `High`, where the loop tree computes as the walk does — and walked lands on
// one set of numbers. These do not touch the printed output the cross-tree compare reads.

// (1) Two machine models: two different programs, so the agreement in `main` is not vacuous.
static_assert(!(compile(build_norm()).program == compile(build_norm(), zen2).program),
              "the two machines produced the same program — the two-machines claim would be vacuous");

// (3) The pinned deployment shape. The old chapter's part 3 names a compiled artifact and reads its
// facts for firmware budgeting ("N nodes, arena M elems"). Here the artifact is the program compiled
// for a pinned graph and machine; its facts — stages, statements, buffers — are compile-time CONSTANTS.
static_assert(compile(build_norm(), zen2).program.stages.size() > 0 &&
                  compile(build_norm(), zen2).program.buffers.size() > 0,
              "the pinned artifact has no stages or no buffers");

// The graph on the loop tree, for the machine `M`, checked against the walk. Its input is F32, so the
// buffers are float.
template <const Machine& M>
std::array<float, kIn> norm_executed(const std::array<float, kIn>& x) {
    std::array<float, kIn> y{};
    Executor<build_norm, M> ex;
    testing::held(ex);
    ex.input("x") = x.data();
    ex.output("y") = y.data();
    ex.run();
    testing::require_walked("y", y, testing::walked<build_norm>({{"x", x.data(), kIn}})[0]);
    return y;
}

} // namespace

int main() {
    std::array<float, kIn> x{};
    for (std::int32_t i = 0; i < kIn; ++i) {
        x[static_cast<std::size_t>(i)] = 0.37f * static_cast<float>(i) - 2.0f;
    }
    // (2) Byte-identical across both runners, and across both machines: each run is checked against the walk.
    const std::array<float, kIn> y = norm_executed<default_machine>(x);
    norm_executed<zen2>(x);
    for (const float v : y) { // the row RMS-normalize output, {R,C}
        std::printf("%.9e\n", static_cast<double>(v));
    }
    return 0;
}
