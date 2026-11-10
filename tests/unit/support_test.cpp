// The fixed-capacity containers and the failure channel.
//
// Almost everything here is a static_assert, for the same reason as the build-context test: these
// types exist to work while the compiler is evaluating, so a test that only ran the program would
// not be testing the thing that matters. The runtime half at the bottom covers the one behaviour a
// static_assert cannot reach — that fail() throws something a caller can read.
#include "heinzel/support/fail.hpp"
#include "heinzel/support/fixed_string.hpp"
#include "heinzel/support/fixed_vector.hpp"

#include <string_view>

using namespace heinzel;

// ---- fixed_string ------------------------------------------------------------------------------
static_assert(fixed_string<8>{}.length() == 0, "a default string is empty");
static_assert(fixed_string<8>{"abc"}.length() == 3);
static_assert(fixed_string<8>{"abc"}.view() == std::string_view{"abc"});

// A shorter literal into a longer buffer: this is what lets one name type hold every name.
static_assert(fixed_string<64>{"x"}.view() == std::string_view{"x"});

// The deduction guide sizes the buffer from the literal, trailing '\0' included.
static_assert(fixed_string{"abc"}.length() == 3);
static_assert(sizeof(fixed_string{"abc"}.data_) == 4);

// Equality is over the buffer, so it is usable as a template argument and as a graph member.
static_assert(fixed_string<8>{"abc"} == fixed_string<8>{"abc"});
static_assert(!(fixed_string<8>{"abc"} == fixed_string<8>{"abd"}));

// ---- fixed_vector: the ordinary behaviour ------------------------------------------------------
static_assert(fixed_vector<int, 4>{}.empty());
static_assert(fixed_vector<int, 4>::capacity() == 4);

static_assert([] {
    fixed_vector<int, 4> v{};
    v.push_back(10);
    v.push_back(20);
    return v.size() == 2 && v[0] == 10 && v[1] == 20 && !v.empty();
}(), "push_back appends in order");

static_assert([] {
    fixed_vector<int, 4> v{};
    v.push_back(1);
    v.push_back(2);
    v.pop_back();
    v.clear();
    return v.empty() && v.size() == 0;
}(), "pop_back and clear shrink the used part");

static_assert([] {
    fixed_vector<int, 4> v{};
    v.push_back(3);
    v.push_back(4);
    int sum = 0;
    for (int x : v) sum += x;
    return sum == 7;
}(), "iteration covers the used part and nothing beyond it");

// ---- fixed_vector: equality ignores spare room -------------------------------------------------
// This is the property whole-graph equality rests on, so it is asserted directly rather than left
// as a consequence of the implementation.
static_assert([] {
    fixed_vector<int, 8> a{};
    fixed_vector<int, 8> b{};
    a.push_back(1);
    a.push_back(2);
    b.push_back(1);
    b.push_back(2);
    b.push_back(3);
    b.pop_back();      // b touched a third slot; a never did
    return a == b;
}(), "two lists holding the same things are equal regardless of what the tail once held");

static_assert([] {
    fixed_vector<int, 4> a{};
    fixed_vector<int, 4> b{};
    a.push_back(1);
    return !(a == b);
}(), "different lengths are not equal");

// ---- fail() ------------------------------------------------------------------------------------
// Overrunning a fixed_vector, or popping an empty one, calls fail(). Under constant evaluation that
// is a compile error by construction — which is exactly why it cannot be written as a static_assert
// here. The build-failure gates cover that half; this covers the runtime half, and with it the
// property that a message assembled on the spot survives the unwind.
namespace {

bool message_survives_unwind() {
    try {
        fixed_vector<int, 2> v{};
        v.push_back(1);
        v.push_back(2);
        v.push_back(3);
    } catch (const error& e) {
        return e.message() == std::string_view{"heinzel::fixed_vector: push_back past capacity"};
    }
    return false;
}

bool pop_on_empty_is_reported() {
    try {
        fixed_vector<int, 2> v{};
        v.pop_back();
    } catch (const error& e) {
        return e.message() == std::string_view{"heinzel::fixed_vector: pop_back on an empty vector"};
    }
    return false;
}

} // namespace

int main() {
    if (!message_survives_unwind()) return 1;
    if (!pop_on_empty_is_reported()) return 2;
    return 0;
}
