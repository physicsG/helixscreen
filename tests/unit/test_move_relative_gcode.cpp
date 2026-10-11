// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
// tests/unit/test_move_relative_gcode.cpp
#include "../../include/axis_move.h"
#include "../../include/moonraker_client.h"
#include "../../include/moonraker_motion_api.h"
#include "../../include/printer_state.h"
#include "../ui_test_utils.h"

#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <vector>

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

// ============================================================================
// Absolute multi-axis moves (generate_absolute_move_gcode / move_to)
// ============================================================================

using helix::AxisTarget;

TEST_CASE("generate_absolute_move_gcode: Z line precedes the combined XY line", "[motion][gcode]") {
    AxisTarget t;
    t.x = 100.0;
    t.y = 50.0;
    t.z = 10.0;
    CHECK(MoonrakerMotionAPI::generate_absolute_move_gcode(t, 6000.0, 600.0) ==
          "G90\nG0 Z10 F600\nG0 X100 Y50 F6000");
}

TEST_CASE("generate_absolute_move_gcode: descent to a known lower Z travels XY first",
          "[motion][gcode]") {
    AxisTarget t;
    t.x = 100.0;
    t.y = 50.0;
    t.z = 5.0; // below current_z 10: travel first, descend last
    CHECK(MoonrakerMotionAPI::generate_absolute_move_gcode(t, 6000.0, 600.0, 10.0) ==
          "G90\nG0 X100 Y50 F6000\nG0 Z5 F600");
}

TEST_CASE("generate_absolute_move_gcode: ascent, equal Z and unknown Z keep Z first",
          "[motion][gcode]") {
    AxisTarget t;
    t.x = 100.0;
    t.z = 15.0;
    const char* expected = "G90\nG0 Z15 F600\nG0 X100 F6000";
    CHECK(MoonrakerMotionAPI::generate_absolute_move_gcode(t, 6000.0, 600.0, 10.0) == expected);
    // Equal Z is not a descent.
    CHECK(MoonrakerMotionAPI::generate_absolute_move_gcode(t, 6000.0, 600.0, 15.0) == expected);
    // Unknown current_z: Z first, as always.
    CHECK(MoonrakerMotionAPI::generate_absolute_move_gcode(t, 6000.0, 600.0) == expected);
    CHECK(MoonrakerMotionAPI::generate_absolute_move_gcode(t, 6000.0, 600.0, std::nullopt) ==
          expected);
}

TEST_CASE("generate_absolute_move_gcode: current_z never reorders a Z-only or XY-only move",
          "[motion][gcode]") {
    AxisTarget z_only;
    z_only.z = 5.0;
    CHECK(MoonrakerMotionAPI::generate_absolute_move_gcode(z_only, 6000.0, 600.0, 10.0) ==
          "G90\nG0 Z5 F600");
    AxisTarget xy_only;
    xy_only.x = 100.0;
    xy_only.y = 50.0;
    CHECK(MoonrakerMotionAPI::generate_absolute_move_gcode(xy_only, 6000.0, 600.0, 10.0) ==
          "G90\nG0 X100 Y50 F6000");
}

TEST_CASE("generate_absolute_move_gcode: XY only", "[motion][gcode]") {
    AxisTarget t;
    t.x = 100.0;
    t.y = 50.0;
    CHECK(MoonrakerMotionAPI::generate_absolute_move_gcode(t, 6000.0, 600.0) ==
          "G90\nG0 X100 Y50 F6000");
    // Either axis alone also lands on the combined line.
    AxisTarget x_only;
    x_only.x = -5.0;
    CHECK(MoonrakerMotionAPI::generate_absolute_move_gcode(x_only, 6000.0, 600.0) ==
          "G90\nG0 X-5 F6000");
}

TEST_CASE("generate_absolute_move_gcode: Z only", "[motion][gcode]") {
    AxisTarget t;
    t.z = 12.5;
    CHECK(MoonrakerMotionAPI::generate_absolute_move_gcode(t, 6000.0, 600.0) ==
          "G90\nG0 Z12.5 F600");
}

