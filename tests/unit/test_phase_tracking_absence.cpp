// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_phase_tracking_absence.cpp
 * @brief The phase-tracking toggle stays out of the app (#1234)
 *
 * Each case first proves its setup ran - the panel built with its plugin and
 * macro rows, the subject scope holding the plugin subjects - so the absence
 * assertions cannot pass on a setup that silently did nothing.
 */

#include "ui_modal.h"
#include "ui_update_queue.h"

#include "../lvgl_ui_test_fixture.h"

#include "../catch_amalgamated.hpp"

using helix::ui::UpdateQueue;

namespace {

struct PhaseTrackingAbsenceFixture : LVGLUITestFixture {
    PhaseTrackingAbsenceFixture() {
        helix::ui::modal_init_subjects();
    }

    ~PhaseTrackingAbsenceFixture() override {
        UpdateQueue::instance().drain();
    }
};

} // namespace

TEST_CASE_METHOD(PhaseTrackingAbsenceFixture, "advanced panel builds without a phase tracking row",
                 "[advanced][plugin][1234]") {
    lv_obj_t* root =
        static_cast<lv_obj_t*>(lv_xml_create(lv_screen_active(), "advanced_panel", nullptr));
    REQUIRE(root != nullptr);

    // Positive controls: the panel's plugin and macro rows all built.
    REQUIRE(lv_obj_find_by_name(root, "row_helix_plugin_install") != nullptr);
    REQUIRE(lv_obj_find_by_name(root, "row_helix_macros_install") != nullptr);
    REQUIRE(lv_obj_find_by_name(root, "row_helix_macros_update") != nullptr);

    CHECK(lv_obj_find_by_name(root, "row_phase_tracking") == nullptr);
    CHECK(lv_obj_find_by_name(root, "container_phase_tracking") == nullptr);

    lv_obj_delete(root);
}

TEST_CASE_METHOD(PhaseTrackingAbsenceFixture, "no phase_tracking_enabled subject is registered",
                 "[plugin][1234]") {
    // The scope is live and holds the plugin subjects...
    REQUIRE(lv_xml_get_subject(nullptr, "helix_plugin_installed") != nullptr);
    REQUIRE(lv_xml_get_subject(nullptr, "helix_macros_status") != nullptr);

    // ...and the phase-tracking subject is not among them.
    CHECK(lv_xml_get_subject(nullptr, "phase_tracking_enabled") == nullptr);
}
