// M7 S2b–S4b — the numpy gradient goldens: the softmax composite (S2b), the MLP layer (S3), and the
// conv1d valid correlation (S4b). Each is checked against the INDEPENDENT numpy golden
// (`tests/goldens/gen_grad_golden.py` at `c4012b8`, vendored beside this file) at tolerance. With
// conv1d green this file holds ALL of oracle O8's rows — M7.1 complete: the golden coverage a later
// exclusion-narrowing commit must not precede. `gradients()` differentiates THROUGH each composition
// with no rule that knows the word "softmax", "matmul" or "conv": a max-shift/`_exp`/sum/`_div` for
// softmax, the restricted contraction for the MLP, an unfold+broadcast+sum for conv1d.
//
// WHY A TEST-LOCAL DRIVER, AND WHY IT IS FAITHFUL. The three streaming runners (`run` / `execute` /
// `emit`) each take an input of ONE number per sample by design — a filter's input is a scalar stream.
// A differentiation target here is a whole `[SB,SC]` matrix, and a wide target must be an `Input`
// (`validate_gradients`), so no streaming runner can feed it: distinct wide values otherwise come only
// from `_iota` synthesis, which makes internal values, not inputs. So this file fills each wide `Input`
// from a golden buffer and walks the primal+adjoint nodes with the tree's OWN per-node step
// (`detail::walk_step`) — that is exactly `run()`'s semantics, only the input fill generalised. The
// golden graphs are delay-free, so a single sample (`kBlock == 1`) is the whole computation. The old
// tree's own softmax golden feeds `z` AND `t` from the golden buffers (`main:tests/unit/autodiff_test`);
// this does the same, keeping the numpy oracle independent rather than recomputing the inputs.
//
// THE FRAMING. numpy's loss is `mean` over the WHOLE `[SB,SC]` matrix — one number coupling all
// `SB*SC` elements. So the matrix is ONE sample (its two axes are the value's element axes) with a
// single streamed time step, not `SB` streamed samples: a per-sample loss would carry the wrong `1/N`.
// The innermost axis is time, so the shape is `[SB, SC, 1]` and the softmax reduces over axis 1 (SC).
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>

#include "heinzel/autodiff/gradients.hpp"
#include "heinzel/sema/analyze.hpp"

#include "../goldens/grad_golden.hpp"
#include "hand_built_graph.hpp"
#include "interpret.hpp"

using namespace heinzel;
namespace gg = heinzel_golden::grad;

