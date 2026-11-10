// An image pipeline: colour to grey, a 3×3 median against salt-and-pepper noise, then an unsharp mask, a Sobel
// edge map and an embossed relief — one graph with three results, planned into loop nests while this file compiles.
//
//   - An image is a tensor: `Input rgb{"rgb", {H, W, 3}}` names its axes and nothing else, since it has no time.
//     The colour weighting is an `einsum`, the median's windows two `unfold`s that one `rearrange` lines up.
//   - Windows are views: `x.unfold(0, 3)` names each pixel's neighbourhood, and the filters read their windows
//     straight from the image. The median is the one place windows are copied: it lines the nine values of each
//     3×3 window up along one axis and takes an order statistic over it.
//   - A filter is a function, its kernel data: the blur and both Sobel gradients are one 3-tap filter run down the
//     columns and along the rows, fed [1 2 1]/4 or [1 0 -1]. The kernels and the colour weights are known now, so they
//     are constants in the graph (`constant(kSmooth)`), compiled into the program like a hand loop's literals.
//   - One graph, several results: `finish` names `sharp`, `edges` and `relief`, which share the cleaned image, its
//     smoothing along the rows and the two gradients. Whether a shared value is stored or recomputed where it is
//     read is the planner's decision.
//   - The machine is data: `Executor<pipeline, zen2>` lays the loops out for AVX2 with FMA. The graph says what
//     to compute; the machine only how.
//
// The results are checked against the same pipeline written as plain C++ loops, then written with the test card
// to `image_input.ppm`, `image_sharp.pgm`, `image_edges.pgm` and `image_relief.pgm`.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

#include "heinzel/dsl.hpp"

using namespace heinzel;

namespace {

constexpr int kHeight = 256;
constexpr int kWidth = 384;

// Every interior pixel's 3×3 neighbourhood, its nine values side by side, and their median.
constexpr Expr despeckle(Expr grey) {
    Expr windows = grey.unfold(0, 3).unfold(2, 3);
    Expr nine = rearrange("h a w b -> h w (a b)", windows);
    return nine.median(2);
}

// A 3-tap filter with taps `k`, run down the columns or along the rows.
constexpr Expr down(Expr image, Expr k) { return (image.unfold(0, 3) * k.reshape({3, 1})).sum(1); }
constexpr Expr across(Expr image, Expr k) { return (image.unfold(1, 3) * k).sum(2); }

// The image without its one-pixel border, to line up with what a 3×3 filter makes of it.
constexpr Expr inner(Expr image, int height, int width) { return image.slice(0, 1, height - 2).slice(1, 1, width - 2); }

constexpr float kLuma[3] = {0.2126f, 0.7152f, 0.0722f};
constexpr float kSmooth[3] = {0.25f, 0.5f, 0.25f};
constexpr float kSlope[3] = {1.0f, 0.0f, -1.0f};

constexpr Graph pipeline() {
    Input rgb{"rgb", {kHeight, kWidth, 3}};
    Expr luma = constant(kLuma);     // how much red, green and blue each count
    Expr smooth = constant(kSmooth); // [1 2 1] / 4
    Expr slope = constant(kSlope);   // [1 0 -1]

    Expr grey = einsum("hwc,c->hw", rgb, luma);
    Expr clean = despeckle(grey);
    Expr smoothed = across(clean, smooth);
    Expr centre = inner(clean, kHeight - 2, kWidth - 2);
    Expr sloped = across(clean, slope);
    Expr dx = down(sloped, smooth);
    Expr dy = down(smoothed, slope);
    Expr blur = down(smoothed, smooth);
    Output sharp{"sharp"};
    Output edges{"edges"};
    Output relief{"relief"};
    sharp = centre + 1.5 * (centre - blur);
    edges = sqrt(dx * dx + dy * dy);
    relief = 0.5 + dx + dy;
    return finish(sharp, edges, relief);
}

constexpr int kOutHeight = kHeight - 4;
constexpr int kOutWidth = kWidth - 4;

using Image = std::vector<float>;

struct Results {
    Image sharp;
    Image edges;
    Image relief;
};

Image test_card();
Results reference(const Image& rgb);
bool matches(const float* got, const Image& want);
bool write_pnm(const char* path, const float* pixels, int height, int width, int channels);

} // namespace

int main() {
    const Image rgb = test_card();
    Image sharp(kOutHeight * kOutWidth);
    Image edges(sharp.size());
    Image relief(sharp.size());
    Executor<pipeline, zen2> images;
    images.input("rgb") = rgb.data();
    images.output("sharp") = sharp.data();
    images.output("edges") = edges.data();
    images.output("relief") = relief.data();
    images.run();

    const Results want = reference(rgb);
    if (!matches(sharp.data(), want.sharp) || !matches(edges.data(), want.edges) || !matches(relief.data(), want.relief)) {
        std::fprintf(stderr, "image_pipeline: a result strays from the plain C++ pipeline's\n");
        return 1;
    }
    if (!write_pnm("image_input.ppm", rgb.data(), kHeight, kWidth, 3) ||
        !write_pnm("image_sharp.pgm", sharp.data(), kOutHeight, kOutWidth, 1) ||
        !write_pnm("image_edges.pgm", edges.data(), kOutHeight, kOutWidth, 1) ||
        !write_pnm("image_relief.pgm", relief.data(), kOutHeight, kOutWidth, 1)) {
        std::fprintf(stderr, "image_pipeline: an image could not be written\n");
        return 1;
    }
    std::printf("image_pipeline: %dx%d in, sharp, edges and relief out (image_*.p?m), each within 1e-5 of the plain "
                "C++ pipeline\n", kWidth, kHeight);
    return 0;
}

