// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_notification_badge_rung.cpp
 * @brief The notifications tile's count badge follows the same rung the bell
 *        glyph does, and its label face fits the circle.
 *
 * Run with: ./build/bin/helix-tests "[widget_size][tile]"
 */

#include "ui_tile_rung.h"
#include "ui_update_queue.h"

#include "../lvgl_ui_test_fixture.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "panel_widget.h"
#include "panel_widget_registry.h"
#include "src/ui/panel_widgets/tile_layout.h"
#include "theme_manager.h"

#include <cstdlib>

#include "../catch_amalgamated.hpp"

namespace {

/// One notifications tile, built the way the manager builds it: a factory
/// instance owns the TileSizing (so the per-instance subjects exist before the
/// component is parsed) and the component is created with its attrs.
struct NotificationsTile {
    std::unique_ptr<helix::PanelWidget> widget;
    lv_obj_t* root = nullptr;
    lv_obj_t* bell = nullptr;
    lv_obj_t* badge = nullptr;

    explicit NotificationsTile(LVGLUITestFixture& fixture) {
        const helix::PanelWidgetDef* def = helix::find_widget_def("notifications");
        REQUIRE(def != nullptr);
        REQUIRE(def->factory != nullptr);
        widget = def->factory("notifications:0");
        REQUIRE(widget != nullptr);

        root = static_cast<lv_obj_t*>(lv_xml_create(
            fixture.test_screen(), "panel_widget_notifications", widget->xml_attrs()));
        REQUIRE(root != nullptr);
        bell = lv_obj_find_by_name(root, "status_notification_icon");
        badge = lv_obj_find_by_name(root, "notification_badge");
        REQUIRE(bell != nullptr);
        REQUIRE(badge != nullptr);
    }

    ~NotificationsTile() {
        if (widget)
            widget->detach();
    }

    /// Publish @p rung as TileSizing does: the requested rung, and the rung
    /// the glyph draws at (@p drawn, or this build's own answer).
    void set_rung(int rung, int drawn = -1) const {
        lv_subject_set_int(lv_xml_get_subject(nullptr, "notifications:0_tile_icon"), rung);
        lv_subject_set_int(lv_xml_get_subject(nullptr, "notifications:0_tile_drawn"),
                           drawn >= 0 ? drawn : helix::ui::tile_drawn_rung(rung));
        helix::ui::UpdateQueue::instance().drain();
    }
};

const char* rung_token(int rung) {
    static const char* tokens[] = {"tile_badge_xs", "tile_badge_sm", "tile_badge_md",
                                   "tile_badge_lg", "tile_badge_xl", "tile_badge_xxl"};
    return tokens[rung];
}

} // namespace

TEST_CASE("the notification badge follows the bell's rung", "[widget_size][tile]") {
    LVGLUITestFixture fixture;
    helix::init_widget_registrations();
    NotificationsTile tile(fixture);

    for (int rung = 0; rung < helix::kTileRungs; ++rung) {
        INFO("rung " << rung);
        tile.set_rung(rung);

        const lv_font_t* bell_face = lv_obj_get_style_text_font(tile.bell, LV_PART_MAIN);
        REQUIRE(bell_face != nullptr);

        const int32_t badge_w = lv_obj_get_style_width(tile.badge, LV_PART_MAIN);
        CHECK(badge_w == theme_manager_get_spacing(rung_token(rung)));

        // The contract is the proportion, not the ladder value: 2/5 of the
        // glyph the bell is actually drawing at this rung.
        // The xxl glyph may draw a smaller face scaled up to its size.
        const int bell_h = bell_face->line_height *
                           lv_obj_get_style_transform_scale_x(tile.bell, LV_PART_MAIN) /
                           LV_SCALE_NONE;
        INFO("badge " << badge_w << " on bell line_height " << bell_h);
        CHECK(std::abs(badge_w * 5 - bell_h * 2) <= 6);
    }
}

TEST_CASE("the notification badge's label face fits the circle", "[widget_size][tile]") {
    LVGLUITestFixture fixture;
    helix::init_widget_registrations();
    NotificationsTile tile(fixture);

    const lv_obj_t* label = lv_obj_get_child(tile.badge, 0);
    REQUIRE(label != nullptr);

    const lv_font_t* font_xs = theme_manager_get_font("font_xs");
    const lv_font_t* font_small = theme_manager_get_font("font_small");
    REQUIRE(font_xs != nullptr);
    REQUIRE(font_small != nullptr);
    REQUIRE(font_xs != font_small);

    // The cramped end: a badge too small for any face falls back to the
    // smallest one rather than keeping a face that overflows.
    tile.set_rung(0);
    const int32_t dot = lv_obj_get_style_width(tile.badge, LV_PART_MAIN);
    if (font_xs->line_height > dot) {
        CHECK(lv_obj_get_style_text_font(label, LV_PART_MAIN) == font_xs);
    }

    // The roomy end: the face grows past font_xs and stays inside the circle.
    tile.set_rung(4);
    const int32_t big = lv_obj_get_style_width(tile.badge, LV_PART_MAIN);
    const lv_font_t* face = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    REQUIRE(face != nullptr);
    CHECK(face != font_xs);
    CHECK(face->line_height <= big);
}

TEST_CASE("an xxl bell held to its xl face keeps the xl badge", "[widget_size][tile][xxl]") {
    // xxl requested where the cap leaves the largest face at the xl size (the
    // ESP32 image, or an animated glyph): the glyph draws at xl, so the badge
    // must too, not at the xxl size the rung asked for.
    LVGLUITestFixture fixture;
    helix::init_widget_registrations();
    NotificationsTile tile(fixture);

    const int xxl = helix::kTileRungs - 1;
    const int drawn = helix::ui::tile_drawn_rung(xxl, 64, 64);
    REQUIRE(drawn == xxl - 1);
    tile.set_rung(xxl, drawn);
    CHECK(lv_obj_get_style_width(tile.badge, LV_PART_MAIN) ==
          theme_manager_get_spacing("tile_badge_xl"));
    CHECK(theme_manager_get_spacing("tile_badge_xl") !=
          theme_manager_get_spacing("tile_badge_xxl"));
}

TEST_CASE("the notifications tile publishes the rung its bell draws at",
          "[widget_size][tile][xxl]") {
    LVGLUITestFixture fixture;
    helix::init_widget_registrations();
    NotificationsTile tile(fixture);
    lv_subject_t* icon = lv_xml_get_subject(nullptr, "notifications:0_tile_icon");
    lv_subject_t* drawn = lv_xml_get_subject(nullptr, "notifications:0_tile_drawn");
    REQUIRE(icon != nullptr);
    REQUIRE(drawn != nullptr);

    for (int px : {60, 420, 900}) {
        INFO("box " << px);
        tile.widget->notify_size_changed(2, 2, px, px);
        CHECK(lv_subject_get_int(drawn) == helix::ui::tile_drawn_rung(lv_subject_get_int(icon)));
    }
    // The biggest box asks for the top rung, which the default seed never holds.
    CHECK(lv_subject_get_int(icon) != 2);
}
