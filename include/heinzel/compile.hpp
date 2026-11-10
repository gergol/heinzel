#pragma once

// Compiling a graph for a `Machine` (`default_machine` unless one is passed): infer -> the ranges -> rewrite -> plan ->
// lower. The result is the analysed graph, as rewritten, and the loop-tree program that runs it (`loops/run.hpp`); a
// graph in which an operation may receive a value outside its domain is refused, and so is one the loop tree cannot
// run. It runs at compile time, and at run time for a graph whose sizes arrive late. `compile` stops on a refusal; `try_compile` takes the same steps and hands the reason back. `checked` (a debug
// build's default) turns each promise the proof of ranges did not prove into an output an `Executor` checks. The rewrite
// begins with the copy a delay line reads of an input (`rewrite/copied_inputs.hpp`), made in place on the analysed graph
// so that the other rules and the planner's fallback to the graph as written see it alike: without it, a graph that
// has no plan.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string_view>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/planner/plan.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/ranges/in_every_rounding.hpp"
#include "heinzel/ranges/interval.hpp"
#include "heinzel/ranges/proven.hpp"
#include "heinzel/rewrite/copied_inputs.hpp"
#include "heinzel/rewrite/rewritten.hpp"
#include "heinzel/schedule/machine.hpp"
#include "heinzel/sema/analyze.hpp"
#include "heinzel/support/fail.hpp"
#include "heinzel/support/fixed_string.hpp"

namespace heinzel {

namespace detail {

// Whether a build checks what an `Executor` is given and the promises the proof did not prove (Plan/26-value-ranges/04):
// not under `NDEBUG`, as `assert` does not. The one place where `NDEBUG` decides anything about ranges. Not `inline`,
// so that a debug unit and a release unit each have their own.
#ifdef NDEBUG
constexpr bool kChecked = false;
#else
constexpr bool kChecked = true;
#endif

} // namespace detail

// What compiling produces: the graph as analysed and rewritten — the program's value numbers are its —
// and the loop-tree program that runs it.
struct Compiled {
    Graph graph{};
    loops::Program program{};

