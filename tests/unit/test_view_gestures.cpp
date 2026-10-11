// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "view_gestures.h"

#include "../catch_amalgamated.hpp"

using namespace helix::ui;
using Catch::Approx;
using Kind = TwoFingerSample::Kind;
using Phase = TwoFingerSample::Phase;

namespace {
TwoFingerSample sample(Kind kind, Phase phase, float dx, float dy, float scale, int sx = 100,
                       int sy = 100) {
    return TwoFingerSample{kind, phase, dx, dy, scale, sx, sy};
}
} // namespace

TEST_CASE("two_finger_step: ONGOING is active with no motion", "[gesture]") {
    TwoFingerState st;
    auto s = two_finger_step(sample(Kind::Pinch, Phase::Ongoing, 5, 5, 1.05f), st);
    CHECK(s.active);
    CHECK_FALSE(s.ended);
    CHECK(s.pan_dx == 0.0f);
    CHECK(s.pan_dy == 0.0f);
    CHECK(s.zoom == 1.0f);
}

TEST_CASE("two_finger_step: first recognized frame catches up pre-recognition motion",
          "[gesture]") {
    TwoFingerState st;
    two_finger_step(sample(Kind::Pan, Phase::Ongoing, 0, 0, 1.0f), st);
    auto s = two_finger_step(sample(Kind::Pan, Phase::Recognized, 60, -20, 1.0f), st);
    CHECK(s.active);
    CHECK(s.pan_dx == Approx(60.0f));
    CHECK(s.pan_dy == Approx(-20.0f));
    CHECK(s.zoom == 1.0f);
}

TEST_CASE("two_finger_step: per-frame pan deltas sum to the cumulative translation", "[gesture]") {
    TwoFingerState st;
    float sum_x = 0, sum_y = 0;
    for (float d : {60.0f, 75.0f, 74.0f, 90.0f}) {
        auto s = two_finger_step(sample(Kind::Pan, Phase::Recognized, d, -d / 2, 1.0f), st);
        sum_x += s.pan_dx;
        sum_y += s.pan_dy;
    }
    CHECK(sum_x == Approx(90.0f));
    CHECK(sum_y == Approx(-45.0f));
}

TEST_CASE("two_finger_step: pinch zooms by the frame ratio and pans in the same frame",
          "[gesture]") {
    TwoFingerState st;
    auto a = two_finger_step(sample(Kind::Pinch, Phase::Recognized, 0, 0, 1.2f), st);
    CHECK(a.zoom == Approx(1.2f));
    auto b = two_finger_step(sample(Kind::Pinch, Phase::Recognized, 10, 4, 1.32f), st);
    CHECK(b.zoom == Approx(1.1f));
    CHECK(b.pan_dx == Approx(10.0f));
    CHECK(b.pan_dy == Approx(4.0f));
}

TEST_CASE("two_finger_step: a pan never zooms, whatever scale LVGL reports", "[gesture]") {
    TwoFingerState st;
    auto s = two_finger_step(sample(Kind::Pan, Phase::Recognized, 5, 0, 1.3f), st);
    CHECK(s.zoom == 1.0f);
    CHECK(s.pan_dx == Approx(5.0f));
}

TEST_CASE("two_finger_step: anchor is the start centre plus the cumulative translation",
          "[gesture]") {
    TwoFingerState st;
    auto s = two_finger_step(sample(Kind::Pinch, Phase::Recognized, 30, -10, 1.2f, 100, 200), st);
    CHECK(s.anchor_x == 130);
    CHECK(s.anchor_y == 190);
}

TEST_CASE("two_finger_step: an implausible frame ratio holds zoom but still pans", "[gesture]") {
    TwoFingerState st;
    two_finger_step(sample(Kind::Pinch, Phase::Recognized, 0, 0, 1.0f), st);
    auto jump = two_finger_step(sample(Kind::Pinch, Phase::Recognized, 5, 0, 2.0f), st);
    CHECK(jump.zoom == 1.0f);
    CHECK(jump.pan_dx == Approx(5.0f));
    // The jumped scale becomes the new baseline.
    auto next = two_finger_step(sample(Kind::Pinch, Phase::Recognized, 5, 0, 2.1f), st);
    CHECK(next.zoom == Approx(1.05f));
    // Below the lower bound holds zoom too.
    auto low = two_finger_step(sample(Kind::Pinch, Phase::Recognized, 5, 0, 2.1f * 0.69f), st);
    CHECK(low.zoom == 1.0f);
}

TEST_CASE(
    "two_finger_step: the first recognized pinch frame catches up zoom beyond the frame filter",
    "[gesture]") {
    TwoFingerState st;
    auto first = two_finger_step(sample(Kind::Pinch, Phase::Recognized, 0, 0, 1.46f), st);
    CHECK(first.zoom == Approx(1.46f));
    auto second = two_finger_step(sample(Kind::Pinch, Phase::Recognized, 0, 0, 1.5f), st);
    CHECK(second.zoom == Approx(1.5f / 1.46f));
    auto third = two_finger_step(sample(Kind::Pinch, Phase::Recognized, 0, 0, 3.0f), st);
    CHECK(third.zoom == 1.0f);
}

TEST_CASE(
    "two_finger_step: the first recognized pinch frame catches up zoom below the frame filter",
    "[gesture]") {
    TwoFingerState st;
    auto first = two_finger_step(sample(Kind::Pinch, Phase::Recognized, 0, 0, 0.6f), st);
    CHECK(first.zoom == Approx(0.6f));
}

TEST_CASE("two_finger_step: a non-positive scale holds zoom and keeps the baseline", "[gesture]") {
    TwoFingerState st;
    two_finger_step(sample(Kind::Pinch, Phase::Recognized, 0, 0, 1.0f), st);
    CHECK(two_finger_step(sample(Kind::Pinch, Phase::Recognized, 0, 0, 0.0f), st).zoom == 1.0f);
    CHECK(two_finger_step(sample(Kind::Pinch, Phase::Recognized, 0, 0, -1.0f), st).zoom == 1.0f);
    auto s = two_finger_step(sample(Kind::Pinch, Phase::Recognized, 0, 0, 1.1f), st);
    CHECK(s.zoom == Approx(1.1f));
}

TEST_CASE("two_finger_step: ENDED reports ended and resets totals", "[gesture]") {
    TwoFingerState st;
    two_finger_step(sample(Kind::Pinch, Phase::Recognized, 40, 40, 1.3f), st);
    auto end = two_finger_step(sample(Kind::Pinch, Phase::Ended, 40, 40, 1.3f), st);
    CHECK(end.ended);
    CHECK_FALSE(end.active);
    CHECK(end.pan_dx == 0.0f);
    CHECK(end.zoom == 1.0f);
    CHECK(st.last_dx == 0.0f);
    CHECK(st.last_scale == 1.0f);
}

TEST_CASE("two_finger_step: ONGOING discards stale totals from a gesture that never ended",
          "[gesture]") {
    TwoFingerState st;
    two_finger_step(sample(Kind::Pan, Phase::Recognized, 80, 0, 1.0f), st);
    two_finger_step(sample(Kind::Pinch, Phase::Ongoing, 0, 0, 1.0f), st);
    auto s = two_finger_step(sample(Kind::Pinch, Phase::Recognized, 10, 0, 1.2f), st);
    CHECK(s.pan_dx == Approx(10.0f));
    CHECK(s.zoom == Approx(1.2f));
}
