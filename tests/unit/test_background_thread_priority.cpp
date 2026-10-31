// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "helix_thread.h"

#include <atomic>

#include "../catch_amalgamated.hpp"

#ifdef __linux__
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
int current_thread_nice() {
    return ::getpriority(PRIO_PROCESS, static_cast<id_t>(::syscall(SYS_gettid)));
}
} // namespace

TEST_CASE("make_background_thread lowers its own thread and nothing else", "[thread_priority]") {
    const int spawner_before = current_thread_nice();
    if (spawner_before >= helix::kBackgroundThreadNice) {
        SKIP("the test runner itself already runs at nice " << spawner_before);
    }

    std::atomic<int> worker_nice{-100};
    auto worker = helix::make_background_thread([&] { worker_nice = current_thread_nice(); });
    worker.join();

    CHECK(worker_nice.load() == helix::kBackgroundThreadNice);
    // Per-thread, not per-process: the UI thread that spawned it keeps its priority.
    CHECK(current_thread_nice() == spawner_before);
}

TEST_CASE("make_thread leaves a worker at the spawner's priority", "[thread_priority]") {
    const int spawner = current_thread_nice();
    std::atomic<int> worker_nice{-100};
    auto worker = helix::make_thread([&] { worker_nice = current_thread_nice(); });
    worker.join();
    CHECK(worker_nice.load() == spawner);
}

TEST_CASE("lower_current_thread_priority is idempotent", "[thread_priority]") {
    std::atomic<bool> first{false};
    std::atomic<bool> second{false};
    std::atomic<int> nice_after{-100};
    auto worker = helix::make_thread([&] {
        first = helix::lower_current_thread_priority();
        second = helix::lower_current_thread_priority();
        nice_after = current_thread_nice();
    });
    worker.join();
    CHECK(first.load());
    CHECK(second.load());
    CHECK(nice_after.load() == helix::kBackgroundThreadNice);
}
#else
TEST_CASE("make_background_thread lowers its own thread and nothing else", "[thread_priority]") {
    SKIP("per-thread nice is Linux-only");
}
#endif
