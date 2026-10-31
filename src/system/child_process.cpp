// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "system/child_process.h"

#include <cerrno>
#include <csignal>
#include <sys/wait.h>
#include <thread>

namespace helix {

int ChildWaitResult::exit_code() const {
    return reaped && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

namespace {

enum class Poll { Running, Reaped, Failed };

Poll poll_once(pid_t pid, ChildWaitResult& result) {
    for (;;) {
        const pid_t waited = ::waitpid(pid, &result.status, WNOHANG);
        if (waited == pid) {
            result.reaped = true;
            return Poll::Reaped;
        }
        if (waited == 0) {
            return Poll::Running;
        }
        if (errno == EINTR) {
            continue;
        }
        result.wait_errno = errno;
        return Poll::Failed;
    }
}

} // namespace

ChildWaitResult terminate_and_reap(pid_t pid, std::chrono::steady_clock::time_point deadline,
                                   std::chrono::milliseconds term_grace,
                                   std::chrono::milliseconds poll_interval,
                                   const std::function<bool()>& on_poll) {
    ChildWaitResult result;

    for (;;) {
        const bool keep_waiting = !on_poll || on_poll();
        const Poll state = poll_once(pid, result);
        if (state == Poll::Reaped) {
            result.outcome = ChildWaitOutcome::Exited;
            return result;
        }
        if (state == Poll::Failed) {
            result.outcome = ChildWaitOutcome::WaitFailed;
            return result;
        }
        if (!keep_waiting) {
            result.outcome = ChildWaitOutcome::Cancelled;
            break;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            result.outcome = ChildWaitOutcome::TimedOut;
            break;
        }
        std::this_thread::sleep_for(poll_interval);
    }

    ::kill(pid, SIGTERM);
    const auto grace_end = std::chrono::steady_clock::now() + term_grace;
    for (;;) {
        if (on_poll) {
            on_poll();
        }
        const Poll state = poll_once(pid, result);
        if (state != Poll::Running) {
            return result;
        }
        if (std::chrono::steady_clock::now() >= grace_end) {
            break;
        }
        std::this_thread::sleep_for(poll_interval);
    }

    ::kill(pid, SIGKILL);
    result.killed = true;
    while (::waitpid(pid, &result.status, 0) < 0) {
        if (errno != EINTR) {
            result.wait_errno = errno;
            return result;
        }
    }
    result.reaped = true;
    return result;
}

} // namespace helix
