# Plan 24: the header `heinzel::host` is written into, from machine.txt's lines. Included by machine.cmake, and by the
# test `host_machine_header` (tests/gates/host_machine_header.cmake), which writes it with every field measured and
# compiles it: `Machine`'s fields are named here by hand, so one renamed or moved in `schedule/machine.hpp` fails that
# test in every build, not only a tuned build directory. Writes ${HEINZEL_TUNE_INCLUDE_DIR}/heinzel/host_machine.hpp;
# its comment names ${_tune_txt}.

# `Machine`'s fields in declaration order, which designated initializers require; and those that are an `OpCost`, a
# latency and slots (`tuned <field> <latency> <slots>`).
set(_tune_fields vector_bytes registers l1_bytes l2_bytes l3_bytes line_bytes page_bytes relay_pays_after
    turn_pays_after recompute_always recompute_if_uncached cores fma band_when_over band_bytes band_stream_rows
    parallel_work add multiply_add divide divide_double root root_double core core_double forwarded fp_per_cycle
    loads_per_cycle stores_per_cycle window shuffle contention stage_start memory_bytes merge_work)
set(_tune_costs add multiply_add divide divide_double root root_double core core_double shuffle)
set(_tune_template ${CMAKE_CURRENT_LIST_DIR}/host_machine.hpp.in)

# The header from the lines of `from` (machine.txt, or stage 1's readings): each field taken, in declaration order, and
# every field's source or reason in its comment.
function(_tune_write_header from)
    file(STRINGS ${from} lines ENCODING UTF-8)
    set(loads "")
    foreach(line IN LISTS lines)
        if(line MATCHES "^load ([^ ]+) (.*)$")
            list(APPEND loads "${CMAKE_MATCH_1} ${CMAKE_MATCH_2}")
        elseif(line MATCHES "^(date|cpu) (.*)$")
            set(${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
        elseif(line MATCHES "^key ([^|]*) \\| ([^|]*) \\|")
            set(compiler "${CMAKE_MATCH_1}")
            set(flags "${CMAKE_MATCH_2}")
        endif()
    endforeach()
    list(JOIN loads ", " loads)
    set(txt ${_tune_txt})
    set(init "")
    set(notes "")
    foreach(field IN LISTS _tune_fields)
        set(note "default, not measured")
        foreach(line IN LISTS lines)
            if(line MATCHES "^tuned ${field} (.+) ([^ ]+)$")
                set(note "${CMAKE_MATCH_1}, ${CMAKE_MATCH_2}")
                string(REGEX REPLACE "^([0-9]+) ([0-9]+)$" "{\\1, \\2}" value "${CMAKE_MATCH_1}")
                string(APPEND init "    .${field} = ${value},\n")
            elseif(line MATCHES "^default ${field} (.*)$")
                set(note "default, ${CMAKE_MATCH_1}")
            endif()
        endforeach()
        string(APPEND notes "//   ${field}: ${note}\n")
    endforeach()
    configure_file(${_tune_template} ${HEINZEL_TUNE_INCLUDE_DIR}/heinzel/host_machine.hpp @ONLY)
endfunction()
