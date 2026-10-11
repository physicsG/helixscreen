// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_color_picker.h"

#include "../lvgl_ui_test_fixture.h"

#include <vector>

#include "../catch_amalgamated.hpp"

extern "C" {
#include "helix-xml/src/xml/lv_xml.h"
}

namespace {

std::vector<uint32_t> g_tapped;

/// Reads the color the way every grid consumer does: off the event target.
void record_tap(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    g_tapped.push_back(lv_color_to_u32(lv_obj_get_style_bg_color(target, LV_PART_MAIN)) & 0xFFFFFF);
}

} // namespace

TEST_CASE_METHOD(LVGLUITestFixture, "color_swatch_grid: a tap reports the tapped palette color",
                 "[color_swatch]") {
    lv_xml_register_event_cb(nullptr, "test_grid_tap_cb", record_tap);
    for (const auto palette :
         {helix::ui::ColorPicker::Palette::General, helix::ui::ColorPicker::Palette::Theme}) {
        const char* component = palette == helix::ui::ColorPicker::Palette::General
                                    ? "color_swatch_grid"
                                    : "theme_swatch_grid";
        INFO(component);
        const char* attrs[] = {"swatch_callback", "test_grid_tap_cb", nullptr};
        auto* grid = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), component, attrs));
        REQUIRE(grid != nullptr);
        const auto& expected = helix::ui::swatch_palette(palette);
        REQUIRE(lv_obj_get_child_count(grid) == expected.size());

        g_tapped.clear();
        for (uint32_t i = 0; i < expected.size(); ++i) {
            lv_obj_send_event(lv_obj_get_child(grid, static_cast<int32_t>(i)), LV_EVENT_CLICKED,
                              nullptr);
        }
        CHECK(g_tapped == expected);
        lv_obj_delete(grid);
    }
}

TEST_CASE("swatch_needs_edge: a swatch too close to its surface gets an edge, in either theme",
          "[color_swatch]") {
    constexpr uint32_t DARK_SCREEN = 0x2E3440;
    constexpr uint32_t DARK_DIALOG = 0x4C566A;
    constexpr uint32_t LIGHT_SCREEN = 0xECEFF4;
    constexpr uint32_t LIGHT_DIALOG = 0xEDEFF6;
    // Dark-on-dark.
    CHECK(helix::ui::swatch_needs_edge(0x000000, DARK_SCREEN));
    CHECK(helix::ui::swatch_needs_edge(0x1A1A1A, DARK_SCREEN));
    CHECK(helix::ui::swatch_needs_edge(0x4A4A4A, DARK_SCREEN));
    CHECK(helix::ui::swatch_needs_edge(0x4A4A4A, DARK_DIALOG));
    // Light-on-light.
    CHECK(helix::ui::swatch_needs_edge(0xFFFFFF, LIGHT_SCREEN));
    CHECK(helix::ui::swatch_needs_edge(0xE8E8E8, LIGHT_DIALOG));
    CHECK(helix::ui::swatch_needs_edge(0xE0D5C7, LIGHT_SCREEN));
    // Plenty of contrast: no edge.
    CHECK_FALSE(helix::ui::swatch_needs_edge(0xFFFFFF, DARK_SCREEN));
    CHECK_FALSE(helix::ui::swatch_needs_edge(0x808080, DARK_SCREEN));
    CHECK_FALSE(helix::ui::swatch_needs_edge(0x1A1A1A, LIGHT_SCREEN));
    CHECK_FALSE(helix::ui::swatch_needs_edge(0xE53935, LIGHT_DIALOG));
}
