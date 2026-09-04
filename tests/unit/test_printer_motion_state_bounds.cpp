// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Covers PrinterMotionState parsing of toolhead.axis_minimum / axis_maximum
// into the AxisBounds struct exposed via get_axis_bounds(). The bounds feed
// the jog-clamp soft-stop in MotionPanel::jog().

#include "../helix_test_fixture.h"
#include "printer_motion_state.h"

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using helix::AxisBounds;
using helix::PrinterMotionState;
using nlohmann::json;

namespace {

// LVGL-free harness — PrinterMotionState's bounds parsing path doesn't touch
// any subjects, so we don't need init_subjects() to exercise it.
class BoundsFixture : public HelixTestFixture {
  public:
    PrinterMotionState state;
};

} // namespace

TEST_CASE_METHOD(BoundsFixture, "AxisBounds defaults to all-unset", "[motion][bounds]") {
    AxisBounds b = state.get_axis_bounds();
    REQUIRE_FALSE(b.has_x);
    REQUIRE_FALSE(b.has_y);
    REQUIRE_FALSE(b.has_z);
}

TEST_CASE_METHOD(BoundsFixture, "AxisBounds populates from toolhead.axis_min/max",
                 "[motion][bounds]") {
    state.init_subjects(false); // no XML registration in test

    json status = {
        {"toolhead",
         {{"axis_minimum", {0.0, 0.0, 0.0, 0.0}}, {"axis_maximum", {235.0, 235.0, 250.0, 0.0}}}}};
    state.update_from_status(status);

    AxisBounds b = state.get_axis_bounds();
    REQUIRE(b.has_x);
    REQUIRE(b.has_y);
    REQUIRE(b.has_z);
    REQUIRE(b.x_min == Catch::Approx(0.0f));
    REQUIRE(b.x_max == Catch::Approx(235.0f));
    REQUIRE(b.y_min == Catch::Approx(0.0f));
    REQUIRE(b.y_max == Catch::Approx(235.0f));
    REQUIRE(b.z_min == Catch::Approx(0.0f));
    REQUIRE(b.z_max == Catch::Approx(250.0f));
}

TEST_CASE_METHOD(BoundsFixture, "AxisBounds ignores arrays shorter than 3 entries",
                 "[motion][bounds]") {
    state.init_subjects(false);
    json status = {{"toolhead", {{"axis_minimum", {0.0}}, {"axis_maximum", {235.0, 235.0}}}}};
    state.update_from_status(status);
    AxisBounds b = state.get_axis_bounds();
    REQUIRE_FALSE(b.has_x);
    REQUIRE_FALSE(b.has_y);
    REQUIRE_FALSE(b.has_z);
}

TEST_CASE_METHOD(BoundsFixture, "AxisBounds reset on deinit (reconnect path)", "[motion][bounds]") {
    state.init_subjects(false);
    state.update_from_status({{"toolhead",
                               {{"axis_minimum", {-10.0, -10.0, 0.0, 0.0}},
                                {"axis_maximum", {300.0, 300.0, 400.0, 0.0}}}}});
    REQUIRE(state.get_axis_bounds().has_x);

    state.deinit_subjects();
    AxisBounds b = state.get_axis_bounds();
    REQUIRE_FALSE(b.has_x);
    REQUIRE_FALSE(b.has_y);
    REQUIRE_FALSE(b.has_z);
    REQUIRE(b.x_max == Catch::Approx(0.0f)); // explicit zero-out, not stale
}

TEST_CASE_METHOD(BoundsFixture, "AxisBounds tolerates negative envelope (delta/coreXY origins)",
                 "[motion][bounds]") {
    state.init_subjects(false);
    state.update_from_status({{"toolhead",
                               {{"axis_minimum", {-150.0, -150.0, 0.0, 0.0}},
                                {"axis_maximum", {150.0, 150.0, 350.0, 0.0}}}}});
    AxisBounds b = state.get_axis_bounds();
    REQUIRE(b.x_min == Catch::Approx(-150.0f));
    REQUIRE(b.x_max == Catch::Approx(150.0f));
}
