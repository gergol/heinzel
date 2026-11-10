// M8.2 — the einsum notation lowers to the SAME nodes the tests build by hand, and to nothing else.
//
// A contraction of aligned operands spelled `"c,c->"` must become a `_mul` and a `_reduce` summing the channel
// axis, the graph `(a * b).sum(0)` builds (Decision 78); a reduction `"c->"` one `_reduce` summing that axis; a
// transposition `"ab->ba"` one `_transpose` with the permutation the labels name; and a spec that both drops and
// reorders must become a reduce THEN a transpose — never another kind of node, and never a cost-model
// consultation (there is nothing here to consult one about). Everything is built at COMPILE TIME and handed to
// the analysis pass: a spec that did not lower to a graph the pass accepts is a build failure, not a test that
// runs and reports one. That the emitted node is byte-identical to the hand-built one (the `==` below) is
// what carries M4/M5's already-measured numeric parity across to the notation without re-running it.
#include <cstdint>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/notation/einsum.hpp"
#include "heinzel/sema/analyze.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t kBlock = 64;

constexpr std::int32_t count_op(const Graph& g, OpCode code) {
    std::int32_t n = 0;
    for (const Node& node : g.nodes) {
        if (node.op == code) {
            ++n;
        }
    }
    return n;
}

// The single node of op `code`. Not "the first" — exactly one, so a test that meant to find the only
// contraction cannot silently pass on a graph that grew a second node it did not expect.
constexpr const Node& only_op(const Graph& g, OpCode code) {
    const Node* found = nullptr;
    for (const Node& node : g.nodes) {
        if (node.op == code) {
            if (found != nullptr) {
                fail("notation_test: more than one node of the expected op");
            }
            found = &node;
        }
    }
    if (found == nullptr) {
        fail("notation_test: no node of the expected op");
    }
    return *found;
}

constexpr Graph resolved(Graph g) {
    if (!infer(g).ok) {
        fail("notation_test: the notation graph did not resolve");
    }
    return g;
}

// ---- contraction: "c,c->" sums the channel axis of two elementwise-aligned wide operands --------------
// A product and a sum, node for node the graph `(a * b).sum(0)` builds: equal once the analysis has checked and
// cleared the ranks the spec named. So is the spec's `"bf,bf->b"` over tensors, `(a * b).sum(1)`, and one that turns
// what it keeps, a sum and then a transpose.
template <int Spelling>
constexpr Graph aligned_contraction() {
    Input a{"a", Signal{3, kBlock}};
    Input b{"b", Signal{3, kBlock}};
    Output y{"y"};
    if constexpr (Spelling == 0) {
        y = einsum("c,c->", a, b);
    } else {
        Expr product = a * b;
        y = product.sum(0);
    }
    return resolved(finish(y));
}
template <int Spelling>
constexpr Graph row_dot() {
    Input a{"a", {64, 32}};
    Input b{"b", {64, 32}};
    Output y{"y"};
    if constexpr (Spelling == 0) {
        y = einsum("bf,bf->b", a, b);
    } else {
        Expr product = a * b;
        y = product.sum(1);
    }
    return resolved(finish(y));
}
template <int Spelling>
constexpr Graph turned_dot() {
    Input a{"a", {4, 3, 2}};
    Input b{"b", {4, 3, 2}};
    Output y{"y"};
    if constexpr (Spelling == 0) {
        y = einsum("bfc,bfc->cb", a, b);
    } else {
        Expr product = a * b;
        Expr summed = product.sum(1);
        y = detail::transpose(summed, Axes{1, 0, 2});
    }
    return resolved(finish(y));
}
constexpr Graph kContract = aligned_contraction<0>();

static_assert(count_op(kContract, op::Mul) == 1);
static_assert(count_op(kContract, op::Reduce) == 1);
static_assert(count_op(kContract, op::Contract) == 0);
static_assert(count_op(kContract, op::Transpose) == 0); // no post-transpose: kept order already matches
static_assert(only_op(kContract, op::Reduce).attr.reduce.op == ReduceOp::Sum);
static_assert(only_op(kContract, op::Reduce).attr.reduce.axes == Axes{0});
static_assert(kContract == aligned_contraction<1>());
static_assert(row_dot<0>() == row_dot<1>() && turned_dot<0>() == turned_dot<1>());
static_assert(count_op(turned_dot<0>(), op::Transpose) == 1 && count_op(turned_dot<0>(), op::Mul) == 1);

