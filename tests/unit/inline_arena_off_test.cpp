// Built with -DHEINZEL_MAX_INLINE_ARENA=0 (`tests/CMakeLists.txt`): no arena lies inside its workspace, and a static
// program's is the page-aligned allocation placed with the stagger, as before `Plan/18-zero-copy/01`. Then the same
// program runs on `Executor`, against the loop by hand.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "heinzel/compile.hpp"
#include "heinzel/executor.hpp"
#include "heinzel/frontend/declarations.hpp"
#include "heinzel/loops/workspace.hpp"

#include "corpus_graphs.hpp"
#include "planned.hpp"

static_assert(heinzel::loops::kMaxInlineArena == 0, "inline_arena_off: built with -DHEINZEL_MAX_INLINE_ARENA=0");

namespace {

using namespace heinzel;

constexpr Graph half() {
    Input x{"x", Signal{64}};
    Output y{"y"};
    y = x * 0.5;
    return y;
}

inline constexpr Graph kLagOfALag = corpus::lag_of_a_lag();
inline constexpr planned::Held kHeld{planned::program_of(kLagOfALag, zen2)};
constexpr const loops::Program& P = kHeld.program;

int g_wrong = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        ++g_wrong;
        std::printf("inline_arena_off: %s\n", what);
    }
}

} // namespace

int main() {
    static_assert(loops::kKindOf<P> == loops::Kind::Static && !loops::kInlineOf<P>);
    loops::Workspace<P> work;
    const auto* object = reinterpret_cast<const std::byte*>(&work);
    constexpr auto paged = loops::place(loops::kPiecesOf<P>, 1, 0, loops::Sizes{}, false);
    expect(work.arena() != nullptr && (work.arena() + work.size_in_bytes() <= object || work.arena() >= object + sizeof work),
           "a static program's arena lies inside its workspace");
    expect(reinterpret_cast<std::uintptr_t>(work.arena()) % loops::kPage == 0 && work.size_in_bytes() == paged.size,
           "the arena is not page-aligned, or not the size the stagger places");
    for (std::size_t k = 0; k < loops::kPiecesOf<P>.size(); ++k) {
        expect(loops::kPlacementOf<P, 1, 0>.at[k] == paged.at[k], "a piece is not where the stagger places it");
    }

    std::vector<float> x(64);
    std::vector<float> y(64);
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] = 0.25f * static_cast<float>(i) - 3.0f;
    }
    Executor<half> ex;
    ex.input("x") = x.data();
    ex.output("y") = y.data();
    ex.run();
    for (std::size_t i = 0; i < y.size(); ++i) {
        expect(y[i] == x[i] * 0.5f, "Executor<half> differs from the loop by hand");
    }
    if (g_wrong != 0) {
        return 1;
    }
    std::printf("inline_arena_off: every static arena on the heap, placed with the stagger; a run as by hand\n");
    return 0;
}
