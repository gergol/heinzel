// The oracle of kept stages (`docs/knob-math.md`; `loops/run.hpp`, `Workspace::changed`): a kept stage runs only in a
// run where a Param it watches changed, bit for bit, since it last ran; its results stay in their workspace buffers in
// between. Each program as `Executor<…, zen2, false>` gets it (a release build: a checked one keeps no RBJ knob stage,
// whose unproven promise `a0 > 0` is an output of the stage), against the same program with every `kept` cleared, bit
// for bit:
//
//   on `Executor`, in runs of 1, 32 and 256 samples: a Param kept, changed, set back to its old value; 0.0 and -0.0;
//   two NaNs of different payloads; `reset()`; a moved `Executor` (a stream program's arena stolen) and moved back;
//   `prepare()` again with another length; a static program whose arena lies inside it moved, so copied, the source
//   and the copy run on apart, each with its own Params;
//   by hand, Params written as bits, every workspace buffer's bytes after every run (a coefficient's sign, a NaN's
//   payload, which no output need show), canaried; and that a kept stage is skipped where nothing it watches changed
//   (a mark in its buffers survives the run), runs in a fresh workspace though every Param's bits are the zeros its
//   last bits start as, and, each Param changed alone in turn, runs exactly where it watches that Param, the numbers
//   and buffers bit for bit those of the program with none kept (every watched slot read, not only the first and last).
//
// Both hold the runner to itself, which a kept stage's results read wrongly by both programs alike would pass. So each
// graph also runs at `High` against the walk, bit for bit, its Params changed before some runs and kept in the others,
// where the stages after a skipped one read its results through their copies (`loops/walk.hpp` `still_slots`).
//
// Graphs: one RBJ peak, three in series, tour 05's one-pole with `exp`, and a gain worked out as 1 / p in f32 and in
// f64 (the sign of a zero and a NaN's payload reach the output).

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <memory>
#include <numbers>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "heinzel/compile.hpp"
#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/loops/workspace.hpp"

#include "canaried.hpp"
#include "executed.hpp"
#include "planned.hpp"

using namespace heinzel;

