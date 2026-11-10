// Tour 11: einsum and rearrange. A two-layer classifier, a batch of transforms composed, and the colour channels of
// an interleaved image.
//
//   - `einsum("bf,fh->bh", x, w)` names every axis by a letter: what both operands share and the output leaves out
//     is summed. That is a matrix product; `"bij,bjk->bik"` is one per batch entry.
//   - The planner turns each product into a loop nest of its own, tiled for the machine's caches and vector lanes:
//     there is no matrix-multiply kernel, only the loops a product needs.
//   - `rearrange("(h w c) -> h w c", rgb, {{"h", kHeight}, {"w", kWidth}})` splits one axis into three (the one
//     size left out is worked out), and `"h w c -> c h w"` reorders them; one spec may do both, `"(h w c) -> c h w"`.
//     `reduce("c h w -> c", …, ReduceOp::Mean)` folds the named axes away. These are views and reductions in einops'
//     notation, nothing new underneath.
//   - Operands with the same labels in the same order (`"bf,bf->b"`) are a product summed elementwise, the same graph
//     as `(a * b).sum(1)`. Not run yet: an `einsum` of a tensor with a signal (multiply, then sum).
//
// The results are checked against plain C++ loops.

#include <cmath>
#include <cstdio>
#include <vector>

#include "heinzel/dsl.hpp"

using namespace heinzel;

namespace {

constexpr int kBatch = 32;
constexpr int kFeatures = 16;
constexpr int kHidden = 64;
constexpr int kClasses = 4;

constexpr Expr softmax(Expr logits) {
    Expr top = logits.max(1, keepdims);
    Expr e = exp(logits - top);
    return e / e.sum(1, keepdims);
}

// A ReLU layer, then a linear one, then a softmax over the classes: each row of `x` to four probabilities.
constexpr Graph classifier() {
    Input x{"x", {kBatch, kFeatures}};
    Input w1{"w1", {kFeatures, kHidden}};
    Input b1{"b1", {kHidden}};
    Input w2{"w2", {kHidden, kClasses}};
    Expr weighted = einsum("bf,fh->bh", x, w1);
    Expr hidden = max(weighted + b1, 0.0);
    Expr logits = einsum("bh,hc->bc", hidden, w2);
    Output probabilities{"probabilities"};
    probabilities = softmax(logits);
    return probabilities;
}

constexpr int kTransforms = 8;

// Each a's 3×3 matrix times b's.
constexpr Graph compose() {
    Input a{"a", {kTransforms, 3, 3}};
    Input b{"b", {kTransforms, 3, 3}};
    Output ab{"ab"};
    ab = einsum("bij,bjk->bik", a, b);
    return ab;
}

constexpr int kHeight = 4;
constexpr int kWidth = 5;

// An image as a camera hands it over, one row of red, green and blue triples after another: its channels apart,
// and each channel's mean.
constexpr Graph channels() {
    Input rgb{"rgb", {kHeight * kWidth * 3}};
    Expr image = rearrange("(h w c) -> h w c", rgb, {{"h", kHeight}, {"w", kWidth}});
    Expr planes = rearrange("h w c -> c h w", image);
    Output planar{"planar"};
    Output means{"means"};
    planar = planes;
    means = reduce("c h w -> c", planes, ReduceOp::Mean);
    return finish(planar, means);
}

using Matrix = std::vector<float>;

Matrix random(int count, float spread, unsigned& seed);
bool classifier_matches(const Matrix& x, const Matrix& w1, const Matrix& b1, const Matrix& w2, const Matrix& got);
bool products_match(const Matrix& a, const Matrix& b, const Matrix& ab);
bool channels_match(const Matrix& rgb, const Matrix& planar, const Matrix& means);

} // namespace

