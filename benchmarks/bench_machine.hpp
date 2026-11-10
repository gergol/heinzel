#pragma once

// The machine every Heinzel arm plans for — `Executor<…, heinzel_bench::kMachine>`, `kProgramOf<G, kMachine>` — named
// by CMake's `HEINZEL_BENCH_MACHINE`: one of `schedule/machine.hpp`'s values, `zen2` by default and `m1_pro` on the M1
// (Plan/23 P6), or `host`, the machine HEINZEL_AUTOTUNE measured (Plan 24). The instruction set is `-march=native`'s
// either way.

#include "heinzel/schedule/machine.hpp"
// `heinzel::host`, where the option put the header it generated on the path.
#if __has_include("heinzel/host_machine.hpp")
#include "heinzel/host_machine.hpp"
#endif

#ifndef HEINZEL_BENCH_MACHINE
#define HEINZEL_BENCH_MACHINE zen2
#endif

namespace heinzel_bench {

inline constexpr const heinzel::Machine& kMachine = heinzel::HEINZEL_BENCH_MACHINE;

} // namespace heinzel_bench
