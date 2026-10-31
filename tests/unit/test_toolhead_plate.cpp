// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_toolhead_plate.cpp
 * @brief plan_toolhead_plate(): where the plate, crosshair and Z bar land in the canvas.
 *
 * Run with: ./build/bin/helix-tests "[toolhead][plate]"
 */

#include "src/ui/panel_widgets/toolhead_plate.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using Catch::Approx;

namespace {

ToolheadPlateInput square_bed(int box_w, int box_h) {
    ToolheadPlateInput in;
    in.box_w = box_w;
    in.box_h = box_h;
    in.inset_px = 2;
    in.bar_w_px = 6;
    in.gap_px = 4;
    in.bed = {200.0f, 200.0f, 0.0f, 0.0f};
    in.homed = true;
    in.has_z_range = true;
    in.z_min_mm = 0.0f;
    in.z_max_mm = 100.0f;
    return in;
}

} // namespace

TEST_CASE("toolhead plate: the plate keeps the bed's aspect and the Z bar stands beside it",
          "[toolhead][plate]") {
    // A wide canvas: the plate is as tall as the box allows, square for a square bed.
    auto in = square_bed(200, 100);
    const auto g = plan_toolhead_plate(in);
    REQUIRE(g.has_room);
    CHECK(g.plate.w == Approx(96.0f));
    CHECK(g.plate.h == Approx(96.0f));
    CHECK(g.z_bar.x == Approx(g.plate.x + g.plate.w + 4.0f));
    CHECK(g.z_bar.h == Approx(g.plate.h));
    CHECK(g.z_bar.w == Approx(6.0f));
    // The plate and the bar are centred as one group.
    CHECK(g.plate.x + (g.z_bar.x + g.z_bar.w - g.plate.x) / 2.0f == Approx(100.0f));

    // A rectangular bed keeps its ratio.
    in.bed = {300.0f, 150.0f, 0.0f, 0.0f};
    const auto wide = plan_toolhead_plate(in);
    CHECK(wide.plate.w == Approx(wide.plate.h * 2.0f));
}

TEST_CASE("toolhead plate: a canvas shared with the values packs the plate to the left",
          "[toolhead][plate]") {
    auto in = square_bed(200, 100);
    in.align_start = true;
    const auto g = plan_toolhead_plate(in);
    CHECK(g.plate.x == Approx(2.0f));
    CHECK(g.z_bar.x == Approx(g.plate.x + g.plate.w + 4.0f));
    // The crosshair travels with the plate.
    CHECK(g.cross_x == Approx(g.plate.x));
}

TEST_CASE("toolhead plate: the crosshair follows the position with Y flipped",
          "[toolhead][plate]") {
    auto in = square_bed(200, 100);
    const auto origin = plan_toolhead_plate(in);
    REQUIRE(origin.show_crosshair);
    CHECK(origin.cross_x == Approx(origin.plate.x));
    CHECK(origin.cross_y == Approx(origin.plate.y + origin.plate.h));

    in.x_mm = 200.0f;
    in.y_mm = 200.0f;
    const auto corner = plan_toolhead_plate(in);
    CHECK(corner.cross_x == Approx(corner.plate.x + corner.plate.w));
    CHECK(corner.cross_y == Approx(corner.plate.y));

    in.x_mm = 100.0f;
    in.y_mm = 100.0f;
    const auto middle = plan_toolhead_plate(in);
    CHECK(middle.cross_x == Approx(middle.plate.x + middle.plate.w / 2.0f));
    CHECK(middle.cross_y == Approx(middle.plate.y + middle.plate.h / 2.0f));
}

TEST_CASE("toolhead plate: a position off the bed is held on its edge", "[toolhead][plate]") {
    auto in = square_bed(200, 100);
    in.x_mm = -50.0f;
    in.y_mm = 999.0f;
    const auto g = plan_toolhead_plate(in);
    CHECK(g.cross_x == Approx(g.plate.x));
    CHECK(g.cross_y == Approx(g.plate.y));
}

TEST_CASE("toolhead plate: a centre-origin bed puts the origin mid-plate", "[toolhead][plate]") {
    auto in = square_bed(200, 100);
    in.bed = {200.0f, 200.0f, -100.0f, -100.0f};
    in.circular = true;
    const auto g = plan_toolhead_plate(in);
    CHECK(g.circular);
    CHECK(g.cross_x == Approx(g.plate.x + g.plate.w / 2.0f));
    CHECK(g.cross_y == Approx(g.plate.y + g.plate.h / 2.0f));
}

TEST_CASE("toolhead plate: an unhomed toolhead draws no crosshair and an empty bar",
          "[toolhead][plate]") {
    auto in = square_bed(200, 100);
    in.homed = false;
    in.z_mm = 50.0f;
    const auto g = plan_toolhead_plate(in);
    CHECK(g.has_room);
    CHECK_FALSE(g.show_crosshair);
    CHECK(g.z_fraction == Approx(0.0f));
}

TEST_CASE("toolhead plate: Z fills the bar, clamped, from the side the kinematics set",
          "[toolhead][plate]") {
    auto in = square_bed(200, 100);
    in.z_mm = 25.0f;
    CHECK(plan_toolhead_plate(in).z_fraction == Approx(0.25f));
    in.z_mm = 400.0f;
    CHECK(plan_toolhead_plate(in).z_fraction == Approx(1.0f));
    in.z_mm = -3.0f;
    CHECK(plan_toolhead_plate(in).z_fraction == Approx(0.0f));

    CHECK_FALSE(plan_toolhead_plate(in).fill_from_top);
    in.bed_moves = true;
    CHECK(plan_toolhead_plate(in).fill_from_top);

    in.has_z_range = false;
    in.z_mm = 25.0f;
    CHECK(plan_toolhead_plate(in).z_fraction == Approx(0.0f));
}

TEST_CASE("toolhead plate: a canvas with no room for the plate draws nothing",
          "[toolhead][plate]") {
    CHECK_FALSE(plan_toolhead_plate(square_bed(12, 100)).has_room);
    CHECK_FALSE(plan_toolhead_plate(square_bed(200, 4)).has_room);
    CHECK(plan_toolhead_plate(square_bed(40, 40)).has_room);
}
