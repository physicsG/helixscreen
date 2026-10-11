// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "jog_coalescer.h"
#include "moonraker_motion_api.h"
#include "motion_presets.h"

#include <iterator>
#include <string>

#include "../catch_amalgamated.hpp"

using helix::AxisBounds;
using helix::circular_bed_kinematics;
using helix::motion_preset_target;
using helix::MotionPreset;

namespace {

AxisBounds known_bounds(float x_min, float x_max, float y_min, float y_max) {
    AxisBounds b;
    b.x_min = x_min;
    b.x_max = x_max;
    b.y_min = y_min;
    b.y_max = y_max;
    b.has_x = true;
    b.has_y = true;
    b.has_z = true;
    return b;
}

struct PresetPoint {
    MotionPreset preset;
    double x;
    double y;
};

/// Assert every preset lands on its exact position and never commands Z.
void check_grid(const PresetPoint* points, size_t count, const AxisBounds& bounds, bool circular) {
    for (size_t i = 0; i < count; ++i) {
        const auto target = motion_preset_target(points[i].preset, bounds, circular);
        CAPTURE(i, circular);
        REQUIRE(target.has_value());
        CHECK(target->x == Catch::Approx(points[i].x).margin(1e-4));
        CHECK(target->y == Catch::Approx(points[i].y).margin(1e-4));
        CHECK_FALSE(target->z.has_value());
    }
}

constexpr double CORNER_REACH_100 = 63.63961030678928; // 90 * sqrt(0.5) on r=100

} // namespace

TEST_CASE("motion presets cover a 235x235 bed", "[motion][presets]") {
    const auto bounds = known_bounds(0, 235, 0, 235);
    // Edge presets sit 10% of the 235 mm span (23.5 mm) in from the edge.
    const PresetPoint grid[] = {
        {MotionPreset::RearLeft, 23.5, 211.5},   {MotionPreset::Rear, 117.5, 211.5},
        {MotionPreset::RearRight, 211.5, 211.5}, {MotionPreset::Left, 23.5, 117.5},
        {MotionPreset::Center, 117.5, 117.5},    {MotionPreset::Right, 211.5, 117.5},
        {MotionPreset::FrontLeft, 23.5, 23.5},   {MotionPreset::Front, 117.5, 23.5},
        {MotionPreset::FrontRight, 211.5, 23.5},
    };
    check_grid(grid, std::size(grid), bounds, false);
}

TEST_CASE("motion presets on a centre-origin bed", "[motion][presets]") {
    const auto bounds = known_bounds(-100, 100, -100, 100);
    // 10% of the 200 mm span is 20 mm in from each edge.
    const PresetPoint grid[] = {
        {MotionPreset::RearLeft, -80, 80},   {MotionPreset::Rear, 0, 80},
        {MotionPreset::RearRight, 80, 80},   {MotionPreset::Left, -80, 0},
        {MotionPreset::Center, 0, 0},        {MotionPreset::Right, 80, 0},
        {MotionPreset::FrontLeft, -80, -80}, {MotionPreset::Front, 0, -80},
        {MotionPreset::FrontRight, 80, -80},
    };
    check_grid(grid, std::size(grid), bounds, false);
}

TEST_CASE("motion presets on a circular bed sit on the inscribed circle", "[motion][presets]") {
    const auto bounds = known_bounds(-100, 100, -100, 100);
    // Rim presets at 90% of the 100 mm radius; diagonals at 45 degrees, not
    // on the bounding square's corners where the round bed has ended.
    const PresetPoint grid[] = {
        {MotionPreset::RearLeft, -CORNER_REACH_100, CORNER_REACH_100},
        {MotionPreset::Rear, 0, 90},
        {MotionPreset::RearRight, CORNER_REACH_100, CORNER_REACH_100},
        {MotionPreset::Left, -90, 0},
        {MotionPreset::Center, 0, 0},
        {MotionPreset::Right, 90, 0},
        {MotionPreset::FrontLeft, -CORNER_REACH_100, -CORNER_REACH_100},
        {MotionPreset::Front, 0, -90},
        {MotionPreset::FrontRight, CORNER_REACH_100, -CORNER_REACH_100},
    };
    check_grid(grid, std::size(grid), bounds, true);

    // The circular flag is the only difference: the same bounds on a
    // rectangular reading put Front at the inset row, not at 90% radius.
    const auto front = motion_preset_target(MotionPreset::Front, bounds, false);
    REQUIRE(front.has_value());
    CHECK(front->y == Catch::Approx(-80.0).margin(1e-4));
}

