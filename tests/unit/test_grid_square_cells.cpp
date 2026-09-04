// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// The properties the square-cell sizing model exists to buy: cells that are
// square on every shipping panel, a grid that transposes when the panel
// rotates, and track counts that never reach the degenerate clamps.
//
// Everything here is driven by MEASURED content boxes, because the content box
// is what get_dimensions() divides. Squareness is checked against the measured
// cell (Owner ruling 7, docs/superpowers/plans/2026-08-08-square-cell-grid.md)
// via grid_cell_metrics(), the same production helper
// PanelWidgetManager::rebuild_page() calls — so these assert what the container
// actually renders rather than a reimplementation of it.

#include "ui_breakpoint.h"

#include "grid_layout.h"

#include <cmath>
#include <vector>

#include "../catch_amalgamated.hpp"

using helix::CellMetrics;
using helix::grid_cell_metrics;
using helix::GridDimensions;
using helix::GridLayout;

namespace {

/// One measured geometry: a resolution, the home grid container's content box
/// at that resolution, the gutter (space_xs at that breakpoint), and the grid
/// the sizing model is expected to produce.
///
/// content_w/content_h/gutter were read off a live instance per resolution —
/// `HELIX_SCREEN_SIZE=<WxH> helix-screen --test -vv`, then the
/// `[PanelWidgetManager] Grid layout:` and `Track geometry:` lines. The content
/// box is a property of the panel chrome and does not change with the track
/// count, so it is stable data to pin against.
struct Geometry {
    const char* name;
    int panel_w, panel_h;
    int content_w, content_h;
    int gutter;
    int cols, rows;
};

// clang-format off
const std::vector<Geometry> kMeasured = {
    //  name                        panel        content     gut   grid
    {"micro 480x272",            480,  272,   430,  264,   2,   12,  8},
    {"micro portrait 272x480",   272,  480,   264,  394,   2,    8, 12},
    {"tiny 480x320",             480,  320,   418,  312,   2,   10,  8},
    {"tiny portrait 320x480",    320,  480,   312,  394,   2,    8, 10},
    {"small 480x400",            480,  400,   414,  388,   4,   10, 10},
    {"small portrait 400x480",   400,  480,   388,  390,   4,   10, 10},
    {"medium 800x480",           800,  480,   710,  466,   5,   12,  8},
    {"portrait 480x800",         480,  800,   466,  664,   5,    8, 12},
    {"large 1024x600",          1024,  600,   904,  584,   6,   16, 10},
    {"large portrait 600x1024",  600, 1024,   584,  868,   6,   10, 14},
    {"xlarge 1280x720",         1280,  720,  1128,  700,   8,   16, 10},
    {"xlarge portrait 720x1280", 720, 1280,   700, 1120,   8,   10, 16},
    {"ultrawide 1920x440",      1920,  440,  1832,  428,   4,   46, 10},
    {"ultratall 440x1920",       440, 1920,   428, 1768,   4,   10, 44},
    {"ultratall 320x1480",       320, 1480,   312, 1332,   2,    8, 34},
    {"ultrawide 1480x320",      1480,  320,  1418,  312,   2,   36,  8},
};
// clang-format on

UiBreakpoint bp_of(const Geometry& g) {
    return breakpoint_for(std::min(g.panel_w, g.panel_h));
}

GridDimensions dims_of(const Geometry& g) {
    return GridLayout::get_dimensions(bp_of(g), g.content_w, g.content_h);
}

/// Landscape/portrait pairs, as indices into kMeasured. Every entry is a real
/// measurement of the same panel in both orientations.
const std::vector<std::pair<size_t, size_t>> kRotationPairs = {
    {0, 1}, {2, 3}, {4, 5}, {6, 7}, {8, 9}, {10, 11}, {12, 13}, {15, 14},
};

} // namespace

TEST_CASE("square cells: measured cell aspect is within 25% of square", "[grid_layout][square]") {
    for (const auto& g : kMeasured) {
        auto d = dims_of(g);
        REQUIRE(d.cols > 0);
        REQUIRE(d.rows > 0);
        CellMetrics m = grid_cell_metrics(g.content_w, g.content_h, d.cols, d.rows, g.gutter);
        REQUIRE(m.cell_w > 0.0f);
        REQUIRE(m.cell_h > 0.0f);
        const double aspect = static_cast<double>(m.cell_w) / static_cast<double>(m.cell_h);
        INFO(g.name << " -> " << d.cols << "x" << d.rows << " cell " << m.cell_w << "x" << m.cell_h
                    << " aspect " << aspect);
        CHECK(aspect >= 0.80);
        CHECK(aspect <= 1.25);
    }
}

TEST_CASE("square cells: the shipped grids match the design table", "[grid_layout][square]") {
    for (const auto& g : kMeasured) {
        auto d = dims_of(g);
        INFO(g.name << " content " << g.content_w << "x" << g.content_h);
        CHECK(d.cols == g.cols);
        CHECK(d.rows == g.rows);
    }
}

TEST_CASE("square cells: every track count is a whole number of cells", "[grid_layout][square]") {
    // A track is half a cell. An odd count leaves a trailing half-cell no
    // whole-cell widget can occupy, and edit mode's snap step could never
    // restore a widget dragged onto it.
    for (const auto& g : kMeasured) {
        auto d = dims_of(g);
        INFO(g.name << " -> " << d.cols << "x" << d.rows);
        CHECK(d.cols % GridLayout::TRACKS_PER_CELL == 0);
        CHECK(d.rows % GridLayout::TRACKS_PER_CELL == 0);
    }
}

