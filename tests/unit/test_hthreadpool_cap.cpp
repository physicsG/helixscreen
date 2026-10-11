// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// HThreadPool's worker cap must hold under concurrent commits, and a spawn
// that fails must degrade to queuing rather than throwing.
//
// createThread() checks cur_thread_num against max_thread_num before
// spawning, and that check has to be atomic with the increment: two threads
// committing at the same moment can both pass an unlocked check and both
// spawn, running live workers above max_thread_num. ThumbnailProcessor sizes
// its pool to leave headroom for the UI thread on 1-2 core devices, and the
// shutdown-race tests commit from four threads at once, so the growth path
// really is contended (prestonbrown/helixscreen#1585).
//
// A failed spawn is the cap path's problem too: commit() ignores
// createThread()'s return and queues the task either way, so createThread()
// must answer false and leave the task for a live worker. A rethrown EAGAIN
// walks out of commit() into whatever LVGL callback asked for the work, and
// nothing between them catches it (prestonbrown/helixscreen#724).
//
// TEST_MIRROR_OK: pins HThreadPool itself — the vendored, patched pool in
// lib/libhv. No helixscreen header wraps the cap reservation under test;
// include/thumbnail_processor.h only owns an instance of this pool.

#include <hv/hthreadpool.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

#ifdef __linux__
#include <fstream>
#include <string>
#include <sys/resource.h>
#include <system_error>
#endif

#include "../catch_amalgamated.hpp"

namespace {

// Race the growth path the way the print-select metadata refresh does: one
// worker already busy, several threads committing at once. Rounds repeat
// across idle-out retirements because the window only opens while
// cur_thread_num sits below max.
TEST_CASE("HThreadPool never exceeds max threads under concurrent commits",
          "[threading][slow][1585]") {
    constexpr int kMax = 2;
    HThreadPool pool(1, kMax, 100);
    pool.start(1);

    for (int round = 0; round < 12; ++round) {
        // Hold the one worker busy so every storm commit sees idle == 0 and
        // takes the growth path into createThread().
        std::promise<void> gate;
        auto busy = pool.commit([&gate] { gate.get_future().wait(); });

        std::atomic<int> started{0};
        std::vector<std::thread> stormers;
        for (int t = 0; t < 4; ++t) {
            stormers.emplace_back([&] {
                ++started;
                while (started < 4) {
                    std::this_thread::yield();
                }
                for (int i = 0; i < 25; ++i) {
                    pool.commit([] {});
                }
            });
        }
        for (auto& t : stormers) {
            t.join();
        }
        gate.set_value();
        busy.wait();

        // Quiescent read: workers are busy or freshly idle, the 100ms idle-out
        // cannot have retired anyone yet, so the cap verdict is stable here.
        REQUIRE(pool.currentThreadNum() <= kMax);

        // Let the pool idle back down to min so the next round races at
        // cur == min again, re-opening the growth window.
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    pool.stop();
}

#ifdef __linux__
// Pin RLIMIT_NPROC at the process's live thread count: the kernel counts
// threads uid-wide, so this process's count is a lower bound of what it
// compares against, and the next pthread_create is guaranteed EAGAIN. That
// makes the spawn failure deterministic without flooding the process with
// threads. Linux-only: RLIMIT_NPROC and /proc/self/status are the levers.
TEST_CASE("commit() queues onto a live worker when the spawn hits EAGAIN",
          "[threading][slow][724]") {
    auto thread_count = [] {
        std::ifstream status("/proc/self/status");
        std::string line;
        while (std::getline(status, line)) {
            if (line.rfind("Threads:", 0) == 0) {
                return std::stoi(line.substr(8));
            }
        }
        return 1;
    };

    // The pool and its one worker must exist before the limit is pinned:
    // start() spawns, and so would EAGAIN under it.
    HThreadPool pool(1, 4, 100);
    pool.start(1);

    // Hold the one worker inside a task so the probe commit sees idle == 0
    // and takes the growth path into createThread().
    std::promise<void> entered;
    std::promise<void> gate;
    auto busy = pool.commit([&entered, &gate] {
        entered.set_value();
        gate.get_future().wait();
    });
    entered.get_future().wait();

    struct rlimit original {};
    REQUIRE(getrlimit(RLIMIT_NPROC, &original) == 0);
    rlimit pinned = original;
    pinned.rlim_cur = thread_count();
    REQUIRE(setrlimit(RLIMIT_NPROC, &pinned) == 0);
    // A failed assertion from here on must not leave the whole test process
    // unable to spawn threads, so restore from a destructor.
    struct RestoreLimit {
        struct rlimit limit;
        ~RestoreLimit() {
            setrlimit(RLIMIT_NPROC, &limit);
        }
    } restore{original};

    // Prove the limit bites before relying on it: a raw spawn must fail. The
    // kernel does not apply RLIMIT_NPROC to root or CAP_SYS_RESOURCE, so as
    // root (the sanitizer container) there is no spawn failure to provoke and
    // nothing here can be tested.
    bool raw_spawn_threw = false;
    try {
        std::thread bystander([] {});
        bystander.join();
    } catch (const std::system_error&) {
        raw_spawn_threw = true;
    }
    if (!raw_spawn_threw) {
        gate.set_value();
        busy.wait();
        pool.stop();
        SKIP("RLIMIT_NPROC does not bind for this process (root or CAP_SYS_RESOURCE)");
    }

    std::future<void> queued;
    bool commit_threw = false;
    try {
        queued = pool.commit([] {});
    } catch (const std::system_error&) {
        commit_threw = true;
    }
    REQUIRE_FALSE(commit_threw);
    REQUIRE(queued.valid());

    gate.set_value();
    busy.wait();
    REQUIRE(queued.wait_for(std::chrono::seconds(5)) == std::future_status::ready);

    pool.stop();
}
#endif

} // namespace
