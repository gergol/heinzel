// USES OF `loops::Workspace` THAT MUST NOT COMPILE (Plan/15-zero-overhead/03).
//
// Each case is built as its own target (`-DCASE=<k>`, tests/CMakeLists.txt) that must fail with one error:
//   1. `set_length` on a program with no stream: only a program with one symbol, on time, has a length to set.
//   2. `bind` of a buffer `kInPlaceOf` does not hold (here an output read in its own past): only such an output may be
//      written in the caller's memory (Plan/18-zero-copy/02).
#include "heinzel/compile.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/workspace.hpp"

using namespace heinzel;

constexpr Index n{};

constexpr Graph comb() {
    Input x{"x", Signal{64}};
    Output y{"y"};
    y[n] = x[n] + 0.5 * y[n - 8]; // (eight back: float, as `zero_copy_test`'s comb)
    return y;
}
constexpr Compiled kComb = compile(comb());

int main() {
#if CASE == 1
    loops::Workspace<kComb.program> work;
    work.set_length(16);
#elif CASE == 2
    loops::Workspace<kComb.program> work;
    float y[64];
    work.bind(std::integral_constant<std::size_t, static_cast<std::size_t>(kComb.program.outputs[0])>{}, y);
#endif
    return 0;
}
