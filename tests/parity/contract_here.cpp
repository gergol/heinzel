// A contraction of two operands, built in THIS tree's language, run on `Executor`, and its samples printed.
//
// `einsum("c,c->", spread, doubled)`: two operands that carry the same labels in the same order, their product summed
// over the channel axis, which this tree builds as `(spread * doubled).sum(0)`, a multiply and a sum (Decision 78).
// `main` spells the same contraction `einsum("ct,ct->t", …)`. It ran on the walk alone, by hand as `_contr`, until
// the loop tree ran it; at `High` `Executor` gives the walk's numbers bit for bit (`executed.hpp`).
//
// WHAT THIS COMPARISON CANNOT SEE, stated rather than left to be discovered. The three channels carry the same number
// at every sample: the old tree's constant pool holds one 3 x 64 tensor and no more (`contract_there.cpp`), so the
// weight is a half on every channel, and what the contraction shows against the old tree is the fan-in and the sum
// of products rather than a per-channel mapping. The mapping itself is checked where it can be — on
// `folded_element` directly, in `tests/unit/collapse_test.cpp`.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/notation/einsum.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t kBlock = 64;

// A sum of PRODUCTS over three channels: the input scaled by a half on each against the same thing doubled,
// contracted over axis 0 — the CHANNEL axis, since the streamed axis is the innermost one in both trees. BOTH
// operands are wide because that is the old tree's rule for a contraction rather than a choice: its `einsum` refuses
// an index that appears in one operand and not in the output.
constexpr Graph build() {
    Input x{"x", Signal{kBlock}};
    Expr halves = constant({0.5, 0.5, 0.5});
    Expr spread = x * halves;
    Expr doubled = spread * 2.0;
    Output y{"y"};
    y = einsum("c,c->", spread, doubled);
    return y;
}

} // namespace

int main() {
    // The same oracle signal the delay-line parity uses, and for the same reason: it visits many
    // magnitudes and few of its values are exactly representable, so arithmetic arranged differently
    // shows up in the numbers instead of cancelling.
    std::array<double, kBlock> x{};
    for (std::size_t n = 0; n < x.size(); ++n) {
        x[n] = static_cast<double>(0.31f * static_cast<float>((n * 5) % 23) - 2.0f);
    }
    const testing::Outputs y = testing::executed<build>({{"x", x.data()}});
    for (const double sample : y[0]) {
        std::printf("%.9e\n", sample);
    }
    return 0;
}
