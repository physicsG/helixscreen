// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for the boot-splash lifetime/status policy (include/splash_status.h).
// Pure logic — no LVGL, no fbdev, no filesystem.

#include "splash_status.h"

#include "catch_amalgamated.hpp"

using helix::splash::compose_splash_status;
using helix::splash::parse_meminfo_available_kb;
using helix::splash::sanitize_splash_message;
using helix::splash::splash_memory_ok;
using helix::splash::splash_should_continue;
using helix::splash::SplashLifetimePolicy;

TEST_CASE("splash lifetime: no heartbeat preserves legacy 30s cap", "[splash][lifetime]") {
    SplashLifetimePolicy p; // default 30 / 180
    const long start = 1000;

    // No heartbeat ever observed (-1).
    REQUIRE(splash_should_continue(p, start, start + 0, -1) == true);
    REQUIRE(splash_should_continue(p, start, start + 29, -1) == true);
    // At exactly the cap it must stop (matches original `>= MAX_LIFETIME_SEC`).
    REQUIRE(splash_should_continue(p, start, start + 30, -1) == false);
    REQUIRE(splash_should_continue(p, start, start + 31, -1) == false);
}

TEST_CASE("splash lifetime: a heartbeat keeps it alive past the legacy cap", "[splash][lifetime]") {
    SplashLifetimePolicy p;
    const long start = 1000;

    // Any observed heartbeat means a gate is driving us — stay up well past 30s.
    REQUIRE(splash_should_continue(p, start, start + 45, start + 45) == true);
    REQUIRE(splash_should_continue(p, start, start + 100, start + 99) == true);
}

TEST_CASE("splash lifetime: stays up after heartbeats stop, until the backstop "
          "(covers gate-end -> UI first paint)",
          "[splash][lifetime]") {
    SplashLifetimePolicy p; // 30 / 180
    const long start = 1000;

    // The gate finished heart­beating at start+56; helix-screen has not yet sent
    // SIGUSR1 and suppresses its own rendering until the splash exits. Exiting at
    // the 30s cap here would blank the screen for the UI's ~20s startup, so once
    // a heartbeat has been seen we keep going regardless of how stale it is...
    REQUIRE(splash_should_continue(p, start, start + 80, start + 56) == true);
    REQUIRE(splash_should_continue(p, start, start + 150, start + 56) == true);
    // ...bounded only by the absolute backstop.
    REQUIRE(splash_should_continue(p, start, start + 180, start + 56) == false);
}

TEST_CASE("splash lifetime: absolute backstop overrides a live heartbeat", "[splash][lifetime]") {
    SplashLifetimePolicy p; // backstop 180s
    const long start = 1000;

    // Even with a perfectly fresh heartbeat, never run past the backstop.
    REQUIRE(splash_should_continue(p, start, start + 179, start + 179) == true);
    REQUIRE(splash_should_continue(p, start, start + 180, start + 180) == false);
    REQUIRE(splash_should_continue(p, start, start + 200, start + 200) == false);
}

TEST_CASE("splash lifetime: monotonic math is immune to a wall-clock jump", "[splash][lifetime]") {
    // The policy never takes wall-clock as input; this documents that a large
    // forward NTP jump (which would wreck an mtime-age comparison) cannot affect
    // the decision, because callers pass only monotonic timestamps. A heartbeat
    // observed 2s ago (monotonic) stays fresh no matter what the wall clock did.
    SplashLifetimePolicy p;
    const long start = 1000;
    REQUIRE(splash_should_continue(p, start, start + 90, start + 88) == true);
}

TEST_CASE("parse_meminfo_available_kb extracts MemAvailable", "[splash][memory]") {
    const std::string meminfo = "MemTotal:         107264 kB\n"
                                "MemFree:           12880 kB\n"
                                "MemAvailable:      34216 kB\n"
                                "Buffers:            1024 kB\n";
    REQUIRE(parse_meminfo_available_kb(meminfo) == 34216);

    // Absent field -> -1 (caller must not enforce a floor on an unknown value).
    REQUIRE(parse_meminfo_available_kb("MemTotal: 107264 kB\nMemFree: 4096 kB\n") == -1);
    REQUIRE(parse_meminfo_available_kb("") == -1);

    // Tab/odd spacing still parses.
    REQUIRE(parse_meminfo_available_kb("MemAvailable:\t8192 kB\n") == 8192);
}

TEST_CASE("splash_memory_ok enforces the floor only when both values are known",
          "[splash][memory]") {
    // Floor disabled (<= 0) -> always ok.
    REQUIRE(splash_memory_ok(1024, 0) == true);
    REQUIRE(splash_memory_ok(1024, -1) == true);
    // Unknown reading (-1) -> always ok (never guess pressure).
    REQUIRE(splash_memory_ok(-1, 8192) == true);
    // Real floor: at or above is ok, below is not.
    REQUIRE(splash_memory_ok(8192, 8192) == true);
    REQUIRE(splash_memory_ok(8193, 8192) == true);
    REQUIRE(splash_memory_ok(8191, 8192) == false);
}

TEST_CASE("sanitize_splash_message trims and clamps", "[splash][status]") {
    REQUIRE(sanitize_splash_message("Starting Klipper… 40s\n") == "Starting Klipper… 40s");
    REQUIRE(sanitize_splash_message("  spaced  \t\n") == "  spaced");
    // Only the first line survives (mtime is the heartbeat, line 1 is the message).
    REQUIRE(sanitize_splash_message("line one\nline two\n") == "line one");
    REQUIRE(sanitize_splash_message("") == "");

    std::string long_msg(200, 'x');
    REQUIRE(sanitize_splash_message(long_msg).size() == 96);
}

TEST_CASE("compose_splash_status appends the splash-owned counter", "[splash][status]") {
    // Label + rising seconds: the splash owns the count so it keeps climbing
    // even after the gate hands off and stops writing the file.
    REQUIRE(compose_splash_status("Starting Klipper…", 0) == "Starting Klipper… 0s");
    REQUIRE(compose_splash_status("Starting Klipper…", 28) == "Starting Klipper… 28s");
    REQUIRE(compose_splash_status("Starting HelixScreen…", 45) == "Starting HelixScreen… 45s");

    // Empty label -> empty line (no bare counter before there's any status).
    REQUIRE(compose_splash_status("", 12) == "");

    // Negative elapsed is clamped to 0 (defensive; clock should never go back).
    REQUIRE(compose_splash_status("Starting Klipper…", -5) == "Starting Klipper… 0s");
}
