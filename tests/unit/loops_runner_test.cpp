// The loop-tree runner on trees built by hand (`Plan/archive/13-loop-tree/04` §3, M1.8), against loops written by hand, bit
// for bit — the runner is general, not a replay of one skeleton:
//   norm      a row's mean and, after the fold, a second loop over the row reading it: sibling loops in one stage,
//             the second in lanes, the fold's value made at width 1 and splatted (the shape M5.1 builds on);
//   jammed    a register tile's bodies enumerated columns-first, equal in value to rows-first (the order is
//             performance only: `02` §4.3 point 4);
//   clipped   a Clip loop over one axis, in chunks, inside a strip over another;
//   in place  a running total in memory: a stage that stores the value of no axis it reads reads it where it stores it,
//             never the copy a still read takes when its stage is placed (`walk.hpp` `still_slots`);
//   per chunk a band of a row a chunk, whose second stage reads the value of no axis its first writes at every row: a
//             copy taken anew in every chunk, where the band places the stage.
// Nothing but `loops/` and its allowlist is in sight (`loops_alone`).

#include "heinzel/loops/run.hpp"
#include "heinzel/loops/workspace.hpp"

#include "hand_built_tree.hpp"
#include "well_formed.hpp"

#include <bit>
#include <cstdio>
#include <cstring>
#include <vector>

namespace loops = heinzel::loops;
namespace op = heinzel::op;
using namespace hand_built_tree;

