// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_grid_edit_drop.cpp
 * @brief How a released home-grid edit drag resolves: move its widget, create
 * a page before the first or past the last one, swap it with the widget it
 * lands on, return to its origin page, or snap back
 * (prestonbrown/helixscreen#1638, prestonbrown/helixscreen#1503).
 */

#include "grid_edit_drop.h"
#include "grid_layout.h"
#include "panel_widget_registry.h"

#include <vector>

#include "../catch_amalgamated.hpp"

using helix::DropInput;
using helix::DropOutcome;
using helix::DropResolution;
using helix::GridLayout;
using helix::GridPlacement;
using helix::resolve_drop;

namespace {

/// A 12x8 track grid, and a one-cell widget of two tracks each way.
constexpr int COLS = 12;
constexpr int ROWS = 8;
constexpr int SPAN = 2;
/// Two config pages: page 1 is the last, page 2 the next-page slot.
constexpr int PAGES = 2;

/// A release on page @p page, previewing (@p col, @p row), of a drag whose entry
/// sits at (0,0) on page 0, with a next-page slot and the widget inside the
/// page frame.
DropInput release_on(int page, int col, int row) {
    DropInput in;
    in.page_index = page;
    in.page_count = PAGES;
    in.origin_page = 0;
    in.origin_col = 0;
    in.origin_row = 0;
    in.colspan = SPAN;
    in.rowspan = SPAN;
    in.target_col = col;
    in.target_row = row;
    in.has_next_page_slot = true;
    in.past_right_border = false;
    return in;
}

DropInput past_border(DropInput in) {
    in.past_right_border = true;
    return in;
}

DropInput past_left_border(DropInput in) {
    in.past_left_border = true;
    return in;
}

DropInput without_slot(DropInput in) {
    in.has_next_page_slot = false;
    return in;
}

GridLayout occupied_by(const std::vector<GridPlacement>& widgets) {
    GridLayout grid(UiBreakpoint::Medium, {COLS, ROWS});
    for (const GridPlacement& widget : widgets) {
        REQUIRE(grid.place(widget));
    }
    return grid;
}

struct Row {
    const char* what;
    DropInput in;
    std::vector<GridPlacement> occupants;
    DropOutcome outcome;
    int col; ///< Checked for Move and CreatePage only
    int row;
};

void check_rows(const std::vector<Row>& rows) {
    for (const Row& row : rows) {
        INFO(row.what);
        const DropResolution drop = resolve_drop(row.in, occupied_by(row.occupants));
        CHECK(drop.outcome == row.outcome);
        if (row.outcome == DropOutcome::Move || row.outcome == DropOutcome::CreatePage) {
            CHECK(drop.col == row.col);
            CHECK(drop.row == row.row);
        }
    }
}

} // namespace

TEST_CASE("a release on a config page moves its widget, snaps back or returns home",
          "[1638][grid_edit][cross_page]") {
    const GridPlacement fan{"fan", 4, 0, SPAN, SPAN};
    check_rows({
        {"a free cell on the origin page", release_on(0, 6, 2), {fan}, DropOutcome::Move, 6, 2},
        {"the origin cell on the origin page",
         release_on(0, 0, 0),
         {fan},
         DropOutcome::Cancel,
         -1,
         -1},
        {"a cell the span overhangs the grid from",
         release_on(0, COLS - 1, 0),
         {fan},
         DropOutcome::Cancel,
         -1,
         -1},
        {"no previewed cell, on the origin page",
         release_on(0, -1, -1),
         {fan},
         DropOutcome::Cancel,
         -1,
         -1},
        {"the origin cell's coordinates on another page",
         release_on(1, 0, 0),
         {fan},
         DropOutcome::Move,
         0,
         0},
        {"a cell another widget holds, on another page",
         release_on(1, 4, 0),
         {fan},
         DropOutcome::ReturnToOrigin,
         -1,
         -1},
        {"no previewed cell, on another page",
         release_on(1, -1, -1),
         {fan},
         DropOutcome::ReturnToOrigin,
         -1,
         -1},
    });
}

TEST_CASE("a release creates a page anywhere on the next-page slot, and from the last page only "
          "past its right border",
          "[1638][grid_edit][cross_page]") {
    // The last page is full where a created page's entry lands: a created page
    // is empty, so the last page's widgets never decide it.
    const GridPlacement rightmost{"clock", COLS - SPAN, 4, SPAN, SPAN};
    check_rows({
        {"a previewed cell on the next-page slot",
         release_on(PAGES, 6, 2),
         {},
         DropOutcome::CreatePage,
         6,
         2},
        {"the next-page slot with no previewed cell",
         release_on(PAGES, -1, -1),
         {},
         DropOutcome::ReturnToOrigin,
         -1,
         -1},
        {"the next-page slot with no page to create",
         without_slot(release_on(PAGES, 6, 2)),
         {},
         DropOutcome::ReturnToOrigin,
         -1,
         -1},
        {"past the last page's right border",
         past_border(release_on(PAGES - 1, 8, 4)),
         {rightmost},
         DropOutcome::CreatePage,
         COLS - SPAN,
         4},
        {"past the last page's right border with no previewed row",
         past_border(release_on(PAGES - 1, -1, -1)),
         {},
         DropOutcome::CreatePage,
         COLS - SPAN,
         0},
        {"inside the last page, as after a flip back off the slot",
         release_on(PAGES - 1, 6, 2),
         {},
         DropOutcome::Move,
         6,
         2},
        {"past an earlier page's right border",
         past_border(release_on(0, 6, 2)),
         {},
         DropOutcome::Move,
         6,
         2},
        {"past the last page's right border with no page to create",
         without_slot(past_border(release_on(PAGES - 1, 6, 2))),
         {},
         DropOutcome::Move,
         6,
         2},
    });
}

TEST_CASE("a created page whose landing cell does not fit an empty page is not created",
          "[1638][grid_edit][cross_page]") {
    DropInput on_slot = release_on(PAGES, COLS - 1, 0);
    CHECK(resolve_drop(on_slot, occupied_by({})).outcome == DropOutcome::ReturnToOrigin);

    DropInput too_wide = past_border(release_on(PAGES - 1, 0, 0));
    too_wide.colspan = COLS + SPAN;
    CHECK(resolve_drop(too_wide, occupied_by({})).outcome == DropOutcome::ReturnToOrigin);

    DropInput too_wide_left = past_left_border(release_on(0, 0, 0));
    too_wide_left.colspan = COLS + SPAN;
    CHECK(resolve_drop(too_wide_left, occupied_by({})).outcome == DropOutcome::Cancel);
}

TEST_CASE("a release past the first page's left border creates a page before it",
          "[1638][grid_edit][cross_page]") {
    // Page 0 holds the drag's origin; a page created before it lands the entry
    // in the leftmost columns its span allows, on the previewed row.
    const GridPlacement fan{"fan", 4, 0, SPAN, SPAN};
    struct LeftRow {
        const char* what;
        DropInput in;
        std::vector<GridPlacement> occupants;
        DropOutcome outcome;
        int col;
        int row;
        bool prepend;
    };
    const LeftRow rows[] = {
        {"a previewed row past the first page's left border",
         past_left_border(release_on(0, 6, 2)),
         {fan},
         DropOutcome::CreatePage,
         0,
         2,
         true},
        {"no previewed row past the first page's left border",
         past_left_border(release_on(0, -1, -1)),
         {},
         DropOutcome::CreatePage,
         0,
         0,
         true},
        {"past the first page's left border with no page to create",
         without_slot(past_left_border(release_on(0, 6, 2))),
         {fan},
         DropOutcome::Move,
         6,
         2,
         false},
        {"past a later page's left border",
         past_left_border(release_on(1, 6, 2)),
         {},
         DropOutcome::Move,
         6,
         2,
         false},
        {"past the last page's right border still appends",
         past_border(release_on(PAGES - 1, 8, 4)),
         {},
         DropOutcome::CreatePage,
         COLS - SPAN,
         4,
         false},
    };
    for (const LeftRow& row : rows) {
        INFO(row.what);
        const DropResolution drop = resolve_drop(row.in, occupied_by(row.occupants));
        CHECK(drop.outcome == row.outcome);
        CHECK(drop.prepend_page == row.prepend);
        if (row.outcome == DropOutcome::Move || row.outcome == DropOutcome::CreatePage) {
            CHECK(drop.col == row.col);
            CHECK(drop.row == row.row);
        }
    }

    // A single page is both first and last: its right border still appends.
    DropInput only_page = past_border(release_on(0, 6, 2));
    only_page.page_count = 1;
    const DropResolution append = resolve_drop(only_page, occupied_by({}));
    CHECK(append.outcome == DropOutcome::CreatePage);
    CHECK_FALSE(append.prepend_page);
    CHECK(append.col == COLS - SPAN);
}

TEST_CASE("a release onto one other widget on the origin page swaps the two", "[1503][grid_edit]") {
    // The dragged entry sits at (0,0), SPAN x SPAN, and lands on the occupant's
    // cell; the occupant takes the dragged entry's origin cell.
    const GridPlacement fan{"fan", 4, 0, SPAN, SPAN};
    const GridPlacement wide{"wide", 4, 0, 2 * SPAN, SPAN};
    const GridPlacement tall{"tall", 4, 0, SPAN, 2 * SPAN};
    struct SwapRow {
        const char* what;
        DropInput in;
        std::vector<GridPlacement> occupants;
        int col;
        int row;
        GridPlacement swapped;
    };
    DropInput from_low_row = release_on(0, 4, 0);
    from_low_row.origin_row = 2;
    const SwapRow rows[] = {
        {"a same-span widget, on its cell",
         release_on(0, 4, 0),
         {fan},
         4,
         0,
         {"fan", 0, 0, SPAN, SPAN}},
        {"a same-span widget the preview only overlaps",
         release_on(0, 3, 1),
         {fan},
         4,
         0,
         {"fan", 0, 0, SPAN, SPAN}},
        {"a wider widget with room at the origin",
         release_on(0, 4, 0),
         {wide},
         4,
         0,
         {"wide", 0, 0, 2 * SPAN, SPAN}},
        {"a taller widget with room at the origin",
         from_low_row,
         {tall},
         4,
         0,
         {"tall", 0, 2, SPAN, 2 * SPAN}},
    };
    for (const SwapRow& row : rows) {
        INFO(row.what);
        const DropResolution drop = resolve_drop(row.in, occupied_by(row.occupants));
        REQUIRE(drop.outcome == DropOutcome::Swap);
        CHECK(drop.col == row.col);
        CHECK(drop.row == row.row);
        CHECK(drop.swapped.widget_id == row.swapped.widget_id);
        CHECK(drop.swapped.col == row.swapped.col);
        CHECK(drop.swapped.row == row.swapped.row);
        CHECK(drop.swapped.colspan == row.swapped.colspan);
        CHECK(drop.swapped.rowspan == row.swapped.rowspan);
    }
}

TEST_CASE("a swap that does not fit both ways commits nothing", "[1503][grid_edit]") {
    const GridPlacement fan{"fan", 4, 0, SPAN, SPAN};
    const GridPlacement lamp{"lamp", 2, 0, SPAN, SPAN};
    const GridPlacement wide{"wide", 4, 0, 2 * SPAN, SPAN};
    const GridPlacement near_wide{"wide", 2, 0, 2 * SPAN, SPAN};
    const GridPlacement tall{"tall", 4, 0, SPAN, 2 * SPAN};
    DropInput from_bottom = release_on(0, 4, 0);
    from_bottom.origin_row = ROWS - SPAN;
    check_rows({
        {"a wider widget whose origin landing overlaps the dragged one's",
         release_on(0, 2, 0),
         {near_wide},
         DropOutcome::Cancel,
         -1,
         -1},
        {"a wider widget whose origin landing overlaps a third widget",
         release_on(0, 4, 0),
         {wide, lamp},
         DropOutcome::Cancel,
         -1,
         -1},
        {"a taller widget that would overhang the grid from the origin",
         from_bottom,
         {tall},
         DropOutcome::Cancel,
         -1,
         -1},
        {"a preview over two widgets",
         release_on(0, 3, 0),
         {fan, lamp},
         DropOutcome::Cancel,
         -1,
         -1},
    });
}

// --- placement_refusal: the check behind `ctl home place` -------------------

TEST_CASE("a placement is refused outside the def's spans, off whole cells, or on a taken track",
          "[grid_edit][ctl]") {
    helix::PanelWidgetDef def;
    def.id = "probe";
    def.colspan = 2;
    def.rowspan = 2;
    def.max_colspan = 8;
    def.max_rowspan = 4;
    GridLayout occupancy(UiBreakpoint::Medium, {COLS, ROWS});
    REQUIRE(occupancy.place({"other", 8, 0, 2, 2}));

    CHECK(helix::placement_refusal(def, occupancy, 0, 0, 8, 4).empty());
    CHECK_FALSE(helix::placement_refusal(def, occupancy, 0, 0, 10, 2).empty()); // past max
    CHECK_FALSE(helix::placement_refusal(def, occupancy, 0, 0, 2, 6).empty());  // past max
    CHECK_FALSE(helix::placement_refusal(def, occupancy, 0, 0, 3, 2).empty());  // half cell
    CHECK(helix::placement_refusal(def, occupancy, 6, 0, 4, 2) ==
          "2x1 at (3, 0) overlaps another widget");
    CHECK(helix::placement_refusal(def, occupancy, 10, 2, 4, 2) ==
          "2x1 at (5, 1) leaves the 6x4 grid");

    def.supports_half_col = true;
    CHECK(helix::placement_refusal(def, occupancy, 0, 0, 3, 2).empty());
    CHECK_FALSE(helix::placement_refusal(def, occupancy, 0, 0, 2, 3).empty()); // rows stay whole
}
