#pragma once

// `arm.cpp`'s counting build (-DCOUNT): user-mode instructions in process, the heap a construction makes, and the
// first run's page faults. The global allocation functions are replaced so that the heap is counted and the arena
// (the one allocation aligned to a page) can be touched page by page before the first run.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <optional>

#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {

std::size_t g_heap = 0; // bytes ever allocated

struct Block {
    void* at;
    std::size_t bytes;
};
std::array<Block, 8> g_aligned{}; // the live allocations aligned to a page: the arena

[[noreturn]] void skip(const char* why) {
    std::printf("SKIP %s\n", why);
    std::exit(77);
}

// `instructions:u` of one call of `f`: a refused counter, a zero count or a multiplexed one exits 77.
struct Counter {
    int fd = -1;

    Counter() {
        perf_event_attr a{};
        a.type = PERF_TYPE_HARDWARE;
        a.size = sizeof a;
        a.config = PERF_COUNT_HW_INSTRUCTIONS;
        a.disabled = 1;
        a.exclude_kernel = 1;
        a.exclude_hv = 1;
        a.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
        fd = static_cast<int>(syscall(SYS_perf_event_open, &a, 0, -1, -1, 0));
        if (fd < 0) {
            skip("perf_event_open refused (kernel.perf_event_paranoid > 2?)");
        }
    }
    template <class F>
    std::uint64_t operator()(F f) {
        ioctl(fd, PERF_EVENT_IOC_RESET, 0);
        ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
        f();
        ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
        std::uint64_t v[3]{}; // the count, the time enabled, the time running
        if (read(fd, v, sizeof v) != static_cast<ssize_t>(sizeof v) || v[0] == 0) {
            skip("a count read zero");
        }
        if (v[2] < v[1]) {
            skip("the counter was multiplexed");
        }
        return v[0];
    }
};

[[gnu::noinline]] void make(std::optional<Arm>& slot, std::int64_t frames) { slot.emplace(frames); }

// I: after three warm-up runs, (min₇ count(2R) − min₇ count(R)) / R, so the counting's own cost cancels. C: a second
// one made (the first paid for lazy binding and the heap's first use). M: its object and the heap it made.
int count() {
    constexpr int kR = 8;
    Counter counter;
    Buffers buffers;
    const std::int64_t frames = launder(w::kFrames);
    std::optional<Arm> slot;
    make(slot, frames);
    slot.reset();
    const std::size_t heap = g_heap;
    const std::uint64_t construction = counter([&] { make(slot, frames); });
    const std::size_t made = g_heap - heap;
    Arm& arm = *slot;
    arm.bind(buffers);
    for (int r = 0; r < 3; ++r) {
        arm.run(launder(frames));
    }
    const auto runs = [&](int times) {
        return counter([&] {
            for (int r = 0; r < times; ++r) {
                arm.run(launder(frames));
            }
        });
    };
    std::uint64_t once = UINT64_MAX;
    std::uint64_t twice = UINT64_MAX;
    for (int k = 0; k < 7; ++k) {
        once = std::min(once, runs(kR));
        twice = std::min(twice, runs(2 * kR));
    }
    const double per_run = (static_cast<double>(twice) - static_cast<double>(once)) / kR;
    std::printf("I %.1f C %llu M %zu+%zu K %zu Z %d\n", per_run, static_cast<unsigned long long>(construction), Arm::kObject, made, arm.kept(), kZeroCopy ? 1 : 0);
    return 0;
}

[[gnu::noinline]] void touch_stack() {
    volatile std::byte stack[64 * 1024];
    for (std::size_t i = 0; i < sizeof stack; i += 1024) {
        stack[i] = std::byte{0};
    }
}

// Reads and writes back one byte of every page `b` spans: its first byte, then each page boundary inside it.
void touch(const Block& b) {
    std::byte* const end = static_cast<std::byte*>(b.at) + b.bytes;
    for (auto* at = static_cast<std::byte*>(b.at); at < end; at = reinterpret_cast<std::byte*>((reinterpret_cast<std::uintptr_t>(at) / 4096 + 1) * 4096)) {
        volatile std::byte* page = at;
        *page = *page;
    }
}

// F: the first run's minor faults, the arena as made or touched first. The code and the stack fault alike either way,
// so the difference is the arena's share. A: the arena's bytes, found (and, when `touched`, touched): with none found
// while the program keeps bytes, F says nothing.
int faults(bool touched) {
    Buffers buffers;
    Arm arm(launder(w::kFrames));
    arm.bind(buffers);
    std::size_t arena = 0;
    for (const Block& b : g_aligned) {
        arena += b.bytes;
        if (touched) {
            touch(b);
        }
    }
    touch_stack();
    rusage before{};
    rusage after{};
    getrusage(RUSAGE_SELF, &before);
    arm.run(launder(w::kFrames));
    getrusage(RUSAGE_SELF, &after);
    std::printf("F %ld A %zu\n", after.ru_minflt - before.ru_minflt, arena);
    return 0;
}

} // namespace

void* operator new(std::size_t bytes) {
    g_heap += bytes;
    if (void* p = std::malloc(bytes != 0 ? bytes : 1)) {
        return p;
    }
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

void* operator new(std::size_t bytes, std::align_val_t align) {
    g_heap += bytes;
    const auto a = static_cast<std::size_t>(align);
    void* p = std::aligned_alloc(a, (bytes + a - 1) / a * a);
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    for (Block& b : g_aligned) {
        if (a >= 4096 && b.at == nullptr) {
            b = Block{p, bytes};
            break;
        }
    }
    return p;
}
void operator delete(void* p, std::align_val_t) noexcept {
    for (Block& b : g_aligned) {
        b = b.at == p ? Block{} : b;
    }
    std::free(p);
}
void operator delete(void* p, std::size_t, std::align_val_t a) noexcept { operator delete(p, a); }
