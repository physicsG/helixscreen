// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_widget_size_print_stats.cpp
 * @brief print_stats picks its 4-way mode from physical pixels on two
 * independent axes (width, height), not colspan/rowspan.
 *
 * `PrintStatsWidget` has no `widget_obj_` guard at all, and its
 * `on_size_changed` writes only a subject (`print_stats_size_mode`) — no
 * object lookups to assert. The XML (`panel_widget_print_stats.xml`) binds
 * all four layout branches off that subject via `bind_flag_if_not_eq`, so
 * asserting the subject alone covers everything the predicate drives.
 *
 * Four cases isolate the 4-way, 2-axis predicate, each pairing pixels for
 * one target mode with a colspan/rowspan that the *old* span-based predicate
 * would have resolved to a *different* mode — so an implementation that
 * still reads spans fails here instead of passing by coincidence.
 */

#include "../helix_test_fixture.h"
#include "panel_widget_manager.h"
#include "panel_widget_size.h"
#include "src/ui/panel_widgets/print_stats_widget.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::widget_size;

namespace {
int print_stats_size_mode() {
    auto* subject = lv_xml_get_subject(nullptr, "print_stats_size_mode");
    REQUIRE(subject != nullptr);
    return lv_subject_get_int(subject);
}
} // namespace

TEST_CASE_METHOD(HelixTestFixture, "print_stats mode follows pixels on both axes, not spans",
                 "[widget_size][print_stats]") {
    // Widget-owned subjects (print_stats_size_mode, ...) are registered
    // lazily; construct the widget only after they exist.
    PanelWidgetManager::instance().init_widget_subjects();

    PrintStatsWidget w;

    // Mode 0 (narrow compact): both axes below threshold. Contradicting
    // span: 1x3 (old predicate: rowspan<=1(false) -> rowspan<=1(false) ->
    // colspan<=2(true) -> mode 1).
    w.on_size_changed(1, 3, w_wide() - 1, h_tall() - 1);
    CHECK(print_stats_size_mode() == 0);

    // Mode 3 (wide compact): height below threshold regardless of width —
    // the wide-but-short case that must NOT land on mode 2. Contradicting
    // span: 1x1 (old predicate: rowspan<=1 && colspan<=2 -> mode 0).
    w.on_size_changed(1, 1, w_wide(), h_tall() - 1);
    CHECK(print_stats_size_mode() == 3);

    // Mode 1 (2x2 grid): width below threshold, height at/over threshold.
    // Contradicting span: 1x1 (old predicate -> mode 0).
    w.on_size_changed(1, 1, w_wide() - 1, h_tall());
    CHECK(print_stats_size_mode() == 1);

    // Mode 2 (3x2 full): both axes at/over threshold. Contradicting span:
    // 1x1 (old predicate -> mode 0).
    w.on_size_changed(1, 1, w_wide(), h_tall());
    CHECK(print_stats_size_mode() == 2);
}

TEST_CASE("print_stats at its own minimum reachable size takes the narrow compact mode",
          "[widget_size][print_stats]") {
    // print_stats' registry entry (panel_widget_registry.cpp) sets
    // min_colspan=2 — grid_edit_mode.cpp enforces that as a hard floor on
    // resize, so unlike clock/camera/fan_stack/favorite_macro this widget can
    // never actually reach a 1-column width. Its true minimum reachable size
    // is colspan=2, rowspan=1 (min_rowspan=1): 221x141px on Large, 276x169px
    // on XLarge.
    //
    // Judged against those tiers' own bands (w_wide 293/351, h_tall 187/225)
    // that is the narrow compact layout — four measured values, four stat
    // slots, no grid crammed into one grid row. mode_for_size() takes the tier
    // explicitly because a measured extent means nothing against a tier the
    // panel is not running.
    CHECK(PrintStatsWidget::mode_for_size(221, 141, UiBreakpoint::Large) == 0);
    CHECK(PrintStatsWidget::mode_for_size(276, 169, UiBreakpoint::XLarge) == 0);

    // Two real rows is what reaches the 2x2 grid on those tiers: still short
    // of the 3x2 full layout's width, but with the vertical room for stacked
    // pairs. Large 2x2 tracks = 221x288, XLarge = 276x346.
    CHECK(PrintStatsWidget::mode_for_size(221, 288, UiBreakpoint::Large) == 1);
    CHECK(PrintStatsWidget::mode_for_size(276, 346, UiBreakpoint::XLarge) == 1);
}
