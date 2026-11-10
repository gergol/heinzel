// Milestone MB, S4b/S4c — the general contraction (`_matmul`) RUNS, and its two operands are read at
// DIFFERENT index maps, on both runners, which must agree.
//
// The shape rule and the refusals were S4a. A matmul is the first operation here whose two operands are
// not read at one shared join index: `out[i,j] = Σ_k a[i,k]·b[k,j]` reads `a` at `(i,k)` and `b` at
// `(k,j)`, and the contracted axis sits at a DIFFERENT position in each. So the test is built on operands
// whose elements are all distinct — a mis-index, a transposed read, or a wrong-operand read would land on
// a different number and change the result — and the walk's answer is compared element-for-element
// against a hand reference that computes the same sum of products directly. The two operands are two
// independently-fed named inputs (the MW wide/multi-input surface), which is what lets the elements be
// distinct rather than the equal siblings a scalar-derived wide value carries.
//
// The loop tree (`Executor`) is held to the walk on two machines, the default and zen2, whose programs
// differ (lanes, unrolling, a fused multiply-add) — a mapping right only for one is caught by the other.
// Every operand, product and sum here is an exact integer, so no order of a sum can show. The walk is a
// compile-time run; `main` runs it again while the program executes, beside the loop tree. The graph is
// analysed first, so the attribute every runner reads is one `infer` accepted.
#include <array>
#include <cstddef>
#include <cstdint>

#include "heinzel/executor.hpp"
#include "heinzel/sema/analyze.hpp"

#include "executed.hpp"
#include "hand_built_graph.hpp"
#include "interpret.hpp"

using namespace heinzel;

namespace {

constexpr std::size_t kBlock = 2; // two samples, so the sample loop is exercised, not just element zero

// ---- a transposing contraction "ik,kj->ij": a[I=2,K=3] against b[K=3,J=2] -> out[I=2,J=2] ----------

constexpr Graph ik_kj_graph() {
    Graph g{};
    const auto a = add_value(g, ElemType::F64, Shape{2, 3, static_cast<std::int32_t>(kBlock)}, ValueCategory::Input);
    const auto b = add_value(g, ElemType::F64, Shape{3, 2, static_cast<std::int32_t>(kBlock)}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::MatMul;
    k.matmul.a_contract = 1; // a's K axis
    k.matmul.b_contract = 0; // b's K axis
    const auto y = add_node(g, op::MatMul, a, b, kNoValue, k);
    g.inputs.push_back(NamedValue{"a", a});
    g.inputs.push_back(NamedValue{"b", b});
    g.outputs.push_back(NamedValue{"y", y});
    if (!infer(g).ok) {
        fail("ik,kj->ij did not resolve");
    }
    return g;
}
constexpr Graph kIkKj = ik_kj_graph();
static_assert(kIkKj.inputs.size() == 2 && kIkKj.values[static_cast<std::size_t>(kIkKj.outputs[0].value)].shape ==
                                              Shape{2, 2, static_cast<std::int32_t>(kBlock)});

// Distinct per (i,k)/(k,j) and per sample, so a wrong index shows. a's element e = i*3 + k; b's e = k*2 + j.
constexpr double a_val(std::int32_t i, std::int32_t k, std::size_t n) {
    return 1.0 + static_cast<double>(i * 3 + k) + 10.0 * static_cast<double>(n);
}
constexpr double b_val(std::int32_t k, std::int32_t j, std::size_t n) {
    return 1.0 + static_cast<double>(k * 2 + j) + 20.0 * static_cast<double>(n);
}

// The two feeds, sample-major, built once and shared by both runners so they cannot disagree about the
// input rather than the operation. a_feed[n*6 + (i*3+k)], b_feed[n*6 + (k*2+j)].
struct IkKjFeeds {
    std::array<double, 3 * 2 * kBlock> a{}; // width I*K = 6
    std::array<double, 3 * 2 * kBlock> b{}; // width K*J = 6
};
constexpr IkKjFeeds ik_kj_feeds() {
    IkKjFeeds f{};
    for (std::size_t n = 0; n < kBlock; ++n) {
        for (std::int32_t i = 0; i < 2; ++i) {
            for (std::int32_t k = 0; k < 3; ++k) {
                f.a[n * 6 + static_cast<std::size_t>(i * 3 + k)] = a_val(i, k, n);
            }
        }
        for (std::int32_t k = 0; k < 3; ++k) {
            for (std::int32_t j = 0; j < 2; ++j) {
                f.b[n * 6 + static_cast<std::size_t>(k * 2 + j)] = b_val(k, j, n);
            }
        }
    }
    return f;
}
constexpr Inputs ik_kj_inputs(const IkKjFeeds& f) {
    Inputs feeds{};
    feeds.push_back(InputFeed{f.a.data(), 6});
    feeds.push_back(InputFeed{f.b.data(), 6});
    return feeds;
}

// The walk's output for the whole block, element e = i*2 + j.
constexpr std::array<double, 4 * kBlock> ik_kj_walked() {
    const IkKjFeeds f = ik_kj_feeds();
    Signals<kBlock> out{};
    run(kIkKj, ik_kj_inputs(f), out);
    std::array<double, 4 * kBlock> got{};
    for (std::size_t n = 0; n < kBlock; ++n) {
        for (std::int32_t e = 0; e < 4; ++e) {
            got[n * 4 + static_cast<std::size_t>(e)] = sample_at(out, kIkKj.outputs[0].value, e, n);
        }
    }
    return got;
}

// The loop tree's output for the graph `Build` makes, fed `a` and `b`, on the machine `M`.
template <auto Build, const Machine& M, std::size_t Width>
std::array<double, Width * kBlock> executed(const double* a, const double* b) {
    Executor<Build, M> ex;
    testing::held(ex);
    std::array<double, Width * kBlock> got{};
    ex.input("a") = a;
    ex.input("b") = b;
    ex.output("y") = got.data();
    ex.run();
    return got;
}

template <const Machine& M>
std::array<double, 4 * kBlock> ik_kj_executed() {
    const IkKjFeeds f = ik_kj_feeds();
    return executed<ik_kj_graph, M, 4>(f.a.data(), f.b.data());
}

constexpr bool same_block(const std::array<double, 4 * kBlock>& a, const std::array<double, 4 * kBlock>& b) {
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!(a[i] == b[i])) {
            return false;
        }
    }
    return true;
}

