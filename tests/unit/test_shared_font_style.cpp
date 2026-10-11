// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_shared_font_style.cpp
 * @brief Every face a build links can ride a shared font style, however many
 *        that is, and replacing one face with another never stacks styles.
 */

#include "ui_breakpoint.h"
#include "ui_tile_rung.h"

#include "../lvgl_ui_test_fixture.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "helix/ui/shared_font_style.h"
#include "lvgl/src/core/lv_obj_private.h"
#include "lvgl/src/core/lv_obj_style_private.h"
#include "src/ui/panel_widgets/tile_layout.h"
#include "theme_manager.h"

#include <set>
#include <string>

#include "../catch_amalgamated.hpp"

TEST_CASE_METHOD(LVGLUITestFixture, "a shared font style exists for every linked face",
                 "[font][shared_font_style]") {
    // Every face the build registers, by the names the asset registry uses.
    std::set<const lv_font_t*> faces;
    auto add = [&](const std::string& name) {
        if (const lv_font_t* f = lv_xml_get_font_silent(nullptr, name.c_str())) {
            faces.insert(f);
        }
    };
    for (int s : {14, 16, 24, 32, 48, 64, 80, 96, 128}) {
        add("mdi_icons_" + std::to_string(s));
    }
    for (int s = 8; s <= 64; ++s) {
        add("noto_sans_" + std::to_string(s));
        add("noto_sans_bold_" + std::to_string(s));
        add("noto_sans_light_" + std::to_string(s));
        add("source_code_pro_" + std::to_string(s));
    }
    INFO(faces.size() << " linked faces");
    REQUIRE(faces.size() > 32);

    lv_obj_t* label = lv_label_create(test_screen());
    for (const lv_font_t* f : faces) {
        helix::ui::apply_font_style(label, f);
        CHECK(lv_obj_get_style_text_font(label, LV_PART_MAIN) == f);
    }
    // Replacing faces left exactly one shared face style on the label.
    int face_styles = 0;
    for (const lv_font_t* f : faces) {
        for (uint32_t i = 0; i < label->style_cnt; ++i) {
            if (label->styles[i].style == helix::ui::shared_font_style(f)) {
                ++face_styles;
            }
        }
    }
    CHECK(face_styles == 1);
    lv_obj_delete(label);
}

TEST_CASE_METHOD(LVGLUITestFixture, "every tier's tile ladders resolve through shared styles",
                 "[font][shared_font_style][tile]") {
    lv_display_t* disp = lv_display_get_default();
    REQUIRE(disp != nullptr);
    lv_obj_t* label = lv_label_create(test_screen());
    for (int edge : {272, 320, 400, 480, 600, 720, 1080}) {
        ScopedResolution res(disp, edge * 5 / 3, edge);
        theme_manager_refresh_layout_constants(disp);
        for (auto ladder : {helix::ui::TileLadder::Icon, helix::ui::TileLadder::Value,
                            helix::ui::TileLadder::Label}) {
            for (int r = 0; r < helix::kTileRungs; ++r) {
                const auto face = helix::ui::tile_rung_face(ladder, r);
                REQUIRE(face.font != nullptr);
                helix::ui::apply_font_style(label, face.font);
                CHECK(lv_obj_get_style_text_font(label, LV_PART_MAIN) == face.font);
            }
        }
    }
    lv_obj_delete(label);
    theme_manager_refresh_layout_constants(disp);
}
