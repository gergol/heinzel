# Heinzel rewrite — Google Benchmark perf suite (opt-in, HEINZEL_BUILD_BENCHMARKS=ON).
#
# Per D-8 (Plan/archive/12-Handoff-Next-Session.md §3.1): the yardstick is the competition, not the old
# `main` tree. Every ported bench carries its Heinzel arm(s) (`heinzel` = the nest path, F32, no
# kernel) alongside plain-C++ reference arms — `naive`, `hand[-variant]`, `std-nth_element` —
# and, opt-in, Eigen (HEINZEL_BENCH_COMPARE_EIGEN). Arm names follow
# `<workload>/<arm>[/<size>]`, one path segment for `<arm>`, size always last
# (tools/bench_report.py depends on this).
#
# The klang / KFR / FAUST / JUCE comparison benches (Plan/archive/12 §3.3, Plan/23 P7): each carries the competitor
# library's arms, main's library-independent hand references, main's hand-written baseline arms where it had them,
# and Heinzel arms written in the DSL (main's called a heinzel::dsp library this tree does not have). All four stay
# opt-in, default OFF, pinned to the SAME tag/commit as `main`, and fetch nothing when
# `-DFETCHCONTENT_SOURCE_DIR_<NAME>=<on-disk checkout>` is given.

include(FetchContent)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.9.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(benchmark)

# Google Benchmark v1.9.0 builds itself with -pedantic-errors; Clang 22 newly flags its
# `__COUNTER__` preprocessor use as a C2y extension, which -pedantic-errors then promotes to an
# error. Silence just that one extension on the benchmark library targets (a no-op on older
# clang/gcc). Copied verbatim from ctg3 cmake/deps.cmake so both trees build the dep identically.
foreach(_bm benchmark benchmark_main)
    if(TARGET ${_bm})
        target_compile_options(${_bm} PRIVATE -Wno-c2y-extensions)
    endif()
endforeach()

