# Two properties of the frontend that are meant to stay true, written as a gate because both are the
# kind that erode quietly.
#
# 1. THE FRONTEND DOES NOT DECIDE TYPES. The old tree's capture layer carried a promotion rule and
#    inserted conversion nodes while recording what the user wrote — a semantic decision executing in
#    the layer whose only job is to record. Here that rule is a column of the rule table and the pass
#    applies it. So no conversion is emitted during capture, ever.
#
# 2. NO DEFERRED-OPERATION MACHINERY. The old tree needed to record operations made before a graph
#    was reachable and replay them later. Under self-constructing leaves there is nothing to defer,
#    so none of it exists. This gate is what keeps someone from reintroducing a small version of it
#    to solve a problem that has already been solved differently.
#
# Every header under ROOT (`frontend/`) is scanned, file by file. "pending" is matched where no lower-case letter
# comes before it, and "Pending" anywhere: so "appending" and "depending" in code pass, while `pending_ops`,
# `m_pending`, `pendingOps`, `kMaxPending` and `isPending`, the likely names of deferred machinery, do not
# (CMake's regex has no `\b`).
#
# Run with:  cmake -DROOT=<include/heinzel/frontend> -P frontend_decides_nothing_semantic.cmake

file(GLOB_RECURSE FRONTEND_SOURCES "${ROOT}/*.hpp")
if(NOT FRONTEND_SOURCES)
    message(FATAL_ERROR "frontend gate: no headers under ${ROOT} — a gate that scanned nothing reports the "
                        "same 'passed' as one that scanned everything")
endif()

foreach(source IN LISTS FRONTEND_SOURCES)
    file(READ "${source}" contents)
    string(REGEX REPLACE "//[^\n]*" "" code "${contents}")

    if(code MATCHES "op::Cast")
        message(FATAL_ERROR
            "${source} emits a conversion.\n"
            "Deciding an element type is the analysis pass's job, reading the rule table's element-type "
            "column. A conversion the user asked for is spelled by the user; one the frontend inserts on "
            "their behalf is the thing this tree removed.")
    endif()

    if(code MATCHES "(^|[^a-z])pending|Pending")
        message(FATAL_ERROR
            "${source} contains deferred-operation machinery.\n"
            "A leaf builds its own one-node graph, so an operation always has somewhere to go and there "
            "is nothing to record for later. If something seems to need deferring, that is worth "
            "understanding before it is worked around.")
    endif()
endforeach()

message(STATUS "gate passed: the frontend records, and decides nothing semantic")
