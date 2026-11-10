// Contractions on the loop-nest path, against the triple loop written by hand. Lanes run ACROSS outputs, so
// every output's sum keeps its one order and the comparison is exact: in float with each multiply-add
// FUSED where the machine has the instruction and the precision grants it (`Normal` on zen2; rounded as the build
// rounds, once where it has the instruction), in float
// unfused on a machine without it, and via a double accumulator, never fused, under `High`. Sizes are chosen so that the unrolled rows, the unrolled lanes, the
// single lanes and the last few columns all run: 7 rows against blocks of 6, 19 columns against 16, 8.
// Then a transposed operand, a batched one, and maps over the result (a bias and a relu) run in the
// contraction's own stage, in its lanes.

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/runtime/hardware_twins.hpp"
#include "heinzel/notation/einsum.hpp"
#include "heinzel/sema/analyze.hpp"

#include "canaried.hpp"
#include "edited_schedules.hpp"
#include "planned.hpp"
#include "tree_facts.hpp"

#include <cstdio>
#include <vector>

using namespace heinzel;

namespace {

namespace loops = heinzel::loops;

constexpr std::int32_t kM = 7;
constexpr std::int32_t kK = 5;
constexpr std::int32_t kN = 19;
constexpr std::int32_t kBatch = 3;

constexpr Graph analysed(Graph g) {
    if (!infer(g).ok) {
        fail("nest_contract_test: a graph did not resolve");
    }
    return g;
}

constexpr Graph matmul(numerics::Precision level) {
    Input a{"a", {kM, kK}};
    Input b{"b", {kK, kN}};
    Graph g = finish(Output{"y"} = einsum("ik,kj->ij", a, b));
    set_default_precision(g, level);
    return analysed(g);
}

// y = max(a.b + bias, 0): the two maps join the contraction's stage and run in its lanes.
constexpr Graph linear_relu() {
    Input a{"a", {kM, kK}};
    Input b{"b", {kK, kN}};
    Input bias{"bias", {kN}};
    return analysed(finish(Output{"y"} = max(einsum("ik,kj->ij", a, b) + bias, 0.0)));
}

// a is read transposed (its contracted axis first), and both carry a batch axis.
constexpr Graph transposed_batched() {
    Input a{"a", {kBatch, kK, kM}};
    Input b{"b", {kBatch, kK, kN}};
    return analysed(finish(Output{"y"} = einsum("bki,bkj->bij", a, b)));
}

constexpr Machine kNoFma{}; // the least a 64-bit target has: 16-byte lanes, no fused multiply-add
constexpr Graph kMatmulGraph = matmul(numerics::Precision::Normal);
constexpr Graph kMatmulHighGraph = matmul(numerics::Precision::High);
constexpr planned::Held kMatmulUnfusedHeld{planned::program_of(kMatmulGraph, kNoFma)};
constexpr const loops::Program& kMatmulUnfused = kMatmulUnfusedHeld.program;
constexpr planned::Held kMatmulHeld{planned::program_of(kMatmulGraph, zen2)};
constexpr const loops::Program& kMatmul = kMatmulHeld.program;
constexpr planned::Held kMatmulHighHeld{planned::program_of(kMatmulHighGraph, zen2)};
constexpr const loops::Program& kMatmulHigh = kMatmulHighHeld.program;
constexpr planned::Held kLinearReluHeld{planned::program_of(linear_relu(), zen2)};
constexpr const loops::Program& kLinearRelu = kLinearReluHeld.program;
constexpr planned::Held kBatchedHeld{planned::program_of(transposed_batched(), zen2)};
constexpr const loops::Program& kBatched = kBatchedHeld.program;

// A contraction's stage in the tree: its sums are held in registers (a `Registers` node), and the contracted axis is
// walked inside it. The loops around the sums are the walking nodes that enclose it, outermost first.
constexpr std::int32_t sums_of(const loops::Program& p, std::size_t s) {
    for (std::int32_t n = p.stages[s].root; n < tree_facts::subtree_end(p, s); ++n) {
        if (loops::node_at(p, n).kind == loops::NodeKind::Registers) {
            return n;
        }
    }
    return -1;
}
constexpr fixed_vector<loops::Node, 16> around_the_sums(const loops::Program& p, std::size_t s) {
    fixed_vector<loops::Node, 16> out{};
    const std::int32_t sums = sums_of(p, s);
    for (std::int32_t n = p.stages[s].root + 1; n < sums; ++n) {
        const loops::NodeKind k = loops::node_at(p, n).kind;
        if (loops::encloses(p, n, sums) && (k == loops::NodeKind::Loop || k == loops::NodeKind::Unroll || k == loops::NodeKind::Vector)) {
            out.push_back(loops::node_at(p, n));
        }
    }
    return out;
}
// ...and the first node of kind `k` inside them: the loop over the contracted axis, the lanes, a prefetch.
constexpr loops::Node first_inside_the_sums(const loops::Program& p, std::size_t s, loops::NodeKind k) {
    const std::int32_t sums = sums_of(p, s);
    for (std::int32_t n = sums + 1; sums >= 0 && n < tree_facts::subtree_end(p, s); ++n) {
        if (loops::encloses(p, sums, n) && loops::node_at(p, n).kind == k) {
            return loops::node_at(p, n);
        }
    }
    return loops::Node{};
}
// A Loop over `axis` in steps of `step`: a plain one (serial, or a tile walking chunks), or a strip, whose chunk the
// Unroll and Vector nodes inside it enumerate (rows unrolled, columns in lanes).
constexpr bool loop_of(const loops::Node& n, std::int32_t axis, std::int32_t step) {
    return n.kind == loops::NodeKind::Loop && n.tail == loops::Tail::Clip && n.axis == axis && n.step == step;
}
constexpr bool strip_of(const loops::Node& n, std::int32_t axis, std::int32_t step) {
    return loops::is_strip(n) && n.axis == axis && n.step == step;
}

// Rows unrolled: a strip of rows whose rows the sums enumerate one by one (an Unroll over them in steps of one).
constexpr bool rows_unrolled(const loops::Program& p, const loops::Node& strip, std::int32_t rows) {
    const loops::Node unroll = first_inside_the_sums(p, 0, loops::NodeKind::Unroll);
    return strip_of(strip, 0, rows) && unroll.axis == 0 && unroll.step == 1;
}

// What was decided: t, then rows unrolled by 6 (a strip of six), then columns in lanes of 8 unrolled by 2 (a strip of
// sixteen) around the sums, and the contracted axis innermost — twelve accumulators, two loaded lanes and one
// broadcast: fifteen registers. Four loops: the three around the sums and the contracted one.
constexpr bool rows_by_six_around_lanes_by_two(const loops::Program& p, std::int32_t lanes) {
    const fixed_vector<loops::Node, 16> around = around_the_sums(p, 0);
    const loops::Node lane = first_inside_the_sums(p, 0, loops::NodeKind::Vector);
    return p.stages.size() == 1 && tree_facts::count(p, 0, loops::NodeKind::Loop) == 4 && around.size() == 3 && loop_of(around[0], 2, 1) &&
           rows_unrolled(p, around[1], 6) && strip_of(around[2], 1, 2 * lanes) && lane.axis == 1 && lane.step == lanes &&
           loop_of(first_inside_the_sums(p, 0, loops::NodeKind::Loop), 3, 1);
}
static_assert(rows_by_six_around_lanes_by_two(kMatmul, 8), "rows unrolled by six around lanes of eight unrolled by two around the contracted axis");
static_assert(kLinearRelu.stages.size() == 1, "the bias and the relu run in the contraction's stage");
static_assert(tree_facts::stmt(kMatmul, 0, 2).fused && !tree_facts::stmt(kMatmulHigh, 0, 2).fused && !tree_facts::stmt(kMatmulUnfused, 0, 2).fused &&
                  first_inside_the_sums(kMatmulUnfused, 0, loops::NodeKind::Vector).step == 4,
              "the multiply-add is fused where the machine can and the precision grants it, and nowhere else");

// The same contraction, and the one with a bias and a relu after it, on a machine whose caches are tiny,
// so that 50 x 21 x 19 is chunked: the contracted axis in chunks of 8 (8, 8, 5 — partial sums resumed
// from the result), the rows in chunks of 30, the columns walked outside the rows.
constexpr std::int32_t kTallM = 50;
constexpr std::int32_t kLongK = 21;
using edited_schedules::kTinyCaches;
constexpr Graph tall(bool with_relu) {
    Input a{"a", {kTallM, kLongK}};
    Input b{"b", {kLongK, kN}};
    Input bias{"bias", {kN}};
    Expr product = einsum("ik,kj->ij", a, b);
    return analysed(finish(Output{"y"} = with_relu ? max(product + bias, 0.0) : product + 0.0 * bias));
}
constexpr Graph kTall = tall(false);
constexpr Graph kTallRelu = tall(true);
// (The planner also relays the second operand — it outgrows the first-level cache — which is the next program; here
// that one decision is taken back in its schedule, so the chunks are seen over the operand as it was fed.)
using edited_schedules::fed_as_it_is;
constexpr loops::Schedule kTallRelaid = planned::schedule_of(kTallRelu, kTinyCaches);
constexpr planned::Held kChunkedHeld{planned::program_of(kTall, fed_as_it_is(planned::schedule_of(kTall, kTinyCaches)))};
constexpr const loops::Program& kChunked = kChunkedHeld.program;
constexpr planned::Held kChunkedReluHeld{planned::program_of(kTallRelu, fed_as_it_is(kTallRelaid))};
constexpr const loops::Program& kChunkedRelu = kChunkedReluHeld.program;
// (Six loops: five around the sums — the t loop, two tiles, the strips of the lanes and of the unrolled rows — and the
// contracted one.)
constexpr bool chunked_around_the_sums(const loops::Program& p) {
    const fixed_vector<loops::Node, 16> around = around_the_sums(p, 0);
    return tree_facts::count(p, 0, loops::NodeKind::Loop) == 6 && around.size() == 5 && loop_of(around[1], 3, 8) && loop_of(around[2], 0, 30) &&
           strip_of(around[3], 1, 16) && first_inside_the_sums(p, 0, loops::NodeKind::Vector).axis == 1 && rows_unrolled(p, around[4], 6) &&
           loop_of(first_inside_the_sums(p, 0, loops::NodeKind::Loop), 3, 1);
}
static_assert(chunked_around_the_sums(kChunkedRelu), "chunks of the contracted axis, chunks of rows, columns in lanes, rows unrolled, the contracted axis");
static_assert(first_inside_the_sums(kChunkedRelu, 0, loops::NodeKind::Prefetch).kind == loops::NodeKind::Prefetch && kChunkedRelu.line_bytes == 64 &&
                  tree_facts::count(kMatmul, 0, loops::NodeKind::Prefetch) == 0,
              "with the columns outside the rows, blocks a (tiny) page apart ask for the next block's result lines; in memory order nothing does");

// As planned: the second operand outgrows the first-level cache, so it is read from a RELAID copy —
// blocks of 16 columns, the last one (19 = 16 + 3) zero-padded and run as a tail.
constexpr planned::Held kRelaidHeld{planned::program_of(kTallRelu, kTallRelaid)};
constexpr const loops::Program& kRelaid = kRelaidHeld.program;
static_assert(kRelaid.stages.size() == 2 && kRelaid.stages[0].count == 1 && tree_facts::stmt(kRelaid, 0, 0).kind == loops::StmtKind::Load &&
                  kRelaid.buffers[static_cast<std::size_t>(tree_facts::stmt(kRelaid, 0, 0).store.buffer)].blocked_axis == 1 &&
                  kRelaid.buffers[static_cast<std::size_t>(tree_facts::stmt(kRelaid, 0, 0).store.buffer)].block == 16,
              "a stage of its own copies the operand into blocks of sixteen columns, and the contraction reads those");

// The copy has to pay (zen2: read twelve blocks of six rows over). What counts is ONE sample's strips —
// not the feed across time — and how often the rows go by them; and a program that would have no room
// for every copy's stage is planned with as many as fit rather than refused.
constexpr Graph product(std::int32_t m, std::int32_t k, std::int32_t n, std::int32_t samples) {
    Input a{"a", Signal{m, k, samples}};
    Input b{"b", Signal{k, n, samples}};
    return analysed(finish(Output{"y"} = einsum("ik,kj->ij", a, b)));
}
static_assert(planned::schedule_of(product(128, 256, 256, 1), zen2).stages[0].relaid == 1, "256 K of strips, read by 128 rows: copied");
// A contracted axis of no elements, or a symbolic one no chunk of which fits the first level, gets no tiles (both
// planners once divided by zero there — review of loop-tree M3).
constexpr Graph empty_product() { // a tensor may have an axis of no elements; a signal's extents are at least one
    Input a{"a", {64, 0}};
    Input b{"b", {0, 64}};
    return analysed(finish(Output{"y"} = einsum("ik,kj->ij", a, b)));
}
constexpr Graph symbolic_product() {
    // a symbolic element axis, which no declaration spells (DS-18)
    const DimDecl k{0, DimConstraints{1, 1}};
    Expr a = detail::input("a", Shape{64, Extent{Dim{k.id}}, 1}, ElemType::F32, k);
    Expr b = detail::input("b", Shape{Extent{Dim{k.id}}, 64, 1}, ElemType::F32, k);
    return analysed(finish(Output{"y"} = einsum("ik,kj->ij", a, b)));
}
constexpr Machine kSmallFirstLevel{.vector_bytes = 32, .registers = 16, .l1_bytes = 1024, .l2_bytes = 65536, .line_bytes = 128};
static_assert(planned::schedule_of(empty_product(), zen2).stages[0].loops.size() == 4 &&
                  planned::schedule_of(symbolic_product(), kSmallFirstLevel).stages[0].loops.size() == 4,
              "no Tile where the contracted axis has no chunk");
// A machine that knows no line size rounds no strip to one (both planners once divided by zero there — M3 review
// round 4): a strip of 32 bytes, chunks of 32768 / (6 rows x 4 + 32) = 585, 584 in eights.
constexpr Machine kNoLineSize{.line_bytes = 0};
constexpr loops::Schedule::Loop kNoLineSizeTile = planned::schedule_of(product(64, 1024, 64, 1), kNoLineSize).stages[0].loops[1];
static_assert(kNoLineSizeTile.kind == loops::LoopKind::Tile && kNoLineSizeTile.step == 584, "no line size: strips of the columns' bytes");
static_assert(planned::schedule_of(product(1, 256, 256, 1), zen2).stages[0].relaid == -1 &&
                  planned::schedule_of(product(32, 256, 256, 1), zen2).stages[0].relaid == -1,
              "a matrix-vector product, and one of few rows, read the operand as it was fed");
static_assert(planned::schedule_of(product(128, 8, 8, 512), zen2).stages[0].relaid == -1, "256 bytes of strips per sample are not 128 K");

constexpr Graph layers(std::int32_t count, bool one_weight) {
    constexpr const char* names[] = {"w0", "w1", "w2", "w3", "w4", "w5", "w6", "w7", "w8", "w9", "w10", "w11"};
    Expr h = Input{"x", {128, 128}};
    Input shared{"w", {128, 128}};
    for (std::int32_t l = 0; l < count; ++l) {
        h = max(einsum("ik,kj->ij", h, one_weight ? shared : Input{names[l], {128, 128}}), 0.0);
    }
    return analysed(finish(Output{"y"} = h));
}
constexpr planned::Held kTwoLayersHeld{planned::program_of(layers(2, true), zen2)};
constexpr const loops::Program& kTwoLayers = kTwoLayersHeld.program;
static_assert(kTwoLayers.stages.size() == 3 && kTwoLayers.buffers.size() == 5, "one weight read by two contractions is copied once");
constexpr Graph kTwelveLayers = layers(12, false);
constexpr loops::Schedule kDeep = planned::schedule_of(kTwelveLayers, zen2);
static_assert(kDeep.stages.size() == 12 && kDeep.stages[3].relaid == 1 && kDeep.stages[4].relaid == -1,
              "twelve layers and four copies are the sixteen stages a program holds: the other weights are read as fed");
static_assert(planned::program_of(kTwelveLayers, kDeep).stages.size() == loops::kMaxStages, "and it lowers");

int g_wrong = 0;

std::vector<float> noise(std::size_t n, std::uint32_t seed) {
    std::vector<float> v(n);
    std::uint32_t state = seed;
    for (float& x : v) {
        state = state * 1664525u + 1013904223u;
        x = static_cast<float>(static_cast<std::int32_t>(state >> 8) % 2001 - 1000) * 0.00173f;
    }
    return v;
}

void expect(const char* what, std::size_t at, float got, float want) {
    if (got != want && ++g_wrong <= 10) {
        std::printf("  %s [%zu]: got %a, want %a\n", what, at, static_cast<double>(got), static_cast<double>(want));
    }
}

// One step of an output's sum, as each variant is owed it.
struct Fused {
    float operator()(float sum, float a, float b) const { return multiply_add_as_the_machine_rounds(a, b, sum); }
};
struct Unfused {
    float operator()(float sum, float a, float b) const { return sum + a * b; }
};
struct ViaDouble {
    double operator()(double sum, float a, float b) const { return sum + static_cast<double>(a) * static_cast<double>(b); }
};

template <const loops::Program& P, class Step>
void check_matmul(const char* what) {
    const std::vector<float> a = noise(kM * kK, 1);
    const std::vector<float> b = noise(kK * kN, 2);
    canaried::Workspace<P> work(loops::Sizes{});
    canaried::run<P>(loops::Feeds{a.data(), b.data()}, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(P.outputs[0]));
    for (std::size_t i = 0; i < kM; ++i) {
        for (std::size_t j = 0; j < kN; ++j) {
            auto sum = Step{}(0.0f, a[i * kK], b[j]);
            for (std::size_t k = 1; k < kK; ++k) {
                sum = Step{}(sum, a[i * kK + k], b[k * kN + j]);
            }
            expect(what, i * kN + j, y[i * kN + j], static_cast<float>(sum));
        }
    }
}

void check_linear_relu() {
    const std::vector<float> a = noise(kM * kK, 3);
    const std::vector<float> b = noise(kK * kN, 4);
    const std::vector<float> bias = noise(kN, 5);
    canaried::Workspace<kLinearRelu> work(loops::Sizes{});
    canaried::run<kLinearRelu>(loops::Feeds{a.data(), b.data(), bias.data()}, work);
    const float* y = work.data<float>(static_cast<std::size_t>(kLinearRelu.outputs[0]));
    for (std::size_t i = 0; i < kM; ++i) {
        for (std::size_t j = 0; j < kN; ++j) {
            float sum = 0.0f;
            for (std::size_t k = 0; k < kK; ++k) {
                sum = Fused{}(sum, a[i * kK + k], b[k * kN + j]);
            }
            const float biased = sum + bias[j];
            expect("linear_relu", i * kN + j, y[i * kN + j], biased > 0.0f ? biased : 0.0f);
        }
    }
}

// Row-by-row dot products, `Q . K^T`: the second operand strides along the outputs' lane axis, so the
// lanes run along the CONTRACTED axis instead — which reorders each sum, so the data are small whole
// numbers, whose sums are exact in any order (19 = two lanes of eight and three single steps).
constexpr Graph row_dots() {
    Input q{"q", {kM, kN}};
    Input k{"k", {kK, kN}};
    return analysed(finish(Output{"y"} = einsum("qd,kd->qk", q, k)));
}
constexpr planned::Held kRowDotsHeld{planned::program_of(row_dots(), zen2)};
constexpr const loops::Program& kRowDots = kRowDotsHeld.program;
static_assert(first_inside_the_sums(kRowDots, 0, loops::NodeKind::Vector).axis == 3 && loop_of(around_the_sums(kRowDots, 0)[1], 0, 1) &&
                  loop_of(around_the_sums(kRowDots, 0)[2], 1, 1),
              "no lane across outputs an operand strides along: lanes along the contracted axis, as a fold's");

void check_row_dots() {
    std::vector<float> q(kM * kN);
    std::vector<float> k(kK * kN);
    for (std::size_t i = 0; i < q.size(); ++i) {
        q[i] = static_cast<float>(static_cast<int>(i * 7 % 17) - 8);
    }
    for (std::size_t i = 0; i < k.size(); ++i) {
        k[i] = static_cast<float>(static_cast<int>(i * 5 % 13) - 6);
    }
    canaried::Workspace<kRowDots> work(loops::Sizes{});
    canaried::run<kRowDots>(loops::Feeds{q.data(), k.data()}, work);
    const float* y = work.data<float>(static_cast<std::size_t>(kRowDots.outputs[0]));
    for (std::size_t i = 0; i < kM; ++i) {
        for (std::size_t j = 0; j < kK; ++j) {
            float sum = 0.0f;
            for (std::size_t d = 0; d < kN; ++d) {
                sum += q[i * kN + d] * k[j * kN + d];
            }
            expect("row dots", i * kK + j, y[i * kK + j], sum);
        }
    }
}

void check_batched() {
    const std::vector<float> a = noise(kBatch * kK * kM, 6);
    const std::vector<float> b = noise(kBatch * kK * kN, 7);
    canaried::Workspace<kBatched> work(loops::Sizes{});
    canaried::run<kBatched>(loops::Feeds{a.data(), b.data()}, work);
    const float* y = work.data<float>(static_cast<std::size_t>(kBatched.outputs[0]));
    for (std::size_t n = 0; n < kBatch; ++n) {
        for (std::size_t i = 0; i < kM; ++i) {
            for (std::size_t j = 0; j < kN; ++j) {
                float sum = 0.0f;
                for (std::size_t k = 0; k < kK; ++k) {
                    sum = Fused{}(sum, a[(n * kK + k) * kM + i], b[(n * kK + k) * kN + j]);
                }
                expect("transposed, batched", (n * kM + i) * kN + j, y[(n * kM + i) * kN + j], sum);
            }
        }
    }
}

template <const loops::Program& P>
void check_chunked(const char* what, bool with_relu) {
    const std::vector<float> a = noise(kTallM * kLongK, 8);
    const std::vector<float> b = noise(kLongK * kN, 9);
    const std::vector<float> bias = noise(kN, 10);
    canaried::Workspace<P> work(loops::Sizes{});
    canaried::run<P>(loops::Feeds{a.data(), b.data(), bias.data()}, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(P.outputs[0]));
    for (std::size_t i = 0; i < kTallM; ++i) {
        for (std::size_t j = 0; j < kN; ++j) {
            float sum = 0.0f;
            for (std::size_t k = 0; k < kLongK; ++k) {
                sum = Fused{}(sum, a[i * kLongK + k], b[k * kN + j]);
            }
            const float biased = sum + bias[j];
            expect(what, i * kN + j, y[i * kN + j], with_relu ? (biased > 0.0f ? biased : 0.0f) : sum + 0.0f * bias[j]);
        }
    }
}

} // namespace

int main() {
    check_matmul<kMatmul, Fused>("matmul");
    check_matmul<kMatmulUnfused, Unfused>("matmul on a machine without fma");
    check_matmul<kMatmulHigh, ViaDouble>("matmul at High");
    check_linear_relu();
    check_batched();
    check_row_dots();
    check_chunked<kChunked>("chunked", false);
    check_chunked<kChunkedRelu>("chunked, then a bias and a relu", true);
    check_chunked<kRelaid>("chunked, an operand relaid, then a bias and a relu", true);
    if (g_wrong != 0) {
        std::printf("%d elements are wrong\n", g_wrong);
        return 1;
    }
    return 0;
}
