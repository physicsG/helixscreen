// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
// tests/unit/test_app_motion_activity.cpp

#include "app_motion_activity.h"

#include "../catch_amalgamated.hpp"

using helix::AppMotionActivity;
using clock_t_ = AppMotionActivity::clock;

TEST_CASE("AppMotionActivity: idle by default", "[motion][busy_guard]") {
    AppMotionActivity a;
    CHECK_FALSE(a.recently_active());
}

TEST_CASE("AppMotionActivity: active while a send is outstanding", "[motion][busy_guard]") {
    AppMotionActivity a;
    a.note_sent();
    CHECK(a.recently_active());
}

TEST_CASE("AppMotionActivity: grace window after ack, then expires", "[motion][busy_guard]") {
    AppMotionActivity a;
    const auto t0 = clock_t_::now();
    a.note_sent();
    a.note_done(t0);
    CHECK(a.recently_active(t0 + std::chrono::milliseconds(500)));
    CHECK(a.recently_active(t0 + std::chrono::milliseconds(1999)));
    CHECK_FALSE(a.recently_active(t0 + std::chrono::milliseconds(2001)));
}

TEST_CASE("AppMotionActivity: overlapping sends stay active until last ack",
          "[motion][busy_guard]") {
    AppMotionActivity a;
    const auto t0 = clock_t_::now();
    a.note_sent();
    a.note_sent();
    a.note_done(t0);
    CHECK(a.recently_active(t0 + std::chrono::hours(1))); // one still outstanding
    a.note_done(t0 + std::chrono::hours(1));
    CHECK_FALSE(a.recently_active(t0 + std::chrono::hours(2)));
}

TEST_CASE("AppMotionActivity: unbalanced note_done clamps at zero", "[motion][busy_guard]") {
    AppMotionActivity a;
    const auto t0 = clock_t_::now();
    a.note_done(t0); // defensive: never sent
    CHECK_FALSE(a.recently_active(t0 + std::chrono::seconds(3)));
    a.note_sent();
    CHECK(a.recently_active(t0 + std::chrono::hours(1)));
}

TEST_CASE("AppMotionActivity: a long move keeps its own busy episode past the ack grace",
          "[motion][busy_guard]") {
    AppMotionActivity a;
    const auto t0 = clock_t_::now();
    a.note_sent(t0);
    a.note_done(t0 + std::chrono::milliseconds(20));
    const auto episode = t0 + std::chrono::milliseconds(30);
    // A 300mm diagonal is still running 5s later: the grace window is over,
    // the episode is still the one this send started.
    const auto later = t0 + std::chrono::seconds(5);
    CHECK_FALSE(a.recently_active(later));
    CHECK(a.owns_busy_episode(episode, later));
}

TEST_CASE("AppMotionActivity: an episode that began without a send is not the app's",
          "[motion][busy_guard]") {
    AppMotionActivity a;
    const auto t0 = clock_t_::now();
    CHECK_FALSE(a.owns_busy_episode(t0, t0 + std::chrono::seconds(1)));

    a.note_sent(t0);
    a.note_done(t0);
    // Someone else's op starts well after the app's last move settled.
    const auto foreign = t0 + std::chrono::seconds(10);
    CHECK_FALSE(a.owns_busy_episode(foreign, foreign + std::chrono::seconds(1)));
}

TEST_CASE("AppMotionActivity: ownership of a busy episode is capped", "[motion][busy_guard]") {
    AppMotionActivity a;
    const auto t0 = clock_t_::now();
    a.note_sent(t0);
    a.note_done(t0);
    CHECK(a.owns_busy_episode(t0, t0 + std::chrono::seconds(29)));
    CHECK_FALSE(a.owns_busy_episode(t0, t0 + std::chrono::seconds(31)));
}

TEST_CASE("AppMotionActivity: a later send never adopts someone else's episode",
          "[motion][busy_guard]") {
    AppMotionActivity a;
    const auto t0 = clock_t_::now();
    a.note_sent(t0);
    a.note_done(t0);
    // Another UI starts a long operation; the app's last move settled 10s ago.
    const auto foreign = t0 + std::chrono::seconds(10);
    CHECK_FALSE(a.owns_busy_episode(foreign, foreign + std::chrono::seconds(1)));
    // A send slipping through afterwards must not make the episode the app's.
    a.note_sent(foreign + std::chrono::seconds(2));
    a.note_done(foreign + std::chrono::seconds(2));
    CHECK_FALSE(a.owns_busy_episode(foreign, foreign + std::chrono::seconds(5)));
}

TEST_CASE("AppMotionActivity: follow-up sends keep an episode the app started",
          "[motion][busy_guard]") {
    AppMotionActivity a;
    const auto t0 = clock_t_::now();
    a.note_sent(t0);
    a.note_done(t0);
    const auto episode = t0 + std::chrono::milliseconds(30);
    CHECK(a.owns_busy_episode(episode, t0 + std::chrono::seconds(4)));
    a.note_sent(t0 + std::chrono::seconds(4));
    a.note_done(t0 + std::chrono::seconds(4));
    CHECK(a.owns_busy_episode(episode, t0 + std::chrono::seconds(8)));
}
