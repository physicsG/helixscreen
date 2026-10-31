// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <functional>
#include <thread>
#include <utility>

#if defined(__linux__) || defined(__APPLE__)
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#define HELIX_THREAD_ALTSTACK 1
#endif

#ifdef __linux__
#include <sys/resource.h>
#include <sys/syscall.h>
#endif

// Header-only so the dlopen()ed Bluetooth plugin, which links none of the
// app's objects, starts its threads the same way.

namespace helix {

/// Signal stack size for every thread. The crash handler formats the whole
/// record on it, stack dump and memory map included.
inline constexpr std::size_t kAltStackSize = 64 * 1024;

/// Give the calling thread its own signal stack.
///
/// sigaltstack is per thread. Without one, a stack overflow leaves the crash
/// handler no stack to run on and the process dies with no crash file.
/// Allocated once per thread and released at thread exit; repeat calls, and
/// calls on a thread that already has a signal stack, do nothing. Returns
/// whether the thread now has one.
inline bool install_thread_altstack() noexcept {
#ifdef HELIX_THREAD_ALTSTACK
    struct ThreadAltStack {
        void* mem = nullptr;
        std::size_t len = 0;
        bool active = false;
        ThreadAltStack() {
            stack_t current{};
            if (sigaltstack(nullptr, &current) == 0 && !(current.ss_flags & SS_DISABLE)) {
                active = true;
                return;
            }
            // mmap rather than the heap: pages stay unbacked until a crash
            // touches them, so an idle thread costs no RSS. The lowest page is
            // a guard, so a handler that overruns its stack faults instead of
            // writing over whatever is mapped below.
            const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
            const std::size_t total = kAltStackSize + page;
            void* p =
                mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (p == MAP_FAILED) {
                return;
            }
            stack_t ss{};
            ss.ss_sp = static_cast<char*>(p) + page;
            ss.ss_size = kAltStackSize;
            if (mprotect(p, page, PROT_NONE) != 0 || sigaltstack(&ss, nullptr) != 0) {
                munmap(p, total);
                return;
            }
            mem = p;
            len = total;
            active = true;
        }
        ~ThreadAltStack() {
            if (mem == nullptr) {
                return;
            }
            // Detach before unmapping, so a signal in a later thread-exit
            // destructor cannot land on freed memory.
            stack_t ss{};
            ss.ss_flags = SS_DISABLE;
            sigaltstack(&ss, nullptr);
            munmap(mem, len);
        }
    };
    thread_local ThreadAltStack s_thread_alt_stack;
    return s_thread_alt_stack.active;
#else
    return false;
#endif
}

/// std::thread(f, args...) for a thread that leaves a crash file if it
/// overflows its stack: the thread installs its own signal stack before
/// running f. Every app-owned thread starts through here (lint-gated).
template <typename F, typename... Args> std::thread make_thread(F&& f, Args&&... args) {
    return std::thread(
        [](auto&& fn, auto&&... a) {
            install_thread_altstack();
            std::invoke(std::move(fn), std::move(a)...);
        },
        std::forward<F>(f), std::forward<Args>(args)...);
}

/// CPU time the calling thread has used, in nanoseconds; 0 where the clock is
/// unavailable (ESP-IDF's newlib defines no per-thread CPU clock). Measures work
/// done rather than time waited, so a thread preempted by Klipper is not charged
/// for it.
inline int64_t thread_cpu_ns() noexcept {
#ifdef CLOCK_THREAD_CPUTIME_ID
    timespec ts{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) {
        return 0;
    }
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
#else
    return 0;
#endif
}

/// Nice value for work nobody is waiting on: G-code indexing, preview ghosts,
/// prefetch. Nice rather than SCHED_IDLE because the UI thread joins these
/// workers on cancel; a SCHED_IDLE thread can be starved indefinitely while
/// Klipper keeps the CPU busy, and the join would freeze the UI with it.
inline constexpr int kBackgroundThreadNice = 19;

/// Lower the calling thread, and only it, to kBackgroundThreadNice.
///
/// On a printer's own board helix-screen shares two slow cores with Klipper.
/// The launcher nices the whole process only when it recognizes the control
/// stack beside it; this holds for the heavy workers whatever the launcher
/// decided. Raising a nice value needs no privileges. A thread already at or
/// above the target is left alone. Returns whether the thread now runs at
/// kBackgroundThreadNice or lower priority; false on hosts without per-thread
/// nice (macOS), where the work runs at its default priority.
inline bool lower_current_thread_priority() noexcept {
#ifdef __linux__
    const auto tid = static_cast<id_t>(::syscall(SYS_gettid));
    errno = 0;
    const int current = ::getpriority(PRIO_PROCESS, tid);
    if (errno == 0 && current >= kBackgroundThreadNice) {
        return true;
    }
    return ::setpriority(PRIO_PROCESS, tid, kBackgroundThreadNice) == 0;
#else
    return false;
#endif
}

/// make_thread() for background work that must yield the CPU to the UI and to
/// Klipper: the thread lowers its own priority before running f.
template <typename F, typename... Args> std::thread make_background_thread(F&& f, Args&&... args) {
    return std::thread(
        [](auto&& fn, auto&&... a) {
            install_thread_altstack();
            lower_current_thread_priority();
            std::invoke(std::move(fn), std::move(a)...);
        },
        std::forward<F>(f), std::forward<Args>(args)...);
}

} // namespace helix
