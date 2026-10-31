// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "bed_plate_gesture.h"

#include <optional>
#include <vector>

#include "../catch_amalgamated.hpp"

using helix::AxisTarget;
using helix::BedPlateGesture;
using helix::kPlateDragThresholdPx;
using helix::PlateLift;

namespace {

/// Records every hook call. A point maps to target (x, y) in millimetres, one to one, so
/// a test reads the sent target straight off the pixels it pressed.
struct Recorder {
    bool can_move = true;
    PlateLift lift_result{PlateLift::Kind::None, 0.0};
    int can_move_calls = 0;
    int lift_calls = 0;
    int refused_calls = 0;
    std::vector<AxisTarget> sent;

    BedPlateGesture make() {
        return BedPlateGesture({
            [](int x, int y) -> std::optional<AxisTarget> {
                AxisTarget t;
                t.x = static_cast<double>(x);
                t.y = static_cast<double>(y);
                return t;
            },
            [this]() {
                ++can_move_calls;
                return can_move;
            },
            [this]() {
                ++lift_calls;
                return lift_result;
            },
            [this](const AxisTarget& t) { sent.push_back(t); },
            [this]() { ++refused_calls; },
        });
    }
};

constexpr int kFar = kPlateDragThresholdPx + 1;

} // namespace

TEST_CASE("plate gesture: a press released in place is a tap that sends nothing",
          "[bed_plate_gesture]") {
    Recorder r;
    auto g = r.make();
    REQUIRE(g.press(100, 100));
    CHECK_FALSE(g.pressing(100 + kPlateDragThresholdPx, 100 - kPlateDragThresholdPx));
    const auto rel = g.release(104, 100);
    CHECK_FALSE(rel.was_drag);
    REQUIRE(rel.tap_target.has_value());
    CHECK(*rel.tap_target->x == 100.0); // the press point, not the release point
    CHECK(*rel.tap_target->y == 100.0);
    CHECK(r.sent.empty());
    CHECK(r.lift_calls == 0);
}

TEST_CASE("plate gesture: past the threshold the target under the finger is sent",
          "[bed_plate_gesture]") {
    Recorder r;
    auto g = r.make();
    REQUIRE(g.press(100, 100));
    g.pressing(100 + kFar, 100);
    REQUIRE(r.sent.size() == 1);
    CHECK(*r.sent[0].x == 100.0 + kFar);
    CHECK(g.dragging());
    REQUIRE(g.drag_target().has_value());
    CHECK(*g.drag_target()->x == 100.0 + kFar);
}

TEST_CASE("plate gesture: two samples on one target send once", "[bed_plate_gesture]") {
    Recorder r;
    auto g = r.make();
    REQUIRE(g.press(100, 100));
    CHECK(g.pressing(120, 100));
    CHECK_FALSE(g.pressing(120, 100));
    CHECK(r.sent.size() == 1);
    CHECK(g.pressing(130, 100));
}

TEST_CASE("plate gesture: one lift per gesture, stamped on every target", "[bed_plate_gesture]") {
    Recorder r;
    r.lift_result = {PlateLift::Kind::Lift, 43.0};
    auto g = r.make();
    REQUIRE(g.press(100, 100));
    g.pressing(120, 100);
    g.pressing(140, 100);
    g.release(160, 100);
    REQUIRE(r.sent.size() == 3);
    CHECK(r.lift_calls == 1);
    for (const auto& t : r.sent) {
        REQUIRE(t.z.has_value());
        CHECK(*t.z == 43.0);
    }

    // The next gesture decides its own lift.
    r.lift_result = {PlateLift::Kind::None, 0.0};
    REQUIRE(g.press(100, 100));
    g.pressing(120, 120);
    CHECK(r.lift_calls == 2);
    REQUIRE(r.sent.size() == 4);
    CHECK_FALSE(r.sent[3].z.has_value());
}

