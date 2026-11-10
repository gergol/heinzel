// One workload in one arm of `Plan/15-zero-overhead/01`, built by `measure.sh`: -DWORKLOAD=w1 … w12 (`hand.hpp`, `workloads.hpp`),
// -DARM=HAND | BARE | BARE_COPY | EXECUTOR, and one of
//   (neither)  the size build: make, bind and run; its text + data + bss is B;
//   -DCOUNT    prints I (instructions per run), C (to make a second one), M (the object and the heap it made), K
//              (the bytes the program keeps, from a bare arm) and Z (1: every output is written in place); given `faults` or `touched`, F instead: the first run's
//              minor page faults, the arena as made or with each of its pages touched first;
//   -DCHECK    the arm against the bare arm over three runs: every output bit for bit, a reordered hand fold within
//              its bound.
// Each run sits in a noinline function, and every pointer and length it is given is laundered, so nothing folds
// against a known global or a literal length.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

#include "heinzel/compile.hpp"
#include "heinzel/executor.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/loops/streams.hpp"
#include "heinzel/loops/workspace.hpp"

#include "workloads.hpp"
#include "harness.hpp"

#if !defined(ARM)
#error "arm.cpp: -DARM=HAND, BARE, BARE_COPY or EXECUTOR"
#endif
#define HAND 1
#define BARE 2
#define BARE_COPY 3
#define EXECUTOR 4

namespace {

namespace loops = heinzel::loops;

#if ARM != HAND || defined(CHECK)
} // namespace

// The program the `Executor` runs, named as a subobject (`CLAUDE.md`). Outside the anonymous namespace, so that it has
// the linkage the `Executor`'s compile result has. `-DINTERNAL_LINKAGE` gives it internal linkage instead: the runner's
// code must not depend on it (`Plan/15-zero-overhead/03`, the runner on constants). Release's program in every build:
// with asserts on, what an `Executor` adds to it is its checks (Plan/26-value-ranges/04).
#if defined(INTERNAL_LINKAGE)
namespace {
#endif
namespace zo_arm {
struct Held {
    heinzel::loops::Program program;
};
inline constexpr Held kHeld{heinzel::compile(zo::WORKLOAD::build(), heinzel::default_machine, {}, false).program};
} // namespace zo_arm
#if defined(INTERNAL_LINKAGE)
} // namespace
#endif

namespace {

constexpr const loops::Program& kP = zo_arm::kHeld.program;
constexpr loops::StreamSymbol kSymbol = loops::stream_symbol(kP);

// A value's elements: its literal extents, and its symbol (W2's `Length`) at `kFrames`.
consteval std::size_t elements(const heinzel::Shape& s) {
    std::size_t count = 1;
    for (std::int32_t k = 0; k < s.rank; ++k) {
        const heinzel::Extent e = heinzel::extent_at(s, k);
        count *= static_cast<std::size_t>(heinzel::is_symbol(e) ? w::kFrames : heinzel::literal_of(e));
    }
    return count;
}

consteval bool ports_in_graph_order() {
    heinzel::Graph g = w::build();
    bool same = heinzel::infer(g).ok && g.inputs.size() == kIns && g.outputs.size() == kOuts;
    for (std::size_t i = 0; same && i < kIns; ++i) {
        same = g.inputs[i].name.view() == w::kIn[i].name &&
               elements(g.values[static_cast<std::size_t>(g.inputs[i].value)].shape) == w::kIn[i].floats;
    }
    for (std::size_t o = 0; same && o < kOuts; ++o) {
        same = g.outputs[o].name.view() == w::kOut[o].name &&
               elements(g.values[static_cast<std::size_t>(g.outputs[o].value)].shape) == w::kOut[o].floats;
    }
    return same;
}
static_assert(ports_in_graph_order(), "zero_overhead: a workload lists its ports in its graph's order, each of its size");

// `loops::run<P>` over a workspace made once; with `Copy`, each output then copied out as the `Executor` copies it.
// (`P` is `kP`, named as a parameter so that a member only a stream program has is looked up per program.)
template <bool Copy, const loops::Program& P = kP>
struct Bare {
    loops::Workspace<P> work;
    loops::Feeds feeds{};
    std::array<float*, kOuts> sinks{};
    static constexpr std::size_t kObject = sizeof(loops::Workspace<P>);

