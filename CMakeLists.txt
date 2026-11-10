# Heinzel — the rewrite tree.
#
# Build configuration encodes operator amendments 01, 03 and 05:
#   - clang is the required toolchain; gcc is nice-to-have and runs only at milestone boundaries
#     (amendment 01), so nothing here assumes gcc.
#   - the constexpr budget is a CAP, not an allocation: raising it costs nothing on translation
#     units that stay under it (amendment 03 A3-1). It may be raised for a design that is
#     linear-or-better and merely large — never to rescue one that grows super-linearly.
#   - test translation units stay small and numerous, and the compile-time regression guard is a
#     SEPARATE target kept out of the default suite (A3-2).
#   - ccache is part of the documented setup, not a personal alias (A3-3).
cmake_minimum_required(VERSION 3.25)
project(heinzel CXX)

# Off when Heinzel is another project's dependency (add_subdirectory, FetchContent): its tests are not that project's.
option(HEINZEL_BUILD_TESTS "Build the test suite" ${PROJECT_IS_TOP_LEVEL})
option(HEINZEL_BUILD_COMPILE_TIME_GUARD "Build the compile-time regression guard (slow, opt-in)" OFF)
# Regression-guard perf suite (Google Benchmark). OFF by default for the same reason the compile-time
# guard is: the default clang configure + ctest must register zero of these. Turning it on
# adds a benchmarks/ subtree that FetchContents google/benchmark and builds one executable per ported
# bench TU; it adds nothing to the default suite and no ctest.
option(HEINZEL_BUILD_BENCHMARKS "Build the Google Benchmark perf suite (opt-in, fetches google/benchmark)" OFF)
# The end-to-end examples (`examples/`). OFF by default like the two above; turning it on builds one executable
# per example and registers each as a ctest, which fails when the example's result leaves its plain C++ reference.
option(HEINZEL_BUILD_EXAMPLES "Build the end-to-end examples, each a self-checking ctest (opt-in)" OFF)
set(HEINZEL_CXX_STANDARD "26" CACHE STRING "26 or 23 (23 is the portable floor)")

# 2^28. The comment that matters is WHY it is safe to raise: it is a ceiling on constant-evaluation
# work, so translation units below it pay nothing. Capability is never traded for the limit.
#
# A freely chosen number, and raising it is an ordinary thing to do when something wants a bigger
# graph (operator, 2026-08-25) — as is raising kMaxValues / kMaxNodes in ir/ir.hpp, which are the
# other half of the same ceiling. What raising it must never be is a way to keep a super-linear
# design alive (amendment 03): a bigger budget buys a constant factor and moves the cliff two steps.
set(HEINZEL_CONSTEXPR_STEPS "268435456" CACHE STRING "constexpr step / ops limit")

# ---- ccache: measured 3m15s cold -> 0.19s warm at a 100% hit rate on the old tree ---------------
# CCACHE_BASEDIR is set by the developer, not here; without it, hits do not survive a change of
# build directory (measured 1.8%) because absolute include paths enter the hash.
find_program(HEINZEL_CCACHE ccache)
if(HEINZEL_CCACHE)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${HEINZEL_CCACHE}")
    message(STATUS "heinzel: using ccache at ${HEINZEL_CCACHE}")
endif()

