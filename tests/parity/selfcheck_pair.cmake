# Run a value-self-checking `_here`/`_there` pair and require BOTH to PASS (exit 0).
#
# Used where the ported program and the old-tree reference each check their OWN output against the SAME
# hand reference (the tutorial/example's own `check()`), so both passing IS the cross-tree agreement —
# a sample dump would only re-derive what each already verified, and where a section spans f32 (there)
# vs f64 (here) a bit-exact dump would need a tolerance for a difference the self-checks already absorb.
# The comparison is "run-and-compared" in MD.5's sense: both recover the same reference behaviour.
#
# Both programs' output is echoed to the test log for the record.

execute_process(COMMAND "${HERE}" OUTPUT_VARIABLE here_out RESULT_VARIABLE here_rc)
execute_process(COMMAND "${THERE}" OUTPUT_VARIABLE there_out RESULT_VARIABLE there_rc)

message(STATUS "this tree (${TAG}):\n${here_out}")
message(STATUS "old tree (${TAG}):\n${there_out}")

if(NOT here_rc EQUAL 0 OR NOT there_rc EQUAL 0)
    message(FATAL_ERROR
            "a ${TAG} self-check FAILED (this tree: ${here_rc}, the old tree: ${there_rc}) — "
            "the reference behaviour was not recovered on both sides")
endif()
