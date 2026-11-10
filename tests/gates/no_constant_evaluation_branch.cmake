# Milestone M2.2: no source under `include/` branches on whether it is being constant-evaluated.
#
# WHY THIS IS LOAD-BEARING AND NOT STYLISTIC. This tree's central check is that a graph built while
# compiling equals the same graph built while running, and that the two produce the same numbers
# (oracle O2). A function containing `if consteval` has two bodies BY CONSTRUCTION, and comparing
# two paths that were written to differ proves nothing about either. So the property that makes O2
# worth running is that there is exactly one body, and this is what enforces it.
#
# The old tree's CI carries the same gate for the same reason (05-Parity-Inventory.md §5.13).
#
# What is NOT an offence: asking the PREPROCESSOR about the toolchain. `#if defined(__cpp_exceptions)`
# in support/fail.hpp chooses between throwing and trapping because one of them does not exist on a
# target with exceptions off. That is a question about the build, answered once, and both answers
# behave identically in both worlds. The three constructs below are different: they ask, at the point
# of use, which world this call is in, and then do different things.
#
# Run with:  cmake -DROOT=<dir> -P no_constant_evaluation_branch.cmake

file(GLOB_RECURSE _sources "${ROOT}/*.hpp" "${ROOT}/*.cpp")
list(LENGTH _sources _source_count)
if(_source_count EQUAL 0)
    message(FATAL_ERROR "constant-evaluation gate: no sources under ${ROOT} — a gate that scanned "
                        "nothing reports the same 'passed' as one that scanned everything")
endif()

set(_offences "")

foreach(source IN LISTS _sources)
    # The one allowed file, when the caller names it: `runtime/hardware_twins.hpp`, whose two bodies
    # ARE compared with each other (`unit/hardware_twins_test.cpp`), which is what the rule is for.
    get_filename_component(_real "${source}" REALPATH)
    if(DEFINED EXEMPT AND _real STREQUAL EXEMPT)
        continue()
    endif()
    file(READ "${source}" contents)
    # Prose about constant evaluation is fine, and this tree does a lot of it.
    string(REGEX REPLACE "//[^\n]*" "" code "${contents}")
    string(REGEX REPLACE "/\\*([^*]|\\*[^/])*\\*/" "" code "${code}")

    foreach(construct "std::is_constant_evaluated" "__builtin_is_constant_evaluated" "if consteval" "if !consteval")
        string(FIND "${code}" "${construct}" _at)
        if(NOT _at EQUAL -1)
            list(APPEND _offences "${source}: ${construct}")
        endif()
    endforeach()
endforeach()

if(_offences)
    string(REPLACE ";" "\n  " _report "${_offences}")
    message(FATAL_ERROR
        "a source branches on whether it is being constant-evaluated:\n  ${_report}\n"
        "Two bodies selected by which world the call is in cannot be compared against each other, "
        "and comparing the compile-time and run-time construction paths is this tree's central "
        "check. Write one body. If the two worlds genuinely need different code, that is a design "
        "question and not a branch — say so where the decision is recorded.")
endif()

message(STATUS "gate passed: ${_source_count} files, none branches on constant evaluation")
