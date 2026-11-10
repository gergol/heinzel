# The property milestone M1 condition 3 actually IS: the analysis pass interprets declared rules,
# and contains no branch about a particular operation. Written as a gate rather than a review note,
# because it is the kind of thing that erodes one convenient special case at a time.
#
# What it looks for: any mention of a specific operation code in the pass. The pass is allowed to
# read `n.op` and hand it to the rule table; it is not allowed to ask whether it is `op::Add`.
#
# Run with:  cmake -DPASS_SOURCE=<file> -P no_per_op_branches.cmake
#       or:  cmake -DPASS_ROOT=<dir> -P no_per_op_branches.cmake   (every header in it)

if(DEFINED PASS_ROOT)
    file(GLOB _sources "${PASS_ROOT}/*.hpp")
    if(NOT _sources)
        message(FATAL_ERROR "no-per-op gate: no headers under ${PASS_ROOT}, and a gate that read nothing passes nothing")
    endif()
else()
    set(_sources "${PASS_SOURCE}")
endif()

foreach(source IN LISTS _sources)
    file(READ "${source}" contents)

    # Strip comments first: prose about operations is fine and is in fact encouraged.
    string(REGEX REPLACE "//[^\n]*" "" code "${contents}")

    # By its code, or by its name in the rule table (`"_add"`), which a pass that emits operations would reach for.
    if(code MATCHES "op::[A-Za-z]" OR code MATCHES "\"_[a-z][a-z0-9_]*\"")
        string(REGEX MATCH "op::[A-Za-z]+|\"_[a-z][a-z0-9_]*\"" offender "${code}")
        message(FATAL_ERROR
            "${source} names a specific operation (${offender}).\n"
            "The pass must interpret the rule columns, not branch on which operation it is looking at. "
            "If no column can express what this operation needs, add a column value for it.")
    endif()
endforeach()

message(STATUS "gate passed: the pass names no specific operation")
