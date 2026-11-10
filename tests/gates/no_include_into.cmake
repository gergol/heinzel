# Milestone M8 / O12: a boundary is an include-graph EDGE that must not exist — no header under the
# scanned root may include a header under the forbidden prefix.
#
# The general form of the `autodiff_is_a_leaf` gate (M7.4). It serves both directions the O12 family
# needs, because both are the same include-edge check with ROOT and FORBIDDEN swapped:
#   - the LEAF edge (M8.1): an opt-in layer under `include/heinzel/<layer>/` may INCLUDE the core, but
#     no core header may include one of ITS headers — `core_no_frontend`, `core_no_dsp`, … Scan
#     `include/`, forbid the layer, exempt the layer subtree. The edge's absence is what makes "delete
#     the layer and every core header still compiles" true by construction.
#   - the CAPTURE edge (M8.2): the notation capture layer may be included by others, but must not itself
#     reach into the cost model — `notation_no_cost_model`. Scan the notation subtree, forbid
#     `heinzel/schedule/`. The edge's absence is what keeps capture from consulting a cost model.
# In both, LABEL names the boundary and the message speaks of "the scanned root" and "the forbidden
# prefix" rather than one direction's roles, so it reads correctly whichever way it is pointed.
#
# WHAT THIS DOES NOT CLAIM. It proves the include EDGE does not exist; it does not recompile the tree
# with the layer physically removed. That stronger form is unnecessary precisely because the edge is
# what a compile would exercise: a core header that pulled the layer in would name it in an `#include`,
# which is what this scans for.
#
# WHAT IS NOT AN OFFENCE. Depending in the ALLOWED direction — a layer header including the core, a
# capture header included BY a higher layer. So the scan EXEMPTS any path in EXEMPT (for the leaf edge,
# the protected subtree itself; a header inside it including a sibling is the leaf-on-trunk direction)
# and reports only a scanned header that reaches into the forbidden prefix.
#
# Run with:
#   cmake -DROOT=<dir> -DFORBIDDEN=heinzel/frontend/ -DLABEL="core_no_frontend (M8.1/O12)" \
#         "-DEXEMPT=<path>;<path>" -P no_include_into.cmake
#
# FORBIDDEN is the include-path prefix of the protected subtree, with its trailing slash, in the form
# an `#include` would spell it (e.g. `heinzel/frontend/`). LABEL names the boundary in messages.

if(NOT DEFINED ROOT)
    message(FATAL_ERROR "no_include_into gate: ROOT (the directory to scan) is required")
endif()
if(NOT DEFINED FORBIDDEN OR FORBIDDEN STREQUAL "")
    message(FATAL_ERROR "no_include_into gate: FORBIDDEN (the protected include prefix) is required")
endif()
if(NOT DEFINED LABEL)
    set(LABEL "no_include_into(${FORBIDDEN})")
endif()
if(NOT DEFINED EXEMPT)
    set(EXEMPT "")
endif()

file(GLOB_RECURSE _sources "${ROOT}/*.hpp" "${ROOT}/*.cpp")
list(LENGTH _sources _source_count)
if(_source_count EQUAL 0)
    message(FATAL_ERROR "${LABEL}: no sources under ${ROOT} — a gate that scanned nothing reports the "
                        "same 'passed' as one that scanned everything")
endif()

# Escape the regex-significant characters in FORBIDDEN (`.` above all) so the prefix matches literally.
string(REGEX REPLACE "([][.*+?^$(){}|\\\\])" "\\\\\\1" _forbidden_re "${FORBIDDEN}")
# An `#include` directive naming a header under the FORBIDDEN prefix, in either quote or angle form.
set(_forbidden_include "^[ \t]*#[ \t]*include[ \t]*[<\"][^\">]*${_forbidden_re}")

set(_offences "")
set(_offence_count 0)
set(_files_scanned 0)

foreach(source IN LISTS _sources)
    get_filename_component(_absolute "${source}" ABSOLUTE)
    set(_skip FALSE)
    foreach(exempt IN LISTS EXEMPT)
        if(exempt STREQUAL "")
            continue()
        endif()
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

        if(_line MATCHES "${_forbidden_include}")
            set(_offences "${_offences}\n  ${source}:${_number}: includes ${FORBIDDEN}")
            math(EXPR _offence_count "${_offence_count}+1")
        endif()
    endwhile()
endforeach()

if(_offence_count GREATER 0)
    message(FATAL_ERROR
        "${LABEL}:${_offences}\n"
        "A header under the scanned root reaches into `${FORBIDDEN}`, an include edge this boundary "
        "forbids. The boundary holds only while that edge does not exist — it is what a compile would "
        "exercise, so naming the header in an `#include` is exactly the offence, and O12 forbids it.")
endif()

message(STATUS "${LABEL}: ${_files_scanned} files under ${ROOT}, none reaches into "
               "${FORBIDDEN} — the boundary holds and this edge does not exist")
