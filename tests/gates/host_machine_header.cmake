# Plan 24's review: `heinzel/host_machine.hpp`, as HEINZEL_AUTOTUNE writes it, still compiles against `Machine`. Its
# writer (`tools/tune/header.cmake`) names `Machine`'s fields by hand, in declaration order, and no other test builds a
# tuned header, so a field renamed or moved in `schedule/machine.hpp` broke every tuned build directory unseen. This
# writes the header through that writer from a machine.txt in which every field was measured (an `OpCost` 7 5, `fma`
# true, the others 40) and compiles a translation unit that includes it and checks a few of them, with -Werror: a
# field out of order is -Wreorder-init-list.
#
# Run with:  cmake -DCXX=.. -DSTD=.. -DINC=<include> -DTUNE=<tools/tune> -DTU=.. -DOUT=<dir> \
#                  -P host_machine_header.cmake

cmake_minimum_required(VERSION 3.25) # (CMP0057: `IN_LIST`)
include(${TUNE}/header.cmake)
set(_tune_txt ${OUT}/machine.txt)
set(HEINZEL_TUNE_INCLUDE_DIR ${OUT}/include)
file(WRITE ${_tune_txt} "key ${CXX} | ${STD} | every field measured\ndate today\ncpu none\n")
foreach(field IN LISTS _tune_fields)
    set(value 40)
    if(field IN_LIST _tune_costs)
        set(value "7 5")
    elseif(field STREQUAL "fma")
        set(value true)
    endif()
    file(APPEND ${_tune_txt} "tuned ${field} ${value} host_machine_header.cmake\n")
endforeach()
_tune_write_header(${_tune_txt})

execute_process(
    COMMAND ${CXX} ${STD} -fsyntax-only -Wall -Wextra -Werror -I ${INC} -I ${OUT}/include ${TU}
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE out)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "host_machine_header: the header HEINZEL_AUTOTUNE writes does not compile against `Machine` (a "
        "field renamed or moved in schedule/machine.hpp and not in tools/tune/header.cmake's _tune_fields?):\n${out}")
endif()
message(STATUS "host_machine_header: the header written with every field measured compiles and holds them")
