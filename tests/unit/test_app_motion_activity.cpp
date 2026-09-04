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
