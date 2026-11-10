// Every form the gate must let pass: commas that separate no declarators, and lines that hold one declaration.
// int a, b; in a comment declares nothing,
/* and neither does int c, d; in a block comment,
   int e, f; on its next line */
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <vector>
#define PAIR int a, b;
#define WRAPPED(x, y) \
    int x, y;

namespace heinzel::fixture {

using std::max, std::min;
typedef int Width, *WidthPointer;
using Pair = std::pair<int, int>;

enum class Kind { a, b };
enum Flags : int { kOne = 1, kTwo = 2 };
enum class Wrapped {
    first,
    second = 2,
};

template <class A, class B>
struct Base {};
template <class A,
          class B = int>
struct Wide {};

struct Derived : Base<int, float>, Wide<int> {
    Derived(int a, int b) : a_(a), b_(b) {}
    Derived(int a, int b, int c)
        : a_(a),
          b_(b + c) {}
    explicit Derived(float f) : a_{static_cast<int>(f)}, b_{0} {}
    int get() const { return a_; }
    int twice(int v) const { int t = v; return t + t; }
    bool operator<(const Derived& other) const { return a_ < other.a_; }
    int operator()(int a, int b) const { return a + b; }
    friend bool operator==(const Derived&, const Derived&) = default;
    virtual void apply(int a, int b) = 0;
    unsigned bits : 3;
    std::array<int, 2> pair_{1, 2};

private:
    int a_;
    int b_;
};

struct Wrapping
    : Base<int, int>,
      Wide<float, float> {};

int add(int a, int b);
int add(int a, int b) { return a + b; }
bool learn(int x,
           bool& learned_something, Diagnostic& why) {
    return x > 0;
}
auto split(int z) -> std::pair<int, int> { return {z, -z}; }
template <class... Ts>
void each(Ts... args) {
    (use(args), ...);
    static_assert(sizeof...(Ts) > 0, "a, b");
}
template <int N>
void positive() requires(N > 0) {}

void statements(std::vector<std::vector<float>>& in, std::map<int, std::vector<int>>& m, int a, int b) {
    for (int i = 0, j = 0; i < j; ++i, ++j) {}
    for (auto [key, values] : m) {}
    if (auto [lo, hi] = split(a); lo < hi) {}
    auto [re, im] = split(b);
    const auto& [k, v] = *m.begin();
    std::pair<int, int> p;
    std::array<float, 3> three{1, 2, 3};
    std::array<std::array<float, 3>, 3> grid{};
    std::vector<std::pair<int, int>> pairs{{1, 2}, {3, 4}};
    std::function<void(int, int)> callback;
    void (*pointer)(int, int) = nullptr;
    const Chain c = chain_from<P, node_at(P, r).link, full, r>();
    auto e = std::make_unique<Executor<Build, zen2, Checked>>();
    constexpr const loops::Program& kNest = kProgramOf<kBlurF32<H, Wd, K>, kMachine>;
    const int smaller = a < b ? a : b;
    bool lo = a < b;
    int total = add(add(a, b),
                    add(b, a));
    Stage stage{.slots = 2, .loads = 1};
    constexpr Table kTable{
        {1, 2},
        {3, 4},
    };
    auto sum = [](int x, int y) { return x + y; };
    auto capture = [a, &b](int x) { return a + b + x; };
    auto local = [&] {
        int first = a;
        int second = b;
        return first + second;
    };
    call([&] { int only = a; return only; }, a, b);
    expect(refused([] { Input x{"x", Signal{8}}; return x; }, "one, two"));
    a = 1, b = 2;
    ex.input("x") = a, ex.output("y") = b;
    std::tie(a, b) = split(a);
    max(a, b);
    x.template get<0, 1>();
    const char* text = "int a, b;";
    const char* glob = "data/*.wav";
    char quote = '"';
    const char* raw = R"(int a, b;
                         int c, d;)";
    char comma = ',';
    int rate = 48'000;
    int* numbers = new int[2]{1, 2};
    delete[] numbers;
    T&& moved = std::move(a);
    if (a && b) {}
    switch (a) {
    case 1: {
        int one = 1;
        break;
    }
    default:
        break;
    }
    do {
    } while (a < b);
    if constexpr (requires { a.v; }) {}
    if !consteval {
        int runtime = a;
    }
    SHAPE(1, 300, 100)
    int after_a_macro = b;
    goto done;
done:
    return;
}

template <class F>
int generic_lambdas(F f) {
    const auto g = [&]<std::size_t... I, std::size_t... O>(std::index_sequence<I...>, std::index_sequence<O...>) {
        return f(sizeof...(I), sizeof...(O));
    };
    return g(std::make_index_sequence<2>{}, std::make_index_sequence<3>{});
}

int comma_expression(int a, int b) {
    if (a) {
        return a, b;
    } else {
        throw Refusal(a, b);
    }
}

} // namespace heinzel::fixture