TEST_CASE("generate_absolute_move_gcode: empty target produces empty script", "[motion][gcode]") {
    CHECK(MoonrakerMotionAPI::generate_absolute_move_gcode(AxisTarget{}, 6000.0, 600.0).empty());
}

TEST_CASE("generate_absolute_move_gcode: NaN/Inf rejected", "[motion][gcode]") {
    AxisTarget t;
    t.x = std::numeric_limits<double>::quiet_NaN();
    CHECK(MoonrakerMotionAPI::generate_absolute_move_gcode(t, 6000.0, 600.0).empty());
    AxisTarget inf_z;
    inf_z.z = std::numeric_limits<double>::infinity();
    CHECK(MoonrakerMotionAPI::generate_absolute_move_gcode(inf_z, 6000.0, 600.0).empty());
    AxisTarget bad_feed;
    bad_feed.x = 10.0;
    CHECK(MoonrakerMotionAPI::generate_absolute_move_gcode(
              bad_feed, std::numeric_limits<double>::quiet_NaN(), 600.0)
              .empty());
}

TEST_CASE("generate_absolute_move_gcode: feedrate term omitted when 0", "[motion][gcode]") {
    AxisTarget t;
    t.x = 100.0;
    t.z = 10.0;
    CHECK(MoonrakerMotionAPI::generate_absolute_move_gcode(t, 0.0, 0.0) == "G90\nG0 Z10\nG0 X100");
}

TEST_CASE("generate_absolute_move_gcode: single-axis bytes stay plain", "[motion][gcode]") {
    // The single-axis path keeps default ostream formatting; these pin it so
    // rerouting it through the multi-axis formatter (which would emit
    // "1234567" as itself but "1e-06" differently) cannot pass silently.
    CHECK(MoonrakerMotionAPI::generate_absolute_move_gcode('Z', 10.0, 600.0) == "G90\nG0 Z10 F600");
    CHECK(MoonrakerMotionAPI::generate_absolute_move_gcode('X', 100.5, 3000.0) ==
          "G90\nG0 X100.5 F3000");
    CHECK(MoonrakerMotionAPI::generate_absolute_move_gcode('E', 5.0, 0.0) == "G90\nG0 E5");
    CHECK(MoonrakerMotionAPI::generate_absolute_move_gcode('Y', -2.25, 120.0) ==
          "G90\nG0 Y-2.25 F120");
}

namespace {

/// Local fixtures for the move_to instance-path validation tests: validation
/// fires before any network I/O, so a disconnected client is sufficient.
struct MoveToTestFixture {
    MoveToTestFixture() {
        lv_init_safe();
        state.init_subjects(false);
    }
    helix::PrinterState state;
    helix::MoonrakerClient client;
    SafetyLimits limits{};
};

} // namespace

TEST_CASE_METHOD(MoveToTestFixture, "move_to rejects an out-of-range axis value",
                 "[motion][gcode]") {
    bool errored = false;
    MoonrakerMotionAPI motion(client, state, limits);

    AxisTarget t;
    t.x = 2000.0; // over the 1000mm default ceiling
    t.y = 50.0;
    motion.move_to(t, 6000.0, 600.0, nullptr, [&errored](const MoonrakerError& err) {
        errored = true;
        CHECK(err.type == MoonrakerErrorType::VALIDATION_ERROR);
    });
    CHECK(errored);
}

TEST_CASE_METHOD(MoveToTestFixture, "move_to: empty target succeeds without an RPC",
                 "[motion][gcode]") {
    bool succeeded = false;
    MoonrakerMotionAPI motion(client, state, limits);
    motion.move_to(AxisTarget{}, 6000.0, 600.0, [&succeeded]() { succeeded = true; }, nullptr);
    CHECK(succeeded);
}