// ---- reduction: "c->" folds the channel axis, sum -----------------------------------------------------
constexpr Graph kReduce = resolved([] {
    Input x{"x", Signal{3, kBlock}};
    return finish(Output{"y"} = einsum("c->", x));
}());

static_assert(count_op(kReduce, op::Reduce) == 1);
static_assert(count_op(kReduce, op::Transpose) == 0); // one survivor, already the output — no permute
static_assert(only_op(kReduce, op::Reduce).attr.kind == AttrKind::Reduce);
static_assert(only_op(kReduce, op::Reduce).attr.reduce.op == ReduceOp::Sum);
static_assert(only_op(kReduce, op::Reduce).attr.reduce.axes == Axes{0});

// ---- transposition: "ab->ba" reorders the two outer axes, the streamed axis untouched ----------------
constexpr Graph kTranspose = resolved([] {
    Input x{"x", Signal{2, 3, kBlock}};
    return finish(Output{"y"} = einsum("ab->ba", x));
}());

static_assert(count_op(kTranspose, op::Transpose) == 1);
static_assert(count_op(kTranspose, op::Reduce) == 0);
static_assert(only_op(kTranspose, op::Transpose).attr.kind == AttrKind::View);
static_assert(only_op(kTranspose, op::Transpose).attr.view.kind == ViewKind::Transpose);
static_assert(only_op(kTranspose, op::Transpose).attr.view.order == Axes{1, 0, 2});

// ---- composition: "abc->cb" drops one axis (reduce) then reorders the survivors (transpose) -----------
// Survivors after dropping `a` are [b, c, n] in operand order; the output asks [c, b, n], so the
// transpose that follows the reduce reads survivor axes {1, 0, 2}. Two nodes, both already tested, no
// third kind.
constexpr Graph kBoth = resolved([] {
    Input x{"x", Signal{2, 3, 4, kBlock}};
    return finish(Output{"y"} = einsum("abc->cb", x));
}());

static_assert(count_op(kBoth, op::Reduce) == 1);
static_assert(count_op(kBoth, op::Transpose) == 1);
static_assert(count_op(kBoth, op::Contract) == 0);
static_assert(only_op(kBoth, op::Reduce).attr.reduce.axes == Axes{0});
static_assert(only_op(kBoth, op::Transpose).attr.view.order == Axes{1, 0, 2});

// ---- general contraction: "ik,kj->ij" lowers to ONE op::MatMul (milestone MB, S4d) ------------------
// The operands do not line up elementwise — `k` sits at a's axis 1 and b's axis 0 — so `_contr` cannot
// express it and this is the general `_matmul`. The binding: a's contracted axis 1, b's 0, no batch. The
// output is already `[a free]++[b free]++[streamed]`, so no transpose follows.
constexpr Graph kMatMul = resolved([] {
    Input a{"a", Signal{2, 3, kBlock}};
    Input b{"b", Signal{3, 4, kBlock}};
    return finish(Output{"y"} = einsum("ik,kj->ij", a, b));
}());

static_assert(count_op(kMatMul, op::MatMul) == 1);
static_assert(count_op(kMatMul, op::Contract) == 0);   // NOT the elementwise contraction
static_assert(count_op(kMatMul, op::Transpose) == 0);  // output already the shape rule's order
static_assert(count_op(kMatMul, op::Reduce) == 0);
static_assert(only_op(kMatMul, op::MatMul).attr.kind == AttrKind::MatMul);
static_assert(only_op(kMatMul, op::MatMul).attr.matmul.a_contract == 1);
static_assert(only_op(kMatMul, op::MatMul).attr.matmul.b_contract == 0);
static_assert(only_op(kMatMul, op::MatMul).attr.matmul.batch_a.rank == 0);
static_assert(only_op(kMatMul, op::MatMul).attr.matmul.batch_b.rank == 0);

// ---- batched contraction: "bik,bkj->bij" — a shared BATCH axis, kept once at the front --------------
constexpr Graph kBatched = resolved([] {
    Input a{"a", Signal{2, 2, 3, kBlock}};
    Input b{"b", Signal{2, 3, 4, kBlock}};
    return finish(Output{"y"} = einsum("bik,bkj->bij", a, b));
}());

