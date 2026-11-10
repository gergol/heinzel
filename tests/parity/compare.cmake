# Run both programs and compare their samples. Milestone M1's fourth condition, extended at M4.5 to
# one graph per family.
#
# The comparison is NUMERIC and not a comparison of compiled artifacts. The two trees' IR types
# differ deliberately — a rule table with shape and element-type columns is not the same type as the
# old tree's — so they are values of different types and a cross-tree hash asks for something that
# cannot exist.
#
# WHY THERE IS A TOLERANCE AT ALL, AND WHY IT IS NEVER ALONE. Each caller supplies both an allowance
# and the NAMED CAUSE that accounts for it, and `compare_samples` prints the cause beside the
# measured difference. That pairing is M4.5's requirement in as many words: a test that reports a
# difference without attributing it does not satisfy the condition. A bare tolerance cannot be told
# apart from a margin widened until the test passed, and telling those two apart is the whole value
# of a check that reaches outside this tree.
#
# An allowance goes to zero when the cause is removed, and that is the moment to revisit it.

execute_process(COMMAND "${HERE}" OUTPUT_FILE "${WORK}/${TAG}-here.txt" RESULT_VARIABLE here_rc)
execute_process(COMMAND "${THERE}" OUTPUT_FILE "${WORK}/${TAG}-there.txt" RESULT_VARIABLE there_rc)

if(NOT here_rc EQUAL 0 OR NOT there_rc EQUAL 0)
    message(FATAL_ERROR "a graph did not run (this tree: ${here_rc}, the old tree: ${there_rc})")
endif()

execute_process(COMMAND "${COMPARE}" "${WORK}/${TAG}-here.txt" "${WORK}/${TAG}-there.txt" "${ALLOWED}" "${BECAUSE}"
                RESULT_VARIABLE rc OUTPUT_VARIABLE report)
message(STATUS "${report}")
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "the two trees disagree about ${TAG} by more than ${BECAUSE} accounts for")
endif()
