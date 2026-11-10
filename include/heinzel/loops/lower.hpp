#pragma once

// The lowering turns an analysed graph and the planner's schedule into a loop-tree program. It checks the schedule,
// forms a stage for each planned stage, applies the schedule's choices to it, lays out the bands, works out what each
// stage's loops imply, validates every stage and grows its tree, and copies which stages are kept and what each
// watches, and which run skewed, checked against the program. It decides nothing, and it derives what follows from the
// schedule rather than trusting it: a schedule that is not legal for the graph is refused by name, as a value. Buffers
// are made in their final order, and that order is the workspace's layout.

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/graph/places.hpp"
#include "heinzel/loops/graph/regions.hpp"
#include "heinzel/loops/graph/views.hpp"
#include "heinzel/loops/layout.hpp"
#include "heinzel/loops/lower_form.hpp"
#include "heinzel/loops/lower_stmts.hpp"
#include "heinzel/loops/normalize.hpp"
#include "heinzel/loops/primitives/bands.hpp"
#include "heinzel/loops/primitives/loops.hpp"
#include "heinzel/loops/primitives/memory.hpp"
#include "heinzel/loops/primitives/registers.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/schedule.hpp"
#include "heinzel/loops/validate.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel::loops {
namespace detail {

// A view with no affine address is refused before any stage is formed, as the planner refuses it
// (`planner/stages.hpp`), so no stage is formed that would read through it.
constexpr Done check_views(Lowering& l) {
    for (const heinzel::Node& n : l.g.nodes) {
        if (graph::is_an_affine_view(n) || graph::folds_into_its_readers(n)) {
            if (const std::string_view why = graph::why_no_address(l.g, l.facts, n); !why.empty()) {
                return std::unexpected(why);
            }
        }
    }
    return {};
}

// Planned stage `ps` as its schedule spells it, or why a schedule may not plan it.
constexpr std::expected<Spelled, Refusal> spelled(const Graph& g, const graph::Facts& facts, const Schedule::Stage& ps) {
    if (ps.nodes.empty()) {
        return std::unexpected("heinzel::compile: a planned stage holds no node");
    }
    const graph::GroupShape shape = graph::shape_of_group(g, ps); // (a HEINZEL_MAX_RANK above seven reaches it)
    const bool contracts = shape.fold >= 0 && graph::is_a_contraction(g.nodes[static_cast<std::size_t>(shape.fold)]);
    if (contracts && static_cast<std::size_t>(shape.domain.rank) >= kMaxAxes) {
        return std::unexpected("heinzel::compile: a contraction's domain has more axes than kMaxAxes");
    }
    // The axes the schedule must walk: the domain's, plus the contracted axis for a contraction; for a value made in
    // parts, the axes of its own shape.
    const heinzel::Node& first = g.nodes[static_cast<std::size_t>(ps.nodes[0])];
    const bool parts = ps.nodes.size() == 1 && graph::is_made_in_parts(first);
    const auto axes = static_cast<std::size_t>(parts ? g.values[static_cast<std::size_t>(first.output)].shape.rank : shape.domain.rank + (contracts ? 1 : 0));
    const auto n = static_cast<std::int32_t>(ps.loops.size());
    const bool has_inner = ps.inner_from < n;
    if (const Done r = loops::check_loops(g, facts, ps, axes, has_inner); !r) {
        return std::unexpected(r.error());
    }
    const std::expected<Skeleton, Refusal> s = loops::skeleton_of(ps, has_inner, axes);
    if (!s) {
        return std::unexpected(s.error());
    }
    const InnerLoops implied = loops::implied_inner(g, facts, ps, *s);
    if (ps.inner_from < 0 || ps.inner_from > n || implied.outer != (has_inner ? ps.inner_from : -1)) {
        return std::unexpected("heinzel::compile: the inner loops a schedule names are not the ones its loops imply");
    }
    const std::int32_t outer = has_inner ? ps.inner_from : 0;
    Spelled sp{.skeleton = *s,
               .inner = has_inner ? loops::loop_of(ps, *s, static_cast<std::size_t>(ps.inner_from)) : static_cast<std::int32_t>(s->walk.size()),
               .resumes = implied.resumes};
    if (ps.fetches && outer >= 1 && ps.loops[static_cast<std::size_t>(outer - 1)].kind != LoopKind::Tile) {
        sp.prefetch = loops::loop_of(ps, *s, static_cast<std::size_t>(outer - 1));
    }
    if (ps.parallel != -1) {
        sp.split = ps.parallel >= 0 && ps.parallel < n ? loops::loop_of(ps, *s, static_cast<std::size_t>(ps.parallel)) : -2;
    }
    return sp;
}

// Checks the schedule before anything is formed: each planned stage must be one a schedule may plan, and is spelled out
// as a skeleton. A stage fetches ahead only by lines of a known size. No band may hold a stage that reads a relaid or
// turned copy.
constexpr Done spell(Lowering& l) {
    const Schedule& s = l.schedule;
    for (const Schedule::Stage& ps : s.stages) {
        if (ps.fetches && s.line_bytes < 1) {
            return std::unexpected("heinzel::compile: a schedule fetches ahead by lines of no size");
        }
        const std::expected<Spelled, Refusal> sp = detail::spelled(l.g, l.facts, ps);
        if (!sp) {
            return std::unexpected(sp.error());
        }
        l.spelled.push_back(*sp);
    }
    for (const Schedule::Band& band : s.bands) {
        for (std::size_t k = 0; k < s.stages.size(); ++k) { // (in 64 bits: a band's count may be any int)
            const std::int64_t past = static_cast<std::int64_t>(k) - band.first;
            if (past >= 0 && past < band.count && (s.stages[k].relaid >= 0 || s.stages[k].turned >= 0)) { // (only a contraction reads one, which no band holds)
                return std::unexpected("heinzel::compile: a band holds a stage planned to read a relaid or turned copy");
            }
        }
    }
    return {};
}

// Applies the schedule's choices (a fused contraction, a relaid copy, a turned operand, a split loop, a prefetch) to each
// formed stage, every part of a value made in parts included, and holds the stage to the rules of loop shape. Then it
// checks the bands.
constexpr Done adopt(Lowering& l) {
    for (Formed& f : l.stages) {
        const Schedule::Stage& ps = l.schedule.stages[static_cast<std::size_t>(f.plan)];
        const Spelled& sp = l.spelled[static_cast<std::size_t>(f.plan)];
        const bool contracts = detail::contraction_in(l.g, f) >= 0;
        if (const Done r = !ps.contract ? Done{} : contracts ? detail::contract(l.g, f) : detail::fuse_the_product(f); !r) {
            return r;
        }
        if (const Done r = contracts && ps.relaid >= 0 ? detail::cache_read(f, ps.relaid, ps.relaid_block) : Done{}; !r) {
            return r;
        }
        if (sp.split == -2) {
            return std::unexpected("heinzel::compile: a schedule's split names a loop its stage does not have");
        }
        f.split = sp.split;
        f.prefetch = sp.prefetch;
        f.state_depth = ps.state_depth;
        if (const Done r = detail::check_shape(l, f); !r) {
            return r;
        }
    }
    return detail::check_bands(l.schedule);
}

// Makes the buffers that exist before any stage: the feeds, in input order, then one for each generator, a ramp or a
// constant tensor (a run's start has none: its readers compute it). One whose past nothing reads is a constant (its numbers are the program's), unless a sample of it
// holds no element (a constant is one sample); a ramp read in its past is filled once, when the workspace is made, with
// silence before it as deep as anything reads its past. (A tensor's past is never read.)
constexpr Done place_inputs(Lowering& l) {
    for (std::size_t i = 0; i < l.g.inputs.size(); ++i) {
        const std::int32_t v = l.g.inputs[i].value;
        const Value& fed = l.g.values[static_cast<std::size_t>(v)];
        const std::expected<std::int32_t, Refusal> b =
            detail::add_buffer(l, Buffer{.type = fed.type, .shape = fed.shape, .home = Home::Feed, .feed = static_cast<std::int32_t>(i), .value = v});
        if (!b) {
            return std::unexpected(b.error());
        }
        l.at.buffer[static_cast<std::size_t>(v)] = *b;
    }
    for (std::size_t ni = 0; ni < l.g.nodes.size(); ++ni) {
        const heinzel::Node& n = l.g.nodes[ni];
        if (!graph::is_generated(n) || graph::marks_a_run_start(n)) {
            continue;
        }
        const Value& made = l.g.values[static_cast<std::size_t>(n.output)];
        const std::int32_t past = l.facts.deepest[static_cast<std::size_t>(n.output)];
        const std::int32_t first = n.attr.generator.first;
        if (first >= 0 && past > 0) { // (a tensor's past is refused by the analysis; nothing would fill it)
            return std::unexpected("heinzel::compile: a constant tensor is read in its past");
        }
        Buffer generated{.type = made.type, .shape = made.shape, .value = n.output, .prefix = past, .ramp = first < 0, .first = first};
        generated.home = past > 0 || loops::bytes_per_sample(generated) == 0 ? Home::Workspace : Home::Constant;
        const std::expected<std::int32_t, Refusal> b = detail::add_buffer(l, generated);
        if (!b) {
            return std::unexpected(b.error());
        }
        l.at.buffer[static_cast<std::size_t>(n.output)] = *b;
    }
    return {};
}

// Forms every planned stage, in order. A value made in parts becomes one stage per part.
constexpr Done form(Lowering& l) {
    for (std::size_t here = 0; here < l.schedule.stages.size(); ++here) {
        const Schedule::Stage& ps = l.schedule.stages[here];
        const bool parts = ps.nodes.size() == 1 && graph::is_made_in_parts(l.g.nodes[static_cast<std::size_t>(ps.nodes[0])]);
        if (const Done r = parts ? detail::form_in_parts(l, static_cast<std::int32_t>(here)) : detail::form_stage(l, static_cast<std::int32_t>(here)); !r) {
            return r;
        }
    }
    return {};
}

// Appends each stage to the program in order: works out what its loops imply, validates it, and grows its tree. A band
// split among workers is checked last, on the finished stages.
constexpr Done settle(Lowering& l) {
    Tree t{.p = l.out};
    for (const Formed& f : l.stages) {
        const std::expected<Implied, Refusal> implied = detail::implied_by(l, f);
        if (!implied) {
            return std::unexpected(implied.error());
        }
        if (const Done r = detail::validate(l, f, *implied); !r) {
            return r;
        }
        if (const Done r = f.band_axis >= 0 ? detail::check_band_accesses(l.g, l.facts, l.schedule.stages[static_cast<std::size_t>(f.plan)], f, f.band_axis) : Done{};
            !r) {
            return r;
        }
        if (const Done r = detail::normalize(l, t, f, *implied); !r) {
            return r;
        }
    }
    for (std::size_t b = 0; b < l.out.bands.size(); ++b) {
        if (const Done r = l.out.bands[b].parallel ? detail::check_split_band(l.out, b) : Done{}; !r) {
            return r;
        }
    }
    return {};
}

using Watched = std::array<std::uint64_t, kWatchWords>;

// Whether all that stage `k` of `p` reads is decided by Params alone: it has no axes, stands in no band, and reads only
// constants, Params (feeds of no axes) and what earlier such stages store (`maker`: per buffer, the last stage before
// `k` that stores it, -1 where none does or that stage reads more). The Params that decide it, those it reads and those
// that decide its makers, go into `on[k]`.
constexpr bool params_alone(const Program& p, std::size_t k, bool banded, const std::vector<std::int32_t>& maker, std::vector<Watched>& on) {
    const Stage& stage = p.stages[k];
    bool alone = stage.axes.empty() && !banded;
    for (std::int32_t j = stage.first; j < stage.first + stage.count; ++j) {
        for (const Access& a : p.stmts[static_cast<std::size_t>(j)].in) {
            alone = alone && a.source != Source::First; // (a run's start changes while no Param does)
            if (a.source != Source::Buffer) {
                continue;
            }
            const Buffer& b = p.buffers[static_cast<std::size_t>(a.buffer)];
            const std::int32_t from = maker[static_cast<std::size_t>(a.buffer)];
            const bool param = b.home == Home::Feed && b.shape.rank == 0;
            alone = alone && (param || from >= 0 || b.home == Home::Constant);
            if (param) {
                on[k][static_cast<std::size_t>(b.feed) / 64] |= std::uint64_t{1} << (static_cast<std::size_t>(b.feed) % 64);
            }
            for (std::size_t w = 0; from >= 0 && w < kWatchWords; ++w) {
                on[k][w] |= on[static_cast<std::size_t>(from)][w];
            }
        }
    }
    return alone;
}

// Each stage's `kept` and `watched`, copied from its plan (a relay or turn copy has none), and held to what keeping a
// stage needs, worked out again from the program in stage order (the planner keeps by a stricter rule, whose makers
// make no output: `planner/kept.hpp`): a kept plan lowers into one stage (a value made in parts lowers into a stage a
// part) of no axes, in no band; all it reads is decided by Params alone, else a skipped run would keep stale numbers;
// it makes no output, which may be written in the caller's memory and a skipped run would leave unwritten; it watches
// exactly the Params that decide it. A stage not kept watches none.
constexpr Done copy_kept(Lowering& l) {
    std::vector<std::int32_t> formed(l.schedule.stages.size(), 0);
    for (const Formed& f : l.stages) {
        if (f.plan >= 0) {
            ++formed[static_cast<std::size_t>(f.plan)];
        }
    }
    std::vector<char> output(l.out.buffers.size(), 0);
    for (const std::int32_t b : l.out.outputs) {
        output[static_cast<std::size_t>(b)] = 1;
    }
    std::vector<std::int32_t> maker(l.out.buffers.size(), -1);
    std::vector<Watched> on(l.stages.size());
    for (std::size_t k = 0; k < l.stages.size(); ++k) {
        Stage& stage = l.out.stages[k];
        bool banded = false;
        for (const BandInfo& band : l.out.bands) {
            banded = banded || (static_cast<std::int32_t>(k) >= band.first && static_cast<std::int32_t>(k) - band.first < band.count);
        }
        const bool alone = detail::params_alone(l.out, k, banded, maker, on);
        bool shown = false;
        for (std::int32_t j = stage.first; j < stage.first + stage.count; ++j) {
            const Access& store = l.out.stmts[static_cast<std::size_t>(j)].store;
            if (store.source == Source::Buffer) {
                maker[static_cast<std::size_t>(store.buffer)] = alone ? static_cast<std::int32_t>(k) : -1;
                shown = shown || output[static_cast<std::size_t>(store.buffer)] != 0;
            }
        }
        if (l.stages[k].plan < 0) {
            continue;
        }
        const Schedule::Stage& ps = l.schedule.stages[static_cast<std::size_t>(l.stages[k].plan)];
        if (ps.kept && formed[static_cast<std::size_t>(l.stages[k].plan)] != 1) {
            return std::unexpected("heinzel::compile: a kept stage lowers into more than one stage");
        }
        if (ps.kept && (!stage.axes.empty() || banded)) {
            return std::unexpected("heinzel::compile: a kept stage has axes, or stands in a band");
        }
        if (ps.kept && !alone) {
            return std::unexpected("heinzel::compile: a kept stage reads what may change while no Param does");
        }
        if (ps.kept && shown) {
            return std::unexpected("heinzel::compile: a kept stage makes an output");
        }
        if (ps.watched != (ps.kept ? on[k] : Watched{})) {
            return std::unexpected("heinzel::compile: a kept stage watches the Params it and its makers read; one not kept, none");
        }
        stage.kept = ps.kept;
        stage.watched = ps.watched;
    }
    return {};
}

// Each stage's `skewed`, copied from its plan and held to what running skewed needs: a skewed plan lowers
// into one stage, whose lanes are its sections (a Vector along axis 0), in one chunk of a literal count: the link reads
// the section below in the lane below, which the first lane of a later chunk has not. Its time loop is its Loop over
// time.
constexpr Done copy_skewed(Lowering& l) {
    std::vector<std::int32_t> formed(l.schedule.stages.size(), 0);
    for (const Formed& f : l.stages) {
        if (f.plan >= 0) {
            ++formed[static_cast<std::size_t>(f.plan)];
        }
    }
    for (std::size_t k = 0; k < l.stages.size(); ++k) {
        const std::int32_t plan = l.stages[k].plan;
        if (plan < 0 || !l.schedule.stages[static_cast<std::size_t>(plan)].skewed) {
            continue;
        }
        if (formed[static_cast<std::size_t>(plan)] != 1) {
            return std::unexpected("heinzel::compile: a skewed stage lowers into more than one stage");
        }
        Stage& stage = l.out.stages[k];
        const auto time = static_cast<std::int32_t>(stage.axes.size()) - 1;
        const std::size_t end = k + 1 < l.out.stages.size() ? static_cast<std::size_t>(l.out.stages[k + 1].root) : l.out.nodes.size();
        const std::int64_t count = is_literal(stage.axes[0]) ? literal_of(stage.axes[0]) : -1;
        bool sections = false;
        bool one_chunk = count >= 0;
        for (auto n = static_cast<std::size_t>(stage.root); n < end; ++n) {
            const Node& here = l.out.nodes[n];
            const bool whole = here.step == count || (here.tail == Tail::Masked && here.step > count);
            sections = sections || (here.kind == NodeKind::Vector && here.axis == 0);
            one_chunk = one_chunk && !(here.kind == NodeKind::Loop && here.axis == 0 && !whole);
            stage.time_loop = here.kind == NodeKind::Loop && here.axis == time ? static_cast<std::int32_t>(n) : stage.time_loop;
        }
        if (!sections) {
            return std::unexpected("heinzel::compile: a skewed stage runs its sections in lanes, a Vector along axis 0");
        }
        if (!one_chunk) {
            return std::unexpected("heinzel::compile: a skewed stage holds its sections in one chunk of lanes, a literal count");
        }
        stage.skewed = true;
    }
    return {};
}

// Each peeled plan's time loop marked to run a run's first sample apart (`Node::peels_start`), held to what that needs:
// it lowers into one stage, which reads a run's start, in no band, split by no worker, skewed not, in no lanes, its time
// walked by one Clip loop a sample at a time, so that loop's first iteration is the run's first sample alone.
constexpr Done copy_peeled(Lowering& l) {
    std::vector<std::int32_t> formed(l.schedule.stages.size(), 0);
    for (const Formed& f : l.stages) {
        if (f.plan >= 0) {
            ++formed[static_cast<std::size_t>(f.plan)];
        }
    }
    for (std::size_t k = 0; k < l.stages.size(); ++k) {
        const std::int32_t plan = l.stages[k].plan;
        if (plan < 0 || !l.schedule.stages[static_cast<std::size_t>(plan)].peeled) {
            continue;
        }
        const Stage& stage = l.out.stages[k];
        bool reads = false;
        for (std::int32_t j = stage.first; j < stage.first + stage.count; ++j) {
            for (const Access& a : l.out.stmts[static_cast<std::size_t>(j)].in) {
                reads = reads || a.source == Source::First;
            }
        }
        bool banded = false;
        for (const BandInfo& band : l.out.bands) {
            banded = banded || (static_cast<std::int32_t>(k) >= band.first && static_cast<std::int32_t>(k) - band.first < band.count);
        }
        if (formed[static_cast<std::size_t>(plan)] != 1 || !reads || banded || stage.split >= 0 || stage.skewed || stage.axes.empty()) {
            return std::unexpected("heinzel::compile: a peeled stage is one stage reading a run's start, in no band, no split");
        }
        const auto time = static_cast<std::int32_t>(stage.axes.size()) - 1;
        const std::size_t end = k + 1 < l.out.stages.size() ? static_cast<std::size_t>(l.out.stages[k + 1].root) : l.out.nodes.size();
        for (auto n = static_cast<std::size_t>(stage.root); n < end; ++n) {
            if (l.out.nodes[n].kind == NodeKind::Vector) {
                return std::unexpected("heinzel::compile: a peeled stage runs in no lanes, until a benchmark asks");
            }
        }
        std::int32_t loop = -1;
        for (auto n = static_cast<std::size_t>(stage.root); n < end; ++n) {
            const Node& here = l.out.nodes[n];
            const bool over_time = (here.kind == NodeKind::Loop || here.kind == NodeKind::Unroll) && here.axis == time;
            if (over_time && (loop >= 0 || here.kind != NodeKind::Loop || here.tail != Tail::Clip || here.step != 1)) {
                return std::unexpected("heinzel::compile: a peeled stage walks time by one loop, a sample at a time");
            }
            loop = over_time ? static_cast<std::int32_t>(n) : loop;
        }
        if (loop < 0) {
            return std::unexpected("heinzel::compile: a peeled stage walks time by one loop, a sample at a time");
        }
        l.out.nodes[static_cast<std::size_t>(loop)].peels_start = true;
    }
    return {};
}

// A constant holds one sample, the same at every sample: no read walks its last axis, and none starts past its first
// sample there (`Home::Constant`).
constexpr Done broadcast_constants(Lowering& l) {
    for (Stmt& st : l.out.stmts) {
        for (std::size_t slot = 0; slot <= kMaxNodeInputs; ++slot) {
            Access& a = loops::access_in(st, slot);
            if (a.source != Source::Buffer || l.out.buffers[static_cast<std::size_t>(a.buffer)].home != Home::Constant) {
                continue;
            }
            const std::int32_t last = l.out.buffers[static_cast<std::size_t>(a.buffer)].shape.rank - 1;
            for (std::int32_t& along : a.buffer_axis) {
                along = along == last ? -1 : along;
            }
            if (last >= 0) {
                a.offset[static_cast<std::size_t>(last)] = 0;
            }
        }
    }
    return {};
}

// The buffer of each named output, in graph order.
constexpr Done place_outputs(Lowering& l) {
    for (const NamedValue& out : l.g.outputs) {
        const std::int32_t b = l.at.buffer[static_cast<std::size_t>(out.value)];
        if (b < 0 || l.g.values[static_cast<std::size_t>(out.value)].category == ValueCategory::Input) {
            return std::unexpected("heinzel::compile: an output is not the result of a lowered node");
        }
        l.out.outputs.push_back(b);
    }
    return {};
}

} // namespace detail

