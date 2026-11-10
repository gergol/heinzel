#pragma once

// A benchmark's `heinzel` arm and its `-par` twin. The arm runs `loops::run` on its graph's program for the machine it
// is planned for (`kProgramOf<G, M>`: planned and lowered once per graph and machine). The twin (P6,
// `Plan/archive/parallel/06` §1) runs the same program — planned for the bench's machine (`kMachine`), so its stages
// and bands are already split among workers where the rule allows — with a pool of one thread per core of it. The pool
// is made once per binary, here: the library keeps no global, a benchmark may. `tools/bench_report.py` runs the `-par`
// rows on the physical cores, the others pinned to one.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>

#include "heinzel/ir/ir.hpp"
#include "heinzel/loops/planner/plan.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/loops/workspace.hpp"
#include "heinzel/compile.hpp"
#include "heinzel/runtime/thread_pool.hpp"
#include "heinzel/schedule/machine.hpp"
#include "heinzel/support/fail.hpp"

#include "bench_machine.hpp"

namespace heinzel_bench {

namespace loops = heinzel::loops;

// The program of a planned graph, or `fail` with the refusal, so a refused graph stops the build at its arm.
constexpr const loops::Program& program_of(const std::expected<loops::Planned, loops::Refusal>& planned) {
    if (!planned) {
        heinzel::fail(planned.error().view());
    }
    return planned->program;
}

// Graph `G`'s program for machine `M` as `compile` makes it (rewritten, or as written where that is refused): planned
// and lowered once per graph and machine, so that an arm runs what an `Executor` would.
template <const heinzel::Graph& G, const heinzel::Machine& M>
inline constexpr auto kPlanned = heinzel::planned_as_rewritten(G, M).planned;
template <const heinzel::Graph& G, const heinzel::Machine& M>
inline constexpr const loops::Program& kProgramOf = program_of(kPlanned<G, M>);

inline heinzel::ThreadPool& pool() {
    static heinzel::ThreadPool threads(static_cast<unsigned>(kMachine.cores));
    return threads;
}

// How many band buffers a `Par` arm's workspace keeps per split band: one per worker of `pool()`.
template <bool Par>
constexpr unsigned copies() {
    return Par ? static_cast<unsigned>(kMachine.cores) : 1;
}

// Does the program split anything at all? A `-par` arm of one that does not would be the serial arm again.
constexpr bool splits_anything(const loops::Program& p) {
    for (const loops::Stage& stage : p.stages) {
        if (stage.split >= 0) {
            return true;
        }
    }
    for (const loops::BandInfo& band : p.bands) {
        if (band.parallel) {
            return true;
        }
    }
    return false;
}

template <const loops::Program& P, bool Par>
void run(const loops::Feeds& feeds, auto& work) {
    static_assert(!Par || splits_anything(P), "a -par arm of a program that splits nothing is the serial arm again");
    if constexpr (Par) {
        loops::run<P>(feeds, work, pool());
    } else {
        loops::run<P>(feeds, work);
    }
}

// Did a split run leave every output as a serial run leaves it, byte for byte? (A split moves no sum.)
template <const loops::Program& P>
bool same_as_serial(const loops::Feeds& feeds, auto& split, const loops::Sizes& sizes = loops::Sizes{}) {
    loops::Workspace<P> serial(sizes);
    loops::run<P>(feeds, serial);
    for (const std::int32_t out : P.outputs) {
        const auto b = static_cast<std::size_t>(out);
        const auto bytes = static_cast<std::size_t>(loops::element_count(P.buffers[b], sizes) * loops::bytes_of(P.buffers[b].type));
        if (std::memcmp(serial.bytes_of(b), split.bytes_of(b), bytes) != 0) {
            return false;
        }
    }
    return true;
}

} // namespace heinzel_bench
