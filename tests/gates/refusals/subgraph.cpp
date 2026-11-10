// USES OF `subgraph` THAT MUST NOT COMPILE (Plan/archive/14-dsl/02 §7, Plan/28-dsl-proposals/02 A5): cases 1-7 would
// let an `Expr` in or out unseen, so an input the body declared could miss its name, or one it did not get one; case
// 8 would declare one piece's names twice; case 9 nests a piece the wrong way, as the body of another. Each case is
// its own target (`-DCASE=<k>`, tests/CMakeLists.txt), required to fail with its own message and one error:
//   1. a lambda that captures (a captured input would be taken for the body's and prefixed), refused when made;
//   2. an `Output` argument (it holds an `Expr` the pins cannot see);
//   3. a struct result (its `Expr`s cannot be scanned);
//   4. a body taking an argument by non-const reference (it could hand a body's input out through it);
//   5. an argument of a class derived from `Expr` that holds another (only the base would be pinned);
//   6. a member function as the body, refused when made;   7. a body returning nothing;
//   8. a piece applied twice: refused while the graph compiles, its text in the compiler's note;
//   9. a piece made the body of another, refused when made (its fallback runs, so the graph is built while compiling).
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/frontend/subgraph.hpp"

using namespace heinzel;

struct Stereo : Expr {
    Expr right;
};
struct Gain {
    Expr times(Expr x) const { return x * 2.0; }
};

struct Two {
    Expr a;
    Expr b;
};

constexpr Graph refused() {
    Input x{"x", Signal{4}};
#if CASE == 1
    Input k{"k", Signal{4}};
    auto piece = subgraph("captures", [k](Expr v) { return v * k; });
    const Expr y = piece(x);
#elif CASE == 2
    Output o{"o"};
    o = x * 2.0;
    auto piece = subgraph("output", [](Expr v, const Output&) { return v; });
    const Expr y = piece(x, o);
#elif CASE == 3
    auto piece = subgraph("struct", [](Expr v) { return Two{v, v}; });
    const Expr y = piece(x).a;
#elif CASE == 4
    auto piece = subgraph("reference", [](Expr& v) { return v; });
    const Expr y = piece(x);
#elif CASE == 5
    auto piece = subgraph("derived", [](const Stereo& s) { return Expr(s); });
    const Expr y = piece(Stereo{x, x});
#elif CASE == 6
    auto piece = subgraph("member", &Gain::times);
    const Expr y = piece(x);
#elif CASE == 7
    auto piece = subgraph("nothing", [](Expr) {});
    const Expr y = piece(x);
#elif CASE == 8
    auto piece = subgraph("twice", [](Expr v) { return v * 2.0; });
    const Expr once = piece(x);
    const Expr y = piece(once);
#elif CASE == 9
    auto inner = subgraph("inner", [](Expr v) { return v * 2.0; });
    auto outer = subgraph("outer", inner);
    const Expr y = outer(x);
#endif
    Output out{"out"};
    out = y;
    return out;
}

// Case 8 is refused while the graph is built, and case 9's fallback builds; built so, the others' fallbacks (an empty
// `Expr`) would fail twice.
#if CASE == 8 || CASE == 9
constexpr Graph kRefused = refused();
int main() { return static_cast<int>(kRefused.nodes.size()); }
#else
int main() { return static_cast<int>(refused().nodes.size()); }
#endif