TEST_CASE("motion presets refuse unknown or degenerate bounds", "[motion][presets]") {
    AxisBounds unknown_x = known_bounds(0, 235, 0, 235);
    unknown_x.has_x = false;
    CHECK_FALSE(motion_preset_target(MotionPreset::Center, unknown_x, false).has_value());
    CHECK_FALSE(motion_preset_target(MotionPreset::RearRight, unknown_x, true).has_value());

    AxisBounds unknown_y = known_bounds(0, 235, 0, 235);
    unknown_y.has_y = false;
    CHECK_FALSE(motion_preset_target(MotionPreset::Front, unknown_y, false).has_value());

    AxisBounds degenerate = known_bounds(100, 100, 0, 235);
    CHECK_FALSE(motion_preset_target(MotionPreset::Center, degenerate, false).has_value());

    AxisBounds inverted = known_bounds(200, 100, 0, 235);
    CHECK_FALSE(motion_preset_target(MotionPreset::Center, inverted, true).has_value());
}

TEST_CASE("circular_bed_kinematics names the round-bed machines", "[motion][presets]") {
    CHECK(circular_bed_kinematics("delta"));
    CHECK(circular_bed_kinematics("rotary_delta"));
    CHECK_FALSE(circular_bed_kinematics("cartesian"));
    CHECK_FALSE(circular_bed_kinematics("corexy"));
    CHECK_FALSE(circular_bed_kinematics(""));
}

TEST_CASE("motion presets aim at the plate, not axis overtravel", "[motion][presets]") {
    // Snapmaker U1: Y travels to 335 but the plate ends at 270; the tool docks
    // fill the rest. homing_origin (-0.088928, -0.016043) shifts G-code space.
    const AxisBounds machine = known_bounds(0, 271, 0, 335);
    const AxisBounds gcode = known_bounds(0.088928f, 271.088928f, 0.016043f, 335.016043f);
    BuildVolume vol;
    vol.plate_x_min = 3;
    vol.plate_x_max = 267;
    vol.plate_y_min = 3;
    vol.plate_y_max = 267;

    const AxisBounds area = helix::preset_area(machine, gcode, vol);
    CHECK(area.x_min == Catch::Approx(3.088928).margin(1e-4));
    CHECK(area.x_max == Catch::Approx(267.088928).margin(1e-4));
    CHECK(area.y_min == Catch::Approx(3.016043).margin(1e-4));
    CHECK(area.y_max == Catch::Approx(267.016043).margin(1e-4));

    const auto rear_left = motion_preset_target(MotionPreset::RearLeft, area, false);
    REQUIRE(rear_left.has_value());
    CHECK(*rear_left->x == Catch::Approx(29.488928).margin(1e-4));
    CHECK(*rear_left->y == Catch::Approx(240.616043).margin(1e-4));
}

TEST_CASE("preset area falls back to travel and never exceeds it", "[motion][presets]") {
    const AxisBounds travel = known_bounds(0, 235, 0, 235);

    SECTION("no plate declared") {
        const AxisBounds area = helix::preset_area(travel, travel, BuildVolume{});
        CHECK(area.x_min == 0.0f);
        CHECK(area.x_max == 235.0f);
        CHECK(area.y_max == 235.0f);
    }

    SECTION("plate wider than travel is clipped to it") {
        BuildVolume vol;
        vol.plate_x_min = -10;
        vol.plate_x_max = 250;
        vol.plate_y_min = 5;
        vol.plate_y_max = 230;
        const AxisBounds area = helix::preset_area(travel, travel, vol);
        CHECK(area.x_min == 0.0f);
        CHECK(area.x_max == 235.0f);
        CHECK(area.y_min == 5.0f);
        CHECK(area.y_max == 230.0f);
    }

    SECTION("unknown travel stays unknown") {
        AxisBounds unknown = travel;
        unknown.has_y = false;
        BuildVolume vol;
        vol.plate_x_min = 3;
        vol.plate_x_max = 200;
        vol.plate_y_min = 3;
        vol.plate_y_max = 200;
        CHECK_FALSE(helix::preset_area(unknown, unknown, vol).has_y);
    }
}