static_assert(count_op(kBatched, op::MatMul) == 1);
static_assert(count_op(kBatched, op::Transpose) == 0);
static_assert(only_op(kBatched, op::MatMul).attr.matmul.a_contract == 2); // b=0,i=1,k=2
static_assert(only_op(kBatched, op::MatMul).attr.matmul.b_contract == 1); // b=0,k=1,j=2
static_assert(only_op(kBatched, op::MatMul).attr.matmul.batch_a == Axes{0});
static_assert(only_op(kBatched, op::MatMul).attr.matmul.batch_b == Axes{0});

// ---- a permuted output: "ik,kj->ji" is one MatMul THEN a transpose ({1,0,2}) ------------------------
// The shape rule builds `[i,j]`; the output asks `[j,i]`, so a transpose of the outer axes follows, the
// streamed axis mapping to itself — the same second node the composition case above produces.
constexpr Graph kMatMulReordered = resolved([] {
    Input a{"a", Signal{2, 3, kBlock}};
    Input b{"b", Signal{3, 4, kBlock}};
    return finish(Output{"y"} = einsum("ik,kj->ji", a, b));
}());

static_assert(count_op(kMatMulReordered, op::MatMul) == 1);
static_assert(count_op(kMatMulReordered, op::Transpose) == 1);
static_assert(only_op(kMatMulReordered, op::Transpose).attr.view.order == Axes{1, 0, 2});

// ---- the retained rejections, each observed objecting rather than asserted to accept ----------------
//
// A lowering `fail()` is a throw, which under constant evaluation IS the compile error — so a refusal
// can only be watched from a run, exactly as `width_test.cpp` explains.
// The input shapes are arbitrary: every refusal below is a LABEL-level decision the lowering makes
// before any shape is consulted.
// `fail()` stores its message in a fixed buffer of `kMaxMessageLength`, so a message longer than that
// arrives truncated. Compare against the same truncation the buffer applies, so a check is faithful
// whatever a message's length.
bool binary_einsum_refused(std::string_view spec, std::string_view expected) {
    const std::size_t cap = kMaxMessageLength - 1;
    const std::string_view want = expected.size() < cap ? expected : expected.substr(0, cap);
    try {
        Input a{"a", Signal{2, 2, kBlock}};
        Input b{"b", Signal{2, 2, kBlock}};
        (void)einsum(spec, a, b);
        return false;
    } catch (const error& e) {
        return e.message() == want;
    }
}

constexpr std::string_view kDiagonal = "heinzel::einsum: a repeated index (a diagonal) is not supported";
constexpr std::string_view kThreeOperands =
    "heinzel::einsum: more than two operands is not supported (the notation picks no pairing order)";
constexpr std::string_view kOneOperandSummed = "heinzel::einsum: an axis summed from one operand alone is not a contraction";
constexpr std::string_view kOneSharedAxis = "heinzel::einsum: a general contraction sums exactly one shared axis";
constexpr std::string_view kOutputInNeither = "heinzel::einsum: an output index is in neither operand";

} // namespace

int main() {
    // The positive lowerings are the static_asserts above — resolved at compile time. What remains for
    // the running body is the refusals: each pointed at a deliberately malformed spec and observed to
    // object with its own message, the "shown rejecting" M8.2 owes for the matmul lowering.
    // (The stream is never named, so the old reading's refusal of a spec that moves it has nothing to refuse.)
    if (!binary_einsum_refused("ii,kl->i", kDiagonal)) return 1;            // a repeated index
    if (!binary_einsum_refused("i,i,i->i", kThreeOperands)) return 2;       // three operands
    if (!binary_einsum_refused("ik,kj->j", kOneOperandSummed)) return 4;    // i summed from a alone
    if (!binary_einsum_refused("ab,ba->", kOneSharedAxis)) return 5;        // two shared summed axes
    if (!binary_einsum_refused("ab,cd->abcd", kOneSharedAxis)) return 8;    // none
    if (!binary_einsum_refused("ik,kj->ijx", kOutputInNeither)) return 6;   // x is in neither operand
    // And the good specs are NOT refused: a refusal "made to pass" everything would slip through above.
    if (binary_einsum_refused("ik,kj->ij", kDiagonal)) return 7;
    return 0;
}