namespace {

namespace loops = heinzel::loops;

constexpr Index n{};
constexpr double kRate = 48'000;
constexpr Length kSamples{.multiple_of = 1, .min = 1, .expect = 64};
constexpr std::int64_t kStatic = 32;

inline constexpr const char* kKnob[3][3] = {{"freq0", "gain0", "q0"}, {"freq1", "gain1", "q1"}, {"freq2", "gain2", "q2"}};

// An RBJ peak over `x`, its coefficients from its three knobs (as the knob-math probe wrote it).
constexpr Expr peak(Expr x, const char* const* name) {
    Param freq{name[0], ge(20), le(20000)};
    Param gain{name[1], ge(-24), le(24)};
    Param q{name[2], ge(0.1), le(10)};
    const Expr turns = freq * (2.0 / kRate);
    const Expr c = cospi(turns);
    const Expr s = sinpi(turns);
    const Expr big = exp(gain * (std::numbers::ln10 / 40.0));
    const Expr alpha = s / (2.0 * q);
    const Expr a0 = (1.0 + alpha / big).promise(gt(0));
    const Expr b0 = (1.0 + alpha * big) / a0;
    const Expr b1 = -2.0 * c / a0;
    const Expr b2 = (1.0 - alpha * big) / a0;
    const Expr a1 = -2.0 * c / a0;
    const Expr a2 = (1.0 - alpha / big) / a0;
    Variable y;
    y[n] = b0 * x + b1 * x[n - 1] + b2 * x[n - 2] - a1 * y[n - 1] - a2 * y[n - 2];
    return y;
}

// `Bands` peaks in series, over a block given at run time, or of `kStatic` samples.
template <std::size_t Bands, bool Static>
constexpr Graph peaks() {
    Input x{"x", Static ? Signal{kStatic} : Signal{kSamples}};
    Expr s = x;
    for (std::size_t k = 0; k < Bands; ++k) {
        s = peak(s, kKnob[k]);
    }
    Output y{"y"};
    y = s;
    return y;
}

// Tour 05's one-pole, its coefficient from a cutoff in Hz.
constexpr Graph one_pole() {
    Input x{"x", Signal{kSamples}};
    Param cutoff{"cutoff", ge(20), le(20000)};
    const Expr a = 1.0 - exp(-2.0 * std::numbers::pi / kRate * cutoff);
    Variable y;
    y[n] = y[n - 1] + a * (x - y[n - 1]);
    Output out{"y"};
    out = y;
    return out;
}

// A gain of 1 / p: its zero's sign and a NaN's payload reach the output. In f32 or f64.
template <ElemType E>
constexpr Graph reciprocal() {
    Input x{"x", Signal{kSamples}, E};
    Param p{"p", E, ne(0)};
    const Expr gain = 1.0 / p;
    Output y{"y"};
    y = x * gain;
    return y;
}

constexpr std::size_t kept_stages(const loops::Program& p) {
    std::size_t k = 0;
    for (const loops::Stage& s : p.stages) {
        k += s.kept ? 1 : 0;
    }
    return k;
}

constexpr loops::Program cleared(const loops::Program& p) {
    loops::Program q = p;
    for (loops::Stage& s : q.stages) {
        s.kept = false;
        s.watched = {};
    }
    return q;
}

// A graph's program as `Executor<Build, zen2, false>` runs it, and the same with every `kept` cleared; its Params, each
// one's bits, and a value of each to start from.
template <auto Build>
struct Case {
    static constexpr CompileResult result = try_compile(Build(), zen2, Bindings{}, false);
    static constexpr const loops::Program& kept = result.compiled.program;
    static constexpr planned::Held held{cleared(kept)};
    static constexpr const loops::Program& unkept = held.program;
    using Ex = Executor<Build, zen2, false>;
    using T = std::conditional_t<result.compiled.program.buffers[static_cast<std::size_t>(kept.outputs[0])].type == ElemType::F64, double, float>;
    static constexpr bool kStream = loops::kKindOf<kept> == loops::Kind::Stream;
    static constexpr std::size_t kInputs = result.compiled.graph.inputs.size();
    // The inputs in reading order (`freq0 q0 gain0 x`): x's place, then the Params'.
    static constexpr std::size_t kX = [] {
        std::size_t x = kInputs;
        for (std::size_t i = 0; i < kInputs; ++i) {
            x = result.compiled.graph.inputs[i].name.view() == "x" ? i : x;
        }
        return x;
    }();
    static constexpr auto kParams = [] {
        std::array<std::size_t, kInputs - 1> ports{};
        std::size_t k = 0;
        for (std::size_t i = 0; i < kInputs; ++i) {
            if (i != kX) {
                ports[k++] = i;
            }
        }
        return ports;
    }();

    static constexpr const char* name(std::size_t i) { return result.compiled.graph.inputs[i].name.view().data(); }
    static constexpr bool is_param(std::size_t i) { return result.compiled.graph.values[static_cast<std::size_t>(result.compiled.graph.inputs[i].value)].shape.rank == 0; }
    static constexpr int bytes(std::size_t i) {
        return result.compiled.graph.values[static_cast<std::size_t>(result.compiled.graph.inputs[i].value)].type == ElemType::F64 ? 8 : 4;
    }
    // A knob's value by its name, 1 kHz for a cutoff.
    static constexpr double start(std::size_t i) {
        const std::string_view s = result.compiled.graph.inputs[i].name.view();
        return s.starts_with("freq") ? 250.0 * (s[4] - '0' + 1) : s.starts_with("gain") ? 6.0 - 4.0 * (s[4] - '0') : s.starts_with("q") ? 1.0 : s == "p" ? 0.5 : 1000.0;
    }

