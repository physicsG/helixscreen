// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_filament_path_canvas.h"

#include "../catch_amalgamated.hpp"

// ============================================================================
// hub_box_hit() — pure coordinate hit-test for the selector/hub box.
//
// Box under test: center (200, 90), width 100, height 30, margin 4.
//   X half-extent + margin = 50 + 4 = 54  -> hit when |x - 200| <= 54
//   Y half-extent + margin = 15 + 4 = 19  -> hit when |y - 90|  <= 19
// ============================================================================

namespace {
constexpr int32_t CX = 200;
constexpr int32_t CY = 90;
constexpr int32_t W = 100;
constexpr int32_t H = 30;
constexpr int32_t MARGIN = 4;

bool hit(int32_t x, int32_t y) {
    lv_point_t p{x, y};
    return helix::ui::hub_box_hit(p, CX, CY, W, H, MARGIN);
}
} // namespace

TEST_CASE("hub_box_hit: dead-center point hits", "[canvas][hit_test]") {
    REQUIRE(hit(200, 90));
}

TEST_CASE("hub_box_hit: inside-edge points hit", "[canvas][hit_test]") {
    // Just inside the un-margined box bounds.
    REQUIRE(hit(151, 76));  // left/top inner corner-ish (|x-200|=49, |y-90|=14)
    REQUIRE(hit(249, 104)); // right/bottom (|x-200|=49, |y-90|=14)
}

TEST_CASE("hub_box_hit: exact box-edge points hit", "[canvas][hit_test]") {
    REQUIRE(hit(150, 90));  // left box edge (|x-200|=50 <= 54)
    REQUIRE(hit(250, 90));  // right box edge
    REQUIRE(hit(200, 75));  // top box edge (|y-90|=15 <= 19)
    REQUIRE(hit(200, 105)); // bottom box edge
}

TEST_CASE("hub_box_hit: within-margin points hit", "[canvas][hit_test]") {
    REQUIRE(hit(253, 90));  // |x-200|=53 <= 54
    REQUIRE(hit(254, 90));  // |x-200|=54 == limit, inclusive
    REQUIRE(hit(200, 109)); // |y-90|=19 == limit, inclusive
}

TEST_CASE("hub_box_hit: just-outside-margin points miss", "[canvas][hit_test]") {
    REQUIRE_FALSE(hit(255, 90));  // |x-200|=55 > 54
    REQUIRE_FALSE(hit(200, 110)); // |y-90|=20 > 19
}

TEST_CASE("hub_box_hit: far-left point misses", "[canvas][hit_test]") {
    REQUIRE_FALSE(hit(140, 90)); // |x-200|=60 > 54
}

TEST_CASE("hub_box_hit: above-box point misses", "[canvas][hit_test]") {
    REQUIRE_FALSE(hit(200, 60)); // |y-90|=30 > 19
}

TEST_CASE("hub_box_hit: argument order is width-then-height", "[canvas][hit_test]") {
    // A wide-short box: 120 wide, 20 tall, margin 0.
    // X half-extent 60, Y half-extent 10. A point at x-offset 55, y-offset 12
    // hits only if width is used for X (55<=60) and height for Y... but 12>10
    // so it MUST miss. If args were swapped (h for X, w for Y), it would hit.
    lv_point_t p{CX + 55, CY + 12};
    REQUIRE_FALSE(helix::ui::hub_box_hit(p, CX, CY, 120, 20, 0));
    // Same x-offset, y-offset 8 -> hits (within both extents).
    lv_point_t p2{CX + 55, CY + 8};
    REQUIRE(helix::ui::hub_box_hit(p2, CX, CY, 120, 20, 0));
}

// ============================================================================
// Record-and-read contract guards. The renderer stores the EXACT drawn box in
// data->{buffer,bypass}_hit_rect (absolute coords); the click handler reads it
// back via hub_box_hit. These cases reproduce the renderer's rect-construction
// math and assert hub_box_hit reproduces the same hit decisions the old inline
// re-derived tests made — so a future drift in either side fails here.
// ============================================================================

