#pragma once

// A fixed set of threads a caller creates once and hands to every run that may split its work among them
// (`loops::run(feeds, work, pool)`, `loops/run.hpp`). No global, no lazy start: the caller owns it and says
// how big it is — the machine's cores, not its logical CPUs, since two runners on one core slow each other
// and a waiting worker spins on its core. Inert unless `HEINZEL_HAVE_THREADS` is defined (CMake's option of
// that name), so a build without threads references none; nothing in the compiler or the runner includes it.

#if defined(HEINZEL_HAVE_THREADS)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace heinzel {

// How a dispatch stays cheap. Each worker waits on a word of its own, so a dispatch wakes exactly the
// workers it uses. After its task a worker spins on that word for `kSpin` by the clock, and only then parks
// on it (a futex, through `std::atomic::wait`): a spinning worker answers in well under a microsecond, a
// parked one costs a system call and its core's wake-up — measured on zen2, 0.6–1.3 µs per dispatch to 2–16
// spinning workers, 8–28 µs to parked ones, 12–45 µs for a mutex and a condition variable. The caller waits
// for the last worker the same way. The spin reads plainly: a pause instruction would need an
// instruction-set macro.
class ThreadPool {
public:
    // `threads` runners in all: the calling thread, and `threads - 1` workers started here (none for 0 or 1).
    // (If starting a thread throws, the ones already started are stopped before the members they read go.)
    explicit ThreadPool(unsigned threads) : size_(threads > 1 ? threads : 1), words_(new Word[size_]) {
#if defined(__cpp_exceptions)
        try {
            start();
        } catch (...) {
            stop();
            workers_.clear();
            throw;
        }
#else
        start();
#endif
    }

    ~ThreadPool() { stop(); } // (the workers join once this body returns, as `workers_` goes)

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    unsigned size() const { return size_; }

    // `task(w)` for every `w` in [0, count): `w == 0` on the calling thread, the others on workers. Returns once
    // every call has returned, and what they wrote is then the caller's to read. `count` is clamped to
    // [1, size()]. One caller at a time; a task may not call `run` itself, and a task that throws ends the
    // program (on the calling thread too: leaving early would leave the workers running a task that is gone).
    template <class Task>
    void run(unsigned count, Task& task) noexcept {
        count = std::clamp(count, 1u, size_);
        if (count == 1) {
            task(0u);
            return;
        }
        task_ = &task;
        call_ = [](void* t, unsigned w) { (*static_cast<Task*>(t))(w); };
        pending_.store(static_cast<int>(count) - 1, std::memory_order_relaxed);
        for (unsigned w = 1; w < count; ++w) {
            words_[w].turn.fetch_add(1, std::memory_order_seq_cst); // publishes the task: the worker acquires it
            if (words_[w].parked.load(std::memory_order_seq_cst)) {
                words_[w].turn.notify_one();
            }
        }
        task(0u);
        wait_for_workers();
    }

private:
    static constexpr std::chrono::microseconds kSpin{1000};
    using Clock = std::chrono::steady_clock;

    // No lost wake-up. The one subtle line of this file: a worker parks by storing `parked` and then reading
    // `turn`; a dispatch stores `turn` and then reads `parked`, all four in sequential consistency. In their
    // single total order either the worker's read sees the new turn (and it does not wait), or the dispatch's
    // read sees `parked` (and it notifies) — and a notify that comes before the wait is harmless, because the
    // wait itself returns at once when `turn` is no longer what the worker saw. `pending_` and
    // `caller_parked_` are the same handshake the other way round.
    struct alignas(64) Word {
        std::atomic<std::uint32_t> turn{0};
        std::atomic<bool> parked{false};
    };

    // Spin until `ready()` or `kSpin` has passed (the clock read every 64 rounds): whether it became ready. Past
    // 4096 rounds each clock read also yields the CPU — with more runners than CPUs, a spinner otherwise holds
    // the one the runner it waits for needs, for the whole spin (a dispatch to a pool of 4 on one CPU: 2.2 ms,
    // 12 µs with the yield), and until then no yield costs the hot dispatch anything.
    template <class Ready>
    static bool spin_until(const Ready& ready) {
        const Clock::time_point until = Clock::now() + kSpin;
        for (unsigned round = 1;; ++round) {
            if (ready()) {
                return true;
            }
            if (round % 64 == 0) {
                if (round > 4096) {
                    std::this_thread::yield();
                }
                if (Clock::now() > until) {
                    return false;
                }
            }
        }
    }

    void work(unsigned w) {
        Word& word = words_[w];
        std::uint32_t seen = 0;
        for (;;) {
            const auto moved = [&] { return word.turn.load(std::memory_order_acquire) != seen; };
            if (!spin_until(moved)) {
                word.parked.store(true, std::memory_order_seq_cst);
                while (word.turn.load(std::memory_order_seq_cst) == seen) {
                    word.turn.wait(seen, std::memory_order_acquire);
                }
                word.parked.store(false, std::memory_order_relaxed);
            }
            seen = word.turn.load(std::memory_order_acquire);
            if (stop_.load(std::memory_order_relaxed)) {
                return;
            }
            call_(task_, w);
            if (pending_.fetch_sub(1, std::memory_order_seq_cst) == 1 && caller_parked_.load(std::memory_order_seq_cst)) {
                pending_.notify_one();
            }
        }
    }

    void start() {
        workers_.reserve(size_ - 1);
        for (unsigned w = 1; w < size_; ++w) {
            workers_.emplace_back([this, w] { work(w); });
        }
    }

    void stop() {
        stop_.store(true, std::memory_order_relaxed);
        for (unsigned w = 1; w < size_; ++w) {
            words_[w].turn.fetch_add(1, std::memory_order_seq_cst);
            words_[w].turn.notify_one();
        }
    }

    void wait_for_workers() {
        const auto done = [&] { return pending_.load(std::memory_order_acquire) == 0; };
        if (spin_until(done)) {
            return;
        }
        caller_parked_.store(true, std::memory_order_seq_cst);
        for (int left; (left = pending_.load(std::memory_order_seq_cst)) != 0;) {
            pending_.wait(left, std::memory_order_acquire);
        }
        caller_parked_.store(false, std::memory_order_relaxed);
    }

    unsigned size_;
    std::unique_ptr<Word[]> words_;
    alignas(64) std::atomic<int> pending_{0};
    std::atomic<bool> caller_parked_{false};
    std::atomic<bool> stop_{false};
    alignas(64) void* task_{nullptr};
    void (*call_)(void*, unsigned){nullptr};
    std::vector<std::jthread> workers_; // last: its threads join before anything they read goes
};

} // namespace heinzel

#endif // HEINZEL_HAVE_THREADS