# Eigen: a benchmark-only GEMM reference for bench_contraction's `eigen` arm. Header-only; we use its
# source include dir directly (not its CMake targets) exactly as build/main-tree/cmake/deps.cmake
# does, so it never enters the library or the test suite. Opt-in, default OFF, so a plain configure
# fetches nothing beyond Google Benchmark.
option(HEINZEL_BENCH_COMPARE_EIGEN "Fetch Eigen 3.4.0 and build bench_contraction's eigen arm" OFF)
if(HEINZEL_BENCH_COMPARE_EIGEN)
    FetchContent_Declare(
        eigen
        GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
        GIT_TAG        3.4.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_GetProperties(eigen)
    if(NOT eigen_POPULATED)
        FetchContent_Populate(eigen)
    endif()
endif()

# The `Machine` every Heinzel arm plans for (`bench_machine.hpp`): one of `schedule/machine.hpp`'s values by name —
# `zen2`, or `m1_pro` on the M1 (Plan/23 P6) — or `host`, this machine as HEINZEL_AUTOTUNE measured it (Plan 24).
# `-march=native` stays: the instruction set is the host's either way.
set(HEINZEL_BENCH_MACHINE "zen2" CACHE STRING "the heinzel::Machine the benchmark arms plan for: zen2, m1_pro or host")
set_property(CACHE HEINZEL_BENCH_MACHINE PROPERTY STRINGS zen2 m1_pro host)
if(HEINZEL_BENCH_MACHINE STREQUAL "host" AND NOT HEINZEL_AUTOTUNE)
    message(FATAL_ERROR "HEINZEL_BENCH_MACHINE=host plans for the machine HEINZEL_AUTOTUNE measures: configure with "
        "-DHEINZEL_AUTOTUNE=ON, or name zen2 or m1_pro.")
endif()

# One executable per ported translation unit, each with its own BENCHMARK_MAIN() — the same
# one-binary-per-bench shape the old suite uses, so run-benchmarks-style grouping lines up. Every TU
# links the `heinzel` INTERFACE library (which injects -std=c++2c, the constexpr budget and the
# mandatory -ffp-contract=off) plus benchmark::benchmark. The rewrite has no separate heinzel::dsp
# target — dsp/oscillator.hpp lives inside `heinzel`.
function(heinzel_add_benchmark name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE heinzel benchmark::benchmark)
    target_compile_definitions(${name} PRIVATE HEINZEL_BENCH_MACHINE=${HEINZEL_BENCH_MACHINE})
endfunction()

# Ported translation units (added incrementally, one per bsdawl porting tick).
heinzel_add_benchmark(bench_core)
heinzel_add_benchmark(bench_contraction)
if(HEINZEL_BENCH_COMPARE_EIGEN)
    target_include_directories(bench_contraction PRIVATE ${eigen_SOURCE_DIR})
    target_compile_definitions(bench_contraction PRIVATE HEINZEL_BENCH_COMPARE_EIGEN)
endif()
heinzel_add_benchmark(bench_channel_strip)
heinzel_add_benchmark(bench_transcendental)
if(HEINZEL_BENCH_COMPARE_EIGEN)
    target_include_directories(bench_transcendental PRIVATE ${eigen_SOURCE_DIR})
    target_compile_definitions(bench_transcendental PRIVATE HEINZEL_BENCH_COMPARE_EIGEN)
endif()
heinzel_add_benchmark(bench_attention)
heinzel_add_benchmark(bench_oscillator)
heinzel_add_benchmark(bench_wavetable)
target_compile_definitions(bench_wavetable PRIVATE HEINZEL_MAX_CONSTANTS=4096) # a table of 2,048 cells
heinzel_add_benchmark(bench_order_stats)
heinzel_add_benchmark(bench_fusion)
heinzel_add_benchmark(bench_lane_recurrence)
heinzel_add_benchmark(bench_apps)
heinzel_add_benchmark(bench_apps_dsp)
heinzel_add_benchmark(bench_apps_image_ml)
heinzel_add_benchmark(bench_ml_layers)
heinzel_add_benchmark(bench_materialize_break)
heinzel_add_benchmark(bench_image_pipeline)
heinzel_add_benchmark(bench_knobs)
# The 8-band knob EQ: 465 values, 336 nodes and 381 statements, more than the defaults hold (`docs/limitations.md`);
# tree nodes as the knob-math probe had them. Stages and stage statements, which decline, stay at their defaults, so
# that it runs the program a user gets.
target_compile_definitions(bench_knobs PRIVATE HEINZEL_MAX_NODES=512 HEINZEL_MAX_VALUES=512 HEINZEL_MAX_STMTS=512
                                               HEINZEL_MAX_TREE_NODES=1024)

# The comparison targets' library and hand-arm files are compiled at Heinzel's language standard and without its
# numeric flags (`-ffp-contract=off -fno-math-errno`), as the libraries are used (Plan/23 P7); their Heinzel arms are an
# object library of each target, which links `heinzel`.
function(heinzel_compare_target name)
    target_compile_options(${name} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${_std_flag}>)
endfunction()

# klang (nashaudio/klang) — a single-header C++ audio-DSP dialect; see bench_compare_klang.cpp's
# header for the arms and what could not be ported without heinzel::dsp (P7). Header-only, no build
# system of its own: populate the source and use its include dir, force-including a small Linux
# platform shim (benchmarks/klang/klang_shim.h) ahead of klang.h. Pinned to the SAME commit as main
# (the repo has no tags). klang.h only builds under Clang (GCC rejects an anonymous aggregate with
# constructor-bearing members) — skip the target rather than hard-fail so enabling this under GCC is
# a no-op, not a broken build.
option(HEINZEL_BENCH_COMPARE_KLANG "Fetch klang and build bench_compare_klang" OFF)
if(HEINZEL_BENCH_COMPARE_KLANG)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(WARNING "HEINZEL_BENCH_COMPARE_KLANG=ON but the compiler is '${CMAKE_CXX_COMPILER_ID}'; "
                        "klang.h only builds under Clang, so bench_compare_klang is skipped.")
    else()
        FetchContent_Declare(
            klang
            GIT_REPOSITORY https://github.com/nashaudio/klang.git
            GIT_TAG        88b849c38997f4c34cadc4b92d1457e8ef43deea
        )
        FetchContent_GetProperties(klang)
        if(NOT klang_POPULATED)
            FetchContent_Populate(klang)
        endif()
        # Heinzel's arms in an object library that links `heinzel`; klang's file does not, so it is compiled as klang
        # is used, without Heinzel's `-ffp-contract=off` (Plan/23 P7).
        add_library(bench_compare_klang_heinzel OBJECT bench_compare_klang_heinzel.cpp)
        target_link_libraries(bench_compare_klang_heinzel PRIVATE heinzel benchmark::benchmark)
        target_compile_definitions(bench_compare_klang_heinzel PRIVATE HEINZEL_BENCH_MACHINE=${HEINZEL_BENCH_MACHINE})
        add_executable(bench_compare_klang bench_compare_klang.cpp)
        target_link_libraries(bench_compare_klang PRIVATE bench_compare_klang_heinzel benchmark::benchmark)
        heinzel_compare_target(bench_compare_klang)
        # CURRENT_SOURCE_DIR so "klang/klang_shim.h" resolves; klang_SOURCE_DIR for <klang.h>.
        target_include_directories(bench_compare_klang PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR} ${klang_SOURCE_DIR})
    endif()
