// The loop tree's workspace (`loops/workspace.hpp`) over a corpus planned for zen2 — split marks included — and two
// schedules with a decision edited: where every workspace buffer stands from the arena's first byte, in every copy a
// split band keeps, where each feed's tail ends, the workspace's size and every buffer's storage arithmetic (elements,
// per sample, strides), printed and compared with its listing in `tests/goldens/loops_workspace/`. The listings pin the
// placement — a performance fact (4K aliasing, `Plan/archive/13-loop-tree/03` §9, `Plan/probes/15-zero-overhead/
// placement/`). Then what a fresh workspace holds (every ramp filled, every other byte zero; in an arena inside its
// workspace, only the pasts and the relaid blocks), a buffer's region (its prefix, block and padding), every region
// inside the arena and apart (a guard apart in a `canaried` one), a workspace moved, and a program that keeps nothing.
// A static program's arena of at most 4 KiB lies inside its workspace, packed (`Plan/18-zero-copy/01`).
//
// A listing that changes is printed; `HEINZEL_REPIN=1 test_loops_workspace` rewrites the files, and the change is
// reviewed in `git diff` like any other — never accepted blind.

#include "heinzel/loops/workspace.hpp"

#include "canaried.hpp"
#include "corpus_graphs.hpp"
#include "edited_schedules.hpp"
#include "planned.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using heinzel::Graph;
using heinzel::zen2;
namespace loops = heinzel::loops;

template <const Graph& G>
inline constexpr planned::Held kPlannedHeld{planned::program_of(G, zen2)};
template <const Graph& G>
inline constexpr const loops::Program& kPlanned = kPlannedHeld<G>.program;

inline constexpr Graph kMM128 = corpus::matmul<128, 128, 128>();
inline constexpr Graph kMM512 = corpus::matmul<512, 512, 512>();
inline constexpr Graph kMMOdd = corpus::matmul<100, 75, 130>();
inline constexpr Graph kLinearRelu = corpus::linear_relu();
inline constexpr Graph kBlur256K = corpus::blurxy<32, 2048, 3>();
inline constexpr Graph kBlur8M = corpus::blurxy<1024, 2048, 3>();
inline constexpr Graph kRowsum = corpus::rowsum<1024>();
inline constexpr Graph kRmsnorm = corpus::rmsnorm();
inline constexpr Graph kSoftmax = corpus::softmax();
inline constexpr Graph kElem = corpus::elem();
inline constexpr Graph kMedian = corpus::sliding_median(37, 5);
inline constexpr Graph kOnePole = corpus::one_pole_bank(11, 0.9375, 64);
inline constexpr Graph kLadder = corpus::ladder_bank();
inline constexpr Graph kLagOfALag = corpus::lag_of_a_lag();
inline constexpr Graph kFir = corpus::fir_over_a_feed();
inline constexpr Graph kDelayLine = corpus::delay_line();
inline constexpr Graph kPadded = corpus::padded();
inline constexpr Graph kTall = corpus::tall(true);

// `nest_contract_test`'s `kChunkedRelu`: planned on tiny caches with no operand relaid, the contracted axis in chunks.
inline constexpr planned::Held kChunkedReluHeld{planned::program_of(kTall, edited_schedules::fed_as_it_is(planned::schedule_of(kTall, edited_schedules::kTinyCaches)))};
inline constexpr const loops::Program& kChunkedRelu = kChunkedReluHeld.program;
// `nest_v6_review_test`'s lag of a lag with its lags swapped: a lag that reads state and is itself handed on.
inline constexpr planned::Held kLagsSwappedHeld{planned::program_of(kLagOfALag, edited_schedules::lags_swapped(kLagOfALag, planned::schedule_of(kLagOfALag, zen2)))};
inline constexpr const loops::Program& kLagsSwapped = kLagsSwappedHeld.program;

