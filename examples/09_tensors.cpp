// Tour 09: tensors. A table of exam scores: each subject's z-scores, each student's weighted average and their best
// subject.
//
//   - A tensor is declared by its shape, `Input scores{"scores", {kStudents, kSubjects}}`, row-major: the same numbers
//     at every sample, no time of its own. The whole table is one run.
//   - `constant(kWeights)` is a tensor known while compiling, written into the program like a hand loop's literals.
//     `iota({kSubjects})` counts 0, 1, 2, … along its axis.
//   - Shapes broadcast as in numpy, lined up from the right: a {6, 4} table times a {4} row of weights weighs every
//     row, and an axis of extent 1 stretches to meet the other.
//   - A reduction names the axes it folds: `scores.mean(0)` averages down the columns, `.sum(1)` along the rows.
//     With `keepdims` the folded axis stays, at extent 1, so the result broadcasts back against what it came from.
//   - `+ 1e-6` keeps the spread, a divisor, away from 0 even for a subject everyone scored alike. The compiler
//     proves it: a value times itself is at least 0, so the root is at least 0.001 (tour 12).
//   - `best`, the index of each student's top z-score, is a mask and a reduction: where a z-score falls short of
//     its row's top, -1, else its column's number, and the row's largest of those. An output is read as any value
//     is, its members too: `z.max(1, keepdims)` is each row's top.
//
// The three results are checked against plain C++ loops.

#include <cmath>
#include <cstdio>
#include <vector>

#include "heinzel/dsl.hpp"

using namespace heinzel;

namespace {

constexpr int kStudents = 6;
constexpr int kSubjects = 4;
constexpr float kWeights[kSubjects] = {0.4f, 0.3f, 0.2f, 0.1f};

constexpr Graph report() {
    Input scores{"scores", {kStudents, kSubjects}};
    Output z{"z"};
    Output average{"average"};
    Output best{"best"};
    Expr centred = scores - scores.mean(0, keepdims);
    Expr variance = (centred * centred).mean(0, keepdims);
    Expr spread = sqrt(variance + 1e-6);
    z = centred / spread;
    Expr row_top = z.max(1, keepdims);
    Expr top_column = where(row_top > z, -1.0, iota({kSubjects}));
    average = (scores * constant(kWeights)).sum(1);
    best = top_column.max(1);
    return finish(z, average, best);
}

constexpr float kScores[kStudents][kSubjects] = {
    {72, 85, 64, 90}, {88, 79, 91, 70}, {55, 62, 58, 75}, {94, 90, 85, 88}, {67, 71, 99, 60}, {80, 58, 73, 82},
};

} // namespace

int main() {
    float z[kStudents][kSubjects];
    float average[kStudents];
    float best[kStudents];
    Executor<report> table;
    table.input("scores") = &kScores[0][0];
    table.output("z") = &z[0][0];
    table.output("average") = average;
    table.output("best") = best;
    table.run();

    double off = 0.0;
    for (int s = 0; s < kSubjects; ++s) {
        double mean = 0.0;
        double squares = 0.0;
        for (const auto& row : kScores) {
            mean += row[s] / static_cast<double>(kStudents);
        }
        for (const auto& row : kScores) {
            squares += (row[s] - mean) * (row[s] - mean) / kStudents;
        }
        for (int r = 0; r < kStudents; ++r) {
            const double want = (kScores[r][s] - mean) / std::sqrt(squares + 1e-6);
            off = std::fmax(off, std::fabs(z[r][s] - want));
        }
    }
    for (int r = 0; r < kStudents; ++r) {
        double sum = 0.0;
        int top = 0;
        for (int s = 0; s < kSubjects; ++s) {
            sum += kWeights[s] * kScores[r][s];
            top = z[r][s] > z[r][top] ? s : top;
        }
        off = std::fmax(off, std::fabs(average[r] - sum) / 100.0); // relative to scores of up to 100
        if (best[r] != static_cast<float>(top)) {
            std::fprintf(stderr, "09_tensors: student %d's best subject is %g, not %d\n", r, best[r], top);
            return 1;
        }
    }
    if (!(off < 1e-6)) {
        std::fprintf(stderr, "09_tensors: %.3g from the plain loops\n", off);
        return 1;
    }
    std::printf("09_tensors: z-scores and averages of a %dx%d table within %.1e, every best subject found\n", kStudents, kSubjects, off);
    return 0;
}
