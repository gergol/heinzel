#pragma once

// Cycles a piece of code takes, for the probes that count them (`latency.cpp`, `memory.cpp`). On Linux the processor's
// own count (perf_event_open, user space; `kernel.perf_event_paranoid` 2 or less), so the clock's boost does not enter.
// Where there is none — macOS lets no user program read the counter, a Linux may refuse it (Ubuntu's default is 4),
// or built with `-DCYCLES_CALIBRATED` — nanoseconds times the cycles a nanosecond holds, measured just before on a
// chain of dependent integer adds: one cycle each on zen2 and on the M1 alike.

#if defined(__linux__) && !defined(CYCLES_CALIBRATED)
#define CYCLES_PERF 1
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace tune {

template <class F>
double nanoseconds(F& f) {
    const auto t0 = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
}

// Cycles a nanosecond, now: 2^24 dependent integer adds, eight to an iteration (the loop's own count runs beside them),
// the least of five runs. The sum is the lambda's own: captured, it would live in memory, and each add would wait on a
// store and a load.
inline double cycles_a_ns() {
    constexpr long kAdds = 1L << 24;
    double best = 1e300;
    for (int r = 0; r < 5; ++r) {
        auto chain = [] {
            std::uint64_t x = 0;
            for (long i = 0; i < kAdds / 8; ++i) {
                x += 1; asm volatile("" : "+r"(x));
                x += 1; asm volatile("" : "+r"(x));
                x += 1; asm volatile("" : "+r"(x));
                x += 1; asm volatile("" : "+r"(x));
                x += 1; asm volatile("" : "+r"(x));
                x += 1; asm volatile("" : "+r"(x));
                x += 1; asm volatile("" : "+r"(x));
                x += 1; asm volatile("" : "+r"(x));
            }
        };
        best = std::min(best, nanoseconds(chain));
    }
    return double(kAdds) / best;
}

class Cycles {
public:
    Cycles() {
#if defined(CYCLES_PERF)
        perf_event_attr a{};
        a.type = PERF_TYPE_HARDWARE, a.size = sizeof a, a.config = PERF_COUNT_HW_CPU_CYCLES;
        a.disabled = 1, a.exclude_kernel = 1, a.exclude_hv = 1;
        fd_ = static_cast<int>(syscall(SYS_perf_event_open, &a, 0, -1, -1, 0));
#endif
    }

    // The processor's count (else the calibrated clock).
    bool counted() const { return fd_ >= 0; }

    // The fewest cycles of `runs` runs of `f`: noise only ever adds cycles.
    template <class F>
    double least(int runs, F&& f) {
        double best = 1e300;
#if defined(CYCLES_PERF)
        if (counted()) {
            for (int r = 0; r < runs; ++r) {
                ioctl(fd_, PERF_EVENT_IOC_RESET, 0), ioctl(fd_, PERF_EVENT_IOC_ENABLE, 0);
                f();
                ioctl(fd_, PERF_EVENT_IOC_DISABLE, 0);
                long long n = 0;
                if (read(fd_, &n, sizeof n) != sizeof n) {
                    return -1;
                }
                best = std::min(best, double(n));
            }
            return best;
        }
#endif
        const double rate = cycles_a_ns();
        for (int r = 0; r < runs; ++r) {
            best = std::min(best, nanoseconds(f));
        }
        return best * rate;
    }

private:
    int fd_ = -1;
};

} // namespace tune
