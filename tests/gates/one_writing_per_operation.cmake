# Milestone M3.1: an operation's arithmetic is written in exactly one place.
#
# THE PROPERTY. `runtime/compute.hpp` says what `_add` does. Every consumer reaches that writing —
# the walk, which reads `node.op` while the program runs, and the loop tree's runner, in which the
# operation is a compile-time constant. A second writing is what the old tree acquired when it grew a
# generated path beside an interpreted one, and undoing it took a whole project: the
# executor-unification work that "killed every `*Dyn` body twin", closed 2026-07-22.
#
# WHAT IS CHECKED, and it is a proxy rather than the property itself. Arithmetic that is specific to
# an operation has to say WHICH operation it is for, so a second writing cannot avoid naming
# operation codes. This gate therefore allows exactly one file under ROOT to name one, and that file
# is WRITING. It is the same shape as `no_per_op_branches.cmake` next door, generalised from one file to
# a whole tree, and it does not replace that gate: the analysis pass and the planner must name NO
# operation at all, which is a stronger claim than this one and has its own message.
#
# WHAT IT DOES NOT CATCH, said plainly: arithmetic that dispatches through a table of function
# pointers rather than by naming codes. Nothing in this tree does that, and if something ever does,
# this gate is where the answer belongs.
#
# EXEMPT is a list of paths; a file is skipped when it IS one or lives under one. The call in
# `tests/CMakeLists.txt` exempts four, each naming operations for a reason that is not arithmetic:
# `ir/op_rules.hpp` IS the table and its lockstep assertion has to name every code;
# `frontend/graph_builder.hpp` and `frontend/ops.hpp` name operations to RECORD what the user wrote;
# `autodiff/` names them to BUILD the adjoint nodes. Each node they make still runs through WRITING.
#
# `tests/` is deliberately outside this gate. A hand-written reference implementation is what a
# parity test IS — `tests/parity/compare.cmake` turns on having one — so a second writing there is
# the point rather than the defect.
#
# Run with:  cmake -DROOT=<dir> -DWRITING=<file> "-DEXEMPT=<path>;<path>" -P one_writing_per_operation.cmake

if(NOT DEFINED EXEMPT)
    set(EXEMPT "")
endif()

file(GLOB_RECURSE _sources "${ROOT}/*.hpp" "${ROOT}/*.cpp")
list(LENGTH _sources _source_count)
if(_source_count EQUAL 0)
    message(FATAL_ERROR "one-writing gate: no sources under ${ROOT} — a gate that scanned nothing "
                        "reports the same 'passed' as one that scanned everything")
endif()

get_filename_component(_writing "${WRITING}" ABSOLUTE)

set(_offences "")

foreach(source IN LISTS _sources)
    get_filename_component(_absolute "${source}" ABSOLUTE)
    if(_absolute STREQUAL _writing)
        continue()
    endif()
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

    file(READ "${source}" contents)
    # Prose naming an operation is fine and is encouraged, so comments go first.
    string(REGEX REPLACE "//[^\n]*" "" code "${contents}")
    string(REGEX REPLACE "/\\*([^*]|\\*[^/])*\\*/" "" code "${code}")

    if(code MATCHES "op::[A-Za-z]")
        string(REGEX MATCH "op::[A-Za-z]+" offender "${code}")
        list(APPEND _offences "${source}: names ${offender}")
    endif()
endforeach()

if(_offences)
    string(REPLACE ";" "\n  " _report "${_offences}")
    message(FATAL_ERROR
        "an operation's arithmetic may be written in exactly one place, and that place is ${WRITING}:\n  ${_report}\n"
        "Arithmetic that is specific to an operation has to name the operation, so naming one here "
        "means either a second writing of something ${WRITING} already says, or a decision about a "
        "particular operation that should have been a column of the rule table. Both drift. Call the "
        "one writing instead — in the loop tree's runner it folds away to the chosen branch, because "
        "the operation is a compile-time constant there.")
endif()

message(STATUS "one writing per operation: ${_source_count} files, only ${WRITING} names an operation")
