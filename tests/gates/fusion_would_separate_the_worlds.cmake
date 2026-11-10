# Compile the fusion probe WITHOUT the library's `-ffp-contract=off`, on a target that has the fused
# instruction, and require it to report that the two worlds differ.
#
# This is the "shown rejecting" half of the pair. `two_worlds_agree_under_fusion` builds the same
# source with the library's flags and requires agreement; on its own that would pass on any machine
# whose baseline instruction set has no fused multiply-add — which is every x86-64 build that does
# not ask for one — and would therefore be evidence of nothing. This run makes the difference appear,
# so the passing run is known to be the flag's doing.
#
# Expects: COMPILER, SOURCE, ARCH_FLAG, WORK.
execute_process(COMMAND ${COMPILER} -std=c++2c -O2 ${ARCH_FLAG} -ffp-contract=fast
                        ${SOURCE} -o ${WORK}/fusion_probe_contracted
                RESULT_VARIABLE built)
if(NOT built EQUAL 0)
    message(FATAL_ERROR "the probe did not compile without the flag, so nothing was measured")
endif()

execute_process(COMMAND ${WORK}/fusion_probe_contracted
                RESULT_VARIABLE differed OUTPUT_VARIABLE said)
string(STRIP "${said}" said)
if(differed EQUAL 0)
    message(FATAL_ERROR
            "with contraction ON and ${ARCH_FLAG}, the two worlds still agreed (${said}).\n"
            "Either this target has no fused multiply-add after all, or the probe stopped probing — "
            "and in both cases `two_worlds_agree_under_fusion` passing next door means nothing.")
endif()
message(STATUS "without the flag, as required: ${said}")
