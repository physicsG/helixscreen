// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// fit_tile_grid: the grid shape for N equal tiles is the one whose tile clears
// the icon-plus-label minimum by the most, so the same tile count lays out
// differently in a wide, a tall and a large box.

#include "tile_grid_fit.h"

#include "../catch_amalgamated.hpp"

using helix::ui::fit_tile_grid;
using helix::ui::TileGridFit;

namespace {

constexpr int kMinW = 76;
constexpr int kMinH = 56;
constexpr int kGap = 4;

TileGridFit fit(int count, int w, int h, int gap = kGap) {
    return fit_tile_grid(count, w, h, gap, gap, kMinW, kMinH);
}

} // namespace

TEST_CASE("fit_tile_grid in a landscape card keeps every tile count to two rows",
          "[tile-grid-fit][calib-grid]") {
    // The 800x480 Controls card's grid.
    const int w = 333, h = 153;
    struct Case {
        int count, cols, rows;
    };
    for (const Case c : {Case{1, 1, 1}, Case{2, 2, 1}, Case{3, 3, 1}, Case{4, 2, 2}, Case{5, 3, 2},
                         Case{6, 3, 2}, Case{7, 4, 2}}) {
        const TileGridFit f = fit(c.count, w, h);
        CAPTURE(c.count);
        CHECK(f.cols == c.cols);
        CHECK(f.rows == c.rows);
        CHECK(f.fits);
    }
}

TEST_CASE("fit_tile_grid stacks rows in a tall card", "[tile-grid-fit][calib-grid]") {
    const TileGridFit six = fit(6, 200, 300);
    CHECK(six.cols == 2);
    CHECK(six.rows == 3);
    CHECK(six.fits);

    const TileGridFit seven = fit(7, 200, 400);
    CHECK(seven.cols == 2);
    CHECK(seven.rows == 4);
}

TEST_CASE("fit_tile_grid widens in a large wide card", "[tile-grid-fit][calib-grid]") {
    const TileGridFit four = fit(4, 700, 200);
    CHECK(four.cols == 4);
    CHECK(four.rows == 1);

    // 4x2 and 3x2 give the same height-bound tile; the full grid wins.
    const TileGridFit six = fit(6, 700, 200);
    CHECK(six.cols == 3);
    CHECK(six.rows == 2);
}

TEST_CASE("fit_tile_grid breaks a tie in score and waste toward more columns",
          "[tile-grid-fit][calib-grid]") {
    // In a square box with a square minimum, 1x2 and 2x1 tie exactly.
    const TileGridFit f = fit_tile_grid(2, 400, 400, 0, 0, 50, 50);
    CHECK(f.cols == 2);
    CHECK(f.rows == 1);

    // 2x2 edges out 1x4 in a tall box: the tighter axis decides, not the shape.
    const TileGridFit tall = fit(4, 200, 300);
    CHECK(tall.cols == 2);
    CHECK(tall.rows == 2);
}

TEST_CASE("fit_tile_grid reports when no shape leaves room for labels",
          "[tile-grid-fit][calib-grid]") {
    const TileGridFit f = fit(6, 100, 60);
    CHECK_FALSE(f.fits);
    CHECK(f.cols == 3);
    CHECK(f.rows == 2);
}

TEST_CASE("fit_tile_grid takes the gaps out of the tile", "[tile-grid-fit][calib-grid]") {
    CHECK(fit(3, 240, 60, 0).fits);
    const TileGridFit gapped = fit(3, 240, 60, 12);
    CHECK_FALSE(gapped.fits);
    CHECK(gapped.cols == 3);
    CHECK(gapped.rows == 1);
}

