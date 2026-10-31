// SPDX-License-Identifier: GPL-3.0-or-later
#include "system/child_process.h"

#include <chrono>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

#include "../catch_amalgamated.hpp"

using helix::ChildWaitOutcome;
using helix::terminate_and_reap;
using namespace std::chrono_literals;

namespace {

/// Fork and exec /bin/sleep @p seconds. With @p ignore_term the child ignores
/// SIGTERM, a disposition that survives exec.
///
/// SIGTERM stays blocked across the fork: a child that has not reached exec
/// yet still carries Catch2's fatal-signal handler, which would report the
/// test's own SIGTERM as a crash and outlive the grace period.
pid_t spawn_sleep(const char* seconds, bool ignore_term = false) {
    sigset_t term, saved;
    sigemptyset(&term);
    sigaddset(&term, SIGTERM);
    ::sigprocmask(SIG_BLOCK, &term, &saved);
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::signal(SIGTERM, ignore_term ? SIG_IGN : SIG_DFL);
        ::sigprocmask(SIG_SETMASK, &saved, nullptr);
        ::execl("/bin/sleep", "sleep", seconds, static_cast<char*>(nullptr));
        _exit(127);
    }
    ::sigprocmask(SIG_SETMASK, &saved, nullptr);
    return pid;
}

pid_t spawn_exit(int code) {
    const pid_t pid = ::fork();
    if (pid == 0) {
        _exit(code);
    }
    return pid;
}

auto from_now(std::chrono::milliseconds ms) {
    return std::chrono::steady_clock::now() + ms;
}

} // namespace

TEST_CASE("terminate_and_reap returns a child that exits on its own", "[child_process]") {
    const pid_t pid = spawn_exit(3);
    REQUIRE(pid > 0);
    int polls = 0;
    const auto r = terminate_and_reap(pid, from_now(5000ms), 500ms, 5ms, [&] {
        ++polls;
        return true;
    });
    CHECK(r.outcome == ChildWaitOutcome::Exited);
    CHECK(r.reaped);
    CHECK_FALSE(r.killed);
    CHECK(r.exit_code() == 3);
    CHECK(polls >= 1);
}

TEST_CASE("terminate_and_reap ends a child past its deadline with SIGTERM", "[child_process]") {
    const pid_t pid = spawn_sleep("30");
    REQUIRE(pid > 0);
    const auto start = std::chrono::steady_clock::now();
    const auto r = terminate_and_reap(pid, from_now(50ms), 10000ms, 5ms);
    const auto took = std::chrono::steady_clock::now() - start;

    CHECK(r.outcome == ChildWaitOutcome::TimedOut);
    CHECK(r.reaped);
    CHECK_FALSE(r.killed);
    CHECK(WIFSIGNALED(r.status));
    CHECK(WTERMSIG(r.status) == SIGTERM);
    CHECK(r.exit_code() == -1);
    // The grace ends at the reap, not at its 10s limit.
    CHECK(took < 5000ms);
}

TEST_CASE("terminate_and_reap SIGKILLs a child that ignores SIGTERM", "[child_process]") {
    const pid_t pid = spawn_sleep("30", true);
    REQUIRE(pid > 0);
    const auto r = terminate_and_reap(pid, from_now(50ms), 100ms, 5ms);

    CHECK(r.outcome == ChildWaitOutcome::TimedOut);
    CHECK(r.reaped);
    CHECK(r.killed);
    CHECK(WIFSIGNALED(r.status));
    CHECK(WTERMSIG(r.status) == SIGKILL);
}

TEST_CASE("terminate_and_reap ends the child when on_poll cancels", "[child_process]") {
    const pid_t pid = spawn_sleep("30");
    REQUIRE(pid > 0);
    int polls = 0;
    const auto r = terminate_and_reap(pid, from_now(30000ms), 10000ms, 5ms, [&] {
        ++polls;
        return polls < 3;
    });

    CHECK(r.outcome == ChildWaitOutcome::Cancelled);
    CHECK(r.reaped);
    CHECK_FALSE(r.killed);
    CHECK(WTERMSIG(r.status) == SIGTERM);
}

TEST_CASE("terminate_and_reap signals nothing once waitpid fails", "[child_process]") {
    // A pid this process already reaped: waitpid reports ECHILD, and the pid may
    // now name an unrelated process, so it must not be signalled.
    const pid_t pid = spawn_exit(0);
    REQUIRE(pid > 0);
    int status = 0;
    REQUIRE(::waitpid(pid, &status, 0) == pid);

    const auto r = terminate_and_reap(pid, from_now(0ms), 0ms, 1ms);
    CHECK(r.outcome == ChildWaitOutcome::WaitFailed);
    CHECK_FALSE(r.reaped);
    CHECK_FALSE(r.killed);
    CHECK(r.wait_errno == ECHILD);
    CHECK(r.exit_code() == -1);
}
