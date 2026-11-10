// Do the two worlds agree at an input where a fused multiply-add would separate them?
//
// This tree's central claim is that a graph evaluated WHILE COMPILING and the same graph evaluated
// WHILE RUNNING produce the same bits. A compiler is permitted to contract `a * b + c` into a single
// fused instruction, and a fused product keeps bits that a rounded product throws away — but a
// compiler's CONSTANT EVALUATOR never contracts, because a constant expression is defined in terms
// of the abstract machine. So on a target that has the instruction, the same expression answers two
// different things in the two worlds, and nothing in the source says so.
//
// `a * a - 1.0` with `a = 1 + 2^-27`. The exact product is `1 + 2^-26 + 2^-54`; a double cannot hold
// the last term, so a ROUNDED product drops it and the answer is `2^-26`, while a FUSED product
// keeps it and the answer is `2^-26 + 2^-54`. One ulp, from one instruction selection.
//
// Read the exit code, not the output: 0 the two worlds agree, 1 they differ. Both are meaningful —
// this file is compiled twice, once with the flag the library sets and once deliberately without it,
// and the run without it is REQUIRED to differ. A check that has only ever been seen to pass says
// nothing about whether the flag is doing any work.
#include <bit>
#include <cstdint>
#include <cstdio>

int main() {
    constexpr double kOnePlusHalfAnUlp = 0x1.0000002p0;                    // 1 + 2^-27
    constexpr double in_the_compiling_world = kOnePlusHalfAnUlp * kOnePlusHalfAnUlp - 1.0;

    // `volatile` so the running world really computes it. Without it the optimiser is free to fold
    // the expression using the same constant evaluator, and the probe would compare a value against
    // itself and always agree.
    volatile double a = kOnePlusHalfAnUlp;
    const double in_the_running_world = a * a - 1.0;

    const bool agree = std::bit_cast<std::uint64_t>(in_the_compiling_world)
                       == std::bit_cast<std::uint64_t>(in_the_running_world);
    std::printf("%s compiling=%a running=%a\n", agree ? "AGREE" : "DIFFER", in_the_compiling_world,
                in_the_running_world);
    return agree ? 0 : 1;
}