// out[i,j,n] = Σ_k a[i,k,n]·b[k,j,n], computed directly from the same values — the reference the walk meets.
constexpr bool ik_kj_matches() {
    const std::array<double, 4 * kBlock> got = ik_kj_walked();
    for (std::size_t n = 0; n < kBlock; ++n) {
        for (std::int32_t i = 0; i < 2; ++i) {
            for (std::int32_t j = 0; j < 2; ++j) {
                double want = 0.0;
                for (std::int32_t k = 0; k < 3; ++k) {
                    want += a_val(i, k, n) * b_val(k, j, n);
                }
                if (!(got[n * 4 + static_cast<std::size_t>(i * 2 + j)] == want)) {
                    return false;
                }
            }
        }
    }
    return true;
}
static_assert(ik_kj_matches(), "the matmul walk read a transposed contraction wrong");

// ---- a BATCHED contraction "bik,bkj->bij": B=2, I=2, K=2, J=2 --------------------------------------

constexpr Graph batched_graph() {
    Graph g{};
    const auto a = add_value(g, ElemType::F64, Shape{2, 2, 2, static_cast<std::int32_t>(kBlock)}, ValueCategory::Input);
    const auto b = add_value(g, ElemType::F64, Shape{2, 2, 2, static_cast<std::int32_t>(kBlock)}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::MatMul;
    k.matmul.a_contract = 2; // a's K axis (B=0, I=1, K=2)
    k.matmul.b_contract = 1; // b's K axis (B=0, K=1, J=2)
    k.matmul.batch_a = Axes{0};
    k.matmul.batch_b = Axes{0};
    const auto y = add_node(g, op::MatMul, a, b, kNoValue, k);
    g.inputs.push_back(NamedValue{"a", a});
    g.inputs.push_back(NamedValue{"b", b});
    g.outputs.push_back(NamedValue{"y", y});
    if (!infer(g).ok) {
        fail("bik,bkj->bij did not resolve");
    }
    return g;
}
constexpr Graph kBatched = batched_graph();
static_assert(kBatched.values[static_cast<std::size_t>(kBatched.outputs[0].value)].shape ==
              Shape{2, 2, 2, static_cast<std::int32_t>(kBlock)});

// a's element e = b*4 + i*2 + k; b's e = b*4 + k*2 + j. Distinct per (b,i,k)/(b,k,j) and per sample.
constexpr double ba_val(std::int32_t bb, std::int32_t i, std::int32_t k, std::size_t n) {
    return 1.0 + static_cast<double>(bb * 4 + i * 2 + k) + 10.0 * static_cast<double>(n);
}
constexpr double bb_val(std::int32_t bb, std::int32_t k, std::int32_t j, std::size_t n) {
    return 1.0 + static_cast<double>(bb * 4 + k * 2 + j) + 20.0 * static_cast<double>(n);
}

struct BatchedFeeds {
    std::array<double, 8 * kBlock> a{}; // width B*I*K = 8
    std::array<double, 8 * kBlock> b{}; // width B*K*J = 8
};
constexpr BatchedFeeds batched_feeds() {
    BatchedFeeds f{};
    for (std::size_t n = 0; n < kBlock; ++n) {
        for (std::int32_t bb = 0; bb < 2; ++bb) {
            for (std::int32_t i = 0; i < 2; ++i) {
                for (std::int32_t k = 0; k < 2; ++k) {
                    f.a[n * 8 + static_cast<std::size_t>(bb * 4 + i * 2 + k)] = ba_val(bb, i, k, n);
                }
            }
            for (std::int32_t k = 0; k < 2; ++k) {
                for (std::int32_t j = 0; j < 2; ++j) {
                    f.b[n * 8 + static_cast<std::size_t>(bb * 4 + k * 2 + j)] = bb_val(bb, k, j, n);
                }
            }
        }
    }
    return f;
}
constexpr Inputs batched_inputs(const BatchedFeeds& f) {
    Inputs feeds{};
    feeds.push_back(InputFeed{f.a.data(), 8});
    feeds.push_back(InputFeed{f.b.data(), 8});
    return feeds;
}

constexpr std::array<double, 8 * kBlock> batched_walked() {
    const BatchedFeeds f = batched_feeds();
    Signals<kBlock> out{};
    run(kBatched, batched_inputs(f), out);
    std::array<double, 8 * kBlock> got{};
    for (std::size_t n = 0; n < kBlock; ++n) {
        for (std::int32_t e = 0; e < 8; ++e) {
            got[n * 8 + static_cast<std::size_t>(e)] = sample_at(out, kBatched.outputs[0].value, e, n);
        }
    }
    return got;
}

template <const Machine& M>
std::array<double, 8 * kBlock> batched_executed() {
    const BatchedFeeds f = batched_feeds();
    return executed<batched_graph, M, 8>(f.a.data(), f.b.data());
}

constexpr bool same_block8(const std::array<double, 8 * kBlock>& a, const std::array<double, 8 * kBlock>& b) {
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!(a[i] == b[i])) {
            return false;
        }
    }
    return true;
}