TEST_CASE("square cells: transposing the content box transposes the grid",
          "[grid_layout][square]") {
    // The invariant the sizing rule itself guarantees: both axes are quantised
    // against the same cell edge, and the breakpoint is chosen from the panel's
    // narrow axis, so it does not change under rotation either.
    for (const auto& g : kMeasured) {
        auto upright = GridLayout::get_dimensions(bp_of(g), g.content_w, g.content_h);
        auto turned = GridLayout::get_dimensions(bp_of(g), g.content_h, g.content_w);
        INFO(g.name << ": " << upright.cols << "x" << upright.rows << " vs " << turned.cols << "x"
                    << turned.rows);
        CHECK(upright.cols == turned.rows);
        CHECK(upright.rows == turned.cols);
    }
}

TEST_CASE("square cells: rotating a panel transposes its grid to within one cell",
          "[grid_layout][square]") {
    // Exact transposition is a property of the content box, not of the panel:
    // panel chrome takes a different bite out of each orientation, so a rotated
    // panel's content box is not the transpose of its own. 1024x600 measures
    // 904x584 upright and 584x868 rotated — 868 is 36px short of 904, which is
    // enough to cross a cell boundary and give 14 rows against 16 columns.
    //
    // What must hold is that the difference stays inside a single cell. A wider
    // gap would mean a widget authored to fill one orientation could not fit the
    // other, which is the failure this invariant exists to catch (#1216).
    for (const auto& [li, pi] : kRotationPairs) {
        const auto& land = kMeasured[li];
        const auto& port = kMeasured[pi];
        auto l = dims_of(land);
        auto p = dims_of(port);
        INFO(land.name << " " << l.cols << "x" << l.rows << " vs " << port.name << " " << p.cols
                       << "x" << p.rows);
        CHECK(std::abs(l.cols - p.rows) <= GridLayout::TRACKS_PER_CELL);
        CHECK(std::abs(l.rows - p.cols) <= GridLayout::TRACKS_PER_CELL);
    }
}

TEST_CASE("square cells: no measured geometry reaches either track clamp",
          "[grid_layout][square]") {
    // MIN_TRACKS is a degenerate-display guard and MAX_TRACKS is a memory
    // ceiling. A real geometry landing on either means the cell size no longer
    // controls the grid and the aspect invariant above is being met by accident.
    for (const auto& g : kMeasured) {
        auto d = dims_of(g);
        INFO(g.name << " -> " << d.cols << "x" << d.rows);
        CHECK(d.cols > GridLayout::MIN_TRACKS);
        CHECK(d.rows > GridLayout::MIN_TRACKS);
        CHECK(d.cols < GridLayout::MAX_TRACKS);
        CHECK(d.rows < GridLayout::MAX_TRACKS);
    }
}

TEST_CASE("square cells: the track clamps bound both ends", "[grid_layout][square]") {
    // Asserted directly rather than through a panel, because no panel reaches
    // them. Both bounds are whole cells, so clamping never yields an odd count.
    auto tiny = GridLayout::get_dimensions(UiBreakpoint::Medium, 1, 1);
    CHECK(tiny.cols == GridLayout::MIN_TRACKS);
    CHECK(tiny.rows == GridLayout::MIN_TRACKS);

    auto huge = GridLayout::get_dimensions(UiBreakpoint::Micro, 100000, 100000);
    CHECK(huge.cols == GridLayout::MAX_TRACKS);
    CHECK(huge.rows == GridLayout::MAX_TRACKS);
}

TEST_CASE("square cells: an unmeasured content box falls to the track floor",
          "[grid_layout][square]") {
    // A container that has not been laid out reports a zero content box. Any
    // consumer asking then gets the floor, deliberately and identically on both
    // axes, rather than a plausible-looking grid derived from nothing.
    auto zero = GridLayout::get_dimensions(UiBreakpoint::Medium, 0, 0);
    CHECK(zero.cols == GridLayout::MIN_TRACKS);
    CHECK(zero.rows == GridLayout::MIN_TRACKS);

    // Negative extents come from an inverted or unresolved lv_area_t and must
    // not wrap into a huge track count.
    auto negative = GridLayout::get_dimensions(UiBreakpoint::Medium, -800, -480);
    CHECK(negative.cols == GridLayout::MIN_TRACKS);
    CHECK(negative.rows == GridLayout::MIN_TRACKS);
}

TEST_CASE("square cells: each axis takes the nearest whole cell, not the largest that fits",
          "[grid_layout][square]") {
    // The defining property of the quantiser, stated against the measured
    // content boxes: the cells granted must be the count whose total edge is
    // closest to the content box. Discarding a partial cell instead spreads it
    // across the tracks that survive and inflates every one of them — micro's
    // 264px height is 3.88 cells, and taking 3 leaves 60px of a 68px cell to
    // redistribute.
    for (const auto& g : kMeasured) {
        auto d = dims_of(g);
        const int cell = GridLayout::TRACKS_PER_CELL *
                         GridLayout::GRID_CELL[static_cast<size_t>(to_int(bp_of(g)))];
        INFO(g.name << " -> " << d.cols << "x" << d.rows << " on a " << cell << "px cell");
        CHECK(std::abs(g.content_w - (d.cols / GridLayout::TRACKS_PER_CELL) * cell) <= cell / 2);
        CHECK(std::abs(g.content_h - (d.rows / GridLayout::TRACKS_PER_CELL) * cell) <= cell / 2);
    }
}
