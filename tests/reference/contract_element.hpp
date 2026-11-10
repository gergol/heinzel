#pragma once

// Which element of a `_matmul` operand one output element sums a contributor from. A general contraction
// reads its two operands at different index maps (`out[b,i,j] = Σ_k a[b,i,k]·b[b,k,j]`), which
// `element_read_by`'s one shared join index cannot express. It names no operation and no attribute case:
// it is handed the operand's binding and answers where the number is.

#include <cstdint>

#include "heinzel/ir/vocab.hpp"
#include "heinzel/support/fail.hpp"

#include "elements.hpp"

namespace heinzel {

// The output element axes are laid out `[batch] ++ [a free] ++ [b free]` (`sema/matmul.hpp`),
// the streamed axis excluded because the walk stands inside the sample loop. For output element `e` and
// contracted index `kc` this fills one operand's own element axes: its contracted axis takes `kc`, each
// batch axis the output's matching leading position, and each free axis its position in this operand's
// free block, which begins at `free_base` (`B` for the first operand, `B + Fa` for the second).
// Recomposed against the operand's extents, innermost outwards like everything in `elements.hpp`. A
// binding that reads past an operand axis stops here rather than returning a plausible wrong element:
// the walk can be reached without the analysis pass.
constexpr std::int32_t contracted_element(const Elements& out, const Elements& from, std::int32_t contract,
                                          const Axes& batch, std::int32_t free_base, std::int32_t kc,
                                          std::int32_t e) {
    std::int32_t out_idx[kMaxRank]{};
    std::int32_t rest = e;
    for (std::int32_t k = out.rank; k > 0; --k) {
        out_idx[k - 1] = rest % out.extent[k - 1];
        rest /= out.extent[k - 1];
    }
    std::int32_t flat = 0;
    std::int32_t free_ord = 0; // this operand's free axes, in its own order, match its output free block
    for (std::int32_t a = 0; a < from.rank; ++a) {
        std::int32_t i = kc;
        if (a != contract) {
            std::int32_t batch_pos = -1;
            for (std::int32_t t = 0; t < batch.rank; ++t) {
                if (batch.axes[t] == a) {
                    batch_pos = t;
                }
            }
            i = batch_pos >= 0 ? out_idx[batch_pos] : out_idx[free_base + free_ord];
            free_ord += batch_pos >= 0 ? 0 : 1;
        }
        if (i < 0 || i >= from.extent[a]) {
            fail("heinzel::contracted_element: a matmul reads past an operand axis");
        }
        flat = flat * from.extent[a] + i;
    }
    return flat;
}

} // namespace heinzel
