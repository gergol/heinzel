#pragma once

// What the compiler is told about the machine it plans for: facts about the processor, given as data.
// The planner asks the compiler nothing about the instruction set (only `runtime/hardware_twins.hpp` asks
// whether an instruction exists), so a build for a machine says which machine by passing one of these to
// `compile`, `try_compile` or `Executor<build, machine>`. The planner (`loops/planner/`) reads them. The
// defaults describe the least any 64-bit target has; a wrong value costs speed, never correctness, and at
// `High` never a bit.
//
// It depends on nothing, so the planner reads it without reaching anything else.

#include <cstdint>

namespace heinzel {

// What an operation costs a stage (`schedule/cost.hpp`): cycles from operand to result on a dependent chain, and issue
// slots, of which the machine has `Machine::fp_per_cycle` a cycle.
struct OpCost {
    std::int32_t latency{3};
    std::int32_t slots{1};

    constexpr bool operator==(const OpCost&) const = default;
};

struct Machine {
    std::int32_t vector_bytes{16};       // width of a vector register
    std::int32_t registers{16};          // vector registers a loop body may keep live
    std::int32_t l1_bytes{32 * 1024};    // data cache sizes, per core
    std::int32_t l2_bytes{256 * 1024};
    std::int32_t l3_bytes{8 * 1024 * 1024};
    std::int32_t line_bytes{64};         // a cache line
    std::int32_t page_bytes{4096};       // a page: no hardware prefetcher follows a walk across one
    // How many times a strided operand has to be read, in blocks of rows, before copying it into a
    // contiguous layout has paid for the copy (`Schedule::Stage::relaid`). Measured on zen2 with 256 x 256 strips: the
    // copy breaks even at 64 rows, about ten blocks of six (`loops/planner/contraction.hpp`). `tools/tune/relay.cpp`
    // reads 24 there, but its B (256 KiB) fits the L2 and the rule counts blocks, not the operand's bytes: planned for
    // 24, `ml/linear_relu` ran 1.50x and `ml/mlp2` 1.25-1.30x, whose weights do not fit it. So HEINZEL_AUTOTUNE runs
    // the probe and records its reading but keeps 12 (Plan 24, Gerald's answer 11).
    std::int32_t relay_pays_after{12};
    // How many times a contraction has to read each number of an operand that strides along its lanes before copying
    // it turned around has paid for the copy (`Schedule::Stage::turned`; the copy goes one element at a time). Measured
    // on zen2 with K 512 x 64 (`tools/tune/turn.cpp`): Q of 16 rows breaks even, 32 rows 1.6x faster, one
    // row 10x slower.
    std::int32_t turn_pays_after{32};
    // How many operations per output recomputing a producer inside the window that reads it may cost
    // before storing it once is cheaper (`Schedule::Stage::through`): always, and when its buffer would
    // take half the last-level cache or more. Measured, like the line above (`tools/tune/recompute.cpp`);
    // -1 stores everything.
    std::int32_t recompute_always{8};
    std::int32_t recompute_if_uncached{32};
    std::int32_t cores{1};
    bool fma{false};                     // a fused multiply-add instruction exists
    // Run consecutive stages under one band loop (`Schedule::Band`) when what they read and write, stored
    // whole, takes more than `band_when_over` bytes — or two of them touch one value of more than `l2_bytes`
    // (`loops/planner/bands.hpp`) — keeping `band_bytes` of what they make per chunk. Measured on zen2: half
    // the L2 per chunk is within 3 % of the best height at every point (`tools/tune/band_bytes.cpp`).
    // 0 bytes never bands. A program never has the whole last-level cache: where the working set alone decides, a band
    // ties at 15/16 of zen2's L3 and pays past it (KFR's EQ as a bank in double 1.05-1.08x banded at 7/8, 0.98-1.00x at
    // 15/16, 0.91-0.94x at the L3; eq8fb 1.01x, 1.00x, 0.99x; `Plan/probes/thresholds/band_when_over.cpp`).
    std::int64_t band_when_over{l3_bytes / 16 * 15};
    std::int32_t band_bytes{l2_bytes / 2};
    // ...and where nothing a band keeps is read ahead of its chunk — a stream, handed from one stage to the
    // next at the point it is made — `band_stream_rows` rows (samples) per chunk: so few that the next stage
    // runs while the first one's latency chain still does. Measured through the runner on zen2: the
    // oscillator 16 samples 11 % faster than whole (64: 7 %, 128 and more: 3 % and less), the ladder bank
    // 8 to 16 samples 13 % (4: 10 %, 32: 4 %) (`tools/tune/stream_rows.cpp`).
    std::int32_t band_stream_rows{16};
    // A stage split among workers (`Schedule::Stage::parallel`) gives each at least this many statement-points —
    // points of its domain times its statements. Below it the dispatch (about a microsecond while the workers
    // spin, 8–28 µs once they have parked) and the caches a split leaves cold cost more than it wins. Measured on
    // zen2 (`main`'s `docs/measurements/parallel-threshold.md`; on this tree
    // `tools/tune/parallel_work.cpp`); a machine with one core splits nothing whatever it says.
    std::int64_t parallel_work{std::int64_t{1} << 18};
    // The cost model's figures (`schedule/cost.hpp`): what each rule-table `Latency` class costs (a division or a root
    // in double more than in float), how long a value stored and loaded back holds up a chain, what the machine issues
    // a cycle, and its out-of-order window in issue slots: one sample's own path overlaps the next samples' only as far
    // as the window holds them. Measured on zen2, the defaults (`tools/tune/latency.cpp`; the window is its
    // data sheet's floating-point scheduler and queue, 36 + 64, which the corpus confirmed, `cost_corpus.cpp`, and where a
// cascade's order flips, `tools/tune/window.cpp`, 95). A core
    // in double is its exact series, a number at a time: 70 (sinpi) to 181 (tanh) cycles on a chain, 27 to 131 of issue
    // a number (exp 101 / 37, sin 106 / 50, log 118 / 50), against the float cores' 46 to 91 and a register's 18 to 34.
    OpCost add{3, 1};
    OpCost multiply_add{5, 1};
    OpCost divide{10, 7};
    OpCost divide_double{13, 10};
    OpCost root{14, 11};
    OpCost root_double{20, 17};
    OpCost core{70, 60};
    OpCost core_double{110, 100};
    std::int32_t forwarded{8};
    std::int32_t fp_per_cycle{2};
    std::int32_t loads_per_cycle{2};
    std::int32_t stores_per_cycle{1};
    std::int32_t window{100};
    // A shuffle across a register's lanes, as a fold along them waits on it: each of a fold's log2(lanes) steps is one
    // and an add (`runtime/lane_value.hpp` `folded`). Measured on zen2 as such a step less its add, 6 cycles a step for
    // four doubles and for eight floats alike (`latency.cpp`).
    OpCost shuffle{3, 1};
    // How far a stage's chain and issue add where they come close: the stage costs their p-norm, p this (0: the larger
    // alone). Work beside a chain is free while the chain dwarfs it, not where the two meet: the chain's operations wait
    // behind it (`Plan/probes/audio-iir/contention.cpp`). zen2's 8 is the corpus's (`cost_corpus*.cpp`, Plan/23 04 4.3a:
    // the faster schedule at 75 of 81 points, 6 and 7 alike; 8 moves the fewest programs).
    std::int32_t contention{8};
    // What a stage costs each time its loop starts, beside its slots, loads and stores: in a stream band (chunks of
    // `band_stream_rows` samples) a stage of its own costs this a chunk. Measured on zen2 as a one-pole's maps in a stage
    // of their own, banded against whole, 5-6 cycles a chunk (`Plan/probes/audio-iir/stage_start.cpp`).
    std::int32_t stage_start{6};
    // Bytes a core moves a cycle through memory past the last-level cache, loads and stores alike: what a buffer stored
    // whole over a run that outgrows the cache costs each pass over it. Measured on zen2 as maps over 64 MB buffers
    // against the same in L1, 4.1 bytes a cycle fitted (loads alone 6.2, stores alone 4.4: a line is read before it is
    // written; `tools/tune/memory.cpp`).
    std::int32_t memory_bytes{4};
    // How much work a sample, graph nodes times lane bodies, a feedback stage takes on as cycles chain onto it
    // (`loops/planner/chains.hpp` `still_fits`; a symbolic count of voices counts as this many): a sample's working set,
    // where the cost model, blind to the registers' pressure, merges wide banks too far. A stand-in for the model, not a
    // capacity: a stage's capacity (`HEINZEL_MAX_STAGE_STMTS`) only caps it. Measured on zen2: banks cut at 96 run
    // 0.88-0.89x the stages merged up to 192 (32 voices of four sections, 16 and 24 of six;
    // `Plan/probes/audio-iir/cost_corpus18.cpp`, slice 4).
    std::int32_t merge_work{96};

