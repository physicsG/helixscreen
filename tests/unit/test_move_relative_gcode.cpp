// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
// tests/unit/test_move_relative_gcode.cpp
#include "../../include/moonraker_motion_api.h"

#include <limits>

#include "../catch_amalgamated.hpp"

TEST_CASE("generate_relative_move_gcode: XY combined on one G0 line", "[motion][gcode]") {
    CHECK(MoonrakerMotionAPI::generate_relative_move_gcode(4.0, -2.0, 0.0, 6000.0, 600.0) ==
          "G91\nG0 X4 Y-2 F6000\nG90");
}

TEST_CASE("generate_relative_move_gcode: Z gets its own feedrate line", "[motion][gcode]") {
    CHECK(MoonrakerMotionAPI::generate_relative_move_gcode(0.0, 0.0, 0.5, 6000.0, 600.0) ==
          "G91\nG0 Z0.5 F600\nG90");
}

TEST_CASE("generate_relative_move_gcode: XY and Z as two moves in one script", "[motion][gcode]") {
    CHECK(MoonrakerMotionAPI::generate_relative_move_gcode(1.0, 0.0, -0.5, 6000.0, 600.0) ==
          "G91\nG0 X1 F6000\nG0 Z-0.5 F600\nG90");
}

TEST_CASE("generate_relative_move_gcode: all-zero deltas produce empty script", "[motion][gcode]") {
    CHECK(MoonrakerMotionAPI::generate_relative_move_gcode(0.0, 0.0, 0.0, 6000.0, 600.0).empty());
}

TEST_CASE("generate_relative_move_gcode: NaN/Inf rejected", "[motion][gcode]") {
    CHECK(MoonrakerMotionAPI::generate_relative_move_gcode(std::numeric_limits<double>::quiet_NaN(),
                                                           0.0, 0.0, 6000.0, 600.0)
              .empty());
    CHECK(MoonrakerMotionAPI::generate_relative_move_gcode(
              0.0, std::numeric_limits<double>::infinity(), 0.0, 6000.0, 600.0)
              .empty());
}

TEST_CASE("generate_relative_move_gcode: sub-epsilon residue axes are omitted", "[motion][gcode]") {
    // A cross-axis reversal leaves float cancellation residue (~1e-17) on one
    // axis while another carries a real delta. Gating each axis at exactly
    // != 0.0 serialized that residue as a real term: "G0 X1e-17 Y2".
    CHECK(MoonrakerMotionAPI::generate_relative_move_gcode(1e-17, 2.0, 0.0, 6000.0, 600.0) ==
          "G91\nG0 Y2 F6000\nG90");
    CHECK(MoonrakerMotionAPI::generate_relative_move_gcode(2.0, 1e-17, 0.0, 6000.0, 600.0) ==
          "G91\nG0 X2 F6000\nG90");
    // Residue on Z alongside a real XY move: no bogus second G0 line.
    CHECK(MoonrakerMotionAPI::generate_relative_move_gcode(2.0, 0.0, -1e-17, 6000.0, 600.0) ==
          "G91\nG0 X2 F6000\nG90");
    // Residue on every axis is a no-op script, exactly like all-zero.
    CHECK(MoonrakerMotionAPI::generate_relative_move_gcode(1e-17, -1e-17, 5e-7, 6000.0, 600.0)
              .empty());
}

TEST_CASE("generate_relative_move_gcode: never emits scientific notation", "[motion][gcode]") {
    // A bare `ostringstream << double` uses ~6 significant digits and switches
    // to scientific notation for small magnitudes. clamp_jog_delta can return a
    // genuine sub-micron residual (predicted=199.9999995, +1, max=200 -> ~5e-7),
    // so a surviving small value must still serialize as a plain decimal.
    const std::string g =
        MoonrakerMotionAPI::generate_relative_move_gcode(2e-6, 0.0, 0.0, 6000.0, 600.0);
    INFO("emitted: " << g);
    CHECK(g.find('e') == std::string::npos);
    CHECK(g.find('E') == std::string::npos);
    CHECK(g == "G91\nG0 X0.000002 F6000\nG90");

    // Large and fractional values stay plain too.
    const std::string big =
        MoonrakerMotionAPI::generate_relative_move_gcode(0.0, 0.0, 1234.5, 6000.0, 600.0);
    CHECK(big.find('e') == std::string::npos);
    CHECK(big == "G91\nG0 Z1234.5 F600\nG90");
}

TEST_CASE("generate_relative_move_gcode: compact formatting for ordinary values",
          "[motion][gcode]") {
    // Trailing zeros must be trimmed: fixed-notation formatting must not turn
    // "X4" into "X4.000000".
    CHECK(MoonrakerMotionAPI::generate_relative_move_gcode(0.1, 0.0, 0.0, 6000.0, 600.0) ==
          "G91\nG0 X0.1 F6000\nG90");
    CHECK(MoonrakerMotionAPI::generate_relative_move_gcode(-10.0, 0.0, 0.0, 3000.0, 600.0) ==
          "G91\nG0 X-10 F3000\nG90");
    CHECK(MoonrakerMotionAPI::generate_relative_move_gcode(0.0, 0.0, 0.05, 6000.0, 600.0) ==
          "G91\nG0 Z0.05 F600\nG90");
}