    static_assert(result.refusal.view().empty(), "loops_kept_test: a case is refused");
    static_assert(kX < kInputs, "loops_kept_test: a case reads x");
    static_assert(kept_stages(kept) > 0, "loops_kept_test: a case's program has a kept stage, or the oracle compares nothing");
    static_assert(kept_stages(unkept) == 0);
};

using OnePeak = Case<peaks<1, false>>;
using ThreeBands = Case<peaks<3, false>>;
using OnePole = Case<one_pole>;
using Reciprocal = Case<reciprocal<f32>>;
using Reciprocal64 = Case<reciprocal<f64>>;
using StaticPeak = Case<peaks<1, true>>;
static_assert(loops::kInlineOf<StaticPeak::kept>, "loops_kept_test: the static peak's arena lies inside its workspace, so a move copies it");

int g_wrong = 0;

void expect(bool ok, const char* what, const char* name) {
    if (!ok && ++g_wrong <= 20) {
        std::printf("  %s: %s\n", name, what);
    }
}

constexpr std::size_t kNoise = 1 << 13;

std::vector<float> noise() {
    std::vector<float> x(kNoise);
    std::uint32_t state = 7;
    for (float& v : x) {
        state = state * 1664525u + 1013904223u;
        v = static_cast<float>(static_cast<std::int32_t>(state >> 8) % 2001 - 1000) * 0.001f;
    }
    return x;
}

// Where each run reads the noise, in f32 or f64: on along it, from the start again before its end.
struct Reader {
    std::vector<float> x = noise();
    std::vector<double> wide = std::vector<double>(x.begin(), x.end());
    std::size_t at = 0;
    template <class T>
    const T* next(std::int64_t frames) {
        at = at + static_cast<std::size_t>(frames) + 256 > kNoise ? 0 : at;
        const T* p = std::is_same_v<T, float> ? reinterpret_cast<const T*>(x.data() + at) : reinterpret_cast<const T*>(wide.data() + at);
        at += static_cast<std::size_t>(frames);
        return p;
    }
};

// A program run by hand as `Executor` runs it, canaried: its workspace, each Param's bits in a slot of its own.
template <const loops::Program& P>
struct ByHand {
    std::size_t x_port;
    std::unique_ptr<canaried::Workspace<P>> work;
    alignas(8) std::array<std::array<std::byte, 8>, kMaxIO> params{};

    ByHand(std::size_t x, std::int64_t most)
        : x_port(x), work(std::make_unique<canaried::Workspace<P>>(loops::kKindOf<P> == loops::Kind::Stream ? loops::sizes_of_length<P>(most) : loops::Sizes{})) {}
    void set_bits(std::size_t port, int bytes, std::uint64_t bits) {
        const auto low = static_cast<std::uint32_t>(bits);
        bytes == 4 ? (void)std::memcpy(params[port].data(), &low, 4) : (void)std::memcpy(params[port].data(), &bits, 8);
    }
    void set(std::size_t port, int bytes, double v) {
        const auto f = static_cast<float>(v);
        set_bits(port, bytes, bytes == 4 ? std::bit_cast<std::uint32_t>(f) : std::bit_cast<std::uint64_t>(v));
    }
    const std::byte* run(const void* x, std::int64_t frames) {
        loops::Feeds feeds{};
        for (std::size_t i = 0; i < kMaxIO; ++i) {
            feeds[i] = params[i].data();
        }
        feeds[x_port] = x;
        if constexpr (loops::kKindOf<P> == loops::Kind::Stream) {
            work->set_length(frames);
        }
        canaried::run<P>(feeds, *work);
        return work->bytes_of(static_cast<std::size_t>(P.outputs[0]));
    }
};

// The same numbers, bit for bit, `frames` of them.
template <class T>
bool same_numbers(const void* a, const void* b, std::int64_t frames) {
    return std::memcmp(a, b, static_cast<std::size_t>(frames) * sizeof(T)) == 0;
}

// ---- on Executor ----------------------------------------------------------------------------------------------

template <class C, class E>
void set_on(E& ex, std::size_t port, double v) {
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        const auto one = [&]<std::size_t J>(std::integral_constant<std::size_t, J>) {
            if constexpr (C::is_param(J)) {
                if (port == J) {
                    ex.param(C::name(J)) = v;
                }
            }
        };
        (one(std::integral_constant<std::size_t, I>{}), ...);
    }(std::make_index_sequence<C::kInputs>{});
}