# ---- the library --------------------------------------------------------------------------------
add_library(heinzel INTERFACE)
target_include_directories(heinzel INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# The application-layer exports (Q2 = option 1, 2026-09-06): headers that COMPOSE the two core leaves
# — the frontend and autodiff — and therefore live ABOVE the O12/A4-2 boundaries, OUTSIDE include/.
# `app/heinzel/gradients.hpp` is the first: the shippable `gradients()` capture spelling. This is a
# SEPARATE interface library so the core (`heinzel`) never advertises an app header; a consumer that
# wants the spelling links `heinzel_app`, which brings the core headers it composes. The O12/A4-2 gates
# scan the include/ source tree and are untouched by this.
add_library(heinzel_app INTERFACE)
target_include_directories(heinzel_app INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/app)
target_link_libraries(heinzel_app INTERFACE heinzel)

if(HEINZEL_CXX_STANDARD STREQUAL "26")
    set(_std_flag -std=c++2c)
elseif(HEINZEL_CXX_STANDARD STREQUAL "23")
    set(_std_flag -std=c++23)
else()
    message(FATAL_ERROR "HEINZEL_CXX_STANDARD must be 26 or 23 (got '${HEINZEL_CXX_STANDARD}')")
endif()
# Gate the dialect flag to C++ sources: a consumer that also compiles C must not get -std=c++2c
# on its .c files.
target_compile_options(heinzel INTERFACE $<$<COMPILE_LANGUAGE:CXX>:${_std_flag}>)

# The two compilers spell the budget differently. Named once, because the parity reference against
# the old tree cannot link this library — it must see only the old tree's headers — and so needs the
# same flag chosen the same way. Hardcoding clang's spelling there is exactly what the milestone
# gcc check found.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(HEINZEL_CONSTEXPR_BUDGET_FLAG "-fconstexpr-steps=${HEINZEL_CONSTEXPR_STEPS}")
else()
    set(HEINZEL_CONSTEXPR_BUDGET_FLAG "-fconstexpr-ops-limit=${HEINZEL_CONSTEXPR_STEPS}")
endif()
target_compile_options(heinzel INTERFACE $<$<COMPILE_LANGUAGE:CXX>:${HEINZEL_CONSTEXPR_BUDGET_FLAG}>)

# ---- no contraction, and this is not a preference --------------------------------------------------
# A compiler may contract `a * b + c` into one fused multiply-add, which keeps bits a rounded product
# throws away. A compiler's CONSTANT EVALUATOR never does, because a constant expression is defined
# over the abstract machine. So on any target that has the instruction, the same node answers two
# different things while compiling and while running — and this tree's central claim is that it does
# not. Measured on this host, clang 22.1.8 and gcc 15.2.0 alike, `1+2^-27` squared less one:
#
#     -mfma -ffp-contract=fast   compiling 0x1p-26   running 0x1.0000001p-26   DIFFER
#     -mfma -ffp-contract=on     compiling 0x1p-26   running 0x1.0000001p-26   DIFFER   <- clang's default
#     -mfma -ffp-contract=off    compiling 0x1p-26   running 0x1p-26           agree
#
# Until this line the claim held only because baseline x86-64 has no fused multiply-add — an accident
# of the default target, not anything the tree said. `-march=native` on this machine was enough to
# break it. Found at M4 while writing `_fma`, which is the operation that cannot be built at all
# without settling this (`plan/M4-STEPS.md` finding S2-A, and S2b-A for the general case).
#
# INTERFACE, so everything that links the library gets it. The old tree's parity reference
# deliberately does NOT — it is compiled the way the old tree is compiled, and changing that would
# make the comparison a comparison against something nobody runs.
target_compile_options(heinzel INTERFACE $<$<COMPILE_LANGUAGE:CXX>:-ffp-contract=off>)

# `-fno-math-errno`: the library never reads `errno` (nothing under include/ or app/ names it or
# <cfenv>), and while libm must be able to set it the compiler keeps a `call sqrt` on the error path
# of every `__builtin_sqrt` and cannot vectorize a loop of them (measured, clang 22: an 8-lane loop
# is 8 x vsqrtss without the flag, one vsqrtps with it). It changes no value the library computes;
# being INTERFACE it also reaches a consumer's own sources, where `errno` after a libm call goes unset.
target_compile_options(heinzel INTERFACE $<$<COMPILE_LANGUAGE:CXX>:-fno-math-errno>)

# A lane wider than the target's registers (`runtime/lane_value.hpp`: any width compiles anywhere) is
# passed in memory, and clang notes that this "changes the ABI". Every function taking one is an inline
# template of this header-only library, so there is no ABI boundary for it to change.
target_compile_options(heinzel INTERFACE $<$<COMPILE_LANGUAGE:CXX>:-Wno-psabi>)

# The hardware twins (`runtime/hardware_twins.hpp`): at run time a correctly-rounded operation uses
# the processor's instruction instead of its software core. OFF is for a freestanding build, where a
# builtin the instruction set lacks becomes a libm call that may not link.
option(HEINZEL_HARDWARE_TWINS "Use the processor's sqrt/rounding instructions at run time" ON)
if(NOT HEINZEL_HARDWARE_TWINS)
    target_compile_definitions(heinzel INTERFACE HEINZEL_HARDWARE_TWINS=0)
endif()

# Knob math kept and sections in lanes (`docs/knob-math.md`, `docs/sections-in-lanes.md`, "Switch"): OFF compiles the
# feature out of every program (and its own tests out of the suite); every file of one program must agree.
option(HEINZEL_KNOB_MATH "Run knob math only when a Param it watches changed (kept stages)" ON)
if(NOT HEINZEL_KNOB_MATH)
    target_compile_definitions(heinzel INTERFACE HEINZEL_KNOB_MATH=0)
endif()
option(HEINZEL_LANES "Run a cascade of like filter sections skewed in SIMD lanes" ON)
if(NOT HEINZEL_LANES)
    target_compile_definitions(heinzel INTERFACE HEINZEL_LANES=0)
endif()

# The thread pool a run may split its work among (`runtime/thread_pool.hpp`). OFF builds no thread and links
# no thread library: every run is serial, and a program the planner split runs on the calling thread.
option(HEINZEL_HAVE_THREADS "Build the thread pool runs may split their work among (off: every run is serial)" ON)
if(HEINZEL_HAVE_THREADS)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(heinzel INTERFACE Threads::Threads)
    target_compile_definitions(heinzel INTERFACE HEINZEL_HAVE_THREADS)
endif()

# Measuring this machine while configuring (Plan 24, `tools/tune/machine.cmake`): `heinzel::host`, a `Machine` of its
# measured values, in <build>/heinzel_tune/include/heinzel/host_machine.hpp, for code that names it. The default machine
# does not change and the tests never see it. Off by default: it takes minutes, once per build directory.
option(HEINZEL_AUTOTUNE "Measure this machine at configure time into heinzel::host (minutes, once per build dir)" OFF)
set(HEINZEL_AUTOTUNE_WORKERS "" CACHE STRING "Workers the parallel_work probe splits among (empty: every core)")
set(HEINZEL_TUNE_CORE "" CACHE STRING "The CPU the probes run pinned to, with taskset (empty: 4, or the last one)")
if(HEINZEL_AUTOTUNE)
    include(${CMAKE_CURRENT_SOURCE_DIR}/tools/tune/machine.cmake)
    target_include_directories(heinzel INTERFACE ${HEINZEL_TUNE_INCLUDE_DIR}) # `#include "heinzel/host_machine.hpp"`
endif()

if(HEINZEL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(HEINZEL_BUILD_BENCHMARKS)
    # `enable_testing()` here too, so the benchmark gate registers when the tests are off.
    enable_testing()
    add_subdirectory(benchmarks)
endif()

if(HEINZEL_BUILD_EXAMPLES)
    enable_testing()
    add_subdirectory(examples)
endif()
