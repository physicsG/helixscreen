// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_app_restart_service.cpp
 * @brief app_restart_strategy_for_env() — which restart a given environment gets
 *
 * Run with: ./build/bin/helix-tests "[app_globals][restart]"
 *
 * app_request_restart_service() itself is not driven here: it acts on the whole
 * process (quit, or re-exec in place). The decision it routes on is a pure
 * function in include/app_globals.h, so that is what these drive.
 *
 * What is at stake: getting this wrong under a supervisor leaves TWO instances
 * running — the supervisor starts a fresh one while the old process also re-execs
 * itself.
 */

#include "app_globals.h"

#include "../catch_amalgamated.hpp"

TEST_CASE("restart strategy: a supervisor gets a plain quit", "[app_globals][restart]") {
    // The values are what getenv() would have returned; passing them keeps this
    // test from mutating the process environment other tests share.
    SECTION("systemd sets INVOCATION_ID") {
        CHECK(app_restart_strategy_for_env("f00dcafe", nullptr) == AppRestartStrategy::Systemd);
    }

    SECTION("the watchdog sets HELIX_SUPERVISED") {
        CHECK(app_restart_strategy_for_env(nullptr, "1") == AppRestartStrategy::Watchdog);
    }

    SECTION("an empty value still counts as set, matching getenv() semantics") {
        // getenv returns a non-null pointer to "" for `FOO=`, and a supervisor that
        // exports the variable bare must still be honoured — re-execing under it is
        // the two-instance bug.
        CHECK(app_restart_strategy_for_env("", nullptr) == AppRestartStrategy::Systemd);
        CHECK(app_restart_strategy_for_env(nullptr, "") == AppRestartStrategy::Watchdog);
    }
}

TEST_CASE("restart strategy: systemd wins when both are set", "[app_globals][restart]") {
    // Deliberate precedence, and the case nothing pinned before: HELIX_SUPERVISED
    // can be inherited from an outer wrapper, while a unit file is the more
    // specific statement about what will actually restart us. Swapping the two
    // branches in app_restart_strategy_for_env() fails here and nowhere else.
    CHECK(app_restart_strategy_for_env("f00dcafe", "1") == AppRestartStrategy::Systemd);
}

TEST_CASE("restart strategy: unsupervised re-execs in place", "[app_globals][restart]") {
    // Nothing will restart us, so the process must replace itself.
    CHECK(app_restart_strategy_for_env(nullptr, nullptr) == AppRestartStrategy::ReExecInPlace);
}

TEST_CASE("restart strategy: the three outcomes are distinct", "[app_globals][restart]") {
    // Collapsing Systemd and Watchdog into one value would still satisfy every
    // case above, and would silently drop the log line that tells a user which
    // supervisor was detected when a restart does not come back.
    CHECK(AppRestartStrategy::Systemd != AppRestartStrategy::Watchdog);
    CHECK(AppRestartStrategy::Systemd != AppRestartStrategy::ReExecInPlace);
    CHECK(AppRestartStrategy::Watchdog != AppRestartStrategy::ReExecInPlace);
}
