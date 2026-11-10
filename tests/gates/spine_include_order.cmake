# The spine's include order: which directory under include/heinzel/ may include which.
#
# A layer reads the layers before it and never one after it: ir -> sema -> schedule -> runtime ->
# loops, with the authoring layers (frontend, notation, dsp) reaching the spine through `ir` only.
# `units`, the named durations, is read by the frontend for a lag of a duration (`n - 3.0 * ms`).
# The table below is the whole rule. A directory it does not list is not checked; a listed directory
# that does not exist yet is checked from the day it appears.
#
# The loop tree (`Plan/archive/13-loop-tree/01` §5): `loops` reads what an operation means in `runtime` (and, from its
# planner on, `schedule/machine.hpp` — the `loops_closure` gate holds it to that one file and the rest of its
# allowlist).
#
# The rewrite layer (`Plan/23-audio-scheduler/03`) rewrites an analysed graph before the planner sees it: it reads the
# graph facts of `loops/graph/` and nothing else of `loops/`, so that it cannot reach the planner it runs before, and
# nothing of the runtime: what it emits is the same program in every build.
#
# The proof of ranges (`Plan/26-value-ranges/03`) reads the graph, and of the runtime only the operations' own writing
# (`runtime/compute.hpp`), with which a monotone rule computes its ends.
#
#   cmake -DROOT=<include/heinzel> -P spine_include_order.cmake

if(NOT DEFINED ROOT)
    message(FATAL_ERROR "spine_include_order gate: ROOT (the include/heinzel directory) is required")
endif()

set(_allowed_support  support)
set(_allowed_ir       ir support)
set(_allowed_sema     sema ir support)
set(_allowed_schedule schedule ir support)
set(_allowed_runtime  runtime ir support)
set(_allowed_frontend frontend units ir support)
set(_allowed_units    units support)
set(_allowed_notation notation frontend ir support)
set(_allowed_dsp      dsp notation frontend ir support)
set(_allowed_loops    loops runtime schedule ir support)
set(_allowed_rewrite  rewrite loops schedule sema ir support)
set(_allowed_ranges   ranges runtime ir support)

set(_offences "")
set(_files_scanned 0)
foreach(layer support ir sema schedule runtime units frontend notation dsp loops rewrite ranges)
    file(GLOB_RECURSE _sources "${ROOT}/${layer}/*.hpp")
    foreach(source IN LISTS _sources)
        math(EXPR _files_scanned "${_files_scanned}+1")
        # Matched out of the whole text, not split into lines: a C++ line's `;` and `[` do not survive
        # becoming a CMake list (the sibling gates record the same hazard). A quoted include that does
        # not start with `heinzel/` would be a relative path around the table, so it is an offence too.
        # Not anchored to the start of a line (`/* c */ #include` is C++); a path holding `..` names no
        # layer either; and an include spelled any way but `#include "` or `#include <` (a macro, a
        # comment in between, `#include_next`) matches one of the bare alternatives and is an offence. No
        # match may hold a backslash: at the end of a list item it would swallow the separator.
        # Fails closed: the word `#include` in a comment or a string literal trips it too — reword it.
        file(READ "${source}" _text)
        string(REGEX REPLACE "\\\\\r?\n" "" _text "${_text}") # a spliced line is one line
        string(REGEX REPLACE "[][;]" "_" _text "${_text}")     # no match may carry a list metacharacter
        string(REGEX MATCHALL "(#|%:)[ \t]*include[ \t]*[<\"][^\">\n\\]*|(#|%:)[ \t]*include|(#|%:)[ \t]*/[*]" _includes "${_text}")
        foreach(include IN LISTS _includes)
            if(include MATCHES "<" AND NOT include MATCHES "<heinzel/|\\.\\.")
                continue() # a system header
            endif()
            if(include MATCHES "\\.\\.")
                set(_target "")
            elseif(include MATCHES "[<\"]heinzel/([^/\">]+)/")
                set(_target "${CMAKE_MATCH_1}")
            else()
                set(_target "")
            endif()
            list(FIND _allowed_${layer} "${_target}" _found)
            if(layer STREQUAL "rewrite" AND _target STREQUAL "loops" AND NOT include MATCHES "heinzel/loops/graph/")
                set(_found -1) # of the loop tree, the rewrite layer reads the graph facts alone
            endif()
            if(_found EQUAL -1)
                string(STRIP "${include}" _shown)
                string(APPEND _offences "\n  ${source}: ${layer}/ has ${_shown}")
            endif()
        endforeach()
    endforeach()
endforeach()

if(_files_scanned EQUAL 0)
    message(FATAL_ERROR "spine_include_order gate: no headers under ${ROOT} — a gate that scanned "
                        "nothing reports the same 'passed' as one that scanned everything")
endif()
if(NOT _offences STREQUAL "")
    message(FATAL_ERROR "spine_include_order gate:${_offences}\n"
                        "A layer includes a layer it may not read. The allowed edges are the table at "
                        "the top of tests/gates/spine_include_order.cmake.")
endif()
message(STATUS "spine_include_order gate: ${_files_scanned} headers, every include follows the spine's order")