TEST_CASE_METHOD(MoveToTestFixture,
                 "move_to: an unused feedrate is not validated: XY-only ignores z_feedrate",
                 "[motion][gcode]") {
    bool validation_error = false;
    MoonrakerMotionAPI motion(client, state, limits);
    AxisTarget t;
    t.x = 100.0; // no z: the z_feedrate is never spent
    motion.move_to(t, 6000.0, limits.max_feedrate_mm_min * 10.0, nullptr,
                   [&validation_error](const MoonrakerError& err) {
                       if (err.type == MoonrakerErrorType::VALIDATION_ERROR) {
                           validation_error = true;
                       }
                   });
    CHECK_FALSE(validation_error);
}

TEST_CASE_METHOD(MoveToTestFixture, "move_to: a used feedrate is validated, both bounds",
                 "[motion][gcode]") {
    limits.min_feedrate_mm_min = 60.0;
    MoonrakerMotionAPI motion(client, state, limits);
    AxisTarget t;
    t.x = 100.0;

    bool too_fast = false;
    motion.move_to(t, limits.max_feedrate_mm_min * 10.0, 600.0, nullptr,
                   [&too_fast](const MoonrakerError& err) {
                       too_fast = true;
                       CHECK(err.type == MoonrakerErrorType::VALIDATION_ERROR);
                   });
    CHECK(too_fast);

    bool too_slow = false;
    motion.move_to(t, 30.0, 600.0, nullptr, [&too_slow](const MoonrakerError& err) {
        too_slow = true;
        CHECK(err.type == MoonrakerErrorType::VALIDATION_ERROR);
    });
    CHECK(too_slow);
}

namespace {

std::vector<double> gcode_format_values() {
    std::vector<double> v = {0.0,      -0.0,     1.0,        -1.0,     0.1,       0.05,
                             -10.0,    1234.5,   6000.0,     0.123456, 0.1234567, 1e-5,
                             1e-4,     123456.0, 1234567.0,  1e7,      -1e-5,     99.99999,
                             250.0004, 0.000002, 199.9999995};
    for (double x = -300.0; x <= 300.0; x += 1.7317) {
        v.push_back(x);
    }
    return v;
}

} // namespace

// The single-axis generators print numbers the way ostream's default (%g, 6
// significant digits) does, including the switch to exponent form.
TEST_CASE("generate_absolute_move_gcode(axis): %g number bytes", "[motion][gcode]") {
    CHECK(MoonrakerMotionAPI::generate_absolute_move_gcode('z', 1e-5, 0.0) == "G90\nG0 Z1e-05");
    CHECK(MoonrakerMotionAPI::generate_absolute_move_gcode('x', 1234567.0, 6000.0) ==
          "G90\nG0 X1.23457e+06 F6000");
    CHECK(MoonrakerMotionAPI::generate_absolute_move_gcode('y', -0.5, 1500.5) ==
          "G90\nG0 Y-0.5 F1500.5");

    for (double v : gcode_format_values()) {
        std::ostringstream ref;
        ref << "G90\nG0 X" << v << " F" << 3000.25;
        INFO("value=" << v);
        CHECK(MoonrakerMotionAPI::generate_absolute_move_gcode('x', v, 3000.25) == ref.str());
    }
}

// The multi-axis generators print fixed six decimals with trailing zeros trimmed.
TEST_CASE("generate_relative_move_gcode: fixed-trimmed number bytes", "[motion][gcode]") {
    auto fixed_trimmed = [](double v) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(6) << v;
        std::string s = ss.str();
        s.erase(s.find_last_not_of('0') + 1);
        if (!s.empty() && s.back() == '.') {
            s.pop_back();
        }
        return s;
    };
    for (double v : gcode_format_values()) {
        if (std::abs(v) <= helix::AxisMove::EPSILON_MM) {
            continue;
        }
        INFO("value=" << v);
        CHECK(MoonrakerMotionAPI::generate_relative_move_gcode(v, 0.0, 0.0, 6000.0, 600.0) ==
              "G91\nG0 X" + fixed_trimmed(v) + " F6000\nG90");
    }
}
