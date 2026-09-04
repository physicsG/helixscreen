// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_widget_size_indicators.cpp
 * @brief humidity and width_sensor pick their font from physical width and height.
 *
 * Each case passes a span that contradicts the pixels, so a widget still
 * reading colspan/rowspan fails rather than passing by coincidence. Every
 * widget touches three objects on resize — the value label, the indicator's
 * icon, and the bottom label — and all three are asserted here, including
 * the narrow+tall case that isolates h_tall()'s contribution to the icon font
 * from w_normal()'s contribution to the text fonts.
 */

#include "ui_fonts.h"

#include "../lvgl_ui_test_fixture.h"
#include "../test_helpers/panel_widget_size_harness.h"
#include "panel_widget_size.h"
#include "src/ui/panel_widgets/humidity_widget.h"
#include "src/ui/panel_widgets/width_sensor_widget.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::widget_size;

TEST_CASE_METHOD(LVGLUITestFixture, "humidity value font follows width, not colspan",
                 "[widget_size][humidity]") {
    require_font_tokens_distinct();

    PanelWidgetHarness<HumidityWidget> h(test_screen());
    lv_obj_t* value = h.child("humidity_value");
    REQUIRE(value != nullptr);
    lv_obj_t* indicator = h.child("humidity_indicator");
    REQUIRE(indicator != nullptr);
    lv_obj_t* icon = lv_obj_get_child(indicator, 0);
    REQUIRE(icon != nullptr);
    REQUIRE(lv_obj_get_child_count(h.root()) >= 2);
    lv_obj_t* label = lv_obj_get_child(h.root(), 1);
    REQUIRE(label != nullptr);

    // Narrow pixels with a large colspan: everything must stay compact.
    // icon_font = (tall || wide) ? 32 : 24, so this also proves neither half
    // of the OR is stuck true.
    h.resize(4, 4, w_normal() - 1, h_tall() - 1);
    REQUIRE(lv_obj_get_style_text_font(value, LV_PART_MAIN) == theme_manager_get_font("font_xs"));
    REQUIRE(lv_obj_get_style_text_font(icon, LV_PART_MAIN) == &mdi_icons_24);
    REQUIRE(lv_obj_get_style_text_font(label, LV_PART_MAIN) == theme_manager_get_font("font_xs"));

    // Narrow width, tall height, colspan/rowspan both 1: isolates `tall`.
    // A rowspan-reading implementation sees rowspan=1 and stays compact; only
    // height_px >= h_tall() can flip the icon here. Text stays compact too —
    // label_token/value_token key off `wide` alone, not `tall`.
    h.resize(1, 1, w_normal() - 1, h_tall());
    REQUIRE(lv_obj_get_style_text_font(value, LV_PART_MAIN) == theme_manager_get_font("font_xs"));
    REQUIRE(lv_obj_get_style_text_font(icon, LV_PART_MAIN) == &mdi_icons_32);
    REQUIRE(lv_obj_get_style_text_font(label, LV_PART_MAIN) == theme_manager_get_font("font_xs"));

    // Wide pixels with colspan 1: everything must go wide.
    h.resize(1, 1, w_normal(), h_tall());
    REQUIRE(lv_obj_get_style_text_font(value, LV_PART_MAIN) == theme_manager_get_font("font_body"));
    REQUIRE(lv_obj_get_style_text_font(icon, LV_PART_MAIN) == &mdi_icons_32);
    REQUIRE(lv_obj_get_style_text_font(label, LV_PART_MAIN) == theme_manager_get_font("font_body"));
}

TEST_CASE_METHOD(LVGLUITestFixture, "width_sensor value font follows width, not colspan",
                 "[widget_size][width_sensor]") {
    require_font_tokens_distinct();

    PanelWidgetHarness<WidthSensorWidget> h(test_screen());
    lv_obj_t* value = h.child("width_value");
    REQUIRE(value != nullptr);
    lv_obj_t* indicator = h.child("width_indicator");
    REQUIRE(indicator != nullptr);
    lv_obj_t* icon = lv_obj_get_child(indicator, 0);
    REQUIRE(icon != nullptr);
    REQUIRE(lv_obj_get_child_count(h.root()) >= 2);
    lv_obj_t* label = lv_obj_get_child(h.root(), 1);
    REQUIRE(label != nullptr);

    h.resize(4, 4, w_normal() - 1, h_tall() - 1);
    REQUIRE(lv_obj_get_style_text_font(value, LV_PART_MAIN) == theme_manager_get_font("font_xs"));
    REQUIRE(lv_obj_get_style_text_font(icon, LV_PART_MAIN) == &mdi_icons_24);
    REQUIRE(lv_obj_get_style_text_font(label, LV_PART_MAIN) == theme_manager_get_font("font_xs"));

    // Narrow width, tall height, colspan/rowspan both 1: isolates `tall` the
    // same way as the humidity case above.
    h.resize(1, 1, w_normal() - 1, h_tall());
    REQUIRE(lv_obj_get_style_text_font(value, LV_PART_MAIN) == theme_manager_get_font("font_xs"));
    REQUIRE(lv_obj_get_style_text_font(icon, LV_PART_MAIN) == &mdi_icons_32);
    REQUIRE(lv_obj_get_style_text_font(label, LV_PART_MAIN) == theme_manager_get_font("font_xs"));

    h.resize(1, 1, w_normal(), h_tall());
    REQUIRE(lv_obj_get_style_text_font(value, LV_PART_MAIN) == theme_manager_get_font("font_body"));
    REQUIRE(lv_obj_get_style_text_font(icon, LV_PART_MAIN) == &mdi_icons_32);
    REQUIRE(lv_obj_get_style_text_font(label, LV_PART_MAIN) == theme_manager_get_font("font_body"));
}
