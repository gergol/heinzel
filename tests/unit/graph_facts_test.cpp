// The graph facts (`loops/graph/facts.hpp`) are what the planner and the lowering ask of a graph again and again,
// worked out once in a few passes. This holds them to their definitions, written here as plain scans of the graph, the
// way each was asked before the table existed: every value's producer, what a read of it finds, what it is read
// through and how far back it is read, and every node's statement; and, once a schedule places the nodes in stages,
// whether anything outside some stages reads a value (`graph::read_outside`, from the readers `where_values_are_made`
// gathers from the last node to the first). Over the bench corpus, the operation corpus, the surface corpus with its
// stream symbolic and literal, the band cases, and views over a variable read through them (the frontend reads a
// defined variable as its definition; they are turned back by hand), each as the planner stages it and as two
// schedules that scatter its nodes over every stage.

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/places.hpp"
#include "heinzel/loops/planner/plan.hpp"
#include "heinzel/sema/analyze.hpp"

#include "agreement_graphs.hpp"
#include "band_cases.hpp"
#include "both_ways.hpp"
#include "corpus_graphs.hpp"

#include <array>
#include <cstdio>
#include <string>
#include <vector>

using namespace heinzel;

namespace {

namespace graph = heinzel::loops::graph;

int g_wrong = 0;

// ---- the definitions, as scans ------------------------------------------------------------------------------

std::int32_t producer_of(const Graph& g, std::int32_t v) {
    for (std::size_t n = 0; n < g.nodes.size(); ++n) {
        if (g.nodes[n].output == v) {
            return static_cast<std::int32_t>(n);
        }
    }
    return -1;
}

// A variable and its defining expression are one number: a read of it finds the first definition's output.
std::int32_t read_as(const Graph& g, std::int32_t v) {
    if (g.values[static_cast<std::size_t>(v)].category != ValueCategory::Variable) {
        return v;
    }
    for (const Node& n : g.nodes) {
        if (n.attr.kind == AttrKind::Recurrence && n.attr.recurrence.defines == v) {
            return n.output;
        }
    }
    return v;
}

// An affine view is a statement unless everything reading it is a view reading through it; new extents over the same
// elements need what they read made. A window never is one, and what is no view always is.
bool has_a_statement(const Graph& g, const Node& view) {
    if (!graph::is_an_affine_view(view)) {
        return !graph::folds_into_its_readers(view);
    }
    if (is_a_result(g, view.output)) {
        return true;
    }
    bool read = false;
    for (const Node& n : g.nodes) {
        for (std::size_t k = 0; k < rules_of(n.op).arity; ++k) {
            if (n.inputs[k] == view.output) {
                read = true;
                if (graph::keeps_elements_in_order(n) || !(graph::is_an_affine_view(n) || graph::folds_into_its_readers(n))) {
                    return true;
                }
            }
        }
    }
    return !read;
}

// `v` read through every view with no statement, recursively; a view with no address reads as itself.
graph::Viewed read_through_views(const Graph& g, const graph::Facts& facts, std::int32_t v) {
    const std::int32_t made_by = producer_of(g, v);
    if (made_by < 0 || has_a_statement(g, g.nodes[static_cast<std::size_t>(made_by)]) ||
        !graph::why_no_address(g, facts, g.nodes[static_cast<std::size_t>(made_by)]).empty()) {
        return graph::read_as_itself(v);
    }
    const Node& view = g.nodes[static_cast<std::size_t>(made_by)];
    return graph::through_view(g, view, read_through_views(g, facts, view.inputs[0]));
}

std::int32_t deepest_past_read(const Graph& g, std::int32_t v) {
    std::int32_t deepest = 0;
    for (const Node& n : g.nodes) {
        const std::int32_t back = graph::hands_its_operand_on(n) ? samples_back(n) : graph::gathers(n) == loops::Gather::Back ? graph::reach_of(n) + 1 : 0;
        if (back > deepest && read_as(g, n.inputs[0]) == v) {
            deepest = back;
        }
    }
    return deepest;
}

// Is `v` read by something outside stages [lo, hi): a node with a statement in another stage or in none, or the caller?
// A view with no statement is not a reader itself, but its readers are the value's; a variable's readers are its
// definition's.
bool read_outside(const Graph& g, const graph::ValuePlaces& at, std::int32_t v, std::int32_t lo, std::int32_t hi) {
    if (is_a_result(g, v)) {
        return true;
    }
    for (const Node& n : g.nodes) {
        for (std::size_t k = 0; k < rules_of(n.op).arity; ++k) {
            if (read_as(g, n.inputs[k]) != v) {
                continue;
            }
            const std::int32_t home = at.stage[static_cast<std::size_t>(n.output)];
            if (!has_a_statement(g, n) ? read_outside(g, at, n.output, lo, hi) : home < lo || home >= hi) {
                return true;
            }
        }
    }
    return false;
}

// ---- the table against them ---------------------------------------------------------------------------------

using Stages = fixed_vector<loops::Schedule::Stage, loops::kMaxStages>;

// Every stage range of `stages`, for every value a node makes: what the readers say against the scan.
void check_readers(const std::string& name, const Graph& g, const graph::Facts& facts, const Stages& stages) {
    const graph::ValuePlaces at = graph::where_values_are_made(g, facts, stages);
    const auto count = static_cast<std::int32_t>(stages.size());
    for (const Node& n : g.nodes) {
        const std::int32_t v = n.output;
        const std::int32_t home = at.stage[static_cast<std::size_t>(v)];
        bool agree = graph::leaves_its_stage(at, v) == read_outside(g, at, v, home, home + 1);
        for (std::int32_t lo = 0; lo < count; ++lo) {
            for (std::int32_t hi = lo + 1; hi <= count; ++hi) {
                agree = agree && graph::read_outside(at, v, lo, hi) == read_outside(g, at, v, lo, hi);
            }
        }
        if (!agree) {
            std::printf("%s: who reads value %d outside some stages disagrees with its definition\n", name.c_str(), v);
            ++g_wrong;
        }
    }
}

// The nodes scattered over every stage in turn: those with a statement only (as the planner places them), or all.
Stages scattered(const Graph& g, const graph::Facts& facts, bool all) {
    std::array<fixed_vector<std::int32_t, loops::kMaxStageStmts>, loops::kMaxStages> members{};
    for (std::size_t i = 0, next = 0; i < g.nodes.size(); ++i) {
        if (all || facts.statement[i]) {
            members[next++ % loops::kMaxStages].push_back(static_cast<std::int32_t>(i));
        }
    }
    Stages stages{};
    for (const auto& nodes : members) {
        stages.push_back(loops::Schedule::Stage{.nodes = nodes});
    }
    return stages;
}

void check(const std::string& name, Graph g) {
    if (!infer(g).ok) {
        return; // (a graph the analysis refuses is planned by nobody)
    }
    const graph::Facts facts = graph::facts_of(g);
    const auto wrong = [&](const char* what, std::size_t at) {
        std::printf("%s: %s of %zu disagrees with its definition\n", name.c_str(), what, at);
        ++g_wrong;
    };
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        if ((facts.statement[i] != 0) != has_a_statement(g, g.nodes[i])) {
            wrong("the statement", i);
        }
    }
    for (std::size_t v = 0; v < g.values.size(); ++v) {
        const auto at = static_cast<std::int32_t>(v);
        if (facts.producer[v] != producer_of(g, at)) {
            wrong("the producer", v);
        }
        if (facts.read_as[v] != read_as(g, at)) {
            wrong("what a read finds", v);
        }
        if (facts.deepest[v] != deepest_past_read(g, at)) {
            wrong("the deepest past read", v);
        }
        if (!(facts.through[v] == read_through_views(g, facts, at))) {
            wrong("what it is read through", v);
        }
    }
    check_readers(name + ", scattered", g, facts, scattered(g, facts, false));
    check_readers(name + ", all scattered", g, facts, scattered(g, facts, true));
    if (const std::expected<loops::Schedule, loops::Refusal> planned = loops::planner::schedule_of(g, zen2); planned) {
        check_readers(name + ", as planned", g, facts, planned->stages);
    }
}