// The lowering itself: schedule `s` lowered for graph `g` into `out` (a default `Program`), or the reason it cannot be.
constexpr std::expected<void, Refusal> lower_into(const Graph& g, const Schedule& s, Program& out) {
    out.parallel_work = s.parallel_work;
    out.line_bytes = s.line_bytes;
    out.constants = g.constants;
    detail::Lowering l{.g = g, .schedule = s, .out = out, .facts = graph::facts_of(g)};
    if (!l.facts.why_not.empty()) {
        return std::unexpected(l.facts.why_not);
    }
    l.at = graph::where_values_are_made(g, l.facts, s.stages);
    for (detail::Done (*pass)(detail::Lowering&) : {detail::spell, detail::check_views, detail::place_inputs, detail::form, detail::turn_reads,
                                                      detail::adopt, detail::turn_all, detail::relay_all, detail::place_bands, detail::settle,
                                                      detail::broadcast_constants, detail::place_outputs, detail::copy_kept, detail::copy_skewed,
                                                      detail::copy_peeled}) {
        if (const detail::Done r = pass(l); !r) {
            return r;
        }
    }
    return {};
}

// The same, returning the program or calling `fail` with the refusal.
constexpr Program lower(const Graph& g, const Schedule& s) {
    Program p{};
    if (const std::expected<void, Refusal> r = loops::lower_into(g, s, p); !r) {
        fail(r.error().view());
    }
    return p;
}

} // namespace heinzel::loops
