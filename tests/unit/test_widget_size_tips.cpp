// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_widget_size_tips.cpp
 * @brief tips picks its text font from physical width, not colspan.
 *
 * `compact` is the widget's only branch (`on_size_changed` predicate at
 * tips_widget.cpp:165), so no isolation is needed beyond the two cases below
 * — each contradicts colspan against width_px so a span-reading
 * implementation fails instead of passing by coincidence. The predicate
 * restyles two objects: the named tip label (`status_text_label`, cached as
 * `tip_label_`) and the unnamed "Tip:" prefix label (first child of
 * `tip_container`). The help_circle icon (last child) uses
 * `theme_manager_get_font("icon_font_md")` unconditionally — not
 * size-dependent — so it is left alone per the task brief and not asserted
 * here.
 */

#include "ui_fonts.h"

#include "../lvgl_ui_test_fixture.h"
#include "../test_helpers/panel_widget_size_harness.h"
#include "panel_widget_size.h"
#include "src/ui/panel_widgets/tips_widget.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::widget_size;

TEST_CASE_METHOD(LVGLUITestFixture, "tips text font follows width, not colspan",
                 "[widget_size][tips]") {
    require_font_tokens_distinct();

    PanelWidgetHarness<TipsWidget> h(test_screen());

    lv_obj_t* tip_container = h.child("tip_container");
    REQUIRE(tip_container != nullptr);

    lv_obj_t* label = h.child("status_text_label");
    REQUIRE(label != nullptr);

    lv_obj_t* prefix = lv_obj_get_child(tip_container, 0);
    REQUIRE(prefix != nullptr);
    // Sanity: the prefix must be a different object from the bound label —
    // otherwise both assertions below would trivially agree.
    REQUIRE(prefix != label);

    // Wide colspan (would be non-compact under the old span predicate),
    // narrow pixels: compact must win on pixels alone.
    h.resize(4, 4, w_wide() - 1, 100);
    REQUIRE(lv_obj_get_style_text_font(label, LV_PART_MAIN) == theme_manager_get_font("font_body"));
    REQUIRE(lv_obj_get_style_text_font(prefix, LV_PART_MAIN) ==
            theme_manager_get_font("font_body"));

    // Narrow colspan (would be compact under the old span predicate), wide
    // pixels: non-compact must win on pixels alone.
    h.resize(1, 1, w_wide(), 100);
    REQUIRE(lv_obj_get_style_text_font(label, LV_PART_MAIN) ==
            theme_manager_get_font("font_heading"));
    REQUIRE(lv_obj_get_style_text_font(prefix, LV_PART_MAIN) ==
            theme_manager_get_font("font_heading"));
}
