// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_speed_flow_layout.cpp
 * @brief decide_speed_flow_layout() at each rung boundary, idle and printing.
 *
 * The needs below are the boxes SpeedFlowWidget measured at the medium tier, in the
 * faces the labels draw in (icon_font_xs, font_xs/small/body/heading), and the budgets
 * are the content boxes of the tile sizes in the design's pixel-budget table (the
 * widget's chrome at medium is 9px on each axis).
 *
 * Run with: ./build/bin/helix-tests "[speed_flow][layout]"
 */

#include "src/ui/panel_widgets/speed_flow_layout.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

constexpr RungNeeds kIdle{rung_box(33, 37), rung_box(86, 19),  rung_box(23, 73),
                          rung_box(71, 49), rung_box(107, 49), rung_box(72, 119)};
constexpr RungNeeds kPrinting{rung_box(33, 37), rung_box(86, 19),  rung_box(23, 73),
                              rung_box(71, 91), rung_box(194, 49), rung_box(162, 131)};
/// The measured readings, units included, standing in for the percentages on the three
/// small rungs. They are wider, so they cost a rung's width where the percentages fit.
constexpr RungNeeds kMeasured{rung_box(58, 37), rung_box(170, 19), rung_box(65, 73)};

constexpr int kChrome = 9;

SpeedFlowVerdict verdict_for(int w, int h, bool printing, bool physical = false) {
    return decide_speed_flow_layout(
        {w, h, printing, physical, printing ? kPrinting : kIdle, kMeasured});
}

/// The verdict for a tile box of the design's budget table.
SpeedFlowVerdict verdict_for_tile(int tile_w, int tile_h, bool printing, bool physical = false) {
    return verdict_for(tile_w - kChrome, tile_h - kChrome, printing, physical);
}

} // namespace

TEST_CASE("speed_flow layout: idle rung boundaries are exact", "[speed_flow][layout]") {
    CHECK(verdict_for(33, 37, false).rung == SizeRung::Glance);
    CHECK(verdict_for(86, 19, false).rung == SizeRung::Strip);
    CHECK(verdict_for(85, 36, false).rung == SizeRung::Glance);
    CHECK(verdict_for(86, 18, false).rung == SizeRung::Glance);
    CHECK_FALSE(verdict_for(86, 18, false).fits);
    CHECK(verdict_for(23, 73, false).rung == SizeRung::Column);
    CHECK(verdict_for(23, 72, false).rung == SizeRung::Glance);
    CHECK(verdict_for(71, 49, false).rung == SizeRung::Cell);
    CHECK(verdict_for(70, 49, false).rung == SizeRung::Glance);
    CHECK(verdict_for(71, 48, false).rung == SizeRung::Glance);
    CHECK(verdict_for(86, 48, false).rung == SizeRung::Strip);
    CHECK(verdict_for(107, 49, false).rung == SizeRung::Wide);
    CHECK(verdict_for(106, 49, false).rung == SizeRung::Cell);
    CHECK(verdict_for(72, 119, false).rung == SizeRung::Full);
    CHECK(verdict_for(72, 118, false).rung == SizeRung::Cell);
    CHECK(verdict_for(107, 118, false).rung == SizeRung::Wide);
}

TEST_CASE("speed_flow layout: printing needs more room at Cell, Wide and Full",
          "[speed_flow][layout]") {
    // The measured line under each value costs Cell height.
    CHECK(verdict_for(71, 91, true).rung == SizeRung::Cell);
    CHECK(verdict_for(71, 90, true).rung != SizeRung::Cell);
    // The measured reading at the right costs Wide width.
    CHECK(verdict_for(194, 49, true).rung == SizeRung::Wide);
    CHECK(verdict_for(193, 49, true).rung == SizeRung::Strip);
    // Steppers cost the Full box width and height.
    CHECK(verdict_for(162, 131, true).rung == SizeRung::Full);
    CHECK(verdict_for(161, 131, true).rung == SizeRung::Cell);
    CHECK(verdict_for(162, 130, true).rung == SizeRung::Cell);
    CHECK(verdict_for(194, 130, true).rung == SizeRung::Wide);
}

TEST_CASE("speed_flow layout: the design's pixel budgets", "[speed_flow][layout]") {
    struct Row {
        const char* name;
        int w, h;
        bool printing;
        SizeRung rung;
    };
    const Row rows[] = {
        // 800x480
        {"medium 0.5x0.5", 55, 54, false, SizeRung::Glance},
        {"medium 0.5x0.5 printing", 55, 54, true, SizeRung::Glance},
        {"medium 1x0.5", 114, 54, false, SizeRung::Strip},
        {"medium 1x0.5 printing", 114, 54, true, SizeRung::Strip},
        {"medium 0.5x1", 55, 113, false, SizeRung::Column},
        {"medium 0.5x1 printing", 55, 113, true, SizeRung::Column},
        {"medium 1x1", 114, 113, false, SizeRung::Cell},
        {"medium 1x1 printing", 114, 113, true, SizeRung::Cell},
        // 1024x600 half cells
        {"large 0.5x0.5", 51, 53, false, SizeRung::Glance},
        {"large 0.5x1", 51, 112, false, SizeRung::Column},
        {"large 1x0.5", 108, 53, false, SizeRung::Strip},
        {"large 0.5x0.5 printing", 51, 53, true, SizeRung::Glance},
        {"large 0.5x1 printing", 51, 112, true, SizeRung::Column},
        {"large 1x0.5 printing", 108, 53, true, SizeRung::Strip},
    };
    for (const auto& r : rows) {
        INFO(r.name);
        const auto v = verdict_for_tile(r.w, r.h, r.printing);
        CHECK(v.fits);
        CHECK(v.rung == r.rung);
    }
}