    constexpr bool operator==(const Compiled&) const = default;
};

// What compiling says when it may say no: `refusal` is the analysis's message or the planner's or the
// lowering's refusal by name, and empty exactly when `compiled` holds a program. `ranges` is what a debug `Executor`
// checks: each input's declared range, by input index, the whole line where none is declared; and the range of each
// promise checked, by k, the output `#k` that checks it.
struct CompileResult {
    fixed_string<kMaxMessageLength> refusal{};
    Compiled compiled{};
    struct {
        fixed_vector<Range, kMaxIO> inputs{};
        fixed_vector<Range, kMaxIO> promises{};
    } ranges{};
};

// The graph the planner is given and its plan: the graph as rewritten (`rewrite/`), or as written where the rewritten
// one is refused. A rule may move a sum's leading terms out of a cycle into a stage of their own, and a graph near
// `kMaxStages` then has one too many (the review of Decision 66); the rewrite never refuses a graph that runs.
struct Planning {
    Graph graph{};
    std::expected<loops::Planned, loops::Refusal> planned;
};
namespace detail {

// `p.graph` planned, or `g` as written where that is refused, each plan made in `p` (a program, half a megabyte at
// raised capacities), so that no frame holds one: an unoptimized build's frames hold every temporary, and a run-time
// `compile` came near 8 MB (`rewrite_parallel_form`).
constexpr void plan(Planning& p, const Graph& g, const Machine& machine) {
    loops::plan_and_lower_into(p.planned, p.graph, machine);
    if (!p.planned && !(p.graph == g)) {
        p.graph = g;
        loops::plan_and_lower_into(p.planned, g, machine);
    }
}

using StatedRanges = decltype(CompileResult::ranges);

// The output that checks the k-th promise checked.
constexpr Name check_name(std::size_t k) {
    MessageWriter digits;
    digits << std::string_view{"#"} << static_cast<std::int64_t>(k);
    const Diagnostic written = digits.problem();
    Name name{};
    for (std::size_t i = 0; i < written.message.length(); ++i) {
        name.data_[i] = written.message.data_[i];
    }
    return name;
}

// The promises of an analysed graph the proof did not prove, as outputs `#k` after the user's, k counting them in the
// order they were written. A promise on a recurrence's definition names the `_recur`'s value, which carries its numbers:
// an output on the definition, inside a filter section, would keep even a cascade's last section from its parallel form.
// A value no node makes needs no check: of those a promise reaches only a constant, which the proof decides. Past
// `kMaxIO` it stops, as a graph's other capacities do, so that `compile`'s note shows the text.
constexpr void add_checks(Graph& g, const ranges::Proof& proof, StatedRanges& out) {
    std::vector<std::int32_t> named(g.values.size(), kNoValue); // per value: the value its check names
    for (const Node& n : g.nodes) {
        named[static_cast<std::size_t>(n.output)] = n.output;
    }
    for (const Node& n : g.nodes) {
        if (n.attr.kind == AttrKind::Recurrence) {
            named[static_cast<std::size_t>(n.inputs[0])] = n.output;
        }
    }
    std::size_t at = 0; // the mark's place among the promises, as the proof lists them
    for (const RangeMark& m : g.ranges) {
        if (!m.promised) {
            continue;
        }
        const bool proven = proof.promises[at++].verdict == ranges::Verdict::Proven;
        const std::int32_t value = named[static_cast<std::size_t>(m.value)];
        if (proven || value == kNoValue) {
            continue;
        }
        if (g.outputs.size() == kMaxIO) {
            fail("heinzel: the checks of promises make more outputs than kMaxIO; rebuild with -DHEINZEL_MAX_IO=N");
        }
        g.outputs.push_back(NamedValue{check_name(out.promises.size()), value});
        out.promises.push_back(m.range);
    }
}

// Each table read marked by whether its phase lies in one turn, [0, 1], at every precision
// (`ranges/in_every_rounding.hpp`): its wrap is then the identity, and the runner leaves it out (`loops/values.hpp`).
// A mark a graph brings is replaced.
constexpr void mark_phases_in_one_turn(Graph& g) {
    const std::vector<Range> held = ranges::in_every_rounding(g);
    for (Node& n : g.nodes) {
        if (loops::graph::gathers(n) == loops::Gather::Table) {
            const Range& phase = held[static_cast<std::size_t>(n.inputs[0])];
            n.attr.table.phase_in_one_turn = phase.count > 0 && ranges::inside(phase, ranges::span(0, 1));
        }
    }
}

// The proof of ranges, on an analysed graph (Plan/26-value-ranges/05): its first finding refuses the graph. Otherwise
// what an `Executor` checks (`04`): each input's declared range, and with `checked` the promises to check. Then the graph
// states no range, so that no rewrite, plan or program sees one: in release, the program is that of the graph written
// without them, but for the one mark the proof leaves the program, the same in every build: which table reads have
// their phase in one turn. The proof runs for every graph, checked or not. A port's range is its first mark, as the
// proof reads it. A function of its own, so that the proof takes no stack while the rewrites run.
constexpr Diagnostic take_ranges(Graph& g, bool checked, StatedRanges& out) {
    for (const NamedValue& port : g.inputs) {
        Range declared = ranges::whole();
        for (const RangeMark& m : g.ranges) {
            if (!m.promised && m.value == port.value) {
                declared = m.range;
                break;
            }
        }
        out.inputs.push_back(declared);
    }
    const ranges::Proof proof = ranges::proof(g);
    if (!proof.findings.empty()) {
        return ranges::message(g, proof, proof.findings[0]);
    }
    if (checked) {
        add_checks(g, proof, out);
    }
    mark_phases_in_one_turn(g);
    g.ranges.clear();
    return {};
}

// A refusal as `try_compile` holds it: cut where `fail` cuts, so the two spellings carry the same text.
constexpr fixed_string<kMaxMessageLength> held_refusal(std::string_view why) {
    fixed_string<kMaxMessageLength> held{};
    for (std::size_t i = 0; i < why.size() && i + 1 < kMaxMessageLength; ++i) {
        held.data_[i] = why[i];
    }
    return held;
}

} // namespace detail

constexpr Planning planned_as_rewritten(const Graph& g, const Machine& machine) {
    Planning p{rewrite::rewritten(g, machine), std::unexpected(loops::Refusal{})};
    detail::plan(p, g, machine);
    return p;
}

namespace detail {

// What `try_compile`'s steps leave: the refusal, empty where the plan holds a program; what an `Executor` checks; and the
// plan, null where the analysis or the proof refuses. The plan is on the heap, a `Planning` being about 200 KB, so that
// no frame holds it while the rewrite runs, the stack's deepest use at run time (`print<g>()`, a late graph's
// `try_compile`); under constant evaluation it is freed before `try_compile` returns.
struct Attempt {
    fixed_string<kMaxMessageLength> refusal{};
    StatedRanges ranges{};
    std::unique_ptr<const Planning> planning{};
};

// `try_compile`'s steps on `g`, which they analyse in place; `print<g>()` takes the same.
constexpr Attempt attempt(Graph& g, const Machine& machine, const Bindings& sizes, bool checked) {
    Attempt a{};
    const Diagnostic analysed = infer(g, sizes);
    if (!analysed.ok) {
        a.refusal = analysed.message;
        return a;
    }
    if (const Diagnostic proven = take_ranges(g, checked, a.ranges); !proven.ok) {
        a.refusal = proven.message;
        return a;
    }
    rewrite::copy_inputs_of_delay_lines(g);
    a.planning.reset(new Planning(planned_as_rewritten(g, machine))); // (the call's `p` is the heap's object)
    if (!a.planning->planned) {
        a.refusal = held_refusal(a.planning->planned.error().view());
    }
    return a;
}

} // namespace detail

// Under constant evaluation a refusal stops the build at the `fail`, and clang's note shows a capacity the frontend
// found passed, or a planner or lowering refusal written as a literal, by its text (`words_of(g.over)`, failed with
// here, not as `infer` copies it); the analysis's message, the proof's finding and a node's refusal, which names its
// operation and shapes (`loops/planner/stages.hpp`), are assembled, so read them from a run-time catch or from
// `try_compile`. At run time a refusal throws a `heinzel::error`.
constexpr Compiled compile(Graph g, const Machine& machine = default_machine, const Bindings& sizes = Bindings{},
                           bool checked = detail::kChecked) {
    if (g.over != Capacity::None) {
        fail(words_of(g.over));
    }
    const Diagnostic analysed = infer(g, sizes);
    if (!analysed.ok) {
        fail(analysed.message.view());
    }
    detail::StatedRanges unread{};
    if (const Diagnostic proven = detail::take_ranges(g, checked, unread); !proven.ok) {
        fail(proven.message.view());
    }
    rewrite::copy_inputs_of_delay_lines(g);
    const Planning p = planned_as_rewritten(g, machine);
    if (!p.planned) {
        fail(p.planned.error().view());
    }
    return Compiled{p.graph, p.planned->program};
}

// The same steps, with the reason handed back: for a caller that prints it or turns it into a
// `static_assert` whose message is the text itself.
constexpr CompileResult try_compile(Graph g, const Machine& machine = default_machine, const Bindings& sizes = Bindings{},
                                    bool checked = detail::kChecked) {
    const detail::Attempt a = detail::attempt(g, machine, sizes, checked);
    CompileResult result{};
    result.refusal = a.refusal;
    result.ranges = a.ranges;
    if (a.refusal.view().empty()) {
        result.compiled.graph = a.planning->graph;
        result.compiled.program = a.planning->planned->program;
    }
    return result;
}

} // namespace heinzel
