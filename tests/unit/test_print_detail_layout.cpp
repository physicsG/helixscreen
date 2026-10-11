// SPDX-License-Identifier: GPL-3.0-or-later
#include "print_detail_layout.h"

#include "../catch_amalgamated.hpp"

using helix::ui::decide_detail_portrait_preview;

namespace {
// 480x800 portrait numbers: preview 452 wide -> base 226 (2:1), floor 150.
constexpr int W = 452;
constexpr int BASE = W / 2;
constexpr int FLOOR = W / 3;
constexpr int TILE = 48;
constexpr int GAP = 6;
constexpr int GRID_TOP = 120; // tile grid starts 120px into the scroll content
constexpr int PITCH = TILE + GAP;
// The shrink target: the middle of the second tile row.
constexpr int TARGET = GRID_TOP + PITCH + TILE / 2;

// Where the visible edge falls inside the scroll content for a given preview height.
int edge_for(int avail_h, int preview_h) {
    return avail_h - preview_h;
}
bool mid_tile(int edge) {
    const int in_row = (edge - GRID_TOP) % PITCH;
    return in_row >= TILE / 4 && in_row <= TILE - TILE / 4;
}
// The smallest avail_h whose nudge regime is entered: h == base there.
constexpr int NUDGE_AVAIL = BASE + TARGET;
} // namespace

TEST_CASE("portrait preview: content fits -> 2:1 base, no shrink", "[print_detail_layout]") {
    const int avail = BASE + 200;
    CHECK(decide_detail_portrait_preview(W, avail, 150, GRID_TOP, TILE, GAP) == BASE);
}

TEST_CASE("portrait preview: no tile rows -> base", "[print_detail_layout]") {
    CHECK(decide_detail_portrait_preview(W, 400, 1000, GRID_TOP, 0, GAP) == BASE);
}

TEST_CASE("portrait preview: overflow shrinks to cut row 2 through its middle",
          "[print_detail_layout]") {
    // avail 378: base leaves only 152 for 300 of content, so it shrinks.
    const int avail = 378;
    const int h = decide_detail_portrait_preview(W, avail, 300, GRID_TOP, TILE, GAP);
    REQUIRE(h < BASE);
    CHECK(h == avail - TARGET);
    CHECK(h > FLOOR);
    // Row 1 fully visible, row 2 cut at its middle half.
    CHECK(edge_for(avail, h) >= GRID_TOP + TILE);
    CHECK(mid_tile(edge_for(avail, h)));
}

TEST_CASE("portrait preview: shrink target is clamped by the content end",
          "[print_detail_layout]") {
    // Content ends at 170, before row 2's middle: the whole grid shows.
    const int avail = 378;
    const int h = decide_detail_portrait_preview(W, avail, 170, GRID_TOP, TILE, GAP);
    REQUIRE(h < BASE);
    CHECK(h == avail - 170);
    CHECK(edge_for(avail, h) == 170);
}

TEST_CASE("portrait preview: shrink stops at the width / 3 floor", "[print_detail_layout]") {
    // avail 298 wants h = 100 for the target; the floor is 150.
    CHECK(decide_detail_portrait_preview(W, 298, 300, GRID_TOP, TILE, GAP) == FLOOR);
}

TEST_CASE("portrait preview: rows fit under base, edge already mid-tile is left alone",
          "[print_detail_layout]") {
    // Edge at TARGET is 24px into row 2 (its middle): no nudge, and it is the
    // exact avail_h where the nudge regime begins.
    CHECK(decide_detail_portrait_preview(W, NUDGE_AVAIL, 1000, GRID_TOP, TILE, GAP) == BASE);
}

TEST_CASE("portrait preview: rows fit, edge in a grid gap is nudged mid-tile",
          "[print_detail_layout]") {
    // Edge 225 is 3px before row 3's start, inside the gap.
    const int avail = BASE + GRID_TOP + 2 * PITCH - 3;
    const int h = decide_detail_portrait_preview(W, avail, 1000, GRID_TOP, TILE, GAP);
    CHECK(h < BASE);
    CHECK(BASE - h <= GAP + TILE / 2);
    CHECK(mid_tile(edge_for(avail, h)));
}

TEST_CASE("portrait preview: rows fit, edge in row 3's top outer quarter is nudged",
          "[print_detail_layout]") {
    // Edge 236 is 8px into row 3 (its top outer quarter).
    const int avail = BASE + GRID_TOP + 2 * PITCH + 8;
    const int h = decide_detail_portrait_preview(W, avail, 1000, GRID_TOP, TILE, GAP);
    CHECK(h < BASE);
    CHECK(BASE - h <= GAP + TILE / 2);
    CHECK(mid_tile(edge_for(avail, h)));
}

TEST_CASE("portrait preview: rows fit, edge in a tile's bottom outer quarter is nudged",
          "[print_detail_layout]") {
    // Edge 220 is 46px into row 2 (its bottom outer quarter).
    const int avail = BASE + GRID_TOP + PITCH + TILE - 2;
    const int h = decide_detail_portrait_preview(W, avail, 1000, GRID_TOP, TILE, GAP);
    CHECK(h < BASE);
    CHECK(BASE - h <= GAP + TILE / 2);
    CHECK(mid_tile(edge_for(avail, h)));
}