// `visit.template operator()<P>(name)` for every program of the corpus.
template <class Visit>
void each(const Visit& visit) {
    visit.template operator()<kPlanned<kMM128>>("matmul_128");
    visit.template operator()<kPlanned<kMM512>>("matmul_512");
    visit.template operator()<kPlanned<kMMOdd>>("matmul_100x75x130");
    visit.template operator()<kPlanned<kLinearRelu>>("linear_relu");
    visit.template operator()<kPlanned<kBlur256K>>("blurxy_256k");
    visit.template operator()<kPlanned<kBlur8M>>("blurxy_8m");
    visit.template operator()<kPlanned<kRowsum>>("rowsum_1024");
    visit.template operator()<kPlanned<kRmsnorm>>("rmsnorm");
    visit.template operator()<kPlanned<kSoftmax>>("softmax");
    visit.template operator()<kPlanned<kMedian>>("sliding_median_5");
    visit.template operator()<kPlanned<kOnePole>>("one_pole_bank");
    visit.template operator()<kPlanned<kLadder>>("ladder_bank");
    visit.template operator()<kPlanned<kFir>>("fir_over_a_feed");
    visit.template operator()<kPlanned<kDelayLine>>("delay_line");
    visit.template operator()<kPlanned<kPadded>>("padded");
    visit.template operator()<kChunkedRelu>("chunked_relu");
    visit.template operator()<kPlanned<kElem>>("elem");
    visit.template operator()<kPlanned<kLagOfALag>>("lag_of_a_lag");
    visit.template operator()<kLagsSwapped>("lags_swapped");
}

int g_wrong = 0;

void expect(bool ok, const char* name, const char* what, std::size_t buffer = 0) {
    if (!ok) {
        ++g_wrong;
        std::printf("%s: %s (buffer %zu)\n", name, what, buffer);
    }
}

// The bytes a fresh workspace must hold: zero, but each ramp's element index within a sample, in its first copy.
template <const loops::Program& P>
std::vector<std::byte> fresh_bytes(auto& work) {
    std::vector<std::byte> want(work.size_in_bytes(), std::byte{0});
    for (std::size_t i = 0; i < P.buffers.size(); ++i) {
        const loops::Buffer& b = P.buffers[i];
        const bool filled = b.ramp && b.home == loops::Home::Workspace; // (a constant ramp lies outside the arena)
        const std::int64_t n = filled ? loops::element_count(b, work.sizes()) : 0;
        const std::int64_t per = filled ? loops::elements_per_sample(b, work.sizes()) : 1;
        std::byte* at = want.data() + (work.bytes_of(i) - work.arena());
        for (std::int64_t e = 0; e < n; ++e) {
            const float f = static_cast<float>(e % per);
            const double d = static_cast<double>(e % per);
            b.type == heinzel::ElemType::F32 ? void(std::memcpy(at + e * 4, &f, 4)) : void(std::memcpy(at + e * 8, &d, 8));
        }
    }
    return want;
}

// ...where it holds them: the whole arena, or in an arena inside its workspace, which leaves a block a run writes before
// it reads as it finds it, every past, every tail, every relaid block and every ramp.
template <const loops::Program& P>
bool fresh(auto& work) {
    const std::vector<std::byte> want = fresh_bytes<P>(work);
    if constexpr (!loops::kInlineOf<P>) {
        return std::memcmp(work.arena(), want.data(), work.size_in_bytes()) == 0;
    }
    const auto same = [&](const loops::ByteRange& r) {
        return r.begin == r.end || std::memcmp(r.begin, want.data() + (r.begin - work.arena()), static_cast<std::size_t>(r.end - r.begin)) == 0;
    };
    bool ok = true;
    for (std::size_t i = 0; i < P.buffers.size(); ++i) {
        const loops::Buffer& b = P.buffers[i];
        if (b.home == loops::Home::Workspace) {
            const loops::ByteRange r = work.region(i);
            ok = ok && same(b.ramp || b.blocked_axis >= 0 ? r : loops::ByteRange{r.begin, work.bytes_of(i)});
        }
    }
    for (std::size_t f = 0; f < heinzel::kMaxIO; ++f) {
        ok = ok && same(work.tail(f));
    }
    return ok;
}