namespace {

// Bright rings on dark blue, fading left to right, with one pixel in 32 knocked to black or white.
Image test_card() {
    Image rgb(kHeight * kWidth * 3);
    unsigned noise = 1;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const float radius = std::hypot(x - kWidth / 2.0f, y - kHeight / 2.0f);
            const bool bright = std::sin(0.12f * radius) > 0;
            const float fade = static_cast<float>(x) / kWidth;
            float* pixel = &rgb[(y * kWidth + x) * 3];
            pixel[0] = bright ? 1.0f : 0.1f;
            pixel[1] = bright ? 0.85f : 0.05f;
            pixel[2] = bright ? fade : 0.3f + 0.5f * fade;
            noise = noise * 1664525u + 1013904223u;
            if (noise >> 27 == 0) {
                pixel[0] = pixel[1] = pixel[2] = (noise >> 26) & 1 ? 1.0f : 0.0f;
            }
        }
    }
    return rgb;
}

// Every value within 1e-5 of the reference's; a NaN is not.
bool matches(const float* got, const Image& want) {
    for (std::size_t i = 0; i < want.size(); ++i) {
        if (!(std::fabs(got[i] - want[i]) < 1e-5f)) {
            return false;
        }
    }
    return true;
}

// P6 for colour, P5 for grey; values in [0, 1].
bool write_pnm(const char* path, const float* pixels, int height, int width, int channels) {
    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr) {
        return false;
    }
    std::fprintf(f, "%s\n%d %d\n255\n", channels == 3 ? "P6" : "P5", width, height);
    for (int i = 0; i < height * width * channels; ++i) {
        std::fputc(static_cast<int>(std::clamp(pixels[i], 0.0f, 1.0f) * 255.0f + 0.5f), f);
    }
    return std::fclose(f) == 0;
}

// ---- the same pipeline as plain C++ loops, to check the compiled one against -------------------------------

// A 3-tap filter along the rows of a height × width image, or down its columns.
Image filtered(const Image& in, int height, int width, const float (&k)[3], bool vertical) {
    const int h = vertical ? height - 2 : height;
    const int w = vertical ? width : width - 2;
    Image out(h * w);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            for (int a = 0; a < 3; ++a) {
                out[y * w + x] += k[a] * (vertical ? in[(y + a) * width + x] : in[y * width + x + a]);
            }
        }
    }
    return out;
}

Results reference(const Image& rgb) {
    constexpr int kCleanHeight = kHeight - 2;
    constexpr int kCleanWidth = kWidth - 2;
    Image grey(kHeight * kWidth);
    Image clean(kCleanHeight * kCleanWidth);
    for (int i = 0; i < kHeight * kWidth; ++i) {
        grey[i] = rgb[3 * i] * kLuma[0] + rgb[3 * i + 1] * kLuma[1] + rgb[3 * i + 2] * kLuma[2];
    }
    for (int y = 0; y < kCleanHeight; ++y) {
        for (int x = 0; x < kCleanWidth; ++x) {
            float nine[9];
            for (int a = 0; a < 9; ++a) {
                nine[a] = grey[(y + a / 3) * kWidth + x + a % 3];
            }
            std::nth_element(nine, nine + 4, nine + 9);
            clean[y * kCleanWidth + x] = nine[4];
        }
    }
    const auto separable = [&](const float (&horizontal)[3], const float (&vertical)[3]) {
        const Image along_rows = filtered(clean, kCleanHeight, kCleanWidth, horizontal, false);
        return filtered(along_rows, kCleanHeight, kCleanWidth - 2, vertical, true);
    };
    const Image blur = separable(kSmooth, kSmooth);
    const Image dx = separable(kSlope, kSmooth);
    const Image dy = separable(kSmooth, kSlope);
    Results r{Image(blur.size()), Image(blur.size()), Image(blur.size())};
    for (int y = 0; y < kOutHeight; ++y) {
        for (int x = 0; x < kOutWidth; ++x) {
            const int i = y * kOutWidth + x;
            const float centre = clean[(y + 1) * kCleanWidth + x + 1];
            r.sharp[i] = centre + 1.5f * (centre - blur[i]);
            r.edges[i] = std::sqrt(dx[i] * dx[i] + dy[i] * dy[i]);
            r.relief[i] = 0.5f + dx[i] + dy[i];
        }
    }
    return r;
}

} // namespace
