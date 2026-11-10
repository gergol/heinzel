#pragma once

// `well_formed`'s rules for a kept stage, one that runs only in a run where a Param it watches changed
// (`docs/knob-math.md`), its results kept in their buffers in between: it has no axes and stands in no band; it reads
// only constants, Params (feeds of no axes) and what earlier stages that could be kept store; it stores only workspace
// buffers no output names and no other stage stores, so that what it keeps is still there; and it watches exactly the
// Params it reads and those its makers depend on. A stage not kept watches nothing.

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>
#include <vector>

#include "heinzel/loops/program.hpp"

#include "well_formed_walks.hpp"

namespace heinzel::loops::detail {

using Watch = std::array<std::uint64_t, kWatchWords>;

// Per buffer, the first stage that stores it and whether another stage does too; per stage passed, whether it could be
// kept and the Params it depends on, its own and its makers'.
struct Keeping {
    std::vector<std::int32_t> maker{};
    std::vector<char> shared{};
    std::vector<char> could{};
    std::vector<Watch> depends{};
};

constexpr Keeping keeping_of(const Program& p) {
    Keeping k{.maker = std::vector<std::int32_t>(p.buffers.size(), -1), .shared = std::vector<char>(p.buffers.size(), 0),
              .could = std::vector<char>(p.stages.size(), 0), .depends = std::vector<Watch>(p.stages.size())};
    for (std::size_t s = 0; s < p.stages.size(); ++s) {
        for (std::int32_t j = 0; j < p.stages[s].count; ++j) {
            const Access& store = stmt_of(p, s, j).store;
            if (store.source != Source::Buffer) {
                continue;
            }
            std::int32_t& maker = k.maker[static_cast<std::size_t>(store.buffer)];
            if (maker >= 0 && maker != static_cast<std::int32_t>(s)) {
                k.shared[static_cast<std::size_t>(store.buffer)] = 1;
            }
            maker = maker < 0 ? static_cast<std::int32_t>(s) : maker;
        }
    }
    return k;
}

constexpr bool is_an_output(const Program& p, std::int32_t buffer) {
    bool named = false;
    for (const std::int32_t out : p.outputs) {
        named = named || out == buffer;
    }
    return named;
}

// Why stage `s` could not be kept ("": it could); the Params it depends on into `k.depends[s]`.
constexpr std::string_view why_not_kept(const Program& p, std::size_t s, Keeping& k) {
    if (!p.stages[s].axes.empty() || band_of(p, s) >= 0) {
        return "heinzel::loops: a kept stage has no axes and stands in no band";
    }
    for (std::int32_t j = 0; j < p.stages[s].count; ++j) {
        for (std::size_t slot = 0; slot <= kMaxNodeInputs; ++slot) {
            const Access& a = loops::access_in(stmt_of(p, s, j), slot);
            if (a.source != Source::Buffer) {
                continue;
            }
            const Buffer& b = p.buffers[static_cast<std::size_t>(a.buffer)];
            if (slot == kMaxNodeInputs && (b.home != Home::Workspace || is_an_output(p, a.buffer) || k.shared[static_cast<std::size_t>(a.buffer)] != 0)) {
                return "heinzel::loops: a kept stage stores only workspace buffers that are no output and no other stage stores";
            }
            const std::int32_t from = k.maker[static_cast<std::size_t>(a.buffer)];
            const bool param = b.home == Home::Feed && b.shape.rank == 0 && b.feed >= 0 && static_cast<std::size_t>(b.feed) < 64 * kWatchWords;
            const bool made = b.home == Home::Workspace && from >= 0 && static_cast<std::size_t>(from) < s && k.could[static_cast<std::size_t>(from)] != 0;
            if (slot < kMaxNodeInputs && b.home != Home::Constant && !param && !made) {
                return "heinzel::loops: a kept stage reads only constants, Params, and what earlier stages that could be kept store";
            }
            Watch& depends = k.depends[s];
            if (slot < kMaxNodeInputs && param) {
                depends[static_cast<std::size_t>(b.feed) / 64] |= std::uint64_t{1} << (static_cast<std::size_t>(b.feed) % 64);
            }
            for (std::size_t w = 0; slot < kMaxNodeInputs && made && w < kWatchWords; ++w) {
                depends[w] |= k.depends[static_cast<std::size_t>(from)][w];
            }
        }
    }
    return "";
}

// Every kept stage could be kept, and watches exactly what it depends on; every other stage watches nothing.
constexpr Checked check_kept(const Program& p) {
    Keeping k = keeping_of(p);
    for (std::size_t s = 0; s < p.stages.size(); ++s) {
        const std::string_view why = why_not_kept(p, s, k);
        k.could[s] = why.empty() ? 1 : 0;
        if (p.stages[s].kept && !why.empty()) {
            return std::unexpected(why);
        }
        const Watch watches = p.stages[s].kept ? k.depends[s] : Watch{};
        if (p.stages[s].watched != watches) {
            return std::unexpected("heinzel::loops: a kept stage watches exactly the Params it and its makers read, a stage not kept none");
        }
    }
    return {};
}

} // namespace heinzel::loops::detail