    explicit Bare(std::int64_t frames) : work(sizes(frames)) {}
    static loops::Sizes sizes(std::int64_t frames) {
        loops::Sizes s{};
        if constexpr (kSymbol.id >= 0) {
            s[static_cast<std::size_t>(kSymbol.id)] = frames;
        }
        return s;
    }
    void bind(const Buffers& b) {
        const std::array<const float*, kIns> in = b.ins();
        std::copy(in.begin(), in.end(), feeds.begin());
        sinks = b.outs();
    }
    [[gnu::noinline]] void run(std::int64_t frames) {
        if constexpr (loops::kKindOf<P> == loops::Kind::Stream) {
            work.set_length(frames);
        }
        loops::run<kP>(feeds, work);
        if constexpr (Copy) {
            for (std::size_t o = 0; o < kP.outputs.size(); ++o) {
                const auto b = static_cast<std::size_t>(kP.outputs[o]);
                const auto bytes = loops::element_count(kP.buffers[b], work.sizes()) * loops::bytes_of(kP.buffers[b].type);
                std::memcpy(sinks[o], work.bytes_of(b), static_cast<std::size_t>(bytes));
            }
        }
    }
    const float* result(std::size_t o) { return Copy ? sinks[o] : work.template data<float>(static_cast<std::size_t>(kP.outputs[o])); }
    // What the program keeps: each workspace buffer's prefix, block and padding, and the feeds' tails.
    std::size_t kept() {
        std::size_t bytes = 0;
        for (std::size_t b = 0; b < kP.buffers.size(); ++b) {
            const loops::ByteRange r = kP.buffers[b].home == loops::Home::Workspace ? work.region(b) : loops::ByteRange{};
            bytes += static_cast<std::size_t>(r.end - r.begin);
        }
        for (std::size_t f = 0; f < heinzel::kMaxIO; ++f) {
            bytes += static_cast<std::size_t>(work.tail(f).end - work.tail(f).begin);
        }
        return bytes;
    }
};
#endif

#if ARM == HAND
using Arm = HandArm<w::Hand>;
#elif ARM == BARE
using Arm = Bare<false>;
#elif ARM == BARE_COPY
using Arm = Bare<true>;
#elif ARM == EXECUTOR
using Ex = heinzel::Executor<w::build>;

template <std::size_t I>
void bind_input(Ex& ex, const float* data) {
    if constexpr (w::kIn[I].param) {
        ex.param(w::kIn[I].name) = *data;
    } else {
        ex.input(w::kIn[I].name) = data;
    }
}

struct Arm {
    Ex ex;
    std::array<float*, kOuts> out{};
    static constexpr std::size_t kObject = sizeof(Ex);

    explicit Arm(std::int64_t frames) {
        if constexpr (Ex::has_length) {
            ex.prepare(frames);
        }
    }
    void bind(const Buffers& b) {
        out = b.outs();
        [&]<std::size_t... I, std::size_t... O>(std::index_sequence<I...>, std::index_sequence<O...>) {
            const std::array<const float*, kIns> in = b.ins();
            (bind_input<I>(ex, in[I]), ...);
            ((ex.output(w::kOut[O].name) = out[O]), ...);
        }(std::make_index_sequence<kIns>{}, std::make_index_sequence<kOuts>{});
    }
    [[gnu::noinline]] void run(std::int64_t frames) {
        if constexpr (Ex::has_length) {
            ex.run(frames);
        } else {
            ex.run();
        }
    }
    const float* result(std::size_t o) { return out[o]; }
    std::size_t kept() { return 0; }
};
#endif

} // namespace

// Is every output written in the caller's memory (`Plan/18-zero-copy/02`)? Then the run-time gate holds the `Executor`
// to the bare arm, not to bare + copy, and `count.hpp` says so.
#if ARM != HAND || defined(CHECK)
constexpr bool kZeroCopy = [] {
    bool all = true;
    for (std::size_t o = 0; o < kP.outputs.size(); ++o) {
        all = all && loops::kInPlaceSlotOf<kP>[static_cast<std::size_t>(kP.outputs[o])] >= 0;
        for (std::size_t e = 0; e < o; ++e) {
            all = all && kP.outputs[e] != kP.outputs[o];
        }
    }
    return all;
}();
#else
constexpr bool kZeroCopy = false;
#endif

#if defined(COUNT)
#include "count.hpp"
#endif

int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
#if defined(COUNT)
    return argc > 1 ? faults(launder(argv[1][0] == 't')) : count();
#elif defined(CHECK) && ARM == HAND // a reordered hand fold within its bound
    return check<Arm, Bare<false>>([](zo::In in, std::size_t e, float) { return w::hand_bound(in, e); });
#elif defined(CHECK)
    return check<Arm, Bare<false>>([](zo::In, std::size_t, float) { return 0.0; });
#else
    Buffers buffers;
    Arm arm(launder(w::kFrames));
    arm.bind(buffers);
    for (int r = 0; r < 4; ++r) {
        arm.run(launder(w::kFrames));
    }
    // An output read, so that no arm's run is dead: a bare arm's output lies in its workspace, and with the arena inside
    // the object nothing else would read it (`Plan/18-zero-copy/02`: its run went away, and its B with it).
    return launder(arm.result(0))[0] == 12345.0f ? 1 : 0;
#endif
}
