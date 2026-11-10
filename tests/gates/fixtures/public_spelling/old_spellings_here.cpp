// The public-spelling gate's would-object fixture: each retired spelling in code, and look-alikes the gate lets
// pass. Never compiled. In a comment none counts: BuildContext, y.is(x), detail::input, notation::einsum.
/* Nor in a block comment: delayed(x, 1), finish(where, "y", e),
   across lines: heinzel::detail, hz::detail, append_unary */
const BuildContext where{};
y.is(x + 0.5 * y.previous (1));
Expr d = delayed(x, 1);
Graph g = finish( where, "y", d);
Expr i = input(where, "x");
Expr t = notation::transpose(x, order);
Expr u = append_unary(x, op::Exp, attr);
Expr v = detail::delayed(x, 1); /* detail:: and delayed( both */
using namespace heinzel::detail;
namespace hzd = hz::detail;
using namespace detail;
namespace d = detail;
Graph h = finish_gradients(context, "loss", loss, {});
DimDecl k = declare_dim(0);
Shape s{extent_of(k)};
BuildContext{}.with_sample_rate(48000).with_default_precision(High);
input(g, "x").set_shape(Shape{8}).set_dtype(F32);
InputAnnot a{};
const char* glob = "data/*.wav"; Expr w = notation::reverse(x);
/* see https://example.org */ Expr q = delayed(x, 2);
char quote = '"'; auto url = "http://x"; Expr e1 = delayed(x, 3);
auto raw = R"(data/*.wav
)"; Expr e2 = delayed(x, 4);
constexpr Index n2{48'000}; Expr e3 = delayed(x, 5);
// a comment continued \
BuildContext hidden_by_the_continued_comment;
y.previous
    (1);
auto raw2 = R"x(a " // b)x"; Expr e4 = delayed(x, 6);
constexpr Index n3{1'0}; // BuildContext, after a digit separator
auto raw3 = R"x(a )" b)x"; // BuildContext, after a raw string whose delimiter matters
#define SPELLED FOR"(" // BuildContext, after an R" that opens no raw string
auto esc = "a\"b /*"; Expr e5 = delayed(x, 7); // */
char esc2 = '\''; Expr e6 = delayed(x, 8); // BuildContext, after an escaped quote
auto spliced = "a\
/*"; Expr e7 = delayed(x, 9); // */
wchar_t wq = L'"'; auto s6 = "/*"; Expr e8 = delayed(x, 10); // */
auto raw4 = u8R"(" /* )"; Expr e9 = delayed(x, 11); // */
loops::planner::Draft plan = *loops::planner::draft_of(g, zen2);
Expr m = y[n - 3.0 * units::millisecond];
Expr u2 = y[n - 7.0 * hz::units::microsecond];
set_default_precision(g, High);
Expr b = hz::instantiate ("bass", bass_shelf, x);
std::string listing = heinzel::loops::print(program);
// Look-alikes, none an offence:
Graph h2 = fir_delayed();
bool open = file.is_open();
MyBuildContexts contexts;
Expr z = notation_like::x;
auto f = kfr::detail::fft;
Drafted drafts = draft_off(g);
auto chrono = std::chrono::milliseconds{3}; auto us = 5.0 * units::us; // three milliseconds back
g.default_precision = High; my_set_default_precision(g); Graph f2 = finish(High, y);
auto bass = subgraph("bass", bass_shelf); Expr b2 = bass(x); Expr b3 = instantiated(x) + my_instantiate(x);
dsp->instantiate(48000); engine.instantiate (48000);
std::string text = print<smooth>() + my_loops::print(p) + loops::printed(p);
