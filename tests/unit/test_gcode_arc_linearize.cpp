// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gcode_parser.h"

#include <cmath>

#include "../catch_amalgamated.hpp"

using helix::gcode::arc_segment_count;
using helix::gcode::GCodeParser;
using helix::gcode::kArcChordToleranceMm;
using helix::gcode::kMaxArcSegments;

namespace {

/// Deviation between a chord and the arc it replaces: r * (1 - cos(theta / 2)).
double sagitta(double radius, double sweep, int segments) {
    return radius * (1.0 - std::cos(std::abs(sweep) / segments / 2.0));
}

constexpr double kPi = 3.14159265358979323846;

} // namespace

TEST_CASE("arc_segment_count keeps every chord within tolerance of the arc",
          "[gcode][parser][arc]") {
    for (double r : {0.05, 0.3, 0.5, 2.0, 10.0, 60.0, 250.0}) {
        for (double sweep : {0.1, kPi / 4, kPi / 2, kPi, 1.9 * kPi, -kPi / 2}) {
            const int n = arc_segment_count(static_cast<float>(r), static_cast<float>(sweep));
            CAPTURE(r, sweep, n);
            REQUIRE(n >= 1);
            REQUIRE(n <= kMaxArcSegments);
            if (n < kMaxArcSegments) {
                CHECK(sagitta(r, sweep, n) <= kArcChordToleranceMm * 1.0001);
            }
        }
    }
}

TEST_CASE("arc_segment_count spends no more segments than the tolerance needs",
          "[gcode][parser][arc]") {
    // One fewer segment would break the tolerance, so the count is the minimum.
    for (double r : {0.5, 2.0, 10.0, 60.0}) {
        for (double sweep : {kPi / 2, kPi, 1.5 * kPi}) {
            const int n = arc_segment_count(static_cast<float>(r), static_cast<float>(sweep));
            CAPTURE(r, sweep, n);
            if (n > 1 && n < kMaxArcSegments) {
                CHECK(sagitta(r, sweep, n - 1) > kArcChordToleranceMm);
            }
        }
    }
}

TEST_CASE("arc_segment_count: a small fillet is a few chords, not eight", "[gcode][parser][arc]") {
    const int n = arc_segment_count(0.5f, static_cast<float>(kPi / 2));
    CHECK(n >= 2);
    CHECK(n <= 4);
}

TEST_CASE("arc_segment_count: degenerate arcs and huge circles stay bounded",
          "[gcode][parser][arc]") {
    CHECK(arc_segment_count(0.0f, 1.0f) == 1);
    CHECK(arc_segment_count(kArcChordToleranceMm / 2, 3.0f) == 1);
    CHECK(arc_segment_count(10.0f, 0.0f) == 1);
    CHECK(arc_segment_count(10000.0f, static_cast<float>(2 * kPi)) == kMaxArcSegments);
    CHECK(arc_segment_count(std::nanf(""), 1.0f) == 1);
}

TEST_CASE("a G3 arc parses into arc_segment_count chords that end on its endpoint",
          "[gcode][parser][arc]") {
    // Quarter circle of radius 10 around (10,0): from (0,0) to (10,10).
    GCodeParser parser;
    parser.parse_line("G90");
    parser.parse_line("M82");
    parser.parse_line("G1 Z0.2");
    parser.parse_line("G1 X0 Y0 E1");
    parser.parse_line("G2 X10 Y10 I10 J0 E2");
    auto result = parser.finalize();
    REQUIRE(result.layers.size() == 1);
    const auto& segs = result.layers[0].segments;

    const int expected = arc_segment_count(10.0f, static_cast<float>(kPi / 2));
    REQUIRE(static_cast<int>(segs.size()) == expected);
    CHECK(segs.back().end.x == Catch::Approx(10.0f).margin(1e-4));
    CHECK(segs.back().end.y == Catch::Approx(10.0f).margin(1e-4));
    for (const auto& s : segs) {
        CHECK(s.is_extrusion);
        const float rs = std::hypot(s.end.x - 10.0f, s.end.y);
        CHECK(rs == Catch::Approx(10.0f).margin(1e-3));
    }
}
