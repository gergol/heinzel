# Milestone M3.5 / amendment 05 A5-2: no source under `include/` asks the preprocessor which
# instruction set it is being compiled for.
#
# WHY THIS IS THE PROPERTY AND NOT "SUPPORT AVX". Amendment 05 asks for the SEAM now and the
# instruction sets later. A library that reaches AVX with `#ifdef __AVX2__` has decided, permanently,
# that the way to have a second implementation of an operation is a preprocessor branch — and then
# every operation gains one, they cannot be tested against each other in a single build, and the
# choice is made by the flags the whole translation unit happened to be compiled with rather than by
# anything the program said. The old tree's `kernel/stage.hpp` is 2,456 lines partly for this reason.
#
# What the hardware can do is data instead: a `Machine` (`schedule/machine.hpp`) the planner reads. Two
# machines can exist in one build, be handed the same graph, and their programs compared against each
# other — which is a thing no preprocessor branch can be. So this gate says: whatever else arrives, it
# does not arrive that way.
#
# WHAT IS NOT AN OFFENCE. Asking the preprocessor about the TOOLCHAIN — `__cpp_exceptions`,
# `__has_include` — is a different question, answered once, about what exists rather than about what
# is fast. `no_constant_evaluation_branch.cmake` draws the same line for the same reason. And naming
# an instruction set in PROSE is not an offence either; this file names several.
#
# The macros are matched inside a preprocessor CONDITIONAL specifically. A comment mentioning
# `__AVX2__`, or a string, is somebody explaining themselves.
#
# Run with:  cmake -DROOT=<dir> "-DEXEMPT=<path>;<path>" -P no_instruction_set_macros.cmake

if(NOT DEFINED EXEMPT)
    set(EXEMPT "")
endif()

file(GLOB_RECURSE _sources "${ROOT}/*.hpp" "${ROOT}/*.cpp")
list(LENGTH _sources _source_count)
if(_source_count EQUAL 0)
    message(FATAL_ERROR "instruction-set gate: no sources under ${ROOT} — a gate that scanned "
                        "nothing reports the same 'passed' as one that scanned everything")
endif()

# The families, not an exhaustive list of every macro any compiler has ever defined.
#
# THE TRAILING UNDERSCORES ARE NOT UNIFORM and that is worth spelling out, because the first draft of
# this line assumed they were and silently missed half the fixture. `__AVX2__` and `__FMA__` carry
# them; `__ARM_NEON` and `__ARM_FEATURE_SVE` do not. So the trailing pair is optional and the
# alternatives are ordered longest-first, since the engine takes the first alternative that matches
# and `__AVX` would otherwise be reported for `__AVX512F__`.
set(_instruction_set "(HEINZEL_(FMA|ROUNDING)_TWIN|__(FP_FAST_FMA[FL]?|AVX512[0-9A-Z]*|AVX2|AVX|SSE4_2|SSE4_1|SSSE3|SSE3|SSE2|SSE|MMX|FMA4|FMA|F16C|BMI2|BMI|AES|POPCNT|ALTIVEC|VSX|ARM_NEON|ARM_FEATURE_[0-9A-Z_]+)(__)?)")

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
    string(REPLACE "\\" " " _contents "${_contents}")
    string(REPLACE "\r" "" _contents "${_contents}")

    # Peeled a line at a time rather than split into a CMake list, for the reason
    # `structure_limits.cmake` records at length: a C++ line usually ends in `;`, the escaping that
    # would need does not survive real source, and the resulting errors are silent.
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

        if(_line MATCHES "^[ \t]*#[ \t]*(if|ifdef|ifndef|elif)" AND _line MATCHES "${_instruction_set}")
            string(REGEX MATCH "${_instruction_set}" _which "${_line}")
            set(_offences "${_offences}\n  ${source}:${_number}: a preprocessor conditional on `${_which}`")
            math(EXPR _offence_count "${_offence_count}+1")
        endif()
    endwhile()
endforeach()

if(_offence_count GREATER 0)
    message(FATAL_ERROR
        "instruction-set gate (milestone M3.5, amendment 05 A5-2):${_offences}\n"
        "What the hardware can do is data — a `Machine` the planner reads — and not a preprocessor "
        "branch. Two machines can exist in one build, be given the same graph, and their programs "
        "compared against each other. Two sides of an `#ifdef` cannot, and whichever one is compiled "
        "is decided by the flags the translation unit happened to get rather than by anything the "
        "program said.")
endif()

message(STATUS "instruction-set gate: ${_files_scanned} files under ${ROOT}, no conditional on an "
               "instruction set")
