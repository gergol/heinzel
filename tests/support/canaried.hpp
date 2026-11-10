#pragma once

// A run of the loop-tree runner with what a test's own numbers do not see checked after it — `both_runners`' checks
// that need only one runner (`Plan/archive/13-loop-tree/07` §6): a canary in every byte of the workspace outside the buffers'
// regions (a buffer's prefix, block and relaid padding, in every copy a split band keeps) and outside the feeds'
// tails. The workspace keeps `kGuard` free bytes before every region and after the last, so a write just past a region
// lands on a canary. It is one allocation, so a sanitizer sees none of this. A run that writes where it may not ends
// the test there, the tree printed. Every block a run writes before it reads (all but a ramp's and a relaid buffer's,
// whose padding is read) starts as NaN, so a run that reads one first shows in its numbers: an arena inside its
// workspace leaves those bytes as it finds them (`loops/workspace.hpp`). Before any of it, the program is
// held to what the runner accepts (`well_formed.hpp`). A call site changes two names: `loops::Workspace<P>` →
// `canaried::Workspace<P>` (one with guards, made the same way), `loops::run<P>` → `canaried::run<P>`.

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/loops/print.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/loops/workspace.hpp"

#include "well_formed.hpp"

namespace canaried {

namespace loops = heinzel::loops;

inline constexpr std::byte kCanary{0xa5};
inline constexpr std::byte kUnwritten{0xff}; // (0xff… is a NaN in float and in double)
inline constexpr std::size_t kGuard = 64;

template <const loops::Program& P, unsigned Copies = 1>
class Workspace : public loops::GuardedWorkspace<P, Copies, kGuard> {
    using Base = loops::GuardedWorkspace<P, Copies, kGuard>;

public:
    explicit Workspace(const loops::Sizes& sizes) : Base(sizes), kept_(this->size_in_bytes(), false) {
        for (std::size_t b = 0; b < P.buffers.size(); ++b) {
            const loops::Buffer& buffer = P.buffers[b];
            const bool split = buffer.band >= 0 && P.bands[static_cast<std::size_t>(buffer.band)].parallel;
            for (unsigned c = 0; buffer.home == loops::Home::Workspace && c < (split ? Base::copies() : 1u); ++c) {
                const loops::ByteRange r = this->region(b, c);
                keep(r);
                if (!buffer.ramp && buffer.blocked_axis < 0) {
                    std::fill(this->bytes_of(b, c), r.end, kUnwritten);
                }
            }
        }
        for (std::size_t f = 0; f < heinzel::kMaxIO; ++f) {
            keep(this->tail(f));
        }
        for (std::size_t i = 0; i < kept_.size(); ++i) {
            this->arena()[i] = kept_[i] ? this->arena()[i] : kCanary;
        }
    }

    // After a run: every canary in place — or the test ends here.
    void check() {
        const auto stop = [&](const char* what, std::size_t at) {
            std::printf("canaried: loops::run %s (byte %zu)\n%s", what, at, loops::print(P).c_str());
            std::exit(1);
        };
        for (std::size_t i = 0; i < kept_.size(); ++i) {
            if (!kept_[i] && this->arena()[i] != kCanary) {
                stop("wrote outside every buffer and the tails", i);
            }
        }
    }

private:
    void keep(loops::ByteRange r) {
        for (std::byte* at = r.begin; at < r.end; ++at) {
            kept_[static_cast<std::size_t>(at - this->arena())] = true;
        }
    }

    std::vector<bool> kept_;
};

// `loops::run`, then the checks. The workspace is made for `P`, so a run of another program does not compile.
template <const loops::Program& P, unsigned Copies, class... Workers>
void run(const loops::Feeds& feeds, Workspace<P, Copies>& work, Workers&... workers) {
    static_assert(loops::accepted<P>());
    loops::run<P>(feeds, work, workers...);
    work.check();
}

} // namespace canaried