TEST_CASE("plate_rear_park centres in X and sits inside the rear of the plate",
          "[motion][presets]") {
    const auto park = helix::plate_rear_park(known_bounds(0, 235, 0, 235));
    REQUIRE(park.has_value());
    CHECK(*park->x == Catch::Approx(117.5));
    CHECK(*park->y == Catch::Approx(225.0));
    CHECK_FALSE(park->z.has_value());

    // Never past the plate, even when it is shallower than the margin allows.
    const auto shallow = helix::plate_rear_park(known_bounds(0, 100, 50, 60));
    REQUIRE(shallow.has_value());
    CHECK(*shallow->y == Catch::Approx(55.0));

    AxisBounds unknown = known_bounds(0, 235, 0, 235);
    unknown.has_y = false;
    CHECK_FALSE(helix::plate_rear_park(unknown).has_value());
}

TEST_CASE("bed map touch maps to the point under the finger", "[motion][bed_map]") {
    // 300x150 viewport over a 200x100 plate: scale 1.5, no letterbox.
    const AxisBounds area = known_bounds(10, 210, 20, 120);
    const auto mapper = helix::bed_map_mapper(area, 300, 150);

    const auto top_left = helix::bed_map_target(0, 0, mapper, area, false);
    REQUIRE(top_left.has_value());
    CHECK(*top_left->x == Catch::Approx(10.0));
    CHECK(*top_left->y == Catch::Approx(120.0)); // top of the view is the rear
    CHECK_FALSE(top_left->z.has_value());

    const auto mid = helix::bed_map_target(150, 75, mapper, area, false);
    REQUIRE(mid.has_value());
    CHECK(*mid->x == Catch::Approx(110.0));
    CHECK(*mid->y == Catch::Approx(70.0));

    const auto front_right = helix::bed_map_target(300, 150, mapper, area, false);
    REQUIRE(front_right.has_value());
    CHECK(*front_right->x == Catch::Approx(210.0));
    CHECK(*front_right->y == Catch::Approx(20.0));
}

TEST_CASE("bed map touch outside the plate clamps to its edge", "[motion][bed_map]") {
    // Square plate in a wide viewport: 100px letterbox each side.
    const AxisBounds area = known_bounds(0, 200, 0, 200);
    const auto mapper = helix::bed_map_mapper(area, 400, 200);

    const auto left_margin = helix::bed_map_target(10, 100, mapper, area, false);
    REQUIRE(left_margin.has_value());
    CHECK(*left_margin->x == Catch::Approx(0.0));
    CHECK(*left_margin->y == Catch::Approx(100.0));

    const auto off_corner = helix::bed_map_target(-50, 500, mapper, area, false);
    REQUIRE(off_corner.has_value());
    CHECK(*off_corner->x == Catch::Approx(0.0));
    CHECK(*off_corner->y == Catch::Approx(0.0));
}

TEST_CASE("bed map on a circular bed clamps to the inscribed radius", "[motion][bed_map]") {
    // Delta: centre origin, radius 100.
    const AxisBounds area = known_bounds(-100, 100, -100, 100);
    const auto mapper = helix::bed_map_mapper(area, 200, 200);

    // The view's top-right corner is (100, 100): outside the round bed, so it
    // pulls in along the diagonal to the rim.
    const auto corner = helix::bed_map_target(200, 0, mapper, area, true);
    REQUIRE(corner.has_value());
    CHECK(*corner->x == Catch::Approx(70.7106781).margin(1e-4));
    CHECK(*corner->y == Catch::Approx(70.7106781).margin(1e-4));

    // Inside the radius nothing moves.
    const auto inside = helix::bed_map_target(150, 100, mapper, area, true);
    REQUIRE(inside.has_value());
    CHECK(*inside->x == Catch::Approx(50.0));
    CHECK(*inside->y == Catch::Approx(0.0));

    // The same corner on a rectangular bed reaches the corner.
    const auto square = helix::bed_map_target(200, 0, mapper, area, false);
    REQUIRE(square.has_value());
    CHECK(*square->x == Catch::Approx(100.0));
    CHECK(*square->y == Catch::Approx(100.0));
}