TEST_CASE("fit_tile_grid degenerate inputs", "[tile-grid-fit][calib-grid]") {
    const TileGridFit none = fit(0, 333, 153);
    CHECK(none.cols == 1);
    CHECK(none.rows == 1);
    CHECK_FALSE(none.fits);

    const TileGridFit unmeasured = fit(5, 0, 0);
    CHECK_FALSE(unmeasured.fits);

    // More tiles than 4x4 holds: the widest grid, flagged as not fitting.
    const TileGridFit crowded = fit(17, 2000, 2000);
    CHECK(crowded.cols == 4);
    CHECK(crowded.rows == 5);
    CHECK_FALSE(crowded.fits);
}

TEST_CASE("fit_tile_grid tile size plus gaps never exceeds the box",
          "[tile-grid-fit][calib-grid]") {
    // A narrow box with a large gap is where percentage widths overflow.
    for (int count = 1; count <= 7; ++count) {
        for (int w : {150, 211, 266, 333}) {
            for (int h : {99, 153, 183, 300}) {
                for (int gap : {2, 6, 12}) {
                    const TileGridFit f = fit_tile_grid(count, w, h, gap, gap + 1, kMinW, kMinH);
                    CAPTURE(count, w, h, gap, f.cols, f.rows, f.tile_w, f.tile_h);
                    CHECK(f.cols * f.rows >= count);
                    CHECK(f.cols * f.tile_w + (f.cols - 1) * gap <= w);
                    CHECK(f.rows * f.tile_h + (f.rows - 1) * (gap + 1) <= h);
                    // Floored, not shrunk: one more pixel each would no longer fit.
                    CHECK(f.cols * (f.tile_w + 1) + (f.cols - 1) * gap > w);
                    CHECK(f.rows * (f.tile_h + 1) + (f.rows - 1) * (gap + 1) > h);
                }
            }
        }
    }
}

TEST_CASE("fit_tile_grid uses the row gap for rows and the column gap for columns",
          "[tile-grid-fit][calib-grid]") {
    // 2x2 in 200x100: tile_w loses one column gap, tile_h one row gap.
    const TileGridFit f = fit_tile_grid(4, 200, 100, 10, 20, 50, 30);
    REQUIRE(f.cols == 2);
    REQUIRE(f.rows == 2);
    CHECK(f.tile_w == 95);
    CHECK(f.tile_h == 40);
}

TEST_CASE("fit_key_grid keeps keys near square in a wide box", "[tile-grid-fit][motion-layout]") {
    // Height-bound: 2 rows of 100 against 6 columns of 200. The keys take the
    // row height and widen only to 1.4x it, not to the full column.
    const auto fit = helix::ui::fit_key_grid(6, 2, 1200, 200, 48);
    CHECK(fit.key_h == 100);
    CHECK(fit.key_w == 140);
}

TEST_CASE("fit_key_grid keeps keys near square in a tall box", "[tile-grid-fit][motion-layout]") {
    const auto fit = helix::ui::fit_key_grid(4, 3, 400, 900, 48);
    CHECK(fit.key_w == 100);
    CHECK(fit.key_h == 140);
}

TEST_CASE("fit_key_grid fills a box already near square", "[tile-grid-fit][motion-layout]") {
    const auto fit = helix::ui::fit_key_grid(4.5, 4, 450, 480, 48);
    CHECK(fit.key_w == 100);
    CHECK(fit.key_h == 120);
}

TEST_CASE("fit_key_grid holds the touch minimum where its cell allows",
          "[tile-grid-fit][motion-layout]") {
    // Columns of 30 against rows of 60: square would be 30x30 and 42 tall at
    // 1.4x; the minimum lifts the height to 48, still inside the 60 row.
    const auto fit = helix::ui::fit_key_grid(3, 2, 90, 120, 48);
    CHECK(fit.key_w == 30);
    CHECK(fit.key_h == 48);
    // A row shorter than the minimum keeps the row: a key never overflows its cell.
    const auto tight = helix::ui::fit_key_grid(3, 2, 90, 60, 48);
    CHECK(tight.key_h == 30);
    CHECK(helix::ui::fit_key_grid(3, 2, 0, 60, 48).key_w == 0);
}
