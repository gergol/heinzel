// Every way a line declares two names, one offence a line: the would-object test in tests/CMakeLists.txt pins the
// count and some of the line numbers, so a line added here moves them.
#include <array>
#include <memory>
#include <vector>

int a, b;
int c = 1, d = 2;
std::vector<int> e{1, 2}, f{3};
std::vector<int> g(3), h(4);
const int* i, *j;
int k(int), l(int);
static constexpr std::array<float, 2> m{}, n{};
unsigned long o = 0, p = 0;
template <class T> T one = T{}, two = T{};

struct Members {
    int x, y;
    float w = 0, h = 0;
    std::vector<float> lo, hi;
    unsigned bits : 3, more : 2;
public: int pa, pb;
};

struct OneLine { int x; int y; };
struct LinReluInput { std::vector<float> x, w, b; };

void block(const std::vector<std::vector<float>>& in) {
    int a = 0, b = 1;
    const std::vector<float>&x = in[0], &wh = in[1];
    Input x{"x", Signal{5}}; Output y{"y"};
    auto f = [](int v) { return v; }, g = f;
    std::vector<int> v(3),
        w(4);
    int i = 1; int j = 2;
    for (int k = 0; k < 3; ++k) { int p = k; int q = p; }
    auto lambda = [&] { int m, n; };
    call([&] { int s = 0, t = 1; return s + t; });
    expect(refused([] { Executor<kEcho> ex, moved = std::move(ex); ex.reset(); }, "moved from"));
    if (true) { float l, r; }
    decltype(a) d1, d2;
    typename Members::type t1, t2;
    alignas(16) float a1[4], a2[4];
    [[maybe_unused]] int u1, u2;
    std::size_t s1 = 0, s2 = 0;
    auto [re, im] = split(v); int n = 0;
    constexpr Graph kBlur = blur(0), kSharp = sharpen(1),
                    kEdge = edges(2);
}

int twice(int v) { int t; int u; return v; }
int first() { return 1; } int second() { return 2; }

constexpr double root(double x) {
    if !consteval { double r1, r2; }
    SHAPE(1, 300, 100)
    int after_macro_a, after_macro_b;
done: int l1, l2;
    const char* glob = "data/*.wav"; int g1, g2;
    char quote = '"'; int q1, q2;
    return x;
}
