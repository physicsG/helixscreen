// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_grid_cell_metrics.cpp
 * @brief Track geometry for the home widget grid.
 *
 * LVGL lays LV_GRID_FR(1) tracks out as (content - (n-1)*gutter) / n, with
 * track i starting at i * (track + gutter). These tests pin that arithmetic
 * against hand-computed values so a caller cannot silently go back to the
 * gutter-blind content/n form.
 */

#include "grid_layout.h"

#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix;

TEST_CASE("grid_cell_metrics: zero gutter degenerates to content/n", "[grid_metrics][grid]") {
    CellMetrics m = grid_cell_metrics(480, 272, 6, 4, 0);
    REQUIRE(m.cols == 6);
    REQUIRE(m.rows == 4);
    REQUIRE(m.gutter == 0);
    REQUIRE(m.cell_w == Catch::Approx(80.0f));
    REQUIRE(m.cell_h == Catch::Approx(68.0f));
}

TEST_CASE("grid_cell_metrics: gutters shrink the track", "[grid_metrics][grid]") {
    // 6 columns in 480px with 4px gutters: 5 gaps = 20px, leaving 460/6.
    CellMetrics m = grid_cell_metrics(480, 272, 6, 4, 4);
    REQUIRE(m.cell_w == Catch::Approx(460.0f / 6.0f));
    // 4 rows in 272px with 4px gutters: 3 gaps = 12px, leaving 260/4 = 65.
    REQUIRE(m.cell_h == Catch::Approx(65.0f));
}

TEST_CASE("grid_cell_metrics: tracks plus gutters exactly fill the content",
          "[grid_metrics][grid]") {
    // The invariant that the gutter-blind form violates.
    for (int cols : {2, 3, 6, 8, 14, 17}) {
        CellMetrics m = grid_cell_metrics(480, 272, cols, 4, 5);
        float total = cols * m.cell_w + (cols - 1) * m.gutter;
        INFO("cols=" << cols << " cell_w=" << m.cell_w);
        REQUIRE(total == Catch::Approx(480.0f));
    }
}

TEST_CASE("grid_cell_metrics: degenerate inputs do not divide by zero", "[grid_metrics][grid]") {
    CellMetrics zero_cols = grid_cell_metrics(480, 272, 0, 4, 4);
    REQUIRE(zero_cols.cell_w == Catch::Approx(0.0f));
    CellMetrics zero_rows = grid_cell_metrics(480, 272, 6, 0, 4);
    REQUIRE(zero_rows.cell_h == Catch::Approx(0.0f));
    CellMetrics no_content = grid_cell_metrics(0, 0, 6, 4, 4);
    REQUIRE(no_content.cell_w == Catch::Approx(0.0f));
    REQUIRE(no_content.cell_h == Catch::Approx(0.0f));
}

TEST_CASE("grid_cell_metrics: gutter larger than content clamps to zero, not negative",
          "[grid_metrics][grid]") {
    CellMetrics m = grid_cell_metrics(20, 20, 6, 6, 40);
    REQUIRE(m.cell_w >= 0.0f);
    REQUIRE(m.cell_h >= 0.0f);
}

TEST_CASE("grid_track_origin: track i starts after i tracks and i gutters",
          "[grid_metrics][grid]") {
    CellMetrics m = grid_cell_metrics(480, 272, 6, 4, 4);
    REQUIRE(grid_track_origin(m.cell_w, m.gutter, 0) == Catch::Approx(0.0f));
    REQUIRE(grid_track_origin(m.cell_w, m.gutter, 1) == Catch::Approx(m.cell_w + 4.0f));
    REQUIRE(grid_track_origin(m.cell_w, m.gutter, 3) == Catch::Approx(3.0f * (m.cell_w + 4.0f)));
    // grid_track_origin(n) is the origin of the track AFTER the last one (n ==
    // cols), which is one gutter beyond the content edge — not the edge itself.
    // The content edge is grid_track_origin(n - 1) + cell_w.
    REQUIRE(grid_track_origin(m.cell_w, m.gutter, 6) == Catch::Approx(480.0f + 4.0f));
}

TEST_CASE("grid_track_extent: a span swallows its interior gutters", "[grid_metrics][grid]") {
    CellMetrics m = grid_cell_metrics(480, 272, 6, 4, 4);
    REQUIRE(grid_track_extent(m.cell_w, m.gutter, 1) == Catch::Approx(m.cell_w));
    // 2 tracks + the 1 gutter between them.
    REQUIRE(grid_track_extent(m.cell_w, m.gutter, 2) == Catch::Approx(2 * m.cell_w + 4.0f));
    // A full-width span is the whole content area.
    REQUIRE(grid_track_extent(m.cell_w, m.gutter, 6) == Catch::Approx(480.0f));
}

TEST_CASE("grid_count_tracks: counts to the terminator", "[grid_metrics][grid]") {
    const int32_t four[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                            LV_GRID_TEMPLATE_LAST};
    REQUIRE(grid_count_tracks(four) == 4);

    const int32_t one[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    REQUIRE(grid_count_tracks(one) == 1);

    const int32_t none[] = {LV_GRID_TEMPLATE_LAST};
    REQUIRE(grid_count_tracks(none) == 0);
}

TEST_CASE("grid_count_tracks: null and unterminated are invalid, not crashes",
          "[grid_metrics][grid]") {
    REQUIRE(grid_count_tracks(nullptr) == 0);

    // No terminator within the cap: report invalid rather than walking off the heap.
    std::vector<int32_t> runaway(300, LV_GRID_FR(1));
    REQUIRE(grid_count_tracks(runaway.data()) == 0);
}