namespace {

int g_wrong = 0;

constexpr std::int32_t kColumns = 37;
constexpr std::int32_t kM = 5;
constexpr std::int32_t kK = 7;
constexpr std::int32_t kN = 19;
constexpr std::int32_t kRows = 6;

// Sibling loops: y[c, r] = x[c, r] - mean over c of x[., r].
constexpr Program norm_after_fold() {
    Program p{};
    p.buffers.push_back(feed(Shape{kColumns, Dim{0}}));
    p.buffers.push_back(made(Shape{kColumns, Dim{0}}, 5));
    p.stmts.push_back(load(read(0, {0, 1})));
    p.stmts.push_back(fold(ReduceOp::Mean, local(0), Access{}));
    p.stmts.push_back(load(read(0, {0, 1})));
    p.stmts.push_back(map(op::Sub, local(2), local(1), read(1, {0, 1})));
    const std::int32_t regs = add(p, registers(Role::Accumulator, 1, -1), add(p, loop(1, 1), stage(p, {kColumns, Dim{0}}, 4)));
    add(p, leaf(NodeKind::Init, 1), regs);
    const std::int32_t fold_loop = add(p, loop(0, 1), regs);
    add(p, leaf(NodeKind::Compute, 0), fold_loop);
    add(p, leaf(NodeKind::Accumulate, 1), fold_loop);
    add(p, leaf(NodeKind::Finish, 1), regs);
    const std::int32_t lanes = add(p, walker(NodeKind::Vector, 0, 8), add(p, loop(0, 8, Tail::Ones, 0), regs));
    add(p, leaf(NodeKind::Compute, 2), lanes);
    add(p, leaf(NodeKind::Compute, 3), lanes);
    p.outputs.push_back(1);
    return p;
}

// C = A · B over (i, j, t, k): two rows by two lanes of eight per block, the columns' last block masked.
// `columns_first`: the bodies enumerated columns outside rows.
constexpr std::int32_t register_tile_chain(Program& p, std::int32_t parent, bool columns_first) {
    const Node rows = walker(NodeKind::Unroll, 0, 1);
    const Node columns = walker(NodeKind::Unroll, 1, 8);
    const std::int32_t outer = add(p, columns_first ? columns : rows, parent);
    return add(p, walker(NodeKind::Vector, 1, 8), add(p, columns_first ? rows : columns, outer));
}

constexpr Program register_tile(bool columns_first) {
    Program p{};
    p.buffers.push_back(feed(Shape{kM, kK, 1}));
    p.buffers.push_back(loops::Buffer{.type = ElemType::F32, .shape = Shape{kK, kN, 1}, .home = Home::Feed, .feed = 1, .value = 1});
    p.buffers.push_back(made(Shape{kM, kN, 1}, 2));
    p.stmts.push_back(load(read(0, {0, -1, 2, 1})));
    p.stmts.push_back(load(read(1, {-1, 1, 2, 0})));
    loops::Stmt product = fold(ReduceOp::Sum, local(0), read(2, {0, 1, 2, -1}));
    product.collapse = heinzel::AttrKind::MatMul, product.in[1] = local(1);
    p.stmts.push_back(product);
    const std::int32_t block = add(p, loop(1, 16, Tail::Masked, 1), add(p, loop(0, 2, Tail::Ones, 0), add(p, loop(2, 1), stage(p, {kM, kN, 1, kK}, 3))));
    const std::int32_t regs = add(p, registers(Role::Accumulator, 2, -1), block);
    add(p, leaf(NodeKind::Init, 2), register_tile_chain(p, regs, columns_first));
    const std::int32_t k = add(p, loop(3, 1), regs);
    const std::int32_t head = static_cast<std::int32_t>(p.nodes.size());
    const std::int32_t update = register_tile_chain(p, k, columns_first);
    p.nodes[static_cast<std::size_t>(regs)].link = head;
    add(p, leaf(NodeKind::Compute, 0), update);
    add(p, leaf(NodeKind::Compute, 1), update);
    add(p, leaf(NodeKind::Accumulate, 2), update);
    add(p, leaf(NodeKind::Finish, 2), register_tile_chain(p, regs, columns_first));
    p.outputs.push_back(2);
    return p;
}

// y = x * 2 + 1 over [19, 6]: lanes of eight along the columns, and inside that strip the rows in chunks of four.
constexpr Program clip_inside_a_strip() {
    Program p{};
    p.buffers.push_back(feed(Shape{kN, kRows}));
    p.buffers.push_back(made(Shape{kN, kRows}, 3));
    p.stmts.push_back(load(read(0, {0, 1})));
    const Access two{.source = Source::Immediate, .immediate = std::bit_cast<std::uint64_t>(2.0)};
    const Access one{.source = Source::Immediate, .immediate = std::bit_cast<std::uint64_t>(1.0)};
    p.stmts.push_back(map(op::Mul, local(0), two));
    p.stmts.push_back(map(op::Add, local(1), one, read(1, {0, 1})));
    const std::int32_t strip = add(p, loop(0, 8, Tail::Ones, 0), stage(p, {kN, kRows}, 3));
    const std::int32_t lanes = add(p, walker(NodeKind::Vector, 0, 8), add(p, loop(1, 1), add(p, loop(1, 4), strip)));
    for (std::int32_t s = 0; s < 3; ++s) {
        add(p, leaf(NodeKind::Compute, s), lanes);
    }
    p.outputs.push_back(1);
    return p;
}

// t = t + x[c], y[c] = t: a value of no axis stored at every column and read back at the next.
constexpr Program running_total() {
    Program p{};
    p.buffers.push_back(feed(Shape{kN}));
    p.buffers.push_back(made(Shape{1}, 1));
    p.buffers.push_back(made(Shape{kN}, 2));
    p.stmts.push_back(load(read(1, {})));
    p.stmts.push_back(map(op::Add, local(0), read(0, {0}), read(1, {})));
    p.stmts.push_back(load(local(1), read(2, {0})));
    const std::int32_t columns = add(p, loop(0, 1), stage(p, {kN}, 3));
    for (std::int32_t s = 0; s < 3; ++s) {
        add(p, leaf(NodeKind::Compute, s), columns);
    }
    p.outputs.push_back(2);
    return p;
}

// A band of one row a chunk over [19, 6]: t = x[0, r] at each row r, then y[c, r] = x[c, r] + t.
constexpr Program row_by_row() {
    Program p{};
    p.buffers.push_back(feed(Shape{kN, kRows}));
    p.buffers.push_back(made(Shape{1}, 1));
    p.buffers.push_back(made(Shape{kN, kRows}, 2));
    p.stmts.push_back(load(read(0, {1}), read(1, {})));
    const std::int32_t rows = add(p, loop(0, 1), stage(p, {kRows}, 1));
    add(p, leaf(NodeKind::Compute, 0), rows);
    p.stmts.push_back(load(read(1, {})));
    p.stmts.push_back(load(read(0, {0, 1})));
    p.stmts.push_back(map(op::Add, local(1), local(0), read(2, {0, 1})));
    const std::int32_t columns = add(p, loop(0, 1), add(p, loop(1, 1), stage(p, {kN, kRows}, 3)));
    for (std::int32_t s = 0; s < 3; ++s) {
        add(p, leaf(NodeKind::Compute, s), columns);
    }
    p.stages[0].band_axis = 0;
    p.stages[1].band_axis = 1;
    p.bands.push_back(BandInfo{.first = 0, .count = 2, .step = 1});
    p.outputs.push_back(2);
    return p;
}

constexpr Program kNorm = norm_after_fold();
constexpr Program kRowsFirst = register_tile(false);
constexpr Program kColumnsFirst = register_tile(true);
constexpr Program kClipped = clip_inside_a_strip();
constexpr Program kRunningTotal = running_total();
constexpr Program kRowByRow = row_by_row();
static_assert(loops::accepted<kNorm>() && loops::accepted<kRowsFirst>() && loops::accepted<kColumnsFirst>() && loops::accepted<kClipped>());
static_assert(loops::accepted<kRunningTotal>() && loops::accepted<kRowByRow>());

std::vector<float> noise(std::size_t n, unsigned seed) {
    std::vector<float> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = static_cast<float>((i * 2654435761u + seed * 97u) % 1000u) / 250.0f - 2.0f;
    }
    return v;
}