// Every region of a workspace: each workspace buffer's, in every copy a split band keeps, and each feed's tail.
template <const loops::Program& P>
std::vector<loops::ByteRange> regions_of(auto& work) {
    std::vector<loops::ByteRange> out;
    for (std::size_t i = 0; i < P.buffers.size(); ++i) {
        const loops::Buffer& b = P.buffers[i];
        const bool split = b.band >= 0 && P.bands[static_cast<std::size_t>(b.band)].parallel;
        for (unsigned c = 0; b.home == loops::Home::Workspace && c < (split ? work.copies() : 1u); ++c) {
            out.push_back(work.region(i, c));
        }
    }
    for (std::size_t f = 0; f < heinzel::kMaxIO; ++f) {
        if (work.tail_end(f) != nullptr) {
            out.push_back(work.tail(f));
        }
    }
    return out;
}

// ...inside the arena and none overlapping; with `guard`, each that many bytes from the one before it and the first
// and the last from the arena's ends.
bool apart(std::vector<loops::ByteRange> regions, std::byte* arena, std::size_t size, std::size_t guard) {
    std::sort(regions.begin(), regions.end(), [](const loops::ByteRange& x, const loops::ByteRange& y) { return x.begin < y.begin; });
    const std::byte* free_from = arena;
    for (const loops::ByteRange& r : regions) {
        if (r.begin < free_from + guard || r.end < r.begin) {
            return false;
        }
        free_from = r.end;
    }
    return regions.empty() || free_from + guard <= arena + size;
}

// The listing of program `P`'s placement with `copies` copies of a split band's buffers: each workspace buffer's
// block in every copy and each feed tail's end, from the arena's first byte. On the way, its fresh bytes and its
// regions are checked, a static program's places against `kPlacementOf`, and a `canaried` workspace's guards.
template <const loops::Program& P, unsigned copies>
std::string placement(const char* name) {
    loops::Sizes sizes{};
    sizes.fill(37); // every symbol a length no lane divides
    loops::Workspace<P, copies> work(sizes);
    std::string out = "copies " + std::to_string(copies) + " size " + std::to_string(work.size_in_bytes()) + "\n";
    for (std::size_t i = 0; i < P.buffers.size(); ++i) {
        const loops::Buffer& b = P.buffers[i];
        if (b.home != loops::Home::Workspace) {
            continue;
        }
        out += "b" + std::to_string(i);
        const bool split = b.band >= 0 && P.bands[static_cast<std::size_t>(b.band)].parallel;
        for (unsigned c = 0; c < (split ? copies : 1u); ++c) {
            out += " " + std::to_string(work.bytes_of(i, c) - work.arena());
            const auto held = static_cast<std::ptrdiff_t>(loops::bytes_of(b.type));
            const loops::ByteRange r = work.region(i, c);
            const std::int64_t block = b.only_past ? 0 : loops::element_count(b, sizes); // (a state in registers: its past alone)
            expect(r.begin == work.bytes_of(i, c) - b.prefix * loops::elements_per_sample(b, sizes) * held && r.end == work.bytes_of(i, c) + block * held,
                   name, "a region is not the prefix, block and padding", i);
            if constexpr (loops::kInlineOf<P>) {
                expect((work.bytes_of(i, c) - work.arena()) % static_cast<std::ptrdiff_t>(loops::kLine) == 0, name, "a packed block is not on a line", i);
            }
            if constexpr (loops::kKindOf<P> == loops::Kind::Static) {
                constexpr auto& placed = loops::kPlacementOf<P, decltype(work)::copies(), 0>;
                const auto k = static_cast<std::size_t>(loops::kPieceOf<P>[i]);
                expect(work.bytes_of(i, c) == work.arena() + placed.at[k] + c * placed.copy_bytes[k], name, "a block is not where kPlacementOf has it", i);
            }
        }
        out += "\n";
    }
    for (std::size_t f = 0; f < heinzel::kMaxIO; ++f) {
        if (work.tail_end(f) != nullptr) {
            out += "t" + std::to_string(f) + " " + std::to_string(work.tail_end(f) - work.arena()) + "\n";
        }
    }
    expect(fresh<P>(work), name, "a fresh workspace holds more than its ramps");
    if constexpr (loops::kInlineOf<P>) {
        const auto* object = reinterpret_cast<const std::byte*>(&work);
        expect(work.arena() >= object && work.arena() + work.size_in_bytes() <= object + sizeof work && work.size_in_bytes() <= loops::kMaxInlineArena,
               name, "an inline arena is not inside its workspace");
    }
    expect(apart(regions_of<P>(work), work.arena(), work.size_in_bytes(), 0), name, "a region overlaps another, or leaves the arena");
    canaried::Workspace<P, copies> guarded(sizes);
    expect(apart(regions_of<P>(guarded), guarded.arena(), guarded.size_in_bytes(), canaried::kGuard), name, "a canaried region is not a guard apart");
    return out;
}

