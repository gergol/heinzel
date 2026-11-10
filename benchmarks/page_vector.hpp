#pragma once

// Where a bench buffer lies on its page (Plan/30, Plan/probes/page-buffers). A std::vector lies where the process's
// earlier allocations left room (glibc's dynamic mmap threshold): echo375 moved by 15 % with the rows run before it.
// A buffer an arm only reads starts at offset 0 of a page; the buffers it writes start at half a page past one, each in
// a slot of its own 320 B further on, as Heinzel's workspace places its blocks (`loops/layout.hpp`'s `place`). A loop
// reading one buffer L samples behind its write into another stalls when (L + d) mod 1024 is about 2-48 (to 80 in a
// vector loop with many stores in flight), d the written buffer's offset past the read one in floats: half a page
// (d = 512) was slow on no probed row, and a slot each keeps two written buffers off one L1 set and off d = 0 (harris's
// hand arms 1.2x, blurxy's 1.09x with all at 2048).
// 16 KiB is a page on the M1 Pro and a 4 KiB page's offset 0 on zen2.

#include <cstddef>
#include <new>
#include <vector>

namespace heinzel_bench {

inline constexpr std::size_t kPageBytes = 16384;
inline constexpr std::size_t kOutputOffset = 2048;
inline constexpr std::size_t kSlotBytes = 320;  // Heinzel's kStagger: cache-line aligned, never a page apart
inline constexpr std::size_t kSlots = 6;        // slot 6 would reach 3968, 128 B before the next page's inputs

template <class T, std::size_t Offset>
struct PageAllocator {
    using value_type = T;
    template <class U>
    struct rebind {
        using other = PageAllocator<U, Offset>;
    };

    PageAllocator() = default;
    template <class U>
    constexpr PageAllocator(const PageAllocator<U, Offset>&) noexcept {}

    T* allocate(std::size_t n) {
        auto* page = static_cast<std::byte*>(::operator new(n * sizeof(T) + Offset, std::align_val_t{kPageBytes}));
        return reinterpret_cast<T*>(page + Offset);
    }
    void deallocate(T* p, std::size_t) noexcept {
        ::operator delete(reinterpret_cast<std::byte*>(p) - Offset, std::align_val_t{kPageBytes});
    }

    template <class U>
    bool operator==(const PageAllocator<U, Offset>&) const noexcept {
        return true;
    }
};

// What an arm only reads.
template <class T>
using PageVector = std::vector<T, PageAllocator<T, 0>>;

// What an arm writes: its outputs, its scratch, an intermediate a later pass reads, its own delay lines. Its output
// takes slot 0 and the others follow, 1, 2, … in the order it writes them; past six they share a slot. Below what it
// reads, a loop that loads at or ahead of the index it stores never meets a store still in flight: with the output
// last, 80 floats past a row it read, blurxy/hand-fused ran 1.15x at H32 and mlp2/hand 1.06x.
template <class T, std::size_t Slot = 0>
using OutputVector = std::vector<T, PageAllocator<T, kOutputOffset + Slot % kSlots * kSlotBytes>>;

} // namespace heinzel_bench
