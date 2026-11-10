# The examples: the tour, numbered and read in order, then the showcases. Each builds its graphs and compiles them
# while it is itself compiled, runs them, and exits non-zero when a result strays from the same computation written as
# plain C++, so each is also a ctest. The files they write (WAV files, images) land in the directory they run in: here,
# the build's examples/. They include `heinzel/dsl.hpp`, which lives in app/ (it composes the frontend and
# `Executor`): `heinzel_app`.
set(tour
    01_first_graph
    02_params_and_functions
    03_the_past
    04_recurrences
    05_composition
    06_subgraphs
    07_oscillators
    08_streaming
    09_tensors
    10_windows
    11_einsum
    12_ranges
    13_precision
    14_learning
    15_under_the_hood)
set(showcases
    synth_voice
    image_pipeline
    transformer_block
    learn_a_filter
    reverb
    mixing_console
    analog_ladder)
foreach(example IN LISTS tour showcases)
    add_executable(example_${example} ${example}.cpp)
    target_link_libraries(example_${example} PRIVATE heinzel_app)
    add_test(NAME example_${example} COMMAND example_${example} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endforeach()

# Freeverb is twenty feedback loops, more than a graph holds by default (128 values, 128 nodes): without these, its build
# stops with one error, "heinzel: a graph has more values than kMaxValues; rebuild with -DHEINZEL_MAX_VALUES=N". A
# capacity is raised by a definition, the same in every translation unit of a program (`docs/limitations.md`,
# "Capacities").
target_compile_definitions(example_reverb PRIVATE HEINZEL_MAX_VALUES=256 HEINZEL_MAX_NODES=256)
