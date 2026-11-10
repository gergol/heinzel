# Milestone M7.4 / amendment 04 A4-2: the core does not know autodiff exists.
#
# WHY THIS IS THE PROPERTY. `gradients()` is an opt-in feature under `include/heinzel/autodiff/`. A4-2's
# test of success is that "no core header's shape would change if the feature were deleted" — the core
# must not have been shaped by the presence of differentiation. That is exactly the statement that
# autodiff is a LEAF of the include graph: its headers INCLUDE core headers (ir, sema, support), and no
# core header includes one of THEM. If that holds, removing `include/heinzel/autodiff/` leaves every
# other header including exactly what it did, so every core header still compiles and every core test —
# none of which names an autodiff header — still builds. The old tree could not hold this by
# construction (its autodiff reached back into the frontend); this tree can, and this gate is the check
# that it still does rather than a reviewer's memory.
#
# WHAT THIS DOES NOT CLAIM. It proves the include EDGE does not exist; it does not recompile the tree
# with the directory physically removed. That stronger form is unnecessary here precisely because the
# edge is what a compile would exercise: a core header that pulled autodiff in would name it in an
# `#include`, which is what this scans for. The construction half — that the core tests build without
# ever naming autodiff — is the whole non-gradient suite, which this gate does not duplicate.
#
# WHAT IS NOT AN OFFENCE. An autodiff header including another autodiff header, or a core one — that is
# the leaf depending on the trunk, the correct direction. So the scan EXEMPTS `include/heinzel/autodiff`
# itself and reports only a NON-autodiff header that reaches into it. A test may name autodiff freely
# (the gradient tests do); this gate is pointed at `include/` only.
#
# Run with:  cmake -DROOT=<dir> "-DEXEMPT=<path>;<path>" -P autodiff_is_a_leaf.cmake

if(NOT DEFINED EXEMPT)
    set(EXEMPT "")
endif()

file(GLOB_RECURSE _sources "${ROOT}/*.hpp" "${ROOT}/*.cpp")
list(LENGTH _sources _source_count)
if(_source_count EQUAL 0)
    message(FATAL_ERROR "autodiff-leaf gate: no sources under ${ROOT} — a gate that scanned nothing "
                        "reports the same 'passed' as one that scanned everything")
endif()

# An `#include` directive naming a header under `heinzel/autodiff/`, in either quote or angle form.
set(_autodiff_include "^[ \t]*#[ \t]*include[ \t]*[<\"][^\">]*heinzel/autodiff/")

set(_offences "")
set(_offence_count 0)
set(_files_scanned 0)

foreach(source IN LISTS _sources)
    get_filename_component(_absolute "${source}" ABSOLUTE)
    set(_skip FALSE)
    foreach(exempt IN LISTS EXEMPT)
        get_filename_component(_exempt_absolute "${exempt}" ABSOLUTE)
        string(FIND "${_absolute}/" "${_exempt_absolute}/" _at) # a prefix, read literally: a path is no regex
        if(_at EQUAL 0)
            set(_skip TRUE)
        endif()
    endforeach()
    if(_skip)
        continue()
    endif()
    math(EXPR _files_scanned "${_files_scanned}+1")
    file(READ "${source}" _contents)
    string(REPLACE "\r" "" _contents "${_contents}")

    # Peeled a line at a time rather than split into a CMake list, for the reason the sibling gates
    # record: a C++ line's `;` and quoting do not survive CMake list escaping, and the errors are silent.
    string(REGEX MATCHALL "\n" _newlines "${_contents}")
    list(LENGTH _newlines _line_count)
    if(NOT _contents STREQUAL "" AND NOT _contents MATCHES "\n$")
        math(EXPR _line_count "${_line_count}+1")
    endif()

    set(_rest "${_contents}")
    set(_number 0)
    while(_number LESS _line_count)
        math(EXPR _number "${_number}+1")
        string(FIND "${_rest}" "\n" _break)
        if(_break LESS 0)
            set(_line "${_rest}")
            set(_rest "")
        else()
            string(SUBSTRING "${_rest}" 0 ${_break} _line)
            math(EXPR _after "${_break}+1")
            string(SUBSTRING "${_rest}" ${_after} -1 _rest)
        endif()

        if(_line MATCHES "${_autodiff_include}")
            set(_offences "${_offences}\n  ${source}:${_number}: a core header includes an autodiff header")
            math(EXPR _offence_count "${_offence_count}+1")
        endif()
    endwhile()
endforeach()

if(_offence_count GREATER 0)
    message(FATAL_ERROR
        "autodiff-leaf gate (milestone M7.4, amendment 04 A4-2):${_offences}\n"
        "`gradients()` is an opt-in feature: its headers include the core, and no core header may "
        "include one of them. This edge means the core was shaped by differentiation — deleting "
        "`include/heinzel/autodiff/` would now stop a core header compiling, which A4-2 forbids.")
endif()

message(STATUS "autodiff-leaf gate: ${_files_scanned} core files under ${ROOT}, none reaches into "
               "heinzel/autodiff — the feature is a leaf and its deletion changes no core header")
