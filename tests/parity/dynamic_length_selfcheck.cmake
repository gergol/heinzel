# MD.4: dynamic_length parity by SELF-CHECK, not by sample dump.
#
# The old `examples/dynamic_length.cpp` is VALUE-SELF-CHECKING — it compares its own output to a hand
# reference `x*gain+bias` in its own precision and exits 0 on PASS, 1 on FAIL. The ported `_here`
# program does the same in this tree's precision. So parity here is exactly MD.4's stated model:
#
#   * SAME PASS — both self-checks exit 0, i.e. both recover the block effect at N=256 and N=64;
#   * SAME ARENA STORY — both print the same `STORY` line: one graph compiled once, run at two lengths
#     via (re-)prepare, allocation-free.
#
# A cross-tree ARENA-SIZE integer is deliberately NOT compared: the two arena models differ (as
# `compare.cmake` explains a cross-tree artifact hash cannot exist), so the "story" compared is the
# narrative both print, never a slot count. The old tree's `arena:` line is for the record only; this
# tree's program prints none.

execute_process(COMMAND "${HERE}" OUTPUT_FILE "${WORK}/${TAG}-here.txt" RESULT_VARIABLE here_rc)
execute_process(COMMAND "${THERE}" OUTPUT_FILE "${WORK}/${TAG}-there.txt" RESULT_VARIABLE there_rc)

if(NOT here_rc EQUAL 0 OR NOT there_rc EQUAL 0)
    message(FATAL_ERROR
            "a dynamic_length self-check FAILED (this tree: ${here_rc}, the old tree: ${there_rc}) — "
            "the block effect was not recovered at both lengths")
endif()

file(STRINGS "${WORK}/${TAG}-here.txt" here_story REGEX "^STORY ")
file(STRINGS "${WORK}/${TAG}-there.txt" there_story REGEX "^STORY ")

if(here_story STREQUAL "" OR there_story STREQUAL "")
    message(FATAL_ERROR "a side printed no STORY line (this tree: '${here_story}', old tree: '${there_story}')")
endif()

if(NOT here_story STREQUAL there_story)
    message(FATAL_ERROR
            "the two trees tell a different arena story:\n"
            "  this tree: ${here_story}\n"
            "  old tree:  ${there_story}")
endif()

message(STATUS "dynamic_length: both self-checks PASS; same arena story (${here_story})")