int main() {
    unsigned seed = 11;
    const Matrix x = random(kBatch * kFeatures, 1.0f, seed);
    const Matrix w1 = random(kFeatures * kHidden, 0.25f, seed);
    const Matrix b1 = random(kHidden, 0.1f, seed);
    const Matrix w2 = random(kHidden * kClasses, 0.25f, seed);
    Matrix probabilities(kBatch * kClasses);
    Executor<classifier> classify;
    classify.input("x") = x.data();
    classify.input("w1") = w1.data();
    classify.input("b1") = b1.data();
    classify.input("w2") = w2.data();
    classify.output("probabilities") = probabilities.data();
    classify.run();

    const Matrix a = random(kTransforms * 9, 1.0f, seed);
    const Matrix b = random(kTransforms * 9, 1.0f, seed);
    Matrix ab(kTransforms * 9);
    Executor<compose> product;
    product.input("a") = a.data();
    product.input("b") = b.data();
    product.output("ab") = ab.data();
    product.run();

    Matrix rgb(kHeight * kWidth * 3);
    Matrix planar(rgb.size());
    Matrix means(3);
    for (std::size_t i = 0; i < rgb.size(); ++i) {
        rgb[i] = static_cast<float>(i % 3) + 0.01f * static_cast<float>(i / 3); // red near 0, green near 1, blue near 2
    }
    Executor<channels> split;
    split.input("rgb") = rgb.data();
    split.output("planar") = planar.data();
    split.output("means") = means.data();
    split.run();

    bool ok = classifier_matches(x, w1, b1, w2, probabilities);
    ok = ok && products_match(a, b, ab);
    ok = ok && channels_match(rgb, planar, means);
    if (!ok) {
        std::fprintf(stderr, "11_einsum: a result strays from the plain loops\n");
        return 1;
    }
    std::printf("11_einsum: %d rows classified, %d transforms composed, an image's channels split; all as the loops\n", kBatch, kTransforms);
    return 0;
}

namespace {

// Uniform in [-spread, spread).
Matrix random(int count, float spread, unsigned& seed) {
    Matrix m(count);
    for (float& v : m) {
        seed = seed * 1664525u + 1013904223u;
        v = spread * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f);
    }
    return m;
}

bool classifier_matches(const Matrix& x, const Matrix& w1, const Matrix& b1, const Matrix& w2, const Matrix& got) {
    for (int r = 0; r < kBatch; ++r) {
        double hidden[kHidden];
        double logits[kClasses];
        double top = -INFINITY;
        double total = 0.0;
        for (int h = 0; h < kHidden; ++h) {
            double sum = b1[h];
            for (int f = 0; f < kFeatures; ++f) {
                sum += x[r * kFeatures + f] * w1[f * kHidden + h];
            }
            hidden[h] = std::fmax(sum, 0.0);
        }
        for (int c = 0; c < kClasses; ++c) {
            logits[c] = 0.0;
            for (int h = 0; h < kHidden; ++h) {
                logits[c] += hidden[h] * w2[h * kClasses + c];
            }
            top = std::fmax(top, logits[c]);
        }
        for (const double l : logits) {
            total += std::exp(l - top);
        }
        for (int c = 0; c < kClasses; ++c) {
            if (!(std::fabs(got[r * kClasses + c] - std::exp(logits[c] - top) / total) < 1e-5)) {
                return false;
            }
        }
    }
    return true;
}

bool products_match(const Matrix& a, const Matrix& b, const Matrix& ab) {
    bool ok = true;
    for (int t = 0; t < kTransforms; ++t) {
        for (int i = 0; i < 9; ++i) {
            double sum = 0.0;
            for (int j = 0; j < 3; ++j) {
                sum += a[t * 9 + i / 3 * 3 + j] * b[t * 9 + j * 3 + i % 3];
            }
            ok = ok && std::fabs(ab[t * 9 + i] - sum) < 1e-5;
        }
    }
    return ok;
}

bool channels_match(const Matrix& rgb, const Matrix& planar, const Matrix& means) {
    bool ok = true;
    for (int c = 0; c < 3; ++c) {
        double sum = 0.0;
        for (int p = 0; p < kHeight * kWidth; ++p) {
            ok = ok && planar[c * kHeight * kWidth + p] == rgb[p * 3 + c];
            sum += rgb[p * 3 + c];
        }
        ok = ok && std::fabs(means[c] - sum / (kHeight * kWidth)) < 1e-5;
    }
    return ok;
}

} // namespace