TEST_CASE("plate gesture: a refused lift sends nothing for the whole gesture",
          "[bed_plate_gesture]") {
    Recorder r;
    r.lift_result = {PlateLift::Kind::Refuse, 0.0};
    auto g = r.make();
    REQUIRE(g.press(100, 100));
    g.pressing(120, 100);
    g.pressing(140, 100);
    g.release(160, 100);
    CHECK(r.sent.empty());
    CHECK(r.refused_calls == 1);
    CHECK(r.lift_calls == 1);
}

TEST_CASE("plate gesture: a plate that may not move ignores the gesture", "[bed_plate_gesture]") {
    Recorder r;
    r.can_move = false;
    auto g = r.make();
    CHECK_FALSE(g.press(100, 100));
    CHECK_FALSE(g.active());
    g.pressing(140, 100);
    const auto rel = g.release(140, 100);
    CHECK(r.sent.empty());
    CHECK_FALSE(rel.tap_target.has_value());
    CHECK_FALSE(rel.was_drag);
}

TEST_CASE("plate gesture: motion becoming blocked mid-drag ends the stream",
          "[bed_plate_gesture]") {
    Recorder r;
    auto g = r.make();
    REQUIRE(g.press(100, 100));
    g.pressing(120, 100);
    REQUIRE(r.sent.size() == 1);
    r.can_move = false; // a print started, or another operation took the head
    g.pressing(140, 100);
    r.can_move = true;
    g.pressing(160, 100);
    g.release(180, 100);
    CHECK(r.sent.size() == 1);
}

TEST_CASE("plate gesture: a failed send stops further sends", "[bed_plate_gesture]") {
    Recorder r;
    auto g = r.make();
    REQUIRE(g.press(100, 100));
    g.pressing(120, 100);
    REQUIRE(r.sent.size() == 1);
    g.mark_failed();
    g.pressing(140, 100);
    g.release(160, 100);
    CHECK(r.sent.size() == 1);

    // The next press starts clean.
    REQUIRE(g.press(100, 100));
    g.pressing(120, 120);
    CHECK(r.sent.size() == 2);
}

TEST_CASE("plate gesture: a drag release sends only a point not already sent",
          "[bed_plate_gesture]") {
    Recorder r;
    auto g = r.make();
    REQUIRE(g.press(100, 100));
    g.pressing(120, 100);
    auto rel = g.release(120, 100);
    CHECK(rel.was_drag);
    CHECK_FALSE(rel.tap_target.has_value());
    CHECK(r.sent.size() == 1);

    REQUIRE(g.press(100, 100));
    g.pressing(120, 100);
    rel = g.release(150, 100);
    REQUIRE(r.sent.size() == 3);
    CHECK(*r.sent[2].x == 150.0);
}

TEST_CASE("plate gesture: a lost press sends nothing", "[bed_plate_gesture]") {
    Recorder r;
    auto g = r.make();
    REQUIRE(g.press(100, 100));
    g.lost();
    CHECK_FALSE(g.active());
    const auto rel = g.release(100, 100);
    CHECK_FALSE(rel.tap_target.has_value());
    CHECK(r.sent.empty());
}

TEST_CASE("plate gesture: send_once asks can_move and lift every call", "[bed_plate_gesture]") {
    Recorder r;
    r.lift_result = {PlateLift::Kind::Lift, 12.0};
    auto g = r.make();
    AxisTarget t;
    t.x = 10.0;
    t.y = 20.0;
    CHECK(g.send_once(t));
    CHECK(g.send_once(t));
    CHECK(r.lift_calls == 2);
    CHECK(r.can_move_calls == 2);
    REQUIRE(r.sent.size() == 2);
    CHECK(*r.sent[1].z == 12.0);

    r.lift_result = {PlateLift::Kind::Refuse, 0.0};
    CHECK_FALSE(g.send_once(t));
    CHECK(r.refused_calls == 1);
    r.can_move = false;
    CHECK_FALSE(g.send_once(t));
    CHECK(r.sent.size() == 2);
    CHECK(r.lift_calls == 3);
}