namespace {
// Mirror the buffer-coil rect the renderer records: box centered at
// (center_x, buffer_y) with draw_buffer_coil()'s clamped dimensions
// (w = max(hub_width*4/5, 36), h = max(hub_h, 16)), read with margin 4.
bool buffer_hit(lv_point_t p, int32_t center_x, int32_t buffer_y, int32_t hub_width,
                int32_t hub_h) {
    int32_t w = hub_width * 4 / 5;
    int32_t h = hub_h;
    if (w < 36)
        w = 36;
    if (h < 16)
        h = 16;
    lv_area_t r{center_x - w / 2, buffer_y - h / 2, center_x + w / 2, buffer_y + h / 2};
    int32_t cx = (r.x1 + r.x2) / 2;
    int32_t cy = (r.y1 + r.y2) / 2;
    return helix::ui::hub_box_hit(p, cx, cy, r.x2 - r.x1, r.y2 - r.y1, 4);
}

// Mirror the bypass rect the renderer records: half-extents sensor_r*3 (X) and
// sensor_r*4 (Y) about (bypass_x, bypass_merge_y), read with margin 0.
bool bypass_hit(lv_point_t p, int32_t bypass_x, int32_t merge_y, int32_t sensor_r) {
    lv_area_t r{bypass_x - sensor_r * 3, merge_y - sensor_r * 4, bypass_x + sensor_r * 3,
                merge_y + sensor_r * 4};
    int32_t cx = (r.x1 + r.x2) / 2;
    int32_t cy = (r.y1 + r.y2) / 2;
    return helix::ui::hub_box_hit(p, cx, cy, r.x2 - r.x1, r.y2 - r.y1, 0);
}
} // namespace

TEST_CASE("buffer hit rect: clamps small box to minimum 36x16", "[canvas][hit_test]") {
    // hub_width=10 -> w=8 -> clamped to 36 (half 18); hub_h=4 -> clamped to 16 (half 8).
    const int32_t cx = 200, by = 150;
    REQUIRE(buffer_hit({cx, by}, cx, by, 10, 4));            // center hits
    REQUIRE(buffer_hit({cx + 18 + 4, by}, cx, by, 10, 4));   // X: 18 half + 4 margin, inclusive
    REQUIRE_FALSE(buffer_hit({cx + 23, by}, cx, by, 10, 4)); // 23 > 22 misses
    REQUIRE(buffer_hit({cx, by + 8 + 4}, cx, by, 10, 4));    // Y: 8 half + 4 margin, inclusive
    REQUIRE_FALSE(buffer_hit({cx, by + 13}, cx, by, 10, 4)); // 13 > 12 misses
}

TEST_CASE("buffer hit rect: large box uses unclamped dimensions", "[canvas][hit_test]") {
    // hub_width=100 -> w=80 (half 40); hub_h=30 (half 15).
    const int32_t cx = 200, by = 150;
    REQUIRE(buffer_hit({cx + 44, by}, cx, by, 100, 30));       // 40 half + 4 margin, inclusive
    REQUIRE_FALSE(buffer_hit({cx + 45, by}, cx, by, 100, 30)); // 45 > 44 misses
    REQUIRE(buffer_hit({cx, by + 19}, cx, by, 100, 30));       // 15 half + 4 margin, inclusive
    REQUIRE_FALSE(buffer_hit({cx, by + 20}, cx, by, 100, 30)); // 20 > 19 misses
}

TEST_CASE("bypass hit rect: full-extent bounds preserved (margin 0)", "[canvas][hit_test]") {
    // sensor_r=4 -> X half-extent 12, Y half-extent 16.
    const int32_t bx = 300, my = 120, sr = 4;
    REQUIRE(bypass_hit({bx, my}, bx, my, sr));            // center hits
    REQUIRE(bypass_hit({bx + 12, my}, bx, my, sr));       // X edge inclusive
    REQUIRE_FALSE(bypass_hit({bx + 13, my}, bx, my, sr)); // beyond X misses
    REQUIRE(bypass_hit({bx, my + 16}, bx, my, sr));       // Y edge inclusive
    REQUIRE_FALSE(bypass_hit({bx, my + 17}, bx, my, sr)); // beyond Y misses
}
