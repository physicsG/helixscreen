// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_exclude_map_hatch.cpp
 * @brief hatch_polygon(): diagonal lines clipped to a polygon by the even-odd rule
 *
 * Run with: ./build/bin/helix-tests "[exclude_map][hatch]"
 */

#include "exclude_map_hatch.h"

#include <glm/geometric.hpp>

#include "../catch_amalgamated.hpp"

using helix::hatch_polygon;
using helix::HatchSegment;

namespace {

const std::vector<glm::vec2> kSquare = {{0, 0}, {20, 0}, {20, 20}, {0, 20}};

float level(const HatchSegment& s) {
    return s.a.x + s.a.y;
}

} // namespace

TEST_CASE("hatch: a square is crossed by one segment per spacing step", "[exclude_map][hatch]") {
    const auto segs = hatch_polygon(kSquare, 10.0f);
    // x + y runs 0..40 over the square; the lines at 10, 20 and 30 cross it.
    REQUIRE(segs.size() == 3);
    for (const auto& s : segs) {
        CHECK(level(s) == Catch::Approx(s.b.x + s.b.y));
        CHECK(std::fmod(level(s), 10.0f) == Catch::Approx(0.0f).margin(1e-4));
        for (const auto& p : {s.a, s.b}) {
            CHECK(p.x >= -1e-4f);
            CHECK(p.y >= -1e-4f);
            CHECK(p.x <= 20.0f + 1e-4f);
            CHECK(p.y <= 20.0f + 1e-4f);
        }
    }
}

TEST_CASE("hatch: the longest line is the diagonal through the middle", "[exclude_map][hatch]") {
    const auto segs = hatch_polygon(kSquare, 10.0f);
    REQUIRE(segs.size() == 3);
    float longest = 0.0f;
    for (const auto& s : segs)
        longest = std::max(longest, glm::length(s.b - s.a));
    CHECK(longest == Catch::Approx(20.0f * std::sqrt(2.0f)).epsilon(0.001));
}

TEST_CASE("hatch: a concave outline hatches only the material", "[exclude_map][hatch]") {
    // A U: two 6-wide arms joined across the bottom, open notch between x=6 and x=14.
    const std::vector<glm::vec2> u = {{0, 0},  {20, 0}, {20, 20}, {14, 20},
                                      {14, 6}, {6, 6},  {6, 20},  {0, 20}};
    const auto segs = hatch_polygon(u, 4.0f);
    REQUIRE_FALSE(segs.empty());
    for (const auto& s : segs) {
        const glm::vec2 mid = (s.a + s.b) * 0.5f;
        const bool in_notch = mid.x > 6.0f && mid.x < 14.0f && mid.y > 6.0f;
        INFO("midpoint " << mid.x << "," << mid.y);
        CHECK_FALSE(in_notch);
    }
    // Some level crosses both arms, so the polygon yields a pair of segments there.
    bool paired = false;
    for (size_t i = 0; i < segs.size(); ++i)
        for (size_t j = i + 1; j < segs.size(); ++j)
            paired = paired || level(segs[i]) == Catch::Approx(level(segs[j]));
    CHECK(paired);
}

TEST_CASE("hatch: a vertex on a hatch line is counted once", "[exclude_map][hatch]") {
    // A rhombus whose long diagonal, (0,10) to (10,0), lies on the x + y = 10 line.
    const std::vector<glm::vec2> diamond = {{0, 10}, {3, 3}, {10, 0}, {7, 7}};
    const auto segs = hatch_polygon(diamond, 10.0f);
    REQUIRE(segs.size() == 1);
    CHECK(glm::length(segs[0].b - segs[0].a) ==
          Catch::Approx(10.0f * std::sqrt(2.0f)).epsilon(0.001));
}

TEST_CASE("hatch: degenerate input draws nothing", "[exclude_map][hatch]") {
    CHECK(hatch_polygon({}, 4.0f).empty());
    CHECK(hatch_polygon({{0, 0}, {5, 5}}, 4.0f).empty());
    CHECK(hatch_polygon(kSquare, 0.0f).empty());
    CHECK(hatch_polygon(kSquare, -3.0f).empty());
}
