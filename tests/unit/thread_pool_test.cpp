// The thread pool a run may split its work among (`runtime/thread_pool.hpp`): every index of a dispatch runs
// once, index 0 on the calling thread, only the workers a dispatch uses run at all, what a task wrote is the
// caller's to read once `run` returns — and nothing hangs, whether the workers were spinning or had parked
// (the ctest has a timeout: a lost wake-up fails it instead of hanging the suite).

#include "heinzel/runtime/thread_pool.hpp"

#include <array>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <numeric>
#include <thread>
#include <vector>

using namespace heinzel;

namespace {

int g_wrong = 0;

void expect(bool ok, const char* what) {
    if (!ok && ++g_wrong <= 20) {
        std::printf("  wrong: %s\n", what);
    }
}

// 20 000 dispatches with the counts cycling 1..4: index w runs once in every dispatch whose count exceeds it.
void every_index_once() {
    ThreadPool pool(4);
    std::array<long, 4> hits{};
    auto task = [&](unsigned w) { ++hits[w]; };
    std::array<long, 4> owed{};
    for (int d = 0; d < 20000; ++d) {
        const unsigned count = 1 + static_cast<unsigned>(d % 4);
        pool.run(count, task);
        for (unsigned w = 0; w < count; ++w) {
            ++owed[w];
        }
    }
    expect(hits == owed, "every index of every dispatch runs exactly once");
}

void index_zero_is_the_caller() {
    ThreadPool pool(3);
    const std::thread::id caller = std::this_thread::get_id();
    std::array<std::thread::id, 3> ran_on{};
    auto task = [&](unsigned w) { ran_on[w] = std::this_thread::get_id(); };
    pool.run(3, task);
    expect(ran_on[0] == caller, "index 0 runs on the calling thread");
    expect(ran_on[1] != caller && ran_on[2] != caller && ran_on[1] != ran_on[2], "the other indices run on workers of their own");
}

void only_the_used_workers_run() {
    ThreadPool pool(8);
    std::array<int, 8> hits{};
    auto task = [&](unsigned w) { ++hits[w]; };
    for (int d = 0; d < 100; ++d) {
        pool.run(2, task);
    }
    expect(hits[0] == 100 && hits[1] == 100 && std::accumulate(hits.begin() + 2, hits.end(), 0) == 0, "run(2) touches indices 0 and 1 only");
    pool.run(99, task); // clamped to the pool's size
    expect(std::accumulate(hits.begin() + 2, hits.end(), 0) == 6, "a count past the size is clamped to it");
}

void a_pool_of_one() {
    for (const unsigned threads : {0u, 1u}) {
        ThreadPool pool(threads);
        int calls = 0;
        auto task = [&](unsigned w) { calls += w == 0 ? 1 : 100; };
        pool.run(5, task);
        expect(pool.size() == 1 && calls == 1, "a pool of one runs index 0, inline, once");
    }
}

// Each dispatch after the caller slept past the workers' spin: every wake-up is from a park.
void parked_and_woken() {
    ThreadPool pool(4);
    std::array<long, 4> hits{};
    auto task = [&](unsigned w) { ++hits[w]; };
    for (int d = 0; d < 200; ++d) {
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        pool.run(4, task);
    }
    expect(hits == std::array<long, 4>{200, 200, 200, 200}, "workers woken from a park run their index");
}

void destruction() {
    std::array<int, 4> hits{};
    auto task = [&](unsigned w) { ++hits[w]; };
    {
        ThreadPool spinning(4);
        spinning.run(4, task); // destroyed while its workers still spin
    }
    {
        ThreadPool parked(4);
        parked.run(4, task);
        std::this_thread::sleep_for(std::chrono::milliseconds(5)); // destroyed parked
    }
    { ThreadPool never_used(4); }
    expect(hits == std::array<int, 4>{2, 2, 2, 2}, "pools destroyed spinning, parked and unused");
}

// Workers busy longer than the spin: the caller parks on its count and is woken by the last of them.
void the_caller_parks() {
    ThreadPool pool(4);
    std::array<long, 4> hits{};
    auto task = [&](unsigned w) {
        if (w != 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
        ++hits[w];
    };
    long owed = 0;
    for (int d = 0; d < 50; ++d) {
        pool.run(2 + static_cast<unsigned>(d % 3), task);
        owed += d % 3 >= 1 ? 1 : 0;
    }
    expect(hits[0] == 50 && hits[1] == 50 && hits[2] == owed, "a caller that parked is woken by the last worker");
}

void run_of_none() {
    ThreadPool pool(4);
    int calls = 0;
    auto task = [&](unsigned w) { calls += w == 0 ? 1 : 100; };
    pool.run(0, task);
    expect(calls == 1, "run(0) runs index 0, once");
}

// Once the workers have spun out and parked, they cost the process no CPU time.
void parked_workers_cost_nothing() {
    ThreadPool pool(8);
    auto task = [](unsigned) {};
    pool.run(8, task);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const std::clock_t before = std::clock();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const double spent = static_cast<double>(std::clock() - before) / CLOCKS_PER_SEC;
    expect(spent < 0.010, "parked workers burn no CPU (spinning ones would burn a core each)");
}

// Workers fill disjoint stretches of a plain vector; the caller reads all of it once `run` returns.
void what_a_task_wrote_is_visible() {
    ThreadPool pool(4);
    std::vector<long> v(4096, 0);
    for (long round = 1; round <= 500; ++round) {
        auto task = [&](unsigned w) {
            for (std::size_t i = w * 1024; i < (w + 1) * 1024; ++i) {
                v[i] = round;
            }
        };
        pool.run(4, task);
        if (std::accumulate(v.begin(), v.end(), 0L) != round * 4096) {
            expect(false, "what the workers wrote is visible to the caller after run");
            return;
        }
    }
}

} // namespace

int main() {
    every_index_once();
    index_zero_is_the_caller();
    only_the_used_workers_run();
    a_pool_of_one();
    parked_and_woken();
    destruction();
    what_a_task_wrote_is_visible();
    the_caller_parks();
    run_of_none();
    parked_workers_cost_nothing();
    if (g_wrong != 0) {
        std::printf("%d checks are wrong\n", g_wrong);
        return 1;
    }
    std::printf("thread_pool: every index once, the caller's first, parked workers woken, nothing hangs\n");
    return 0;
}
