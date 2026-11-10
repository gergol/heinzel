#pragma once

// The cascade the SOLVER's cost is measured on, at three regimes of the same graph.
//
// The guard's original cascade (in `guard.cpp`) exercises construction and the analysis sweeps, and
// its recorded numbers stay comparable because it is left exactly as it was. This is a second
// ladder for a second question: what does the extent solver cost, and does that cost stay flat per
// graph section.
//
// It has to be a DIFFERENT cascade, because the original one never asks the solver anything. Its
// only shapes are one block and some scalars, and a scalar broadcasts against anything without two
// axes ever meeting — so a symbolic version of it would pay the solver's construction and nothing
// else. Here each section takes a coefficient signal OF ITS OWN, declared with its own extent, so
// every section makes exactly one pair of axes meet. One unification per section is what makes
// "the marginal cost per section" a question about the solver rather than about the pass around it.
//
// The three regimes are the solver's three call sites, in the order a graph reaches them:
//
//   Literal          every extent is a number. The solver is CONSTRUCTED and never asked anything —
//                    which is what almost every graph in this tree does, so it is the regime whose
//                    cost is paid by everybody.
//   SymbolicBound    every section's extent is a symbol, and a binding supplies the size. The
//                    seeding loop, one `unify` per section, and the resolving pass all run.
//   SymbolicUnbound  the same, with no binding. The unifications happen and nothing pins a class,
//                    so the resolving pass canonicalises rather than substitutes.

#include <cstddef>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"

namespace heinzel {

enum class Extents { Literal, SymbolicBound, SymbolicUnbound };

inline constexpr std::int32_t kSolverCascadeBlock = 64;

// "g0", "g1", ... — a distinct name per section, so the graph does not depend on duplicate names
// being tolerated.
constexpr Name coefficient_name(int section) {
    Name n{};
    n.data_[0] = 'g';
    if (section < 10) {
        n.data_[1] = static_cast<char>('0' + section);
    } else {
        n.data_[1] = static_cast<char>('0' + section / 10);
        n.data_[2] = static_cast<char>('0' + section % 10);
    }
    return n;
}

constexpr Extent section_extent(Extents how, int section) {
    return how == Extents::Literal ? literal_extent(kSolverCascadeBlock) : Extent{Dim{section}};
}

template <int Sections, Extents How>
constexpr Graph solver_cascade() {
    constexpr Index n{};
    // Symbols past the declarations' table (twelve sections name thirteen), which no declaration says: the
    // inputs keep their old spelling (Plan/archive/14-dsl/02 §3, DS-18).

    // The running signal's axis is symbol 0; each section's coefficient signal declares symbol s+1,
    // and multiplying them is where the two meet.
    Expr signal = detail::input("x", Shape{section_extent(How, 0)});

    for (int s = 0; s < Sections; ++s) {
        Expr gain = detail::input(coefficient_name(s).view(), Shape{section_extent(How, s + 1)});
        Variable stage;
        const double feedback1 = 1.2 + 0.001 * s;
        const double feedback2 = -0.81;
        stage[n] = signal * gain - feedback1 * stage[n - 1] - feedback2 * stage[n - 2];
        signal = stage;
    }

    Graph g = finish(Output{"y"} = signal);

    Bindings sizes{};
    if constexpr (How == Extents::SymbolicBound) {
        for (int s = 0; s <= Sections; ++s) {
            sizes.bind(Dim{s}, kSolverCascadeBlock);
        }
    }

    const Diagnostic d = infer(g, sizes);
    if (!d.ok) {
        fail("the solver cascade did not resolve");
    }
    return g;
}

} // namespace heinzel