TEST_CASE("bed map refuses unknown plate bounds", "[motion][bed_map]") {
    AxisBounds area = known_bounds(0, 200, 0, 200);
    area.has_x = false;
    const auto mapper = helix::bed_map_mapper(known_bounds(0, 200, 0, 200), 200, 200);
    CHECK_FALSE(helix::bed_map_target(100, 100, mapper, area, false).has_value());
}

TEST_CASE("bed map lift rises to clearance only from below it", "[motion][bed_map]") {
    const auto lift = helix::bed_map_lift_z(0.2, 5.0, 250.0);
    REQUIRE(lift.has_value());
    CHECK(*lift == Catch::Approx(5.0));

    CHECK_FALSE(helix::bed_map_lift_z(5.0, 5.0, 250.0).has_value());
    CHECK_FALSE(helix::bed_map_lift_z(12.0, 5.0, 250.0).has_value());

    // A clearance above travel is capped there.
    const auto capped = helix::bed_map_lift_z(1.0, 5.0, 3.0);
    REQUIRE(capped.has_value());
    CHECK(*capped == Catch::Approx(3.0));
    CHECK_FALSE(helix::bed_map_lift_z(3.0, 5.0, 3.0).has_value());
}

TEST_CASE("bed map lift completes before XY travel, even behind an in-flight move",
          "[motion][bed_map]") {
    // A tap's lift rides in the same target as its XY, so a later tap
    // replacing it in the coalescer replaces both. A drag's later samples
    // replace it too, which is why the panel stamps the gesture's lift on
    // every one of them (test_motion_move_tab.cpp).
    helix::JogCoalescer coalescer;
    helix::AxisTarget first;
    first.x = 10.0;
    first.y = 10.0;
    first.z = 5.0;
    REQUIRE(coalescer.on_target(first).has_value());

    helix::AxisTarget second;
    second.x = 150.0;
    second.y = 80.0;
    second.z = *helix::bed_map_lift_z(0.2, 5.0, 250.0);
    CHECK_FALSE(coalescer.on_target(second).has_value());
    const auto flushed = coalescer.on_ack();
    REQUIRE(flushed.has_value());
    const auto* sent = std::get_if<helix::AxisTarget>(&*flushed);
    REQUIRE(sent != nullptr);
    REQUIRE(sent->z.has_value());

    // From Z 0.2 the script lifts on its own line, then travels.
    const std::string gcode =
        MoonrakerMotionAPI::generate_absolute_move_gcode(*sent, 6000.0, 600.0, 0.2);
    const auto z_line = gcode.find("Z5");
    const auto xy_line = gcode.find("X150");
    REQUIRE(z_line != std::string::npos);
    REQUIRE(xy_line != std::string::npos);
    CHECK(z_line < xy_line);
    CHECK(gcode.substr(xy_line).find('Z') == std::string::npos);
}

TEST_CASE("bed map guides span the plate, or the chord of a round one", "[motion][bed_map]") {
    // Rectangular: edge to edge wherever the line sits.
    CHECK(helix::bed_map_guide_half_span(0.0f, 100.0f, false) == Catch::Approx(100.0f));
    CHECK(helix::bed_map_guide_half_span(80.0f, 100.0f, false) == Catch::Approx(100.0f));
    // Circular: the chord at that offset from the centre.
    CHECK(helix::bed_map_guide_half_span(0.0f, 100.0f, true) == Catch::Approx(100.0f));
    CHECK(helix::bed_map_guide_half_span(60.0f, 100.0f, true) == Catch::Approx(80.0f));
    CHECK(helix::bed_map_guide_half_span(-60.0f, 100.0f, true) == Catch::Approx(80.0f));
    // A head off the round plate draws no guide rather than a NaN.
    CHECK(helix::bed_map_guide_half_span(120.0f, 100.0f, true) == 0.0f);
}