namespace {

constexpr std::size_t kBlock = 1; // one sample: the whole matrix, one streamed time step

struct Built {
    Graph g{};
    std::int32_t zs{kNoValue};
    std::int32_t ts{kNoValue};
    std::int32_t loss{kNoValue};
    std::int32_t grad{kNoValue};
};

// A reduction over ONE axis that KEEPS it at extent 1, so the reduced result broadcasts back against
// the operand — the shape a max-shift and a softmax denominator both need.
constexpr std::int32_t reduce_keep(Graph& g, std::int32_t v, ReduceOp which, std::int32_t axis) {
    OpAttr a{};
    a.kind = AttrKind::Reduce;
    a.reduce.op = which;
    a.reduce.axes = Axes{axis};
    a.reduce.keep_axes = true;
    return add_node(g, op::Reduce, v, kNoValue, kNoValue, a);
}

// p = softmax(zs, axis 1); loss = mean((p - ts)^2). Built with `add_node` in the graph's own
// vocabulary — the max-shift, exp, sum, div a numpy softmax is, with no rule that knows the word.
constexpr Built build_softmax() {
    Built b{};
    Graph& g = b.g;
    const Shape wide{gg::SB, gg::SC, 1};
    b.zs = add_value(g, ElemType::F64, wide, ValueCategory::Input);
    b.ts = add_value(g, ElemType::F64, wide, ValueCategory::Input);
    g.inputs.push_back(NamedValue{Name{"z"}, b.zs});
    g.inputs.push_back(NamedValue{Name{"t"}, b.ts});

    const auto m = reduce_keep(g, b.zs, ReduceOp::Max, 1);       // [SB,1,1] — the per-row max
    const auto shifted = add_node(g, op::Sub, b.zs, m);          // [SB,SC,1]
    const auto ex = add_node(g, op::Exp, shifted);               // [SB,SC,1]
    const auto denom = reduce_keep(g, ex, ReduceOp::Sum, 1);     // [SB,1,1]
    const auto p = add_node(g, op::Div, ex, denom);              // [SB,SC,1]
    const auto diff = add_node(g, op::Sub, p, b.ts);             // [SB,SC,1]
    const auto sq = add_node(g, op::Mul, diff, diff);            // [SB,SC,1]

    OpAttr mean{};
    mean.kind = AttrKind::Reduce;
    mean.reduce.op = ReduceOp::Mean;
    mean.reduce.axes = Axes{0, 1};
    b.loss = add_node(g, op::Reduce, sq, kNoValue, kNoValue, mean); // one number per sample
    g.outputs.push_back(NamedValue{Name{"loss"}, b.loss});

    b.grad = autodiff::gradients(g, b.loss, {b.zs})[0];
    return b;
}

// Built at compile time: an ill-shaped adjoint node would fail the augmented-graph `infer` here.
constexpr Built kSoftmax = build_softmax();

// The MLP layer golden: loss = mean((relu(x@w + b) − t)^2), with dW / dB / dX checked against numpy's
// hand-derived backprop. `x@w` is this tree's RESTRICTED contraction — the operands unsqueezed so they
// broadcast into a joined `[B,K,N,1]` and the K axis summed away (`rules_contract.hpp`'s matmul case),
// so the contraction adjoint (`dY·Wᵀ`, `Xᵀ·dY`) is what carries dX and dW here. relu is `max(·,0)`; the
// golden's inputs are chosen off the kink so the subgradient is unambiguous.
struct Mlp {
    Graph g{};
    std::int32_t x{kNoValue};
    std::int32_t w{kNoValue};
    std::int32_t bias{kNoValue};
    std::int32_t t{kNoValue};
    std::int32_t loss{kNoValue};
    std::int32_t dX{kNoValue};
    std::int32_t dW{kNoValue};
    std::int32_t dB{kNoValue};
};

constexpr std::int32_t unsqueeze(Graph& g, std::int32_t v, std::int32_t axis) {
    OpAttr a{};
    a.kind = AttrKind::View;
    a.view.kind = ViewKind::Unsqueeze;
    a.view.axis = axis;
    return add_node(g, op::Reshape, v, kNoValue, kNoValue, a);
}

constexpr std::int32_t contract_k(Graph& g, std::int32_t a, std::int32_t b) {
    OpAttr s{};
    s.kind = AttrKind::Contract;
    s.contract.axes = Axes{1}; // the K axis of the joined [B,K,N,1]
    return add_node(g, op::Contract, a, b, kNoValue, s);
}

constexpr Mlp build_mlp() {
    Mlp b{};
    Graph& g = b.g;
    // Element axes then a single streamed time step: X [B,K,1], W [K,N,1], bias [N,1], T [B,N,1].
    b.x = add_value(g, ElemType::F64, Shape{gg::B, gg::K, 1}, ValueCategory::Input);
    b.w = add_value(g, ElemType::F64, Shape{gg::K, gg::N, 1}, ValueCategory::Input);
    b.bias = add_value(g, ElemType::F64, Shape{gg::N, 1}, ValueCategory::Input);
    b.t = add_value(g, ElemType::F64, Shape{gg::B, gg::N, 1}, ValueCategory::Input);
    g.inputs.push_back(NamedValue{Name{"x"}, b.x});
    g.inputs.push_back(NamedValue{Name{"w"}, b.w});
    g.inputs.push_back(NamedValue{Name{"b"}, b.bias});
    g.inputs.push_back(NamedValue{Name{"t"}, b.t});

    // x@w summed over K: X→[B,K,1,1], W→[1,K,N,1], joined [B,K,N,1], collapse K → [B,N,1].
    const auto xr = unsqueeze(g, b.x, 2);
    const auto wr = unsqueeze(g, b.w, 0);
    const auto z = contract_k(g, xr, wr);                     // [B,N,1]
    const auto zb = add_node(g, op::Add, z, b.bias);          // [B,N,1] (bias broadcasts)
    const auto zero = add_value(g, ElemType::F64, Shape{}, ValueCategory::Const);
    const auto y = add_node(g, op::Max, zb, zero);            // relu
    const auto diff = add_node(g, op::Sub, y, b.t);           // [B,N,1]
    const auto sq = add_node(g, op::Mul, diff, diff);         // [B,N,1]

    OpAttr mean{};
    mean.kind = AttrKind::Reduce;
    mean.reduce.op = ReduceOp::Mean;
    mean.reduce.axes = Axes{0, 1};
    b.loss = add_node(g, op::Reduce, sq, kNoValue, kNoValue, mean); // one number per sample
    g.outputs.push_back(NamedValue{Name{"loss"}, b.loss});

    const auto grads = autodiff::gradients(g, b.loss, {b.x, b.w, b.bias});
    b.dX = grads[0];
    b.dW = grads[1];
    b.dB = grads[2];
    return b;
}

constexpr Mlp kMlp = build_mlp();

// The conv1d (valid) golden: y[f] = Σ_k x[f+k]·w[k]; loss = mean((y − t)^2), with dW and dX checked
// against numpy's hand-derived backprop. The valid correlation is built as an UNFOLD of the signal
// (window KW, hop 1 → frames x[f+k]) times the broadcast kernel, summed over the window axis — so the
// unfold adjoint (`rules_structural.hpp`, `adjoint_frame`: overlap-add scatter) is what carries dX, the
// "overlap_add adjoint path" the golden's comment names. Exact tiling here: (F−1)·1+KW == L, no tail.
struct Conv {
    Graph g{};
    std::int32_t x{kNoValue};
    std::int32_t w{kNoValue};
    std::int32_t t{kNoValue};
    std::int32_t loss{kNoValue};
    std::int32_t dX{kNoValue};
    std::int32_t dW{kNoValue};
};

// A sliding window over `axis` — the unfold that turns the signal into overlapping frames.
constexpr std::int32_t unfold(Graph& g, std::int32_t v, std::int32_t axis, std::int32_t window,
                              std::int32_t hop) {
    OpAttr a{};
    a.kind = AttrKind::View;
    a.view.kind = ViewKind::Unfold;
    a.view.axis = axis;
    a.view.window = window;
    a.view.hop = hop;
    return add_node(g, op::Unfold, v, kNoValue, kNoValue, a);
}

constexpr Conv build_conv() {
    Conv b{};
    Graph& g = b.g;
    // One signal, element axis then a single streamed step: x [L,1], w [KW,1], t [F,1].
    b.x = add_value(g, ElemType::F64, Shape{gg::L, 1}, ValueCategory::Input);
    b.w = add_value(g, ElemType::F64, Shape{gg::KW, 1}, ValueCategory::Input);
    b.t = add_value(g, ElemType::F64, Shape{gg::F, 1}, ValueCategory::Input);
    g.inputs.push_back(NamedValue{Name{"x"}, b.x});
    g.inputs.push_back(NamedValue{Name{"w"}, b.w});
    g.inputs.push_back(NamedValue{Name{"t"}, b.t});

    // x unfolded into F frames of KW taps: [L,1] → [F,KW,1], Xu[f,k] = x[f+k]. Kernel broadcast to
    // [1,KW,1], multiplied, then the KW window axis summed away → y [F,1], the valid correlation.
    const auto xu = unfold(g, b.x, 0, gg::KW, 1);            // [F,KW,1]
    const auto wr = unsqueeze(g, b.w, 0);                    // [1,KW,1]
    const auto prod = add_node(g, op::Mul, xu, wr);          // [F,KW,1]
    OpAttr sum_kw{};
    sum_kw.kind = AttrKind::Reduce;
    sum_kw.reduce.op = ReduceOp::Sum;
    sum_kw.reduce.axes = Axes{1};                            // the window axis
    const auto y = add_node(g, op::Reduce, prod, kNoValue, kNoValue, sum_kw); // [F,1]
    const auto diff = add_node(g, op::Sub, y, b.t);          // [F,1]
    const auto sq = add_node(g, op::Mul, diff, diff);        // [F,1]

    OpAttr mean{};
    mean.kind = AttrKind::Reduce;
    mean.reduce.op = ReduceOp::Mean;
    mean.reduce.axes = Axes{0};                              // mean over the F frames
    b.loss = add_node(g, op::Reduce, sq, kNoValue, kNoValue, mean); // one number
    g.outputs.push_back(NamedValue{Name{"loss"}, b.loss});

    const auto grads = autodiff::gradients(g, b.loss, {b.x, b.w});
    b.dX = grads[0];
    b.dW = grads[1];
    return b;
}

constexpr Conv kConv = build_conv();

// Fill every `Const` value's planes, exactly as `run()` does — one number repeated over its elements.
template <std::size_t BlockSize>
void fill_consts(const Graph& g, Signals<BlockSize>& out) {
    for (std::size_t i = 0; i < g.values.size(); ++i) {
        if (g.values[i].category != ValueCategory::Const) {
            continue;
        }
        for (std::int32_t e = 0; e < elements_of(g.values[i].shape).count; ++e) {
            for (std::size_t n = 0; n < BlockSize; ++n) {
                detail::write(out, static_cast<std::int32_t>(i), e, n, g.values[i].constant);
            }
        }
    }
}

// Fill one wide `Input`'s planes from a golden buffer: element e (the flat row-major index over the
// value's element axes, SC innermost) is `buf[e]`, the same order numpy's C-order `ravel` produces.
template <std::size_t BlockSize, std::size_t K>
void fill_wide(Signals<BlockSize>& out, std::int32_t value, const std::array<float, K>& buf) {
    for (std::int32_t e = 0; e < static_cast<std::int32_t>(K); ++e) {
        for (std::size_t n = 0; n < BlockSize; ++n) {
            detail::write(out, value, e, n, static_cast<double>(buf[e]));
        }
    }
}

// The wide-input walk: lay the graph out, fill the constants and the two wide inputs, then step every
// node in written order — `run()` without its one-number-per-sample input refusal.
void run_golden(const Built& b, Signals<kBlock>& out) {
    lay_out(b.g, out);
    fill_consts(b.g, out);
    fill_wide(out, b.zs, gg::ZS);
    fill_wide(out, b.ts, gg::TS);
    for (std::size_t n = 0; n < kBlock; ++n) {
        for (const Node& node : b.g.nodes) {
            detail::walk_step(b.g, node, n, out);
        }
    }
}

// The MLP walk: lay out, fill the constants and the four wide inputs from the golden buffers, then step
// every node in written order — the same wide-input driver the softmax golden uses.
void run_mlp(const Mlp& b, Signals<kBlock>& out) {
    lay_out(b.g, out);
    fill_consts(b.g, out);
    fill_wide(out, b.x, gg::X);
    fill_wide(out, b.w, gg::W);
    fill_wide(out, b.bias, gg::BIAS);
    fill_wide(out, b.t, gg::T);
    for (std::size_t n = 0; n < kBlock; ++n) {
        for (const Node& node : b.g.nodes) {
            detail::walk_step(b.g, node, n, out);
        }
    }
}

// The conv1d walk: lay out, fill the constants and the three wide inputs from the golden buffers, then
// step every node in written order — the same wide-input driver.
void run_conv(const Conv& b, Signals<kBlock>& out) {
    lay_out(b.g, out);
    fill_consts(b.g, out);
    fill_wide(out, b.x, gg::XC);
    fill_wide(out, b.w, gg::WC);
    fill_wide(out, b.t, gg::TC);
    for (std::size_t n = 0; n < kBlock; ++n) {
        for (const Node& node : b.g.nodes) {
            detail::walk_step(b.g, node, n, out);
        }
    }
}

// One gradient buffer checked element-for-element against its golden (row-major, matching numpy's
// C-order ravel). Returns the number of disagreements.
template <std::size_t K>
int check_grad(const char* name, const Signals<kBlock>& sig, std::int32_t grad,
               const std::array<float, K>& want) {
    int failures = 0;
    for (std::int32_t e = 0; e < static_cast<std::int32_t>(K); ++e) {
        const double got = sample_at(sig, grad, e, 0);
        const double w = static_cast<double>(want[static_cast<std::size_t>(e)]);
        if (std::fabs(got - w) > 5e-3 * (1.0 + std::fabs(w))) {
            std::printf("%s[%d]: tree %.9e  golden %.9e\n", name, e, got, w);
            ++failures;
        }
    }
    return failures;
}

} // namespace