void expect_same(const char* what, const float* got, const std::vector<float>& want) {
    for (std::size_t i = 0; i < want.size(); ++i) {
        if (std::memcmp(&got[i], &want[i], sizeof(float)) != 0 && ++g_wrong <= 10) {
            std::printf("  %s, element %zu: got %a, want %a\n", what, i, static_cast<double>(got[i]), static_cast<double>(want[i]));
        }
    }
}

void check_norm() {
    constexpr std::int64_t rows = 3;
    const std::vector<float> x = noise(kColumns * rows, 1);
    std::vector<float> want(x.size());
    for (std::int64_t r = 0; r < rows; ++r) {
        float total = 0.0f;
        for (std::int32_t c = 0; c < kColumns; ++c) {
            total = total + x[static_cast<std::size_t>(r * kColumns + c)];
        }
        const float mean = total / static_cast<float>(kColumns);
        for (std::int32_t c = 0; c < kColumns; ++c) {
            want[static_cast<std::size_t>(r * kColumns + c)] = x[static_cast<std::size_t>(r * kColumns + c)] - mean;
        }
    }
    loops::Workspace<kNorm> work(loops::Sizes{rows});
    loops::run<kNorm>(loops::Feeds{x.data()}, work);
    expect_same("the norm", work.data<float>(1), want);
}

template <const Program& P>
void check_tile(const char* what) {
    const std::vector<float> a = noise(kM * kK, 2);
    const std::vector<float> b = noise(kK * kN, 3);
    std::vector<float> want(kM * kN);
    for (std::int32_t i = 0; i < kM; ++i) {
        for (std::int32_t j = 0; j < kN; ++j) {
            float sum = 0.0f;
            for (std::int32_t k = 0; k < kK; ++k) {
                sum = sum + a[static_cast<std::size_t>(i * kK + k)] * b[static_cast<std::size_t>(k * kN + j)];
            }
            want[static_cast<std::size_t>(i * kN + j)] = sum;
        }
    }
    loops::Workspace<P> work(loops::Sizes{});
    loops::run<P>(loops::Feeds{a.data(), b.data()}, work);
    expect_same(what, work.template data<float>(2), want);
}

void check_clipped() {
    const std::vector<float> x = noise(kN * kRows, 4);
    std::vector<float> want(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        want[i] = x[i] * 2.0f + 1.0f;
    }
    loops::Workspace<kClipped> work(loops::Sizes{});
    loops::run<kClipped>(loops::Feeds{x.data()}, work);
    expect_same("a Clip loop inside a strip", work.data<float>(1), want);
}

void check_running_total() {
    const std::vector<float> x = noise(kN, 5);
    std::vector<float> want(x.size());
    float total = 0.25f;
    for (std::size_t c = 0; c < x.size(); ++c) {
        total = total + x[c];
        want[c] = total;
    }
    loops::Workspace<kRunningTotal> work(loops::Sizes{});
    work.data<float>(1)[0] = 0.25f;
    loops::run<kRunningTotal>(loops::Feeds{x.data()}, work);
    expect_same("a running total in memory", work.data<float>(2), want);
}

void check_row_by_row() {
    const std::vector<float> x = noise(kN * kRows, 6);
    std::vector<float> want(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        want[i] = x[i] + x[i / kN * kN];
    }
    loops::Workspace<kRowByRow> work(loops::Sizes{});
    loops::run<kRowByRow>(loops::Feeds{x.data()}, work);
    expect_same("a band reading what its first stage writes at every row", work.data<float>(2), want);
}

} // namespace

int main() {
    check_norm();
    check_tile<kRowsFirst>("the register tile, rows first");
    check_tile<kColumnsFirst>("the register tile, columns first");
    check_clipped();
    check_running_total();
    check_row_by_row();
    if (g_wrong != 0) {
        std::printf("%d elements are wrong\n", g_wrong);
        return 1;
    }
    return 0;
}