    constexpr bool operator==(const Machine&) const = default;
};

// The machine the performance work is measured on: AMD Zen 2 (AVX2 + FMA, 512 K L2, 16 M L3 per CCX).
inline constexpr Machine zen2{.vector_bytes = 32, .registers = 16, .l1_bytes = 32 * 1024, .l2_bytes = 512 * 1024,
                              .l3_bytes = 16 * 1024 * 1024, .cores = 16, .fma = true};

// The second one (Plan/23 P6): Apple's M1 Pro, its performance cores (NEON, 128 KiB L1 data a core, a 12 MiB L2 shared
// by a cluster of four, 128-byte lines, 16 KiB pages; `sysctl hw.perflevel0`), eight on the 10-core part. `l3_bytes` is
// the system-level cache, which no `sysctl` reports (24 MiB as published measurements of the M1 Pro have it;
// `Plan/probes/thresholds/caches.cpp` checks it), and with it `band_when_over` (15/16 of it by zen2's rule: 22.5 MiB),
// `band_bytes` (half the L2 by zen2's rule: 6 MiB): placeholders. The latencies and issue rates as its run measured them
// (`tools/tune/latency.cpp`, 2026-10-10: four pipes that each add; a multiply-add of the runtime's in lanes 5.5-5.6
// cycles and half an issue cycle), the window where a cascade's order flips (`tools/tune/window.cpp`, 2026-10-11:
// between three and four sections, against zen2's two and three; right from 135 to 235, nearest the measured at 180),
// three loads a cycle and two stores, 16 bytes or 4 each (`Plan/probes/loads/loads.cpp`, 2026-10-12; `latency.cpp`
// read one of each before, its loop's taken branches' rate). Every other threshold is zen2's (the defaults), each to
// be measured by the probe its comment names.
inline constexpr Machine m1_pro{.vector_bytes = 16, .registers = 32, .l1_bytes = 128 * 1024,
                                .l2_bytes = 12 * 1024 * 1024, .l3_bytes = 24 * 1024 * 1024, .line_bytes = 128,
                                .page_bytes = 16 * 1024, .cores = 8, .fma = true, .multiply_add = {6, 2}, .divide = {8, 4},
                                .divide_double = {10, 4}, .root = {10, 8}, .root_double = {13, 8}, .core = {70, 50},
                                .core_double = {120, 80}, .forwarded = 9, .fp_per_cycle = 4, .loads_per_cycle = 3,
                                .stores_per_cycle = 2, .window = 180, .shuffle = {1, 1}};

inline constexpr Machine default_machine{};

} // namespace heinzel