TEST_CASE("speed_flow layout: the micro 1x1 budget", "[speed_flow][layout]") {
    // Micro faces are smaller, so the needs differ from the medium ones above; the
    // chrome there is 3px on each axis.
    constexpr RungNeeds micro_idle{rung_box(28, 34), rung_box(74, 19), rung_box(20, 67),
                                   rung_box(51, 37), rung_box(70, 35), rung_box(38, 71)};
    constexpr RungNeeds micro_printing{rung_box(28, 34), rung_box(74, 19),  rung_box(20, 67),
                                       rung_box(55, 69), rung_box(125, 35), rung_box(90, 81)};
    const auto at = [](const RungNeeds& needs, int w, int h) {
        return decide_speed_flow_layout({w - 3, h - 3, false, false, needs});
    };
    // Idle: a whole cell holds both percentages in rows.
    CHECK(at(micro_idle, 70, 64).rung == SizeRung::Cell);
    CHECK(at(micro_idle, 70, 64).fits);
    CHECK(at(micro_idle, 53, 64).rung != SizeRung::Cell);
    CHECK(at(micro_idle, 70, 40).rung == SizeRung::Cell);
    CHECK(at(micro_idle, 70, 39).rung != SizeRung::Cell);
    // Printing: the measured line does not fit 64px, so the tile keeps what it can
    // draw and never clips.
    CHECK(at(micro_printing, 70, 64).fits);
    CHECK(at(micro_printing, 70, 64).rung == SizeRung::Glance);
    CHECK(at(micro_printing, 70, 72).rung == SizeRung::Cell);
    CHECK(at(micro_printing, 70, 71).rung != SizeRung::Cell);
}

TEST_CASE("speed_flow layout: flags follow rung and state", "[speed_flow][layout]") {
    struct Row {
        int w, h;
        bool printing, physical;
        SizeRung rung;
        bool measured, value_measured, steppers;
    };
    const Row rows[] = {
        // idle never shows measured values or steppers, whatever the setting
        {72, 119, false, false, SizeRung::Full, false, false, false},
        {72, 119, false, true, SizeRung::Full, false, false, false},
        {33, 37, false, true, SizeRung::Glance, false, false, false},
        // printing, small rungs: only the physical-units setting swaps the value
        {33, 37, true, false, SizeRung::Glance, false, false, false},
        {58, 37, true, true, SizeRung::Glance, false, true, false},
        {57, 37, true, true, SizeRung::Glance, false, false, false},
        {170, 19, true, true, SizeRung::Strip, false, true, false},
        {169, 19, true, true, SizeRung::Strip, false, false, false},
        {65, 73, true, true, SizeRung::Column, false, true, false},
        {64, 73, true, true, SizeRung::Column, false, false, false},
        // printing at Cell and up: both shown, whatever the setting
        {71, 91, true, false, SizeRung::Cell, true, false, false},
        {71, 91, true, true, SizeRung::Cell, true, false, false},
        {194, 49, true, true, SizeRung::Wide, true, false, false},
        // steppers only at Full, only while printing
        {162, 131, true, false, SizeRung::Full, true, false, true},
        {400, 400, true, true, SizeRung::Full, true, false, true},
    };
    for (const auto& r : rows) {
        INFO(r.w << "x" << r.h << " printing=" << r.printing << " physical=" << r.physical);
        const auto v = verdict_for(r.w, r.h, r.printing, r.physical);
        CHECK(v.rung == r.rung);
        CHECK(v.show_measured == r.measured);
        CHECK(v.value_is_measured == r.value_measured);
        CHECK(v.show_steppers == r.steppers);
    }
}

TEST_CASE("speed_flow layout: fits is false below Glance and monotonic above",
          "[speed_flow][layout]") {
    CHECK_FALSE(verdict_for(32, 37, false).fits);
    CHECK_FALSE(verdict_for(33, 36, false).fits);
    CHECK(verdict_for(33, 37, false).fits);

    for (const bool printing : {false, true}) {
        bool fitted_w = false;
        for (int w = 0; w <= 300; ++w) {
            const bool now = verdict_for(w, 200, printing).fits;
            if (fitted_w)
                CHECK(now);
            fitted_w = fitted_w || now;
        }
        bool fitted_h = false;
        for (int h = 0; h <= 300; ++h) {
            const bool now = verdict_for(300, h, printing).fits;
            if (fitted_h)
                CHECK(now);
            fitted_h = fitted_h || now;
        }
    }
}