// The graph with every read of a variable's definition, but the definition's own, turned back into a read of the
// variable: what the frontend spells only before the definition, here after it.
Graph turned_back(Graph g) {
    for (const Node& def : g.nodes) {
        for (Node& r : g.nodes) {
            for (std::size_t k = 0; def.attr.kind == AttrKind::Recurrence && k < kMaxNodeInputs; ++k) {
                r.inputs[k] = r.inputs[k] == def.output && r.attr.kind != AttrKind::Recurrence ? def.attr.recurrence.defines : r.inputs[k];
            }
        }
    }
    return g;
}

// A variable, fed back or not, read through views with no statement: a window, a slice under a window, two slices
// under a map, and a window and a slice side by side.
Graph viewed_variable(bool fed_back, int how) {
    constexpr Index n{};
    Input x{"x", Signal{8, 64}};
    Variable v{Signal{8, 64}};
    if (fed_back) {
        v[n] = x[n] + 0.5 * v[n - 1];
    } else {
        v[n] = x[n] * 0.5;
    }
    Output out{"out"};
    out = how == 0   ? Expr(v).unfold(0, 3).sum(1)
          : how == 1 ? Expr(v).slice(0, 1, 6).unfold(0, 3).sum(1)
          : how == 2 ? Expr(v).slice(0, 1, 6).slice(0, 1, 4) * 2.0
                     : Expr(v).unfold(0, 3).max(1) + Expr(v).slice(0, 1, 6) * 0.0;
    return out;
}