int main() {
    std::int32_t needed = 0;
    for (const Value& v : kSoftmax.g.values) {
        needed += elements_of(v.shape).count;
    }
    std::int32_t needed_mlp = 0;
    for (const Value& v : kMlp.g.values) {
        needed_mlp += elements_of(v.shape).count;
    }
    std::int32_t needed_conv = 0;
    for (const Value& v : kConv.g.values) {
        needed_conv += elements_of(v.shape).count;
    }
    std::printf("softmax: %zu values, %d planes; mlp: %zu values, %d planes; conv1d: %zu values, %d planes "
                "(kMaxPlanes=%zu)\n",
                kSoftmax.g.values.size(), needed, kMlp.g.values.size(), needed_mlp, kConv.g.values.size(),
                needed_conv, kMaxPlanes);

    int failures = 0;

    Signals<kBlock> sig{};
    run_golden(kSoftmax, sig);
    const double loss = sample_at(sig, kSoftmax.loss, 0, 0);
    if (std::fabs(loss - gg::loss_softmax) > 1e-4 * (1.0 + std::fabs(gg::loss_softmax))) {
        std::printf("softmax loss: tree %.9e  golden %.9e\n", loss, static_cast<double>(gg::loss_softmax));
        ++failures;
    }
    failures += check_grad("dZS", sig, kSoftmax.grad, gg::dZS);

    Signals<kBlock> mlp{};
    run_mlp(kMlp, mlp);
    const double loss_mlp = sample_at(mlp, kMlp.loss, 0, 0);
    if (std::fabs(loss_mlp - gg::loss_mlp) > 1e-4 * (1.0 + std::fabs(gg::loss_mlp))) {
        std::printf("mlp loss: tree %.9e  golden %.9e\n", loss_mlp, static_cast<double>(gg::loss_mlp));
        ++failures;
    }
    failures += check_grad("dW", mlp, kMlp.dW, gg::dW);
    failures += check_grad("dB", mlp, kMlp.dB, gg::dB);
    failures += check_grad("dX", mlp, kMlp.dX, gg::dX);

    Signals<kBlock> conv{};
    run_conv(kConv, conv);
    const double loss_conv = sample_at(conv, kConv.loss, 0, 0);
    if (std::fabs(loss_conv - gg::loss_conv) > 1e-4 * (1.0 + std::fabs(gg::loss_conv))) {
        std::printf("conv1d loss: tree %.9e  golden %.9e\n", loss_conv, static_cast<double>(gg::loss_conv));
        ++failures;
    }
    failures += check_grad("dWC", conv, kConv.dW, gg::dWC);
    failures += check_grad("dXC", conv, kConv.dX, gg::dXC);

    if (failures != 0) {
        std::printf("numpy gradient goldens: %d checks disagreed\n", failures);
        return 1;
    }
    return 0;
}
