// SPDX-License-Identifier: GPL-3.0-or-later
#include "../test_helpers/script_lines.h"
#include "belt_settle.h"

#include <cmath>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix::calibration;

namespace {

BeltSettleInputs square_bed(float side, std::array<float, 3> probe, float max_velocity) {
    BeltSettleInputs in;
    in.x_min = 0.0f;
    in.x_max = side;
    in.y_min = 0.0f;
    in.y_max = side;
    in.has_xy_limits = true;
    in.probe_point = probe;
    in.max_velocity_mm_s = max_velocity;
    return in;
}

} // namespace

TEST_CASE("a 350 mm Voron at 500 mm/s max velocity laps in about 13 seconds", "[belt][settle]") {
    const auto plan = plan_belt_settle(square_bed(350.0f, {175.0f, 175.0f, 20.0f}, 500.0f));
    REQUIRE(plan.refusal == BeltSettleRefusal::NONE);
    CHECK(plan.speed_mm_s == Catch::Approx(300.0f));
    CHECK(plan.inset_x_mm == Catch::Approx(24.5f));
    CHECK(plan.inset_y_mm == Catch::Approx(24.5f));
    // Three 1204 mm laps plus the 212.8 mm diagonal back to the probe point.
    CHECK(plan.seconds == Catch::Approx(12.75f).margin(0.05f));
    CHECK(plan.timeout_ms == 60000u);

    const std::string lap = "G1 X325.5 Y24.5\n"
                            "G1 X325.5 Y325.5\n"
                            "G1 X24.5 Y325.5\n"
                            "G1 X24.5 Y24.5\n";
    const std::string expected = "SAVE_GCODE_STATE NAME=helix_belt_settle\n"
                                 "G90\n"
                                 "G1 Z20.0 F18000\n"
                                 "G1 X24.5 Y24.5 F18000\n" +
                                 lap + lap + lap +
                                 "G1 X175.0 Y175.0\n"
                                 "M400\n"
                                 "RESTORE_GCODE_STATE NAME=helix_belt_settle";
    CHECK(plan.gcode == expected);
}

TEST_CASE("a Voron 2.4 300 laps 21 mm inside its 300x305 travel at 300 mm/s", "[belt][settle]") {
    BeltSettleInputs in = square_bed(300.0f, {150.0f, 152.5f, 20.0f}, 500.0f);
    in.y_max = 305.0f;
    const auto plan = plan_belt_settle(in);
    REQUIRE(plan.refusal == BeltSettleRefusal::NONE);
    CHECK(plan.speed_mm_s == Catch::Approx(300.0f));
    CHECK(plan.inset_x_mm == Catch::Approx(21.0f));
    CHECK(plan.inset_y_mm == Catch::Approx(21.35f));
    const auto lines = helix::test::script_lines(plan.gcode);
    REQUIRE(lines.size() == 4 + 4 * belt_settle::kLaps + 3);
    CHECK(lines[3] == "G1 X21.0 Y21.4 F18000");
    CHECK(lines[5] == "G1 X279.0 Y283.6");
}

TEST_CASE("a K1C's 229x227 travel takes the 20 mm minimum inset", "[belt][settle]") {
    BeltSettleInputs in = square_bed(229.0f, {114.5f, 113.5f, 20.0f}, 500.0f);
    in.y_max = 227.0f;
    const auto plan = plan_belt_settle(in);
    REQUIRE(plan.refusal == BeltSettleRefusal::NONE);
    CHECK(plan.inset_x_mm == Catch::Approx(20.0f));
    CHECK(plan.inset_y_mm == Catch::Approx(20.0f));
    CHECK(plan.speed_mm_s == Catch::Approx(300.0f));
    const auto lines = helix::test::script_lines(plan.gcode);
    REQUIRE(lines.size() >= 6);
    CHECK(lines[3] == "G1 X20.0 Y20.0 F18000");
    CHECK(lines[5] == "G1 X209.0 Y207.0");
}

TEST_CASE("speed is 0.6 x max_velocity below 500", "[belt][settle]") {
    const auto plan = plan_belt_settle(square_bed(350.0f, {175.0f, 175.0f, 20.0f}, 200.0f));
    REQUIRE(plan.refusal == BeltSettleRefusal::NONE);
    CHECK(plan.speed_mm_s == Catch::Approx(120.0f));
    const auto lines = helix::test::script_lines(plan.gcode);
    REQUIRE(lines.size() >= 4);
    CHECK(lines[2] == "G1 Z20.0 F7200");
    CHECK(lines[3] == "G1 X24.5 Y24.5 F7200");
}

TEST_CASE("the inset is 20 mm on a small bed", "[belt][settle]") {
    const auto plan = plan_belt_settle(square_bed(120.0f, {60.0f, 60.0f, 20.0f}, 500.0f));
    REQUIRE(plan.refusal == BeltSettleRefusal::NONE);
    CHECK(plan.inset_x_mm == Catch::Approx(20.0f));
    CHECK(plan.inset_y_mm == Catch::Approx(20.0f));
    const auto lines = helix::test::script_lines(plan.gcode);
    REQUIRE(lines.size() >= 6);
    CHECK(lines[3] == "G1 X20.0 Y20.0 F18000");
    CHECK(lines[5] == "G1 X100.0 Y100.0");
}

