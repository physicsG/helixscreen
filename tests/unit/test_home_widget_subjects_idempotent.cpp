// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_home_widget_subjects_idempotent.cpp
 * @brief PanelWidgetManager::init_widget_subjects() runs on every printer switch, so each
 * home widget's subject hook must leave live subjects (and the values on them) alone.
 *
 * Run with: ./build/bin/helix-tests "[home_widgets][subjects]"
 */

#include "../lvgl_ui_test_fixture.h"
#include "panel_widget_manager.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

TEST_CASE_METHOD(LVGLUITestFixture,
                 "home widget subject hooks keep live subjects when run a second time",
                 "[home_widgets][subjects]") {
    const char* const names[] = {"speed_flow_speed_status", "z_offset_status", "host_health_status",
                                 "bed_mesh_tile_status"};

    PanelWidgetManager::instance().init_widget_subjects();
    for (const char* name : names) {
        lv_subject_t* subject = lv_xml_get_subject(nullptr, name);
        REQUIRE(subject != nullptr);
        lv_subject_set_int(subject, 2);
    }

    PanelWidgetManager::instance().init_widget_subjects();

    for (const char* name : names) {
        INFO(name);
        lv_subject_t* subject = lv_xml_get_subject(nullptr, name);
        REQUIRE(subject != nullptr);
        CHECK(lv_subject_get_int(subject) == 2);
    }
}
