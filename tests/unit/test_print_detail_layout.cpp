// SPDX-License-Identifier: GPL-3.0-or-later
#include "print_detail_layout.h"

#include "../catch_amalgamated.hpp"

using helix::ui::decide_detail_portrait_preview;

namespace {
// 480x800 portrait numbers: preview 452 wide -> base 226 (2:1), floor 150.
constexpr int W = 452;
constexpr int BASE = W / 2;
constexpr int kFloor = W / 3;
constexpr int kTile = 48;
constexpr int kGap = 6;
constexpr int kGridTop = 120; // tile grid starts 120px into the scroll content
constexpr int kPitch = kTile + kGap;
// The shrink target: the middle of the second tile row.
constexpr int TARGET = kGridTop + kPitch + kTile / 2;

// Where the visible edge falls inside the scroll content for a given preview height.
int edge_for(int avail_h, int preview_h) {
    return avail_h - preview_h;
}
bool mid_tile(int edge) {
    const int in_row = (edge - kGridTop) % kPitch;
    return in_row >= kTile / 4 && in_row <= kTile - kTile / 4;
}
// The smallest avail_h whose nudge regime is entered: h == base there.
constexpr int kNudgeAvail = BASE + TARGET;
} // namespace

TEST_CASE("portrait preview: content fits -> 2:1 base, no shrink", "[print_detail_layout]") {
    const int avail = BASE + 200;
    CHECK(decide_detail_portrait_preview(W, avail, 150, kGridTop, kTile, kGap) == BASE);
}

TEST_CASE("portrait preview: no tile rows -> base", "[print_detail_layout]") {
    CHECK(decide_detail_portrait_preview(W, 400, 1000, kGridTop, 0, kGap) == BASE);
}

TEST_CASE("portrait preview: overflow shrinks to cut row 2 through its middle",
          "[print_detail_layout]") {
    // avail 378: base leaves only 152 for 300 of content, so it shrinks.
    const int avail = 378;
    const int h = decide_detail_portrait_preview(W, avail, 300, kGridTop, kTile, kGap);
    REQUIRE(h < BASE);
    CHECK(h == avail - TARGET);
    CHECK(h > kFloor);
    // Row 1 fully visible, row 2 cut at its middle half.
    CHECK(edge_for(avail, h) >= kGridTop + kTile);
    CHECK(mid_tile(edge_for(avail, h)));
}

TEST_CASE("portrait preview: shrink target is clamped by the content end",
          "[print_detail_layout]") {
    // Content ends at 170, before row 2's middle: the whole grid shows.
    const int avail = 378;
    const int h = decide_detail_portrait_preview(W, avail, 170, kGridTop, kTile, kGap);
    REQUIRE(h < BASE);
    CHECK(h == avail - 170);
    CHECK(edge_for(avail, h) == 170);
}

TEST_CASE("portrait preview: shrink stops at the width / 3 floor", "[print_detail_layout]") {
    // avail 298 wants h = 100 for the target; the floor is 150.
    CHECK(decide_detail_portrait_preview(W, 298, 300, kGridTop, kTile, kGap) == kFloor);
}

TEST_CASE("portrait preview: rows fit under base, edge already mid-tile is left alone",
          "[print_detail_layout]") {
    // Edge at TARGET is 24px into row 2 (its middle): no nudge, and it is the
    // exact avail_h where the nudge regime begins.
    CHECK(decide_detail_portrait_preview(W, kNudgeAvail, 1000, kGridTop, kTile, kGap) == BASE);
}

TEST_CASE("portrait preview: rows fit, edge in a grid gap is nudged mid-tile",
          "[print_detail_layout]") {
    // Edge 225 is 3px before row 3's start, inside the gap.
    const int avail = BASE + kGridTop + 2 * kPitch - 3;
    const int h = decide_detail_portrait_preview(W, avail, 1000, kGridTop, kTile, kGap);
    CHECK(h < BASE);
    CHECK(BASE - h <= kGap + kTile / 2);
    CHECK(mid_tile(edge_for(avail, h)));
}

TEST_CASE("portrait preview: rows fit, edge in row 3's top outer quarter is nudged",
          "[print_detail_layout]") {
    // Edge 236 is 8px into row 3 (its top outer quarter).
    const int avail = BASE + kGridTop + 2 * kPitch + 8;
    const int h = decide_detail_portrait_preview(W, avail, 1000, kGridTop, kTile, kGap);
    CHECK(h < BASE);
    CHECK(BASE - h <= kGap + kTile / 2);
    CHECK(mid_tile(edge_for(avail, h)));
}

TEST_CASE("portrait preview: rows fit, edge in a tile's bottom outer quarter is nudged",
          "[print_detail_layout]") {
    // Edge 220 is 46px into row 2 (its bottom outer quarter).
    const int avail = BASE + kGridTop + kPitch + kTile - 2;
    const int h = decide_detail_portrait_preview(W, avail, 1000, kGridTop, kTile, kGap);
    CHECK(h < BASE);
    CHECK(BASE - h <= kGap + kTile / 2);
    CHECK(mid_tile(edge_for(avail, h)));
}

TEST_CASE("portrait preview: fit_all shows the whole content when the floor allows",
          "[print_detail_layout]") {
    // avail 378, content 220: cutting row 2 would leave h = avail - TARGET, but
    // the whole 220 fits with a 158px preview, still above the 150 floor.
    const int avail = 378;
    const int content = 220;
    const int h = decide_detail_portrait_preview(W, avail, content, kGridTop, kTile, kGap, true);
    CHECK(h == avail - content);
    CHECK(h >= kFloor);
    CHECK(edge_for(avail, h) == content);
    // Without fit_all the same column keeps cutting row 2.
    CHECK(decide_detail_portrait_preview(W, avail, content, kGridTop, kTile, kGap) ==
          avail - TARGET);
}

TEST_CASE("portrait preview: fit_all falls back to cutting row 2 below the floor",
          "[print_detail_layout]") {
    // Showing all 300 would need a 78px preview, under the 150 floor.
    const int avail = 378;
    CHECK(decide_detail_portrait_preview(W, avail, 300, kGridTop, kTile, kGap, true) ==
          avail - TARGET);
}

TEST_CASE("portrait preview: fit_all leaves a column that already fits at base alone",
          "[print_detail_layout]") {
    const int avail = BASE + 200;
    CHECK(decide_detail_portrait_preview(W, avail, 150, kGridTop, kTile, kGap, true) == BASE);
}