// out[b,i,j,n] = Σ_k a[b,i,k,n]·b[b,k,j,n]; element e = b*4 + i*2 + j.
constexpr bool batched_matches() {
    const std::array<double, 8 * kBlock> got = batched_walked();
    for (std::size_t n = 0; n < kBlock; ++n) {
        for (std::int32_t bb = 0; bb < 2; ++bb) {
            for (std::int32_t i = 0; i < 2; ++i) {
                for (std::int32_t j = 0; j < 2; ++j) {
                    double want = 0.0;
                    for (std::int32_t k = 0; k < 2; ++k) {
                        want += ba_val(bb, i, k, n) * bb_val(bb, k, j, n);
                    }
                    if (!(got[n * 8 + static_cast<std::size_t>(bb * 4 + i * 2 + j)] == want)) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}
static_assert(batched_matches(), "the matmul walk read a batched contraction wrong");

} // namespace

int main() {
    // The walk again while the program executes, not only while it compiles; and the loop tree on both
    // machines lands on the walk's answer, element for element. The batch axis is an ordinary outer axis
    // iterated (not summed), so a runner that dropped or mis-strided it would disagree.
    const bool walks = ik_kj_matches() && batched_matches();
    const bool transposing = same_block(ik_kj_executed<default_machine>(), ik_kj_walked()) &&
                             same_block(ik_kj_executed<zen2>(), ik_kj_walked());
    const bool batched = same_block8(batched_executed<default_machine>(), batched_walked()) &&
                        same_block8(batched_executed<zen2>(), batched_walked());
    return (walks && transposing && batched) ? 0 : 1;
}