endif()

# KFR (kfrlib/kfr) — see bench_compare_kfr.cpp's header for the arms and the dropped Heinzel arms
# (P7). The bench needs KFR's separately-compiled DFT library (kfr_dft), which only exists once
# KFR's own CMakeLists runs, so — unlike klang above — let KFR configure via MakeAvailable (with
# everything except DFT off) and link `kfr` (headers) + `kfr_dft`. KFR_ARCH=host detects the actual
# host CPU (matches heinzel::simd's AVX2/256-bit); the default "target" would build only sse2.
# Multiarch OFF => a single libkfr_dft.a. KFR is GPLv2 but stays benchmark-only here (no
# HEINZEL_DSP_FFT_KFR backend option on this branch), so it never enters the shipped library.
option(HEINZEL_BENCH_COMPARE_KFR "Fetch KFR and build the bench_compare_kfr target" OFF)
if(HEINZEL_BENCH_COMPARE_KFR)
    FetchContent_Declare(
        kfr
        GIT_REPOSITORY https://github.com/kfrlib/kfr.git
        GIT_TAG        7.0.1
        GIT_SHALLOW    TRUE
    )
    set(KFR_ARCH              host CACHE STRING "" FORCE)
    set(KFR_ENABLE_MULTIARCH  OFF CACHE BOOL "" FORCE)
    set(KFR_ENABLE_DFT        ON  CACHE BOOL "" FORCE)
    set(KFR_ENABLE_DSP        OFF CACHE BOOL "" FORCE)
    set(KFR_ENABLE_IO         OFF CACHE BOOL "" FORCE)
    set(KFR_ENABLE_AUDIO      OFF CACHE BOOL "" FORCE)
    set(KFR_ENABLE_CAPI_BUILD OFF CACHE BOOL "" FORCE)
    set(KFR_INSTALL_HEADERS   OFF CACHE BOOL "" FORCE)
    set(KFR_INSTALL_LIBRARIES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(kfr) # defines the `kfr` (interface) + `kfr_dft` targets
    # Heinzel's arms in an object library of their own, which links `heinzel`; KFR's and the hand arms' files do not,
    # so they are compiled as KFR is used, without Heinzel's `-ffp-contract=off` (Plan/23 P7). The eight-section
    # cascade (`biquad8/heinzel` and the rows around it) and Freeverb outgrow the default graph and program capacities;
    # they are part of Heinzel's types, so they are raised for every Heinzel file of the target, never for one. A stage's
    # statements, for `eq8fb`: its eight sections and the clip are one feedback cycle. `biquad8/heinzel-param` has 40
    # Params and `x` and `y` for inputs, past the default 32 (`HEINZEL_MAX_IO`).
    add_library(bench_compare_kfr_heinzel OBJECT
        bench_compare_kfr_heinzel.cpp
        bench_compare_kfr_heinzel_channels.cpp
        bench_compare_kfr_heinzel_eq8.cpp
        bench_compare_kfr_heinzel_fir.cpp)
    target_link_libraries(bench_compare_kfr_heinzel PRIVATE heinzel benchmark::benchmark)
    target_compile_definitions(bench_compare_kfr_heinzel PRIVATE HEINZEL_BENCH_MACHINE=${HEINZEL_BENCH_MACHINE}
                                                                 HEINZEL_MAX_VALUES=256 HEINZEL_MAX_NODES=256
                                                                 HEINZEL_MAX_STMTS=256 HEINZEL_MAX_TREE_NODES=1024
                                                                 HEINZEL_MAX_STAGES=24 HEINZEL_MAX_STAGE_STMTS=256
                                                                 HEINZEL_MAX_IO=48)
    add_executable(bench_compare_kfr
        bench_compare_kfr.cpp
        bench_compare_kfr_channels.cpp
        bench_compare_kfr_delay_reverb.cpp
        bench_compare_kfr_eq8.cpp
        bench_compare_kfr_fft.cpp
        bench_compare_kfr_fir.cpp)
    target_link_libraries(bench_compare_kfr PRIVATE bench_compare_kfr_heinzel benchmark::benchmark kfr kfr_dft)
    heinzel_compare_target(bench_compare_kfr)
endif()

# FAUST (https://faust.grame.fr) — see bench_compare_faust.cpp's header for the arms and the dropped
# Heinzel arms (P7). The generated C++ is COMMITTED (benchmarks/faust/generated/, copied verbatim
# from main — host-independent codegen, -march=native at COMPILE time here) — no `faust` binary or
# fetch needed. Each generated TU force-includes the vendored faust_dsp.h.
option(HEINZEL_BENCH_COMPARE_FAUST "Build the bench_compare_faust target from the committed FAUST-generated sources" OFF)
if(HEINZEL_BENCH_COMPARE_FAUST)
    set(_faust_arms ${CMAKE_CURRENT_SOURCE_DIR}/faust/generated/arms.cmake)
    if(NOT EXISTS ${_faust_arms})
        message(FATAL_ERROR "HEINZEL_BENCH_COMPARE_FAUST=ON but ${_faust_arms} is missing.")
    endif()
    include(${_faust_arms}) # sets HEINZEL_FAUST_GENERATED (the per-arm .cpp source list)
    # Heinzel's arms in an object library that links `heinzel` (with the capacities Freeverb needs); FAUST's
    # generated code and its arms' files do not, so they are compiled as FAUST's output is used (Plan/23 P7).
    add_library(bench_compare_faust_heinzel OBJECT bench_compare_faust_heinzel.cpp)
    target_link_libraries(bench_compare_faust_heinzel PRIVATE heinzel benchmark::benchmark)
    target_compile_definitions(bench_compare_faust_heinzel PRIVATE HEINZEL_BENCH_MACHINE=${HEINZEL_BENCH_MACHINE}
                                                                   HEINZEL_MAX_VALUES=256 HEINZEL_MAX_NODES=256
                                                                   HEINZEL_MAX_STMTS=256 HEINZEL_MAX_TREE_NODES=1024
                                                                   HEINZEL_MAX_STAGES=24)
    add_executable(bench_compare_faust
        bench_compare_faust.cpp
        bench_compare_faust_ladder.cpp
        ${HEINZEL_FAUST_GENERATED})
    target_link_libraries(bench_compare_faust PRIVATE bench_compare_faust_heinzel benchmark::benchmark)
    heinzel_compare_target(bench_compare_faust)
    target_include_directories(bench_compare_faust PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/faust)
    # Force-include the vendored FAUST base into each generated TU so the committed output needs no
    # faust headers.
    set_source_files_properties(${HEINZEL_FAUST_GENERATED} PROPERTIES
        COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/faust/faust_dsp.h")
endif()

# Cmajor (https://cmajor.dev) — see bench_compare_cmajor.cpp's header for the arms. The generated C++ is COMMITTED
# (benchmarks/cmajor/generated/, `cmaj generate --target=cpp`; cmajor/README.md): no `cmaj` needed to build. Its arms
# are compiled by a command of their own with exactly the compiler line Cmajor's cpp engine runs, so that none of this
# build's flags (`-march=native`, the build type's, Heinzel's) reach them: `-O3` (the default) and `-O3 -ffast-math`
# (its `-O4`), the second with `-Wno-nan-infinity-disabled`, without which clang 22 refuses Cmajor's own output.
option(HEINZEL_BENCH_COMPARE_CMAJOR "Build bench_compare_cmajor from the committed Cmajor-generated sources" OFF)
if(HEINZEL_BENCH_COMPARE_CMAJOR)
    set(_cmaj_dir ${CMAKE_CURRENT_SOURCE_DIR}/cmajor)
    set(_cmaj_flags -std=c++17 -fPIC "-Wno-#pragma-messages" -Wno-parentheses-equality -Wno-deprecated-declarations
                    -Wno-tautological-compare -Werror -Wall -Wextra)
    set(_cmaj_objects "")
    foreach(_level O3 O4)
        if(_level STREQUAL "O3")
            set(_opt -O3)
        else()
            set(_opt -O3 -ffast-math -Wno-nan-infinity-disabled)
        endif()
        set(_obj ${CMAKE_CURRENT_BINARY_DIR}/cmajor_arms_${_level}.o)
        add_custom_command(
            OUTPUT ${_obj}
            COMMAND ${CMAKE_CXX_COMPILER} ${_opt} ${_cmaj_flags} -c -o ${_obj} ${_cmaj_dir}/arms_${_level}.cpp
            DEPENDS ${_cmaj_dir}/arms_${_level}.cpp ${_cmaj_dir}/arms.inc ${_cmaj_dir}/cmajor_arms.hpp
                    ${_cmaj_dir}/generated/Freeverb.cpp ${_cmaj_dir}/generated/Filter.cpp
            COMMENT "Cmajor arms at ${_level}, Cmajor's compiler line"
            VERBATIM)
        list(APPEND _cmaj_objects ${_obj})
    endforeach()
    add_library(bench_compare_cmajor_heinzel OBJECT bench_compare_cmajor_heinzel.cpp)
    target_link_libraries(bench_compare_cmajor_heinzel PRIVATE heinzel benchmark::benchmark)
    # Freeverb's two rooms: twice the capacities one room takes in the FAUST target; its five smoothers, more buffers.
    target_compile_definitions(bench_compare_cmajor_heinzel PRIVATE HEINZEL_BENCH_MACHINE=${HEINZEL_BENCH_MACHINE}
                                                                    HEINZEL_MAX_VALUES=512 HEINZEL_MAX_NODES=512
                                                                    HEINZEL_MAX_STMTS=512 HEINZEL_MAX_TREE_NODES=2048
                                                                    HEINZEL_MAX_STAGES=48 HEINZEL_MAX_BUFFERS=192)
    add_executable(bench_compare_cmajor bench_compare_cmajor.cpp ${_cmaj_objects})
    target_link_libraries(bench_compare_cmajor PRIVATE bench_compare_cmajor_heinzel benchmark::benchmark)
    heinzel_compare_target(bench_compare_cmajor)
    target_include_directories(bench_compare_cmajor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# JUCE (juce::dsp), the framework an audio developer knows — see bench_compare_juce.cpp's header for the rows. Pinned to
# the same tag as `main`; JUCE builds its `juceaide` tool while configuring (an unbounded nested build: configure with
# CMAKE_BUILD_PARALLEL_LEVEL set). Its modules hold C sources, so C is enabled here, under the option only: every other
# build stays CXX-only. `juce_add_console_app` compiles the modules into the target itself, so they and the JUCE arms
# are compiled as JUCE is used, without Heinzel's flags; Heinzel's arms are the object library (Plan/23 P7).
option(HEINZEL_BENCH_COMPARE_JUCE "Fetch JUCE and build the bench_compare_juce target" OFF)
if(HEINZEL_BENCH_COMPARE_JUCE)
    enable_language(C)
    FetchContent_Declare(
        juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.13
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(juce)
    # The eight-section EQ (`biquad8/heinzel`, `eq8fb/heinzel`) outgrows the default graph and program capacities, and
    # `biquad8/heinzel-param`'s 40 Params the default IO, as in the KFR target; raised for every Heinzel file of the
    # target, never for one.
    add_library(bench_compare_juce_heinzel OBJECT
        bench_compare_juce_heinzel.cpp
        bench_compare_juce_heinzel_channels.cpp
        bench_compare_juce_heinzel_eq8.cpp
        bench_compare_juce_heinzel_filters.cpp)
    target_link_libraries(bench_compare_juce_heinzel PRIVATE heinzel benchmark::benchmark)
    target_compile_definitions(bench_compare_juce_heinzel PRIVATE HEINZEL_BENCH_MACHINE=${HEINZEL_BENCH_MACHINE}
                                                                  HEINZEL_MAX_VALUES=256 HEINZEL_MAX_NODES=256
                                                                  HEINZEL_MAX_STMTS=256 HEINZEL_MAX_TREE_NODES=1024
                                                                  HEINZEL_MAX_STAGES=24 HEINZEL_MAX_STAGE_STMTS=256
                                                                  HEINZEL_MAX_IO=48)
    juce_add_console_app(bench_compare_juce VERSION "1.0.0" PRODUCT_NAME "bench_compare_juce")
    # Beside the other benchmarks, where tools/bench_report.py looks (JUCE's default is `<target>_artefacts/<config>`).
    set_target_properties(bench_compare_juce PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    target_sources(bench_compare_juce PRIVATE
        bench_compare_juce.cpp
        bench_compare_juce_channels.cpp
        bench_compare_juce_eq8.cpp
        bench_compare_juce_filters.cpp)
    target_compile_definitions(bench_compare_juce PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(bench_compare_juce PRIVATE
        bench_compare_juce_heinzel benchmark::benchmark juce::juce_dsp juce::juce_recommended_config_flags)
    heinzel_compare_target(bench_compare_juce)
endif()

# crv (scztt/crv, Scott Carver's ADC 2025 prototype) — see bench_compare_crv_heinzel.cpp's header for the arms; the
# workload is crv's recorded additive synth, moved here from Plan/probes/crv/. Header-only C++26 that only clang 22
# compiles, and only with <cstdint> and <cstring> forced in (crv leans on libc++'s transitive includes): skipped under
# any other compiler, as klang is. crv's arms are compiled with crv's own benchmark flags (without -flto), twice: as crv
# builds them, and with a higher unroll threshold (`bench_compare_crv_unrolled.cpp`), the `crv-unrolled-*` arms.
# Heinzel's and the hand arms are the executable's own file, which links `heinzel`.
option(HEINZEL_BENCH_COMPARE_CRV "Fetch crv and build the bench_compare_crv target" OFF)
if(HEINZEL_BENCH_COMPARE_CRV)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(WARNING "HEINZEL_BENCH_COMPARE_CRV=ON but the compiler is '${CMAKE_CXX_COMPILER_ID}'; "
                        "crv only builds under Clang, so bench_compare_crv is skipped.")
    else()
        FetchContent_Declare(
            crv
            GIT_REPOSITORY https://github.com/scztt/crv.git
            GIT_TAG        1f769ff05ee5ff9f34b05ac726d59e8d9eb48269
        )
        FetchContent_GetProperties(crv)
        if(NOT crv_POPULATED)
            FetchContent_Populate(crv)
        endif()
        set(_crv_flags -std=gnu++26 -D_LIBCPP_ENABLE_EXPERIMENTAL -O3 -ffast-math -fvectorize -fslp-vectorize
                       -funroll-loops -march=native -fomit-frame-pointer "SHELL:-include cstdint" "SHELL:-include cstring"
                       "SHELL:-include crv/prefix.hpp")
        set(_crv_includes ${crv_SOURCE_DIR}/include ${crv_SOURCE_DIR}/benchmarks/support
                          ${crv_SOURCE_DIR}/tests/support ${crv_SOURCE_DIR}/tests)
        add_library(bench_compare_crv_arms OBJECT bench_compare_crv.cpp)
        target_compile_options(bench_compare_crv_arms PRIVATE ${_crv_flags})
        target_include_directories(bench_compare_crv_arms PRIVATE ${_crv_includes})
        add_library(bench_compare_crv_unrolled OBJECT bench_compare_crv_unrolled.cpp)
        target_compile_options(bench_compare_crv_unrolled PRIVATE ${_crv_flags} "SHELL:-mllvm -unroll-threshold=4000")
        target_include_directories(bench_compare_crv_unrolled PRIVATE ${_crv_includes})
        heinzel_add_benchmark(bench_compare_crv_heinzel)
        set_target_properties(bench_compare_crv_heinzel PROPERTIES OUTPUT_NAME bench_compare_crv)
        target_compile_definitions(bench_compare_crv_heinzel PRIVATE HEINZEL_MAX_CONSTANTS=4096)
        target_link_libraries(bench_compare_crv_heinzel PRIVATE heinzel_app bench_compare_crv_arms bench_compare_crv_unrolled)
    endif()
endif()

# The benchmark gate (tools/bench_report.py's `gate` subcommand, which folds in the old
# tools/bench_gate.py): gated rows of baseline-zen2.json against the old tree's medians. Registered
# here, so the default configure still registers zero benchmarks. On a loaded box (a build or a
# `ctest -j` just before it is enough) or an unknown CPU it exits 77, which ctest reports as Skipped
# rather than as a drop that did not happen.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    # A Release build of the pinned old tree, to measure its arms live instead of trusting recorded
    # medians (which do not reproduce across allocators and days): `build/main-bench`, when it exists.
    set(HEINZEL_MAIN_BENCH "${CMAKE_SOURCE_DIR}/build/main-bench" CACHE PATH "a Release build of the pinned old tree")
    set(_main_build "")
    if(EXISTS "${HEINZEL_MAIN_BENCH}/benchmarks")
        set(_main_build --main-build ${HEINZEL_MAIN_BENCH})
    endif()
    add_test(NAME bench_gate
             COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tools/bench_report.py gate --build ${CMAKE_BINARY_DIR} ${_main_build})
    set_tests_properties(bench_gate PROPERTIES LABELS perf RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
else()
    message(STATUS "heinzel: no Python 3 interpreter — the benchmark gate is NOT registered")
endif()