TEST_CASE("a homing origin shifts every coordinate", "[belt][settle]") {
    BeltSettleInputs in = square_bed(350.0f, {175.0f, 175.0f, 20.0f}, 500.0f);
    in.origin_x = 5.0f;
    in.origin_y = -3.0f;
    in.origin_z = 0.2f;
    const auto plan = plan_belt_settle(in);
    REQUIRE(plan.refusal == BeltSettleRefusal::NONE);
    const auto lines = helix::test::script_lines(plan.gcode);
    REQUIRE(lines.size() == 4 + 4 * belt_settle::kLaps + 3);
    CHECK(lines[2] == "G1 Z19.8 F18000");
    CHECK(lines[3] == "G1 X19.5 Y27.5 F18000");
    CHECK(lines[lines.size() - 3] == "G1 X170.0 Y178.0");
}

TEST_CASE("settling refuses, and sends nothing, when it cannot plan", "[belt][settle]") {
    const auto good = square_bed(350.0f, {175.0f, 175.0f, 20.0f}, 500.0f);
    REQUIRE(plan_belt_settle(good).refusal == BeltSettleRefusal::NONE);

    auto expect_refusal = [](const BeltSettleInputs& in, BeltSettleRefusal want) {
        const auto plan = plan_belt_settle(in);
        CHECK(plan.refusal == want);
        CHECK(plan.gcode.empty());
    };

    SECTION("no axis limits") {
        auto in = good;
        in.has_xy_limits = false;
        expect_refusal(in, BeltSettleRefusal::NO_AXIS_LIMITS);
    }
    SECTION("no probe point") {
        auto in = good;
        in.probe_point.reset();
        expect_refusal(in, BeltSettleRefusal::NO_PROBE_POINT);
    }
    SECTION("no max velocity") {
        auto in = good;
        in.max_velocity_mm_s = 0.0f;
        expect_refusal(in, BeltSettleRefusal::NO_VELOCITY);
    }
    SECTION("a 50 mm bed leaves a 10 mm side") {
        expect_refusal(square_bed(50.0f, {25.0f, 25.0f, 20.0f}, 500.0f),
                       BeltSettleRefusal::SPAN_TOO_SMALL);
    }
    SECTION("refusals are checked in enum order") {
        auto in = good;
        in.has_xy_limits = false;
        in.probe_point.reset();
        in.max_velocity_mm_s = 0.0f;
        expect_refusal(in, BeltSettleRefusal::NO_AXIS_LIMITS);
    }
}

TEST_CASE("a long lap raises the timeout", "[belt][settle]") {
    const auto plan = plan_belt_settle(square_bed(1000.0f, {500.0f, 500.0f, 20.0f}, 100.0f));
    REQUIRE(plan.refusal == BeltSettleRefusal::NONE);
    CHECK(plan.speed_mm_s == Catch::Approx(60.0f));
    // Inset 70: three 3440 mm laps plus the 430 mm-square diagonal home.
    const double expected_s = (3.0 * 4.0 * 860.0 + 430.0 * std::sqrt(2.0)) / 60.0;
    CHECK(plan.seconds == Catch::Approx(expected_s).epsilon(1e-4));
    REQUIRE(plan.seconds > 15.0f);
    const double want_ms = 4.0 * static_cast<double>(plan.seconds) * 1000.0;
    CHECK(static_cast<double>(plan.timeout_ms) >= want_ms);
    CHECK(static_cast<double>(plan.timeout_ms) < want_ms + 1.0);
}

TEST_CASE("a 4-tool printer laps the central half, clear of its docks", "[belt][settle]") {
    // A U1-sized 270 mm travel: a quarter of each span either side.
    auto in = square_bed(270.0f, {135.0f, 135.0f, 20.0f}, 500.0f);
    in.tool_count = 4;
    const auto plan = plan_belt_settle(in);
    REQUIRE(plan.refusal == BeltSettleRefusal::NONE);
    CHECK(plan.inset_x_mm == Catch::Approx(67.5f));
    CHECK(plan.inset_y_mm == Catch::Approx(67.5f));
    CHECK(plan.speed_mm_s == Catch::Approx(300.0f));
    const auto lines = helix::test::script_lines(plan.gcode);
    REQUIRE(lines.size() == 4 + 4 * belt_settle::kLaps + 3);
    CHECK(lines[2] == "G1 Z20.0 F18000");
    CHECK(lines[3] == "G1 X67.5 Y67.5 F18000");
    CHECK(lines[5] == "G1 X202.5 Y202.5");
    CHECK(lines[lines.size() - 3] == "G1 X135.0 Y135.0");
}

TEST_CASE("a 2-tool printer laps the central half too", "[belt][settle]") {
    auto in = square_bed(350.0f, {175.0f, 175.0f, 20.0f}, 500.0f);
    in.y_max = 300.0f;
    in.tool_count = 2;
    const auto plan = plan_belt_settle(in);
    REQUIRE(plan.refusal == BeltSettleRefusal::NONE);
    CHECK(plan.inset_x_mm == Catch::Approx(87.5f));
    CHECK(plan.inset_y_mm == Catch::Approx(75.0f));
    const auto lines = helix::test::script_lines(plan.gcode);
    REQUIRE(lines.size() >= 6);
    CHECK(lines[3] == "G1 X87.5 Y75.0 F18000");
    CHECK(lines[5] == "G1 X262.5 Y225.0");
}

TEST_CASE("one tool keeps the perimeter inset", "[belt][settle]") {
    auto in = square_bed(350.0f, {175.0f, 175.0f, 20.0f}, 500.0f);
    in.tool_count = 1;
    CHECK(plan_belt_settle(in).inset_x_mm == Catch::Approx(24.5f));
    in.tool_count = 0; // not reported yet reads as one
    CHECK(plan_belt_settle(in).inset_x_mm == Catch::Approx(24.5f));
}
