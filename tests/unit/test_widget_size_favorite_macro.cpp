// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_widget_size_favorite_macro.cpp
 * @brief favorite_macro picks badge size, icon font, and name font from
 * physical pixels, not colspan/rowspan.
 *
 * `on_size_changed` (favorite_macro_widget.cpp) restyles three objects:
 *   - `fav_macro_badge`: width/height 64 when tall, 48 otherwise (also sets
 *     radius, a derived value not asserted separately here).
 *   - `fav_macro_icon`: `&mdi_icons_48` when tall, `&mdi_icons_32` otherwise
 *     — driven by `tall` alone, same as the badge.
 *   - `fav_macro_name`: `font_small` when `(tall || wide)`, `font_xs`
 *     otherwise — the one object whose predicate differs from the other two.
 *
 * Three cases isolate the two flags: neither set, `tall` alone, and `wide`
 * alone. The `wide`-alone case is the one that actually separates the two
 * predicates — badge and icon must stay compact while the name label goes
 * to `font_small`. A badge predicate accidentally written as `(tall ||
 * wide)` would fail only this case.
 */

#include "ui_fonts.h"

#include "../lvgl_ui_test_fixture.h"
#include "../test_helpers/panel_widget_size_harness.h"
#include "favorite_macro_widget.h"
#include "panel_widget_size.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::widget_size;

TEST_CASE_METHOD(LVGLUITestFixture, "favorite_macro badge/icon/name follow pixels, not spans",
                 "[widget_size][favorite_macro]") {
    require_font_tokens_distinct();

    PanelWidgetHarness<FavoriteMacroWidget> h(test_screen(), "favorite_macro:1");

    lv_obj_t* badge = h.child("fav_macro_badge");
    REQUIRE(badge != nullptr);
    lv_obj_t* icon = h.child("fav_macro_icon");
    REQUIRE(icon != nullptr);
    lv_obj_t* name = h.child("fav_macro_name");
    REQUIRE(name != nullptr);

    // Neither flag: large span, sub-threshold pixels on both axes. A
    // span-reading implementation would go tall/wide here; pixels must win.
    h.resize(4, 4, w_normal() - 1, h_tall() - 1);
    REQUIRE(lv_obj_get_width(badge) == 48);
    REQUIRE(lv_obj_get_style_text_font(icon, LV_PART_MAIN) == &mdi_icons_32);
    REQUIRE(lv_obj_get_style_text_font(name, LV_PART_MAIN) == theme_manager_get_font("font_xs"));

    // `tall` alone: span 1x1 (would read compact under the old predicate),
    // height at threshold, width still sub-threshold.
    h.resize(1, 1, w_normal() - 1, h_tall());
    REQUIRE(lv_obj_get_width(badge) == 64);
    REQUIRE(lv_obj_get_style_text_font(icon, LV_PART_MAIN) == &mdi_icons_48);
    REQUIRE(lv_obj_get_style_text_font(name, LV_PART_MAIN) == theme_manager_get_font("font_small"));

    // `wide` alone: width at threshold, height still sub-threshold. Proves
    // wide and tall are not conflated — badge/icon must stay compact while
    // the name label (driven by tall || wide) goes to font_small.
    h.resize(1, 1, w_normal(), h_tall() - 1);
    REQUIRE(lv_obj_get_width(badge) == 48);
    REQUIRE(lv_obj_get_style_text_font(icon, LV_PART_MAIN) == &mdi_icons_32);
    REQUIRE(lv_obj_get_style_text_font(name, LV_PART_MAIN) == theme_manager_get_font("font_small"));
}
