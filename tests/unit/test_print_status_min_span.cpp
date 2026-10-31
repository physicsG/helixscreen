// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_print_status_min_span.cpp
 * @brief print_status refuses a box its idle card cannot draw in: the filename,
 * the detail column and both buttons.
 */

#include "ui_breakpoint.h"

#include "../lvgl_ui_test_fixture.h"
#include "../test_helpers/measured_grids.h"
#include "../test_helpers/panel_widget_size_harness.h"
#include "default_layout_resolver.h"
#include "grid_layout.h"
#include "layout_manager.h"
#include "panel_widget_config.h"
#include "src/ui/panel_widgets/print_status_widget.h"
#include "theme_manager.h"

#include <algorithm>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

IdleCardNeeds sample_needs() {
    IdleCardNeeds n;
    n.chrome_w = 14;
    n.chrome_h = 14;
    n.title_h = 17;
    n.detail_h = 32;
    n.gap = 4;
    n.button_h = 24;
    n.reprint_w = 100;
    n.queue_w = 90;
    return n;
}

/// Tile pixels for a span on a measured grid, as the grid hands them out.
int extent(float cell, int gutter, int span) {
    return static_cast<int>(grid_track_extent(cell, gutter, span));
}

class LayoutTypeGuard {
  public:
    LayoutTypeGuard(int w, int h) {
        LayoutManager::instance().init(w, h);
    }
    ~LayoutTypeGuard() {
        LayoutManager::instance().init(800, 480);
    }
};

} // namespace

TEST_CASE("print_status idle card: buttons side by side need one row, stacked need two",
          "[print_status][min_span]") {
    const IdleCardNeeds n = sample_needs();
    const int side_w = n.chrome_w + n.reprint_w + n.gap + n.queue_w;
    const int one_row_h = n.chrome_h + n.title_h + n.gap + n.detail_h + n.gap + n.button_h;
    const int two_row_h = one_row_h + n.button_h + n.gap;

    CHECK(idle_card_fits(n, side_w, one_row_h));
    CHECK_FALSE(idle_card_fits(n, side_w, one_row_h - 1));
    // A pixel narrower and the row wraps, so the same height no longer holds it.
    CHECK_FALSE(idle_card_fits(n, side_w - 1, one_row_h));
    CHECK(idle_card_fits(n, side_w - 1, two_row_h));
    CHECK_FALSE(idle_card_fits(n, side_w - 1, two_row_h - 1));
}

TEST_CASE("print_status idle card: a box narrower than the widest button is refused",
          "[print_status][min_span]") {
    const IdleCardNeeds n = sample_needs();
    const int widest = n.chrome_w + std::max(n.reprint_w, n.queue_w);
    CHECK(idle_card_fits(n, widest, 1000));
    CHECK_FALSE(idle_card_fits(n, widest - 1, 1000));
}

TEST_CASE("print_status idle card: the verdict is monotonic on both axes",
          "[print_status][min_span]") {
    const IdleCardNeeds n = sample_needs();
    for (int w = 60; w <= 300; w += 3) {
        for (int h = 40; h <= 200; h += 3) {
            if (!idle_card_fits(n, w, h)) {
                continue;
            }
            INFO(w << "x" << h << " fits");
            CHECK(idle_card_fits(n, w + 1, h));
            CHECK(idle_card_fits(n, w, h + 1));
        }
    }
}

TEST_CASE_METHOD(LVGLUITestFixture,
                 "print_status refuses one cell of height on the 320x240 panel and takes two",
                 "[print_status][min_span]") {
    lv_display_t* disp = lv_display_get_default();
    REQUIRE(disp != nullptr);
    ScopedResolution res(disp, 320, 240);
    theme_manager_refresh_layout_constants(disp);
    require_font_tokens_distinct();

    // The measured 320x240 home content box: an 8x6 grid.
    const GridDimensions dims = GridLayout::get_dimensions(UiBreakpoint::Micro, 270, 232);
    REQUIRE(dims.cols == 8);
    REQUIRE(dims.rows == 6);
    const CellMetrics m = grid_cell_metrics(270, 232, dims.cols, dims.rows, 2);

    PrintStatusWidget widget;
    const int one_cell_h = extent(m.cell_h, m.gutter, 2);
    const int two_cell_h = extent(m.cell_h, m.gutter, 4);
    for (int cols : {4, 6, 8}) {
        INFO("colspan " << cols << " tracks");
        CHECK_FALSE(widget.fits_at(extent(m.cell_w, m.gutter, cols), one_cell_h));
    }
    CHECK(widget.fits_at(extent(m.cell_w, m.gutter, 8), two_cell_h));
}

TEST_CASE_METHOD(LVGLUITestFixture,
                 "every shipped print_status placement holds the idle card on its own grid",
                 "[print_status][min_span][default_layout]") {
    lv_display_t* disp = lv_display_get_default();
    REQUIRE(disp != nullptr);
    const auto table = PanelWidgetConfig::load_default_table();
    REQUIRE(table);

    layout::CapabilitySnapshot caps;
    int checked = 0;
    for (const auto& g : test::kMeasuredGrids) {
        ScopedResolution res(disp, g.panel_w, g.panel_h);
        theme_manager_refresh_layout_constants(disp);
        LayoutTypeGuard layout_type(g.panel_w, g.panel_h);
        const UiBreakpoint bp = breakpoint_for(std::min(g.panel_w, g.panel_h));
        static const char* const kTiers[] = {"micro", "tiny",   "small",  "medium",
                                             "large", "xlarge", "xxlarge"};
        const char* tier = kTiers[to_int(bp)];
        const auto r =
            layout::resolve_default_layout(*table, nullptr, tier, g.cols, g.rows,
                                           LayoutManager::instance().variant_chain(), "", caps);
        const auto it = std::find_if(r.placed.begin(), r.placed.end(),
                                     [](const auto& p) { return p.id == "print_status"; });
        INFO(g.name);
        REQUIRE(it != r.placed.end());

        const CellMetrics m = grid_cell_metrics(g.content_w, g.content_h, g.cols, g.rows, g.gutter);
        const int w = extent(m.cell_w, m.gutter, it->colspan);
        const int h = extent(m.cell_h, m.gutter, it->rowspan);
        INFO("print_status " << it->colspan << "x" << it->rowspan << " tracks = " << w << "x" << h
                             << "px");
        CHECK(idle_card_fits(measure_idle_card_needs(), w, h));
        ++checked;
    }
    theme_manager_refresh_layout_constants(disp);
    CHECK(checked == static_cast<int>(test::kMeasuredGrids.size()));
}
