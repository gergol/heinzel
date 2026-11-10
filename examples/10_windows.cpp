// Tour 10: windows and views. A sensor trace cleaned of its spikes and smoothed, and a small image blurred and
// rearranged.
//
//   - A view reads the same numbers another way, and copies nothing: `slice` takes a run of an axis (every k-th one
//     with a step), `transpose` swaps axes, `reshape` reads them with other extents.
//   - `trace.unfold(0, 5)` is every window of five neighbours: a {N} trace becomes {N - 4, 5}, one row per window,
//     read straight from the trace. Any reduction over the window axis then makes a filter: `.mean(1)` a moving
//     average, `.median(1)` a median, which takes out spikes a mean would smear.
//   - `unfold2d(0, 3, 1, 3)` windows two axes at once: an {H, W} image becomes {H - 2, 3, W - 2, 3}, each pixel's
//     3×3 neighbourhood, and `.mean({1, 3})` a box blur.
//   - Windows run over a tensor's axes. A signal's time is read through its past instead (`x[n - k]`, tour 03).
//
// The results are checked against plain C++ loops.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "heinzel/dsl.hpp"

using namespace heinzel;

namespace {

constexpr int kTrace = 1000;
constexpr int kHeight = 6;
constexpr int kWidth = 8;

// The trace without its spikes (a median of 5), smoothed (a mean of 9), and what the two took out of it.
constexpr Graph clean() {
    Input trace{"trace", {kTrace}};
    Expr fives = trace.unfold(0, 5);                // {kTrace - 4, 5}
    Expr despiked = fives.median(1);                // {kTrace - 4}, centred on trace[2 …]
    Expr nines = despiked.unfold(0, 9);             // {kTrace - 12, 9}
    Expr smooth = nines.mean(1);                    // {kTrace - 12}, centred on trace[6 …]
    Output smoothed{"smoothed"};
    Output removed{"removed"};
    smoothed = smooth;
    removed = trace.slice(0, 6, kTrace - 12) - smooth;
    return finish(smoothed, removed);
}

// A 3×3 box blur, then the blurred image turned on its diagonal, every other pixel of it, and its pixels in a row.
constexpr Graph views() {
    Input image{"image", {kHeight, kWidth}};
    Expr neighbourhoods = image.unfold2d(0, 3, 1, 3);            // {4, 3, 6, 3}
    Expr blurred = neighbourhoods.mean({1, 3});                  // {4, 6}
    Output turned{"turned"};
    Output coarse{"coarse"};
    Output flat{"flat"};
    turned = blurred.transpose({1, 0});                           // {6, 4}
    coarse = blurred.slice(0, 0, 2, 2).slice(1, 0, 3, 2);         // {2, 3}: rows 0 and 2, columns 0, 2 and 4
    flat = blurred.reshape({(kHeight - 2) * (kWidth - 2)});      // {24}
    return finish(turned, coarse, flat);
}

bool trace_matches(const std::vector<float>& trace, const std::vector<float>& smoothed, const std::vector<float>& removed);
bool views_match(const float* image, const float* turned, const float* coarse, const float* flat);

} // namespace

int main() {
    std::vector<float> trace(kTrace);
    std::vector<float> smoothed(kTrace - 12);
    std::vector<float> removed(kTrace - 12);
    for (int t = 0; t < kTrace; ++t) { // a slow wave, a little hiss and, now and then, a spike
        const float wave = std::sin(0.01f * static_cast<float>(t));
        const float noise = std::sin(1.3f * static_cast<float>(t * t % 97));
        trace[t] = wave + 0.05f * noise + (t % 97 == 13 ? 4.0f : 0.0f);
    }
    Executor<clean> cleaner;
    cleaner.input("trace") = trace.data();
    cleaner.output("smoothed") = smoothed.data();
    cleaner.output("removed") = removed.data();
    cleaner.run();

    float image[kHeight * kWidth];
    float turned[24];
    float coarse[6];
    float flat[24];
    for (int i = 0; i < kHeight * kWidth; ++i) {
        image[i] = static_cast<float>(i % 7) * 0.25f;
    }
    Executor<views> view;
    view.input("image") = image;
    view.output("turned") = turned;
    view.output("coarse") = coarse;
    view.output("flat") = flat;
    view.run();

    if (!trace_matches(trace, smoothed, removed) || !views_match(image, turned, coarse, flat)) {
        return 1;
    }
    std::printf("10_windows: a trace despiked and smoothed, an image blurred, turned, thinned and flattened; all as the loops\n");
    return 0;
}

namespace {

bool close(double got, double want, const char* what, int at) {
    if (std::fabs(got - want) < 1e-5) {
        return true;
    }
    std::fprintf(stderr, "10_windows: %s[%d] is %g, the loop's %g\n", what, at, got, want);
    return false;
}

bool trace_matches(const std::vector<float>& trace, const std::vector<float>& smoothed, const std::vector<float>& removed) {
    std::vector<float> despiked(kTrace - 4);
    for (int t = 0; t < kTrace - 4; ++t) {
        float five[5];
        std::copy(&trace[t], &trace[t] + 5, five);
        std::nth_element(five, five + 2, five + 5);
        despiked[t] = five[2];
    }
    for (int t = 0; t < kTrace - 12; ++t) {
        double sum = 0.0;
        for (int k = 0; k < 9; ++k) {
            sum += despiked[t + k];
        }
        if (!close(smoothed[t], sum / 9, "smoothed", t) || !close(removed[t], trace[t + 6] - sum / 9, "removed", t)) {
            return false;
        }
    }
    return true;
}

bool views_match(const float* image, const float* turned, const float* coarse, const float* flat) {
    float blurred[kHeight - 2][kWidth - 2];
    for (int y = 0; y < kHeight - 2; ++y) {
        for (int x = 0; x < kWidth - 2; ++x) {
            double sum = 0.0;
            for (int a = 0; a < 9; ++a) {
                sum += image[(y + a / 3) * kWidth + x + a % 3];
            }
            blurred[y][x] = static_cast<float>(sum / 9);
        }
    }
    for (int y = 0; y < kHeight - 2; ++y) {
        for (int x = 0; x < kWidth - 2; ++x) {
            const int i = y * (kWidth - 2) + x;
            if (!close(flat[i], blurred[y][x], "flat", i) || !close(turned[x * (kHeight - 2) + y], blurred[y][x], "turned", i)) {
                return false;
            }
        }
    }
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 3; ++x) {
            if (!close(coarse[y * 3 + x], blurred[2 * y][2 * x], "coarse", y * 3 + x)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace
