// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_widget_size_indicators.cpp
 * @brief humidity and width_sensor draw their glyph and reading in the faces
 *        their box earns, not the ones their span suggests.
 *
 * Each case passes a span that contradicts the pixels, so a tile still reading
 * colspan/rowspan fails rather than passing by coincidence.
 */

#include "ui_tile_rung.h"

#include "../lvgl_ui_test_fixture.h"
#include "../test_helpers/panel_widget_size_harness.h"
#include "panel_widget_registry.h"
#include "src/ui/panel_widgets/tile_layout.h"
#include "theme_manager.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

void check_faces_follow_pixels(PanelWidgetHarnessBase& h, const char* id, const char* value_name) {
    lv_obj_t* value = h.child(value_name);
    REQUIRE(value != nullptr);
    lv_obj_t* icon = h.child((std::string(id) + "_icon").c_str());
    REQUIRE(icon != nullptr);
    lv_subject_t* rung = lv_xml_get_subject(nullptr, (std::string(id) + "_tile_icon").c_str());
    REQUIRE(rung != nullptr);

    auto faces_match_rung = [&] {
        const int r = lv_subject_get_int(rung);
        CHECK(lv_obj_get_style_text_font(value, LV_PART_MAIN) ==
              ui::tile_rung_face(ui::TileLadder::Value, r).font);
        CHECK(lv_obj_get_style_text_font(icon, LV_PART_MAIN) ==
              ui::tile_rung_face(ui::TileLadder::Icon, r).font);
        return r;
    };

    // A big span over a small box, then a small span over a big box.
    h.resize(8, 8, 60, 60);
    const int small = faces_match_rung();
    h.resize(1, 1, 400, 400);
    const int large = faces_match_rung();
    CHECK(large > small);

    // At the top rung the glyph doubles, and the reading steps up with it.
    if (large == helix::kTileRungs - 1) {
        CHECK(lv_obj_get_style_text_font(value, LV_PART_MAIN) == theme_manager_get_font("font_xl"));
    }
    CHECK(large == helix::kTileRungs - 1);
}

} // namespace

TEST_CASE_METHOD(LVGLUITestFixture, "humidity faces follow pixels, not colspan",
                 "[widget_size][humidity]") {
    require_font_tokens_distinct();
    init_widget_registrations();
    const PanelWidgetDef* def = find_widget_def("humidity");
    REQUIRE(def != nullptr);
    RegistryWidgetHarness h(test_screen(), *def);
    check_faces_follow_pixels(h, "humidity", "humidity_value");
}

TEST_CASE_METHOD(LVGLUITestFixture, "width_sensor faces follow pixels, not colspan",
                 "[widget_size][width_sensor]") {
    require_font_tokens_distinct();
    init_widget_registrations();
    const PanelWidgetDef* def = find_widget_def("width_sensor");
    REQUIRE(def != nullptr);
    RegistryWidgetHarness h(test_screen(), *def);
    check_faces_follow_pixels(h, "width_sensor", "width_value");
}
