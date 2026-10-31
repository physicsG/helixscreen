// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_dryer_layout.cpp
 * @brief decide_dryer_layout() at each rung boundary, idle and drying, plus the ring
 * and the row count.
 *
 * The needs below are round illustrative boxes, so each boundary is exact and easy to read;
 * test_widget_size_dryer.cpp pins the real measured ladder against the pixel budgets.
 *
 * Run with: ./build/bin/helix-tests "[dryer][layout]"
 */

#include "src/ui/panel_widgets/dryer_layout.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

constexpr RungNeeds kIdle{rung_box(30, 34), rung_box(60, 18), rung_box(30, 60),
                          rung_box(60, 66), rung_box(60, 66), rung_box(100, 150)};
constexpr RungNeeds kDrying{rung_box(40, 40), rung_box(80, 34),  rung_box(40, 80),
                            rung_box(60, 74), rung_box(150, 70), rung_box(150, 150)};

constexpr int kRowH = 44;
constexpr int kRowGap = 4;
constexpr int kMaxRows = 8;

DryerVerdict verdict_for(int w, int h, bool drying, bool multi = false, bool known = true,
                         int sources = 3) {
    DryerInput in;
    in.content_w = w;
    in.content_h = h;
    in.drying = drying;
    in.multi = multi;
    in.progress_known = known;
    in.source_count = sources;
    in.needs = drying ? kDrying : kIdle;
    in.row_h = kRowH;
    in.row_gap = kRowGap;
    in.max_rows = kMaxRows;
    return decide_dryer_layout(in);
}

} // namespace

TEST_CASE("dryer layout: idle rung boundaries are exact", "[dryer][layout]") {
    CHECK(verdict_for(30, 34, false).rung == SizeRung::Glance);
    CHECK_FALSE(verdict_for(29, 34, false).fits);
    CHECK_FALSE(verdict_for(30, 33, false).fits);
    CHECK(verdict_for(60, 18, false).rung == SizeRung::Strip);
    CHECK(verdict_for(60, 17, false).rung == SizeRung::Glance);
    CHECK(verdict_for(30, 60, false).rung == SizeRung::Column);
    CHECK(verdict_for(30, 59, false).rung == SizeRung::Glance);
    CHECK(verdict_for(60, 66, false).rung == SizeRung::Wide);
    CHECK(verdict_for(59, 66, false).rung == SizeRung::Column);
    CHECK(verdict_for(100, 150, false).rung == SizeRung::Full);
    CHECK(verdict_for(100, 149, false).rung == SizeRung::Wide);
}

TEST_CASE("dryer layout: drying changes what each rung needs", "[dryer][layout]") {
    CHECK(verdict_for(60, 74, true).rung == SizeRung::Cell);
    CHECK(verdict_for(60, 73, true).rung == SizeRung::Glance);
    CHECK(verdict_for(150, 70, true).rung == SizeRung::Wide);
    CHECK(verdict_for(149, 70, true).rung == SizeRung::Strip);
    // The same box idle holds more than it does while drying.
    CHECK(verdict_for(60, 66, false).rung == SizeRung::Wide);
    CHECK(verdict_for(60, 66, true).rung == SizeRung::Glance);
}

TEST_CASE("dryer layout: a tall narrow box takes Column, a short wide one Strip",
          "[dryer][layout]") {
    CHECK(verdict_for(45, 90, true).rung == SizeRung::Column);
    CHECK(verdict_for(90, 45, true).rung == SizeRung::Strip);
}

TEST_CASE("dryer layout: the ring needs a drying source with a known progress", "[dryer][layout]") {
    CHECK(verdict_for(40, 40, true, false, true).show_ring);
    CHECK_FALSE(verdict_for(40, 40, true, false, false).show_ring);
    CHECK_FALSE(verdict_for(40, 40, false, false, true).show_ring);
}

TEST_CASE("dryer layout: the ring rungs are Glance, Cell and single-source Wide",
          "[dryer][layout]") {
    CHECK(verdict_for(60, 74, true).show_ring);              // Cell
    CHECK(verdict_for(150, 70, true, false).show_ring);      // Wide, one source
    CHECK_FALSE(verdict_for(150, 70, true, true).show_ring); // Wide, several
    CHECK_FALSE(verdict_for(80, 34, true).show_ring);        // Strip
    CHECK_FALSE(verdict_for(150, 150, true).show_ring);      // Full has bars instead
}

TEST_CASE("dryer layout: Full lists as many sources as fit, never more than exist",
          "[dryer][layout]") {
    // 100 -> floor((h + 4) / 48)
    CHECK(verdict_for(100, 150, false, false, true, 6).row_count == 3);
    CHECK(verdict_for(100, 188, false, false, true, 6).row_count == 4);
    CHECK(verdict_for(100, 187, false, false, true, 6).row_count == 3);
    CHECK(verdict_for(100, 400, false, false, true, 2).row_count == 2);
    CHECK(verdict_for(100, 400, false, false, true, 20).row_count == kMaxRows);
    CHECK(verdict_for(100, 150, false, false, true, 0).row_count == 1);
    CHECK(verdict_for(60, 66, false).row_count == 0);
}

TEST_CASE("dryer layout: fits_at is monotonic in both axes", "[dryer][layout]") {
    for (bool drying : {false, true}) {
        for (int w = 20; w < 260; w += 5) {
            for (int h = 10; h < 240; h += 5) {
                if (!verdict_for(w, h, drying).fits)
                    continue;
                INFO(w << "x" << h << " drying=" << drying);
                CHECK(verdict_for(w + 5, h, drying).fits);
                CHECK(verdict_for(w, h + 5, drying).fits);
            }
        }
    }
}

TEST_CASE("dryer layout: the drop takes the largest rung the text leaves room for",
          "[dryer][layout]") {
    // A glyph ladder of 10, 20, 30, 40, 50 and 60 px squares.
    constexpr int kGlyph[kTileRungs] = {10, 20, 30, 40, 50, 60};
    constexpr DropTextBlock kBeside{80, 30, true, 4, 2};
    constexpr DropTextBlock kStacked{40, 12, false, 2, 2};

    // Beside: 40 + 4 + 80 + 2 = 126 wide, 40 + 1 tall.
    CHECK(drop_block_box(40, 40, kBeside).w == 126);
    CHECK(drop_block_box(40, 40, kBeside).h == 41);
    CHECK(choose_drop_rung(126, 41, kGlyph, kGlyph, kBeside) == 3);
    CHECK(choose_drop_rung(125, 200, kGlyph, kGlyph, kBeside) == 2);
    CHECK(choose_drop_rung(126, 40, kGlyph, kGlyph, kBeside) == 2);
    // A short glyph sits inside the text's height.
    CHECK(drop_block_box(20, 20, kBeside).h == 31);

    // Stacked: 30 over 2 + 12, so 45 tall; width is the wider of the two plus slack.
    CHECK(choose_drop_rung(42, 45, kGlyph, kGlyph, kStacked) == 2);
    CHECK(choose_drop_rung(42, 44, kGlyph, kGlyph, kStacked) == 1);
    CHECK(choose_drop_rung(500, 500, kGlyph, kGlyph, kStacked) == kTileRungs - 1);
    // No room for even the smallest: the floor the rung was measured at.
    CHECK(choose_drop_rung(5, 5, kGlyph, kGlyph, kStacked) == 0);
}
