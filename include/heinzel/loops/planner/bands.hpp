#pragma once

// The planner's band pass decides which runs of consecutive stages share one band loop (`band_where_it_pays`; its rule
// is at the end of the file). Whether a band is legal is for `loops/band_layout.hpp` to say, and the lowering refuses
// with the same answer. It names no operation.

#include <cstdint>
#include <expected>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/loops/band_layout.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/graph/places.hpp"
#include "heinzel/loops/graph/regions.hpp"
#include "heinzel/loops/graph/views.hpp"
#include "heinzel/loops/planner/stages.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/schedule.hpp"
#include "heinzel/schedule/machine.hpp"
#include "heinzel/support/fixed_vector.hpp"

namespace heinzel::loops::planner {

namespace detail {

// Do stages `k` and `k + 1` link? They do when `k` stores something (something outside it reads it), and everything it
// stores is read by `k + 1` (and perhaps by `k` itself, through its past), by nothing else, and is no output.
constexpr bool linked(const Graph& g, const Stages& stages, const graph::ValuePlaces& at, std::size_t k) {
    bool stores = false;
    const std::int32_t home = static_cast<std::int32_t>(k);
    for (const std::int32_t ni : stages[k].nodes) {
        const std::int32_t v = g.nodes[static_cast<std::size_t>(ni)].output;
        if (!graph::read_outside(at, v, home, home + 1)) {
            continue;
        }
        stores = true;
        if (is_a_result(g, v) || at.first_reader[static_cast<std::size_t>(v)] < home || at.last_reader[static_cast<std::size_t>(v)] > home + 1) {
            return false;
        }
    }
    return stores;
}

// The axis of stage `last`'s domain that a band ending there walks (-1: none): the outermost in memory order that no
// fold of the stage collapses, that is longer than one, and along which the stage reads a value the stage before it
// makes, through a window that reaches ahead or, with `streamed`, in any way. An axis whose reads have no answer
// offers nothing; the lowering refuses it by name.
constexpr std::int32_t candidate_axis(const Graph& g, const graph::Facts& facts, const Stages& stages, std::size_t last, ReadsMemo& memo, bool streamed = false) {
    const Schedule::Stage& stage = stages[last];
    const Shape domain = graph::shape_of_group(g, stage).domain;
    std::vector<char> made_before(g.values.size(), false);
    for (const std::int32_t ni : stages[last - 1].nodes) {
        made_before[static_cast<std::size_t>(g.nodes[static_cast<std::size_t>(ni)].output)] = true;
    }
    for (const std::int32_t axis : graph::memory_order(domain.rank)) {
        if (extent_at(domain, axis) == literal_extent(1) || !loops::why_not_in_a_band(g, stage, axis).empty()) {
            continue;
        }
        const std::expected<const Reads*, Refusal> reads = loops::memo_reads(memo, g, facts, stages, last, axis);
        for (std::size_t k = 0; reads && k < (*reads)->size(); ++k) {
            const BandTouch& t = (**reads)[k];
            if (t.value != kNoValue && made_before[static_cast<std::size_t>(t.value)] && t.value_axis >= 0 && (t.ahead >= 1 || streamed) && !t.unbounded) {
                return axis;
            }
        }
    }
    return -1;
}

inline constexpr std::int64_t kLarge = std::int64_t{1} << 62;

// A value's bytes, stored whole (a symbolic extent counts as large; a literal constant takes none).
constexpr std::int64_t whole_bytes(const Value& held) {
    if (held.category == ValueCategory::Const) {
        return 0;
    }
    std::int64_t whole = loops::bytes_of(held.type);
    for (std::int32_t axis = 0; axis < held.shape.rank && whole < kLarge; ++axis) {
        const Extent e = extent_at(held.shape, axis);
        whole = !is_literal(e) || (e.encoded > 0 && whole > kLarge / e.encoded) ? kLarge : whole * e.encoded; // (saturates, never overflows)
    }
    return whole;
}

// The stored value node `n`'s operand `k` is read from (through views with no statement).
constexpr std::int32_t source_of(const graph::Facts& facts, const heinzel::Node& n, std::size_t k) {
    return facts.through[static_cast<std::size_t>(facts.read_as[static_cast<std::size_t>(n.inputs[k])])].source;
}

// The bytes stages `first .. last` read and write, stored whole: every value one of them makes that is stored (read
// outside its stage, or a result) and every stored value one of them reads, inputs included. A symbolic extent counts
// as large. This working set is what a band spares the caches: a run whose working set fits the last-level cache gains
// nothing from being cut into bands.
constexpr std::int64_t working_set(const Graph& g, const graph::Facts& facts, const Stages& stages, const graph::ValuePlaces& at, std::size_t first,
                                   std::size_t last) {
    std::vector<char> counted(g.values.size(), false);
    std::int64_t bytes = 0;
    const auto count = [&](std::int32_t v) {
        if (counted[static_cast<std::size_t>(v)]) {
            return;
        }
        counted[static_cast<std::size_t>(v)] = true;
        const std::int64_t whole = whole_bytes(g.values[static_cast<std::size_t>(v)]);
        bytes = whole >= kLarge - bytes ? kLarge : bytes + whole;
    };
    for (std::size_t si = first; si <= last; ++si) {
        for (const std::int32_t ni : stages[si].nodes) {
            const heinzel::Node& n = g.nodes[static_cast<std::size_t>(ni)];
            if (graph::read_outside(at, n.output, static_cast<std::int32_t>(si), static_cast<std::int32_t>(si) + 1)) {
                count(n.output);
            }
            for (std::size_t k = 0; k < rules_of(n.op).arity; ++k) {
                const std::int32_t source = source_of(facts, n, k);
                if (at.stage[static_cast<std::size_t>(source)] != static_cast<std::int32_t>(si)) {
                    count(source);
                }
            }
        }
    }
    return bytes;
}

// Is every node of the stage cheap (`CostClass::Cheap`)?
constexpr bool cheap(const Graph& g, const Schedule::Stage& stage) {
    for (const std::int32_t ni : stage.nodes) {
        if (rules_of(g.nodes[static_cast<std::size_t>(ni)].op).cost != CostClass::Cheap) {
            return false;
        }
    }
    return true;
}

// The bytes of the largest stored value two stages of `first .. last` touch — the first to make or read it and a later,
// cheap one reading it, the one making it cheap too: a band keeps a chunk of it near between the two, where whole it
// would come from memory again once it outgrows the second-level cache. Next to a costlier stage the traffic is hidden.
// Measured: the norms' second read of a row -10 to -14 %, conv's square read by its taps -5 to -9 %; softmax's second
// read behind its `exp` -1 %, its `exp(x - max)` read by its division +-0, as attention's (`Plan/21-row-bands`,
// `Plan/22-conv-bands`). In a stream (`streamed`) only a value read twice counts: a sample less its mean over 8 voices
// -25 %, but a value a recurrence hands on, chunked into 16 samples, +5 to +17 % (a bank of one voice, a phase).
constexpr std::int64_t touched_twice(const Graph& g, const graph::Facts& facts, const Stages& stages, const graph::ValuePlaces& at, std::size_t first,
                                     std::size_t last, bool streamed) {
    std::vector<std::int32_t> touched_in(g.values.size(), -1); // the first stage of the run that makes or reads it
    std::vector<char> is_cheap(last - first + 1);
    for (std::size_t si = first; si <= last; ++si) {
        is_cheap[si - first] = cheap(g, stages[si]);
    }
    std::int64_t most = 0;
    for (std::size_t si = first; si <= last; ++si) {
        const auto here = static_cast<std::int32_t>(si);
        const bool here_cheap = is_cheap[si - first];
        for (const std::int32_t ni : stages[si].nodes) {
            const heinzel::Node& n = g.nodes[static_cast<std::size_t>(ni)];
            touched_in[static_cast<std::size_t>(n.output)] = here;
            for (std::size_t k = 0; k < rules_of(n.op).arity; ++k) {
                const auto source = static_cast<std::size_t>(source_of(facts, n, k));
                if (at.stage[source] == here) {
                    continue;
                }
                const std::int32_t before = touched_in[source];
                touched_in[source] = before < 0 ? here : before;
                const bool made_there = before >= 0 && at.stage[source] == before;
                const bool twice = before >= 0 && before != here && here_cheap &&
                                   (!made_there || (!streamed && is_cheap[static_cast<std::size_t>(before) - first]));
                const std::int64_t whole = twice ? whole_bytes(g.values[source]) : 0;
                most = whole > most ? whole : most;
            }
        }
    }
    return most;
}

// Does a band over stages `first .. last` pay? Where their working set outgrows the last-level cache, or two of them
// touch one value that outgrows the second-level cache. A stream's chunk is `band_stream_rows` of its `rows`, whatever
// they weigh: where that much of the value outgrows `band_bytes`, nothing stays near between the two touches (rownorm
// 40 x 32768, banded: +-2 % on one core) and a band split among workers has few chunks to share (24 ranges to 3). (A
// value without the band axis is counted as if it had it: a chunk reads all of it, so such a value can pass the cap
// and still gain nothing — none is known to matter.)
constexpr bool pays(const Graph& g, const graph::Facts& facts, const Stages& stages, const graph::ValuePlaces& at, std::size_t first, std::size_t last,
                    bool streamed, std::int64_t rows, const Machine& m) {
    if (working_set(g, facts, stages, at, first, last) > m.band_when_over) {
        return true;
    }
    const std::int64_t touched = touched_twice(g, facts, stages, at, first, last, streamed);
    return touched > m.l2_bytes &&
           (!streamed || (rows > 0 && m.band_stream_rows > 0 && touched / rows <= m.band_bytes / m.band_stream_rows)); // (never overflows)
}

// A stream band's rows — a sample of each value it keeps — are one number or at least a lane of them, in the caches.
// Measured there (512 and 65,536 samples, `Plan/probes/review_m6/perf_narrow.cpp`): banks of 3 to 5 voices lose 2 to
// 7 % banded, while one voice and 8, 16, 64 gain or tie. Past the last-level cache (`known_to_spill`) a band spares the
// memory its traffic, whatever the lanes.
constexpr bool whole_lanes(const Graph& g, const BandLayout& layout, const Machine& m) {
    for (const BandLayout::Kept& kept : layout.kept) {
        const Value& held = g.values[static_cast<std::size_t>(kept.value)];
        std::int64_t row = 1;
        for (std::int32_t axis = 0; axis < held.shape.rank; ++axis) {
            const Extent e = extent_at(held.shape, axis);
            row *= axis == kept.value_axis ? 1 : is_literal(e) ? e.encoded : 1 << 20;
        }
        if (row != 1 && row * loops::bytes_of(held.type) < m.vector_bytes) {
            return false;
        }
    }
    return true;
}

// Is the run `first .. last`, its length known, known to outgrow the last-level cache stored whole? Then a band over a
// bank narrower than a lane pays all the same: KFR's EQ as a bank of three voices in double, 1M samples, 4.35 -> 2.6 ns
// a sample (Plan/23 03, 3.7). A run of unknown length is a plugin's block, in the caches (the same bank in blocks of
// 256: 1.88 ns unbanded, 2.45 banded).
constexpr bool known_to_spill(const Graph& g, const graph::Facts& facts, const Stages& stages, const graph::ValuePlaces& at, std::size_t first,
                              std::size_t last, std::int64_t rows, const Machine& m) {
    return rows > 0 && working_set(g, facts, stages, at, first, last) > m.band_when_over;
}

// Does a stream band over stages `first .. last` of a run of unknown length — a plugin's block, in the caches, where no
// traffic is spared — hide what starting its stages costs? Its gain there is the lane work it runs beside a chain: the
// slots, a sample of all its voices, of each stage that hands no state on, over what the machine issues a cycle, in the
// lanes its widest element leaves (none without a stage that hands state on). Each chunk starts every stage
// (`Machine::stage_start`), and a stage that hands state on stores and loads it back on its chain (`forwarded`).
// Measured in blocks of 256 to 16384 samples, chunks of 16, banded against not (`Plan/probes/tail-cut/bands.cpp`): a
// cascade and its cast to float after it 1.06-1.08 on the M1 (gain 1 cycle a chunk, starts 21), eq8fb's 1.015 on both
// (1, 20-21); eq8mix's gain, cascade and mix 1.22 on zen2, 1.10 on the M1 (16, 26-27); the FM oscillator's sine beside
// its phase 0.96-0.98 and 0.90 (51-61, 20-21), eq8sweep's coefficients beside its swept section 1.00 (232, 27).
constexpr bool hides_its_starts(const Graph& g, const graph::Facts& facts, const Stages& stages, std::size_t first, std::size_t last,
                                const BandLayout& layout, const Machine& m) {
    double work = 0;
    double starts = 0;
    bool chain = false;
    for (std::size_t si = first; si <= last; ++si) {
        const bool state = graph::hands_state_on(g, facts, stages[si]);
        chain = chain || state;
        starts += m.stage_start + (state ? m.forwarded : 0);
        if (state) {
            continue;
        }
        const Shape domain = graph::shape_of_group(g, stages[si]).domain;
        std::int64_t widest = 4;
        double slots = 0;
        for (const std::int32_t ni : stages[si].nodes) {
            const heinzel::Node& n = g.nodes[static_cast<std::size_t>(ni)];
            const std::int64_t b = loops::bytes_of(g.values[static_cast<std::size_t>(n.output)].type);
            widest = b > widest ? b : widest;
            slots += detail::cost_of(g, n, m).slots;
        }
        double voices = 1;
        for (std::int32_t axis = 0; axis < domain.rank; ++axis) {
            const Extent e = extent_at(domain, axis);
            voices *= axis == layout.axis[si - first] ? 1 : is_literal(e) ? static_cast<double>(e.encoded) : 1;
        }
        const double lanes = m.vector_bytes / widest > 1 ? static_cast<double>(m.vector_bytes / widest) : 1;
        work += slots * voices / lanes / m.fp_per_cycle;
    }
    return chain && work * m.band_stream_rows > starts;
}

// The band's height: as many rows as keep what it holds per chunk within `budget` bytes, a row of a kept value being
// its extents inside the band axis in memory order, and a whole number of blocks where a stage's lanes run along its
// band axis. It is 0 when no height pays (a chunk would copy more than it makes) and -1 when one chunk would hold the
// whole band. A negative `budget` asks for that many rows, whatever their bytes: a stream's chunk. Split among workers,
// a band hands each a range of whole chunks: at 17 chunks on 16 workers, 9 ranges of up to two, 7 workers idle. So
// where `cores` is more than one the chunks are made at most a whole multiple of it, each a little lower — the height
// rounded UP to whole blocks, which stays within the natural one and adds no chunk. Measured on 4 workers (a zen2
// program): conv at 1M 159 -> 122-135 us, at 256K 62 -> 31, blurxy at 512 rows 74 -> 62; on one core level or 1-3 %
// faster (`Plan/probes/m5-conv/workers.cpp`). Where the lower chunks would no longer pay, the band keeps its height.
constexpr std::int32_t band_height(const Graph& g, const graph::Facts& facts, const Schedule& s, const Schedule::Band& band, const BandLayout& layout,
                                   std::int64_t budget, std::int64_t cores) {
    std::int64_t per_row = 0;
    std::int64_t fixed = 0;
    std::int64_t carried = 0;
    for (const BandLayout::Kept& kept : layout.kept) {
        const Value& held = g.values[static_cast<std::size_t>(kept.value)];
        std::int64_t row = loops::bytes_of(held.type);
        bool inside = false;
        for (const std::int32_t axis : graph::memory_order(held.shape.rank)) {
            const Extent e = extent_at(held.shape, axis);
            if (inside && !is_literal(e)) {
                return 0;
            }
            row *= inside ? e.encoded : 1;
            inside = inside || axis == kept.value_axis;
        }
        per_row += row;
        fixed += kept.lead * row;
        const std::int64_t reach = kept.lead - layout.lead[static_cast<std::size_t>(kept.reader)] + facts.deepest[static_cast<std::size_t>(kept.value)];
        carried = reach > carried ? reach : carried;
    }
    std::int64_t rows = budget < 0 ? -budget : per_row > 0 ? (budget - fixed) / per_row : 0;
    rows = rows > kMaxBandStep ? kMaxBandStep : rows; // (a stream's rows are data)
    std::int64_t block = 1;
    for (std::int32_t pos = 0; pos < band.count; ++pos) {
        for (const Schedule::Loop& loop : s.stages[static_cast<std::size_t>(band.first + pos)].loops) {
            const std::int64_t lanes = std::int64_t{loop.step} * loop.unroll;
            block = loop.kind == LoopKind::Vector && loop.axis == layout.axis[static_cast<std::size_t>(pos)] && lanes > block ? lanes : block;
        }
    }
    rows -= rows % block;
    const Extent tail = extent_at(graph::shape_of_group(g, s.stages[static_cast<std::size_t>(band.first + band.count - 1)]).domain, band.axis);
    if (is_literal(tail) && rows >= tail.encoded) {
        return -1; // one chunk: nothing gained — and a shorter run, keeping less, would be taller still
    }
    if (is_literal(tail) && cores > 1 && rows > 0) {
        const std::int64_t chunks = (tail.encoded + rows - 1) / rows;
        const std::int64_t even = (chunks + cores - 1) / cores * cores;
        std::int64_t lower = (tail.encoded + even - 1) / even;
        lower = (lower + block - 1) / block * block; // (rounded down, one chunk more: the 17 of 16 again)
        rows = lower < 1 + carried ? rows : lower;
    }
    return rows < 1 + carried ? 0 : static_cast<std::int32_t>(rows);
}

// The longest run ending at stage `last` that is legal and worth a band, with its height and its `layout` — or none
// (`count` 0).
constexpr Schedule::Band best_band_ending_at(const Graph& g, const graph::Facts& facts, const Schedule& s, const graph::ValuePlaces& at, std::size_t last,
                                          const Machine& m, ReadsMemo& memo, BandLayout& layout_of) {
    const Stages& stages = s.stages;
    const bool streamed = detail::candidate_axis(g, facts, stages, last, memo) < 0;
    const std::int32_t axis = detail::candidate_axis(g, facts, stages, last, memo, streamed);

    // (Each linked stage keeps a value of its own, so no run longer than `kMaxBandBuffers + 1` stages is legal: not laid
    // out at all — on a long chain whose working set is large, that is most of the planning.)
    std::size_t first = last - 1;
    while (first >= 1 && last - first < kMaxBandBuffers && detail::linked(g, stages, at, first - 1) &&
           loops::why_not_in_any_band(g, facts, stages[first - 1]).empty()) {
        --first;
    }
    const Extent tail = axis >= 0 ? extent_at(graph::shape_of_group(g, stages[last]).domain, axis) : Extent{};
    const std::int64_t rows = is_literal(tail) ? tail.encoded : -1;
    // (A shorter run's working set and values touched twice are part of a longer one's: where the longest does not pay,
    // none does — but for a stream's cap on its chunk, which can refuse a long run a shorter one would pass: a band
    // missed, never a wrong one.)
    const bool may_pay = axis >= 0 && detail::pays(g, facts, stages, at, first, last, streamed, rows, m);
    // What refuses the last two stages a band — a read, an extent, a kept value's rows — refuses every longer run
    // ending there too: asked first, a chain of stages nothing can band costs one layout each.
    const Schedule::Band two{.first = static_cast<std::int32_t>(last - 1), .count = 2, .axis = axis};
    const BandLayout pair = may_pay ? loops::band_layout(g, facts, at, stages, two, memo) : BandLayout{};
    const bool one_chunk = streamed && is_literal(tail) && m.band_stream_rows >= tail.encoded;
    const std::int64_t budget = streamed ? -std::int64_t{m.band_stream_rows} : m.band_bytes;
    const std::int64_t cores = streamed ? 1 : std::int64_t{m.cores}; // (a stream's rows are a latency's, not a share's)
    // (A longer run keeps what the pair keeps and more: where the pair gets no height, none does.)
    const bool narrow_may = !streamed || detail::known_to_spill(g, facts, stages, at, first, last, rows, m); // (the longest run spills most)
    const bool pair_may = may_pay && !one_chunk && pair.why_not.empty() && (narrow_may || detail::whole_lanes(g, pair, m)) &&
                          detail::band_height(g, facts, s, two, pair, budget, cores) != 0;
    for (std::size_t from = first; from < last && pair_may; ++from) {
        Schedule::Band band{.first = static_cast<std::int32_t>(from), .count = static_cast<std::int32_t>(last - from + 1), .axis = axis};
        const BandLayout layout = loops::band_layout(g, facts, at, stages, band, memo);
        const bool lanes = !streamed || detail::known_to_spill(g, facts, stages, at, from, last, rows, m) || detail::whole_lanes(g, layout, m);
        const bool hides = !streamed || rows > 0 || detail::hides_its_starts(g, facts, stages, from, last, layout, m);
        if (layout.why_not.empty() && detail::pays(g, facts, stages, at, from, last, streamed, rows, m) && lanes && hides) {
            band.step = detail::band_height(g, facts, s, band, layout, budget, cores);
            if (band.step > 0) {
                layout_of = layout;
                return band;
            }
            if (band.step < 0) {
                break; // one chunk here: one for every shorter run too
            }
        }
    }
    return Schedule::Band{};
}

} // namespace detail

// The layouts of a schedule's bands, in its band order: worked out where each band was chosen, for the parallel pass.
// (The schedule holds what was decided; the lowering works a band's layout out again from it.)
using BandLayouts = fixed_vector<BandLayout, kMaxBands>;

// Finds the bands back to front, stores them in stage order, and returns their layouts (`at`: where the stages make and
// read each value).
//
// A run of consecutive stages shares one band loop when every value a stage of the run (but the last) stores is read
// only by the NEXT stage of the run (or by itself, through its own past), is no output, along one axis that no fold of
// the run collapses, at step 1 and from no offset — at least one of them through a window — and either what the run
// reads and writes, stored whole, would take more than `Machine::band_when_over` bytes (its working set: inputs, what
// it keeps, what it stores), or two of its stages touch one value of more than `Machine::l2_bytes`, the later one cheap
// and, where the first made it, that one too (`touched_twice`). Where the last stage reads its predecessor's values only
// at the point — a stream — the run is banded in chunks of `Machine::band_stream_rows`, if a sample of each value it
// keeps is one number or whole lanes: short enough that the next stage runs while the first one's latency chain still
// does (and, for a value touched twice, where that many rows of it fit `Machine::band_bytes`, a value made in the run
// not counting); over a run of unknown length, in the caches, only where the lane work it runs beside a chain pays
// for starting its stages every chunk (`hides_its_starts`). Split among cores, a band's chunks are evened among them
// (`band_height`).
//
// Measured on zen2: blurxy at 8 MB 1596 -> 800 µs banded, harris at 25 MB 3936 -> 2667 µs, within 2 % of a ring
// buffer; just past the last-level cache, blurxy at 690 rows (16.9 MB) -9 %. Under it, by the second rule: blurxy at
// 512 rows -20 to -27 %, conv at N1048576 -5 to -9 %, rownorm -20 %, rmsnorm -4 %; harris at 232 rows (9.6 MB) +1.3 %
// (`Plan/21-row-bands`, `Plan/22-conv-bands`).
constexpr BandLayouts band_where_it_pays(Schedule& s, const Graph& g, const graph::Facts& facts, const graph::ValuePlaces& at, const Machine& m) {
    BandLayouts layouts{};
    if (m.band_bytes <= 0) {
        return layouts;
    }
    const Stages& stages = s.stages;
    ReadsMemo memo{};
    fixed_vector<Schedule::Band, kMaxBands> found{};
    BandLayouts laid{};
    for (std::size_t last = s.stages.size(); last-- > 1 && found.size() < kMaxBands;) {
        if (!loops::why_not_in_any_band(g, facts, stages[last - 1]).empty() || !loops::why_not_in_any_band(g, facts, stages[last]).empty() ||
            !detail::linked(g, stages, at, last - 1)) {
            continue;
        }
        BandLayout layout{};
        const Schedule::Band band = detail::best_band_ending_at(g, facts, s, at, last, m, memo, layout);
        if (band.count > 0) {
            found.push_back(band);
            laid.push_back(layout);
            last = static_cast<std::size_t>(band.first);
        }
    }
    for (std::size_t k = found.size(); k-- > 0;) {
        s.bands.push_back(found[k]);
        layouts.push_back(laid[k]);
        // A map's lanes widened past a register (`planner/loops.hpp` `put_the_lanes`) narrow to a chunk along them,
        // which a wider vector would leave whole to the tail: a stream's chunk of 8 samples, for one.
        for (std::int32_t i = 0; i < found[k].count; ++i) {
            for (Schedule::Loop& l : s.stages[static_cast<std::size_t>(found[k].first + i)].loops) {
                while (l.kind == LoopKind::Vector && l.unroll == 1 && l.axis == laid[k].axis[static_cast<std::size_t>(i)] && l.step > found[k].step && l.step > 2) {
                    l.step /= 2;
                }
            }
        }
    }
    return layouts;
}

} // namespace heinzel::loops::planner