std::vector<std::pair<std::string, Graph>> bench_corpus() {
    return {{"elem", corpus::elem()},
            {"rowsum4", corpus::rowsum<4>()},
            {"mm3x4x5", corpus::matmul<3, 4, 5>()},
            {"mm64", corpus::matmul<64, 64, 64>()},
            {"linear_relu", corpus::linear_relu()},
            {"blurxy8x6", corpus::blurxy<8, 6, 3>()},
            {"blurxy1024", corpus::blurxy<1024, 2048, 3>()},
            {"rmsnorm", corpus::rmsnorm()},
            {"softmax", corpus::softmax()},
            {"ladder_bank", corpus::ladder_bank()},
            {"bank4", corpus::one_pole_bank(4, 0.5, 64)},
            {"bank_none", corpus::one_pole_bank_of_none(0.5)},
            {"median37", corpus::sliding_median(37, 5)},
            {"strip", corpus::strip()},
            {"attention16", corpus::attention<16, 8>()},
            {"conv4096x9", corpus::conv<4096, 9>()},
            {"blur8x6", corpus::blur<8, 6, 3>()},
            {"harris64", corpus::harris<64, 64>()},
            {"fm", corpus::fm_oscillator()},
            {"brightness", corpus::brightness()},
            {"rownorm", corpus::rownorm()},
            {"mlp2", corpus::mlp2()},
            {"unsharp64", corpus::unsharp<64, 64, 3>()},
            {"three_blurs64", corpus::three_blurs<64, 70>()},
            {"lag_of_a_lag", corpus::lag_of_a_lag()},
            {"fir_feed", corpus::fir_over_a_feed()},
            {"delay_line", corpus::delay_line()},
            {"padded", corpus::padded()},
            {"tall_relu", corpus::tall(true)}};
}

} // namespace

int main() {
    std::size_t graphs = 0;
    const auto both = [&](const std::string& name, const Graph& g) {
        check(name, g);
        check(name + ", turned back", turned_back(g));
        graphs += 2;
    };
    for (const auto& [name, g] : bench_corpus()) {
        both(name, g);
    }
    for (std::size_t i = 0; i < testing::kCorpusSize; ++i) {
        both("operation corpus " + std::to_string(i), testing::corpus_graph(i));
    }
    for (const bool symbolic : {true, false}) {
        agreement_graphs::sym = symbolic;
        for (const agreement_graphs::Expected& one : agreement_graphs::kCorpus) {
            both(std::string(symbolic ? "symbolic " : "literal ") + one.name, one.build());
        }
    }
    for (const int also : {0, 1, 2}) {
        both("band blurxy " + std::to_string(also), band_cases::blurxy(also));
    }
    for (const auto& [name, g] : std::vector<std::pair<std::string, Graph>>{{"band harris", band_cases::harris()},
                                                                         {"band chain", band_cases::chain()},
                                                                         {"band conv", band_cases::conv()},
                                                                         {"band oscillator", band_cases::oscillator()},
                                                                         {"band one-pole", band_cases::onepole()}}) {
        both(name, g);
    }
    for (const bool fed_back : {false, true}) {
        for (const int how : {0, 1, 2, 3}) {
            both("a viewed variable " + std::to_string(fed_back) + std::to_string(how), viewed_variable(fed_back, how));
        }
    }
    if (g_wrong != 0) {
        std::printf("%d facts disagree with their definitions\n", g_wrong);
        return 1;
    }
    std::printf("graph_facts: every fact of %zu graphs agrees with its definition\n", graphs);
    return 0;
}