// The storage arithmetic of every buffer, feeds included: how many elements it holds, how many make a sample, and how
// far one step along each axis moves — a relaid buffer's blocks included.
template <const loops::Program& P>
std::string arithmetic() {
    loops::Sizes sizes{};
    sizes.fill(37);
    std::string out;
    for (std::size_t i = 0; i < P.buffers.size(); ++i) {
        const loops::Buffer& b = P.buffers[i];
        out += "b" + std::to_string(i) + " elements " + std::to_string(loops::element_count(b, sizes)) + " per sample " +
               std::to_string(loops::elements_per_sample(b, sizes)) + " strides";
        const auto strides = loops::element_strides(b, sizes);
        for (std::int32_t a = 0; a < b.shape.rank; ++a) {
            out += " " + std::to_string(strides[static_cast<std::size_t>(a)]);
        }
        out += "\n";
    }
    return out;
}

// A move takes the arena and leaves the source empty; a stream program's workspace is empty until it is made.
void moves() {
    static constexpr const loops::Program& P = kPlanned<kElem>;
    loops::Workspace<P> made(loops::Sizes{64});
    loops::Workspace<P> empty;
    expect(empty.arena() == nullptr && empty.size_in_bytes() == 0, "elem", "a stream workspace not made yet holds an arena");
    std::byte* const arena = made.arena();
    const std::size_t size = made.size_in_bytes();
    made.data<float>(static_cast<std::size_t>(P.outputs[0]))[3] = 42.0f;
    loops::Workspace<P> moved(static_cast<loops::Workspace<P>&&>(made));
    expect(moved.arena() == arena && moved.size_in_bytes() == size && made.arena() == nullptr && made.size_in_bytes() == 0, "elem",
           "a moved workspace did not take the arena");
    empty = static_cast<loops::Workspace<P>&&>(moved);
    loops::Workspace<P>& same = empty;
    empty = static_cast<loops::Workspace<P>&&>(same);
    expect(empty.arena() == arena && empty.data<float>(static_cast<std::size_t>(P.outputs[0]))[3] == 42.0f && moved.arena() == nullptr, "elem",
           "a move onto a workspace, or onto itself, lost the arena");
}

// An arena inside its workspace is copied by a move, and the source keeps it; one program each side of the line.
void inline_moves() {
    static constexpr const loops::Program& P = kPlanned<kLagOfALag>;
    static_assert(loops::kInlineOf<P> && !loops::kInlineOf<kPlanned<kMM128>>);
    loops::Workspace<P> made;
    const auto b = static_cast<std::size_t>(P.outputs[0]);
    made.data<float>(b)[3] = 42.0f;
    loops::Workspace<P> moved(static_cast<loops::Workspace<P>&&>(made));
    expect(moved.arena() != made.arena() && moved.data<float>(b)[3] == 42.0f && made.data<float>(b)[3] == 42.0f, "lag_of_a_lag",
           "a move did not copy an inline arena");
    made.data<float>(b)[3] = 7.0f;
    moved = static_cast<loops::Workspace<P>&&>(made);
    expect(moved.data<float>(b)[3] == 7.0f, "lag_of_a_lag", "a move onto an inline workspace did not copy it");
}

