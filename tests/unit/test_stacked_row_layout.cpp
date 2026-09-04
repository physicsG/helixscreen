// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_stacked_row_layout.cpp
 * @brief The temp_stack tall-layout decision, exercised without a display.
 *
 * wants_icon_above() is the whole rule for the icon-over-temperature
 * layout: an authored height of more than one grid cell AND enough measured
 * pixels for every visible heater to draw two lines. Both halves matter and
 * both are asserted here — the rowspan gate alone would promote a 3-track
 * widget on a Micro panel that has nowhere to put the second line, and the
 * pixel test alone would promote a one-cell widget on XXLarge, where a single
 * cell is tall enough to fit three stacked pairs.
 *
 * The widget-level counterpart (fonts and flex_flow actually applied to the
 * rows) is tests/unit/test_widget_size_temp_stack.cpp.
 */

#include "src/ui/panel_widgets/stacked_row_layout.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {
// A representative Small-tier row: 24px icon glyph, 12px temperature text,
// 2px between them.
constexpr StackedRowMetrics SMALL_ROW{24, 12, 2};
constexpr int ROW_GAP = 4;

// Height that exactly fits `rows` stacked heaters at SMALL_ROW.
constexpr int exact_fit(int rows) {
    return rows * (SMALL_ROW.icon_px + SMALL_ROW.icon_gap_px + SMALL_ROW.text_px) +
           (rows - 1) * ROW_GAP;
}
} // namespace

TEST_CASE("temp_stack tall layout needs more than one cell of authored height",
          "[widget_size][temp_stack][layout]") {
    // Plenty of pixels, so rowspan is the only thing under test.
    const int roomy = exact_fit(3) * 4;

    // The default 2x2-track size is one grid cell — rows stay side by side
    // there no matter how many pixels that cell happens to be worth.
    CHECK_FALSE(wants_icon_above(2, roomy, 3, SMALL_ROW, ROW_GAP));
    CHECK_FALSE(wants_icon_above(1, roomy, 3, SMALL_ROW, ROW_GAP));

    // 3 tracks (1.5 cells) is the first size that qualifies; 4 (the widget's
    // max_rowspan) also does.
    CHECK(wants_icon_above(3, roomy, 3, SMALL_ROW, ROW_GAP));
    CHECK(wants_icon_above(4, roomy, 3, SMALL_ROW, ROW_GAP));

    // The gate is pinned to the half-cell grid, not to a bare literal.
    STATIC_REQUIRE(ICON_ABOVE_MIN_ROWSPAN == 3);
}

TEST_CASE("temp_stack tall layout needs pixels for every visible heater",
          "[widget_size][temp_stack][layout]") {
    // Exactly enough is enough; one pixel short is not.
    CHECK(wants_icon_above(4, exact_fit(3), 3, SMALL_ROW, ROW_GAP));
    CHECK_FALSE(wants_icon_above(4, exact_fit(3) - 1, 3, SMALL_ROW, ROW_GAP));

    // A printer with no chamber sensor hides that row, so the same height that
    // could not fit three heaters comfortably fits two. Dropping the visible
    // count must be what changes the verdict, not the height.
    const int two_row_height = exact_fit(2);
    CHECK_FALSE(wants_icon_above(3, two_row_height, 3, SMALL_ROW, ROW_GAP));
    CHECK(wants_icon_above(3, two_row_height, 2, SMALL_ROW, ROW_GAP));
}

TEST_CASE("temp_stack tall layout falls back to rows when it cannot measure",
          "[widget_size][temp_stack][layout]") {
    const int roomy = exact_fit(3) * 4;

    // Pre-layout: the widget has been created but never sized.
    CHECK_FALSE(wants_icon_above(4, 0, 3, SMALL_ROW, ROW_GAP));
    CHECK_FALSE(wants_icon_above(4, -1, 3, SMALL_ROW, ROW_GAP));

    // Every heater row hidden — nothing to lay out either way.
    CHECK_FALSE(wants_icon_above(4, roomy, 0, SMALL_ROW, ROW_GAP));

    // Fonts not resolved yet: a zero line height would make any height look
    // roomy enough, so an unmeasurable row must decline instead.
    CHECK_FALSE(wants_icon_above(4, roomy, 3, StackedRowMetrics{0, 12, 2}, ROW_GAP));
    CHECK_FALSE(wants_icon_above(4, roomy, 3, StackedRowMetrics{24, 0, 2}, ROW_GAP));
}