// The Executor and the program with no stage kept, the same Params set on both, run over the same samples.
template <class C>
struct Pair {
    std::unique_ptr<typename C::Ex> ex;
    ByHand<C::unkept> ref;

    explicit Pair(std::int64_t most) : ex(std::make_unique<typename C::Ex>()), ref(C::kX, most) {
        if constexpr (C::kStream) {
            ex->prepare(most);
        }
        for (const std::size_t i : C::kParams) {
            set(i, C::start(i));
        }
    }
    void set(std::size_t port, double v) {
        set_on<C>(*ex, port, v);
        ref.set(port, C::bytes(port), v);
    }
    void run(const typename C::T* x, std::int64_t frames, const char* what, const char* name) {
        std::vector<typename C::T> y(static_cast<std::size_t>(frames));
        ex->input("x") = x;
        ex->output("y") = y.data();
        if constexpr (C::kStream) {
            ex->run(frames);
        } else {
            ex->run();
        }
        const std::byte* want = ref.run(x, frames);
        expect(same_numbers<typename C::T>(y.data(), want, frames), what, name);
    }
};

constexpr double kNanA = std::bit_cast<double>(0x7ff8'0002'0000'0000ull); // (payloads apart in an f32's bits too)
constexpr double kNanB = std::bit_cast<double>(0x7ff8'0004'0000'0000ull);

// A stream program: the sequence, then a move, a move back and `prepare` again.
template <class C>
void check_on_executor(const char* name) {
    Reader r{};
    Pair<C> p(256);
    const std::size_t first = C::kParams.front();
    const std::size_t last = C::kParams.back();
    p.run(r.next<typename C::T>(32), 32, "the first run", name);
    p.run(r.next<typename C::T>(32), 32, "nothing changed", name);
    p.set(first, C::start(first) * 1.5);
    p.run(r.next<typename C::T>(1), 1, "a Param changed, a run of 1", name);
    p.run(r.next<typename C::T>(256), 256, "nothing changed, a run of 256", name);
    p.set(first, C::start(first));
    p.run(r.next<typename C::T>(32), 32, "a Param set back to its old value", name);
    const double special[] = {0.0, -0.0, 0.0, kNanA, kNanB, kNanA};
    for (const double v : special) {
        p.set(last, v);
        p.run(r.next<typename C::T>(32), 32, "0.0, -0.0, or a NaN", name);
    }
    p.set(last, C::start(last));
    p.run(r.next<typename C::T>(32), 32, "set back from a NaN", name);
    p.ex->reset();
    p.ref.work->reset();
    p.run(r.next<typename C::T>(32), 32, "after reset", name);
    auto moved = std::make_unique<typename C::Ex>(std::move(*p.ex));
    p.ex.swap(moved);
    p.run(r.next<typename C::T>(256), 256, "moved", name);
    *moved = std::move(*p.ex);
    p.ex.swap(moved);
    p.run(r.next<typename C::T>(32), 32, "moved back", name);
    p.set(first, C::start(first) * 0.75);
    p.run(r.next<typename C::T>(1), 1, "changed after the moves", name);
    p.ex->prepare(64);
    p.ref = ByHand<C::unkept>(C::kX, 64);
    for (const std::size_t i : C::kParams) {
        p.ref.set(i, C::bytes(i), i == first ? C::start(first) * 0.75 : C::start(i));
    }
    p.run(r.next<typename C::T>(64), 64, "prepare again, another length", name);
    p.run(r.next<typename C::T>(17), 17, "nothing changed since prepare", name);
}

// The static peak, whose arena lies inside its Executor: a move copies it, and the source runs on from where it was.
void check_copied() {
    using C = StaticPeak;
    Reader r{};
    Pair<C> a(kStatic);
    ByHand<C::unkept> twin(C::kX, kStatic); // the copy's reference, a's until the move
    for (const std::size_t i : C::kParams) {
        twin.set(i, C::bytes(i), C::start(i));
    }
    for (int k = 0; k < 3; ++k) {
        const float* x = r.next<float>(kStatic);
        a.run(x, kStatic, "before the move", "static peak");
        twin.run(x, kStatic);
    }
    Pair<C> b(kStatic);
    b.ex = std::make_unique<C::Ex>(std::move(*a.ex));
    b.ref = std::move(twin);
    const float* x = r.next<float>(kStatic);
    a.run(x, kStatic, "the source of a move, its arena copied", "static peak");
    b.run(x, kStatic, "the copy", "static peak");
    b.set(C::kParams[0], C::start(C::kParams[0]) * 2.0);
    x = r.next<float>(kStatic);
    a.run(x, kStatic, "the source, the copy's Param changed", "static peak");
    b.run(x, kStatic, "the copy, its Param changed", "static peak");
    a.set(C::kParams[1], 2.0);
    x = r.next<float>(kStatic);
    a.run(x, kStatic, "the source, its own Param changed", "static peak");
    b.run(x, kStatic, "the copy, nothing changed", "static peak");
}

// ---- by hand: the buffers, and the skip itself -----------------------------------------------------------------

// Every workspace buffer of the two programs holds the same bytes.
template <class C>
bool same_buffers(ByHand<C::kept>& k, ByHand<C::unkept>& u) {
    bool same = true;
    for (std::size_t b = 0; b < C::kept.buffers.size(); ++b) {
        if (C::kept.buffers[b].home != loops::Home::Workspace) {
            continue;
        }
        const loops::ByteRange x = k.work->region(b);
        const loops::ByteRange y = u.work->region(b);
        const auto bytes = static_cast<std::size_t>(x.end - x.begin);
        same = same && x.end - x.begin == y.end - y.begin && std::memcmp(x.begin, y.begin, bytes) == 0;
    }
    return same;
}

// Params written as bits: 0, -0, NaNs of two payloads, a signalling one; every buffer compared after every run.
template <class C>
void check_buffers(const char* name) {
    Reader r{};
    ByHand<C::kept> k(C::kX, 256);
    ByHand<C::unkept> u(C::kX, 256);
    const auto set = [&](std::size_t port, std::uint64_t bits) {
        k.set_bits(port, C::bytes(port), bits);
        u.set_bits(port, C::bytes(port), bits);
    };
    const auto both = [&](std::int64_t frames, const char* what) {
        const auto* x = r.next<typename C::T>(frames);
        const std::byte* got = k.run(x, frames);
        const std::byte* want = u.run(x, frames);
        const bool same = same_numbers<typename C::T>(got, want, frames) && same_buffers<C>(k, u);
        expect(same, what, name);
    };
    for (const std::size_t i : C::kParams) {
        k.set(i, C::bytes(i), C::start(i));
        u.set(i, C::bytes(i), C::start(i));
    }
    both(32, "by hand, the first run");
    both(256, "by hand, nothing changed");
    const std::size_t last = C::kParams.back();
    const bool f64 = C::bytes(last) == 8;
    const std::uint64_t bits[] = {0, f64 ? 0x8000'0000'0000'0000ull : 0x8000'0000ull, f64 ? 0x7ff8'0000'0000'0001ull : 0x7fc0'0001ull,
                                  f64 ? 0x7ff8'0000'0000'0002ull : 0x7fc0'0002ull, f64 ? 0x7ff0'0000'0000'0001ull : 0x7f80'0001ull};
    for (const std::uint64_t b : bits) {
        set(last, b);
        both(1, "by hand, 0, -0 or a NaN, as bits");
        both(32, "by hand, the same bits again");
    }
}

// The buffers kept stage `s` stores.
template <const loops::Program& P>
std::vector<std::size_t> stored_by(std::size_t s) {
    std::vector<std::size_t> out{};
    for (std::int32_t j = 0; j < P.stages[s].count; ++j) {
        const loops::Access& a = P.stmts[static_cast<std::size_t>(P.stages[s].first + j)].store;
        if (a.source == loops::Source::Buffer) {
            out.push_back(static_cast<std::size_t>(a.buffer));
        }
    }
    return out;
}

constexpr std::byte kMark{0x5a};

// Marks every kept stage's buffers; after a run, per kept stage, whether its marks are all still there.
template <const loops::Program& P>
struct Marks {
    static void put(canaried::Workspace<P>& w) {
        for (std::size_t s = 0; s < P.stages.size(); ++s) {
            if (!P.stages[s].kept) {
                continue;
            }
            for (const std::size_t b : stored_by<P>(s)) {
                const auto bytes = static_cast<std::size_t>(loops::bytes_of(P.buffers[b].type));
                std::memset(w.bytes_of(b), static_cast<int>(kMark), bytes);
            }
        }
    }
    static bool kept(canaried::Workspace<P>& w, std::size_t s) {
        bool all = true;
        for (const std::size_t b : stored_by<P>(s)) {
            for (std::int32_t i = 0; i < loops::bytes_of(P.buffers[b].type); ++i) {
                all = all && w.bytes_of(b)[i] == kMark;
            }
        }
        return all;
    }
};

// A fresh workspace runs every kept stage though each Param's bits are the zeros its last bits start as; then, nothing
// changed, every kept stage is skipped; each Param changed alone, in turn, exactly the stages that watch it run (seen
// in `k`, marked, whose skipped stages then read marks) and the numbers and buffers stay those of the program with none
// kept (`same`, unmarked, against `u`).
template <class C>
void check_skipped(const char* name) {
    using M = Marks<C::kept>;
    Reader r{};
    ByHand<C::kept> k(C::kX, 256);
    M::put(*k.work);
    k.run(r.next<typename C::T>(32), 32);
    for (std::size_t s = 0; s < C::kept.stages.size(); ++s) {
        expect(!C::kept.stages[s].kept || !M::kept(*k.work, s), "a fresh workspace runs a kept stage, every Param's bits zero", name);
    }
    for (const std::size_t i : C::kParams) {
        k.set(i, C::bytes(i), C::start(i));
    }
    k.run(r.next<typename C::T>(32), 32);
    M::put(*k.work);
    k.run(r.next<typename C::T>(256), 256);
    for (std::size_t s = 0; s < C::kept.stages.size(); ++s) {
        expect(!C::kept.stages[s].kept || M::kept(*k.work, s), "nothing changed: a kept stage is skipped", name);
    }
    ByHand<C::kept> same(C::kX, 256);
    ByHand<C::unkept> u(C::kX, 256);
    for (const std::size_t i : C::kParams) {
        same.set(i, C::bytes(i), C::start(i));
        u.set(i, C::bytes(i), C::start(i));
    }
    const auto* first = r.next<typename C::T>(32);
    same.run(first, 32);
    u.run(first, 32);
    for (const std::size_t moved : C::kParams) {
        const double v = C::start(moved) * 1.25;
        k.set(moved, C::bytes(moved), v);
        same.set(moved, C::bytes(moved), v);
        u.set(moved, C::bytes(moved), v);
        M::put(*k.work);
        const auto* x = r.next<typename C::T>(32);
        k.run(x, 32);
        const std::byte* got = same.run(x, 32);
        const std::byte* want = u.run(x, 32);
        expect(same_numbers<typename C::T>(got, want, 32) && same_buffers<C>(same, u), "a Param changed alone: the numbers and buffers of none kept", name);
        for (std::size_t s = 0; s < C::kept.stages.size(); ++s) {
            const bool watches = ((C::kept.stages[s].watched[moved / 64] >> (moved % 64)) & 1u) != 0;
            expect(!C::kept.stages[s].kept || M::kept(*k.work, s) != watches, "a Param changed alone: exactly the stages watching it run", name);
        }
    }
}

// ---- against the walk ---------------------------------------------------------------------------------------------

constexpr std::int64_t kRuns[] = {32, 32, 1, 256, 17, 32}; // the first Param changed before the third, the last before
constexpr std::size_t kWalked = 370;                         // the fifth

template <class C, auto Build>
void check_against_the_walk(const char* name) {
    using Ex = Executor<testing::at_high<Build>, zen2, false>;
    static_assert(kept_stages(try_compile(testing::at_high<Build>(), zen2, Bindings{}, false).compiled.program) > 0, "loops_kept_test: at High too, a stage is kept");
    const auto value = [](std::size_t port, std::size_t run) {
        return C::start(port) * (port == C::kParams.front() && run >= 2 ? 1.5 : 1.0) * (port == C::kParams.back() && run >= 4 ? 0.75 : 1.0);
    };
    Reader r{};
    const auto* x = r.next<typename C::T>(kWalked);
    std::vector<typename C::T> y(kWalked);
    std::vector<std::vector<double>> fed(C::kInputs, std::vector<double>(x, x + kWalked));
    const auto ex = std::make_unique<Ex>();
    ex->prepare(256);
    std::size_t at = 0;
    for (std::size_t k = 0; k < std::size(kRuns); ++k) {
        for (const std::size_t i : C::kParams) {
            set_on<C>(*ex, i, value(i, k));
            std::fill_n(fed[i].begin() + static_cast<std::ptrdiff_t>(at), kRuns[k], value(i, k));
        }
        ex->input("x") = x + at;
        ex->output("y") = y.data() + at;
        ex->run(kRuns[k]);
        at += static_cast<std::size_t>(kRuns[k]);
    }
    static constexpr testing::Store store = testing::store_of<Build, kWalked>();
    const Graph g = testing::analysed<Build>(kWalked);
    Inputs feeds{};
    for (std::size_t i = 0; i < C::kInputs; ++i) {
        expect(g.inputs[i].name.view() == C::name(i), "the walk's inputs in the program's order", name);
        feeds.push_back(InputFeed{fed[i].data(), 1});
    }
    const auto walk = std::make_unique<Signals<store.samples, store.planes>>();
    run(g, feeds, *walk);
    for (std::size_t t = 0; t < kWalked; ++t) {
        const double want = sample_at(*walk, g.outputs[0].value, 0, t);
        expect(std::bit_cast<std::uint64_t>(static_cast<double>(y[t])) == std::bit_cast<std::uint64_t>(want), "at High, against the walk", name);
    }
}

template <class C>
void check(const char* name) {
    check_on_executor<C>(name);
    check_buffers<C>(name);
    check_skipped<C>(name);
}

} // namespace

int main() {
    check<OnePeak>("one peak");
    check<ThreeBands>("three bands");
    check<OnePole>("one-pole");
    check<Reciprocal>("1 / p");
    check<Reciprocal64>("1 / p, f64");
    check_copied();
    check_against_the_walk<OnePeak, peaks<1, false>>("one peak");
    check_against_the_walk<ThreeBands, peaks<3, false>>("three bands");
    check_against_the_walk<OnePole, one_pole>("one-pole");
    check_against_the_walk<Reciprocal, reciprocal<f32>>("1 / p");
    check_against_the_walk<Reciprocal64, reciprocal<f64>>("1 / p, f64");
    if (g_wrong != 0) {
        std::printf("loops_kept_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("loops_kept_test: each kept program bit for bit the program with none kept, on Executor and by hand, its buffers too; a "
                "kept stage skipped where nothing it watches changed; at High, bit for bit the walk\n");
    return 0;
}