// An output `bind` gives the caller's block (`Plan/18-zero-copy/02`): a run writes it there and leaves its own block as
// it was, and `bind` with null gives it back.
void binds() {
    static constexpr const loops::Program& P = kPlanned<kElem>;
    constexpr auto y = static_cast<std::size_t>(P.outputs[0]);
    static_assert(loops::kInPlaceSlotOf<P>[y] == 0, "elem's output may be written in the caller's memory");
    std::vector<float> a(16);
    std::vector<float> b(16);
    std::vector<float> mine(16, -1.0f);
    for (std::size_t i = 0; i < a.size(); ++i) {
        a[i] = 0.5f * static_cast<float>(i), b[i] = 1.0f + static_cast<float>(i % 3);
    }
    loops::Workspace<P> work(loops::Sizes{16});
    std::byte* const own = work.bytes_of(y);
    work.data<float>(y)[0] = 42.0f;
    work.bind(std::integral_constant<std::size_t, y>{}, mine.data());
    loops::run<P>(loops::Feeds{a.data(), b.data()}, work);
    bool ok = work.bytes_of(y) == reinterpret_cast<std::byte*>(mine.data()) && reinterpret_cast<float*>(own)[0] == 42.0f;
    for (std::size_t i = 0; i < a.size(); ++i) {
        ok = ok && mine[i] == a[i] * b[i] + b[i];
    }
    work.bind(std::integral_constant<std::size_t, y>{}, nullptr);
    expect(ok && work.bytes_of(y) == own, "elem", "a bound output is not written in the caller's block, or its own is not back");
}

// A program that keeps nothing (its one buffer a feed, read in no past) has no arena; it runs, plain and canaried.
inline constexpr loops::Program kKeepsNothing = [] {
    loops::Program p{};
    p.buffers.push_back(loops::Buffer{.type = heinzel::ElemType::F32, .shape = heinzel::Shape{4}, .home = loops::Home::Feed, .feed = 0});
    return p;
}();
void keeps_nothing() {
    static_assert(loops::accepted<kKeepsNothing>());
    loops::Workspace<kKeepsNothing> plain;
    canaried::Workspace<kKeepsNothing> guarded(loops::Sizes{});
    const float x[4]{};
    loops::run<kKeepsNothing>(loops::Feeds{x}, plain);
    canaried::run<kKeepsNothing>(loops::Feeds{x}, guarded);
    expect(plain.arena() == nullptr && plain.size_in_bytes() == 0 && guarded.arena() == nullptr, "keeps_nothing", "a program that keeps nothing has an arena");
}

std::string pinned(const std::string& path) {
    std::ifstream in(path);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

} // namespace

int main() {
    const bool repin = std::getenv("HEINZEL_REPIN") != nullptr;
    each([&]<const loops::Program& P>(const char* name) {
        const std::string path = std::string{HEINZEL_LOOPS_WORKSPACE_GOLDENS} + "/" + name + ".txt";
        const std::string got = placement<P, 1>(name) + placement<P, 3>(name) + arithmetic<P>();
        if (repin) {
            std::ofstream(path) << got;
        } else if (got != pinned(path)) {
            ++g_wrong;
            std::printf("%s: the placement differs from %s:\n%s\n", name, path.c_str(), got.c_str());
        }
    });
    moves();
    inline_moves();
    binds();
    keeps_nothing();
    if (g_wrong != 0) {
        std::printf("%d placements differ\n", g_wrong);
        return 1;
    }
    return 0;
}
