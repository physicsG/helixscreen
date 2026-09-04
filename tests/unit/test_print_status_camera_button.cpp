// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_print_status_camera_button.cpp
 * @brief btn_camera visibility = webcam AND remote AND platform camera support
 *
 * The camera button must stay hidden on the printer's own screen (the print is
 * right in front of it), with no webcam configured, and on builds whose camera
 * code is compiled out (platform_extras_available gates the ESP32 v1 cut).
 * All 8 combinations of the three gate subjects are driven through the REAL
 * panel XML — no C++ visibility pokes exist to drift from the binding.
 */

#include "ui_panel_print_status.h"
#include "ui_update_queue.h"

#include "../lvgl_ui_test_fixture.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "printer_state.h"

#include <lvgl.h>
#include <memory>

#include "../catch_amalgamated.hpp"

namespace {

/// Owns a real PrintStatusPanel built from production XML.
/// Construction/teardown mirrors PrintStatusHeaderFixture in
/// test_print_status_header_action_button.cpp: subjects before create(),
/// widgets deleted before ~PrintStatusPanel withdraws them.
struct CameraButtonFixture : public LVGLUITestFixture {
    CameraButtonFixture() {
        panel_ = std::make_unique<PrintStatusPanel>(state(), nullptr);
        panel_->init_subjects();
        root_ = panel_->create(test_screen());
        REQUIRE(root_ != nullptr);
        process_lvgl(50);

        webcam_ = lv_xml_get_subject(nullptr, "printer_has_webcam");
        remote_ = lv_xml_get_subject(nullptr, "moonraker_is_remote");
        extras_ = lv_xml_get_subject(nullptr, "platform_extras_available");
        REQUIRE(webcam_ != nullptr);
        REQUIRE(remote_ != nullptr);
        REQUIRE(extras_ != nullptr);
    }

    ~CameraButtonFixture() override {
        // platform_extras_available is an app_globals global that outlives this
        // fixture — restore the desktop default so no 0 leaks into later tests.
        lv_subject_set_int(extras_, 1);
        helix::ui::UpdateQueue::instance().drain();
        // Widgets first: the XML bindings observe subjects the panel owns, and
        // ~PrintStatusPanel calls deinit_subjects().
        if (root_ && lv_obj_is_valid(root_)) {
            lv_obj_delete(root_);
        }
        root_ = nullptr;
        helix::ui::UpdateQueue::instance().drain();
        panel_.reset();
        helix::ui::UpdateQueue::instance().drain();
    }

    lv_obj_t* button() {
        lv_obj_t* btn = lv_obj_find_by_name(root_, "btn_camera");
        REQUIRE(btn != nullptr);
        return btn;
    }

    /// Text of btn_camera's label child. The icon is also an lv_label (MDI
    /// glyphs are 4-byte UTF-8, 0xF3 lead byte) — skip it.
    std::string button_label_text() {
        lv_obj_t* btn = button();
        for (uint32_t i = 0; i < lv_obj_get_child_count(btn); ++i) {
            lv_obj_t* child = lv_obj_get_child(btn, i);
            if (lv_obj_check_type(child, &lv_label_class)) {
                const char* t = lv_label_get_text(child);
                if (t && t[0] != '\0' && static_cast<unsigned char>(t[0]) != 0xF3)
                    return t;
            }
        }
        return "";
    }

    void set(lv_subject_t* s, int v) {
        lv_subject_set_int(s, v);
        helix::ui::UpdateQueue::instance().drain();
        process_lvgl(20);
    }

    std::unique_ptr<PrintStatusPanel> panel_;
    lv_obj_t* root_ = nullptr;
    lv_subject_t* webcam_ = nullptr;
    lv_subject_t* remote_ = nullptr;
    lv_subject_t* extras_ = nullptr;
};

} // namespace

TEST_CASE_METHOD(CameraButtonFixture, "btn_camera visible only for remote webcam builds",
                 "[print_status][camera][ui_integration]") {
    const bool visible[2][2][2] = {
        /* webcam=0 */ {/* remote=0 */ {/* extras=0 */ false, /* extras=1 */ false},
                        /* remote=1 */ {/* extras=0 */ false, /* extras=1 */ false}},
        /* webcam=1 */ {/* remote=0 */ {/* extras=0 */ false, /* extras=1 */ false},
                        /* remote=1 */ {/* extras=0 */ false, /* extras=1 */ true}}};

    for (int w = 0; w <= 1; ++w)
        for (int r = 0; r <= 1; ++r)
            for (int e = 0; e <= 1; ++e) {
                set(webcam_, w);
                set(remote_, r);
                set(extras_, e);
                INFO("webcam=" << w << " remote=" << r << " extras=" << e);
                const bool hidden = lv_obj_has_flag(button(), LV_OBJ_FLAG_HIDDEN);
                CHECK(hidden != visible[w][r][e]);
            }
}

TEST_CASE_METHOD(CameraButtonFixture, "btn_camera label shortens at tight breakpoints",
                 "[print_status][camera][ui_integration]") {
    lv_subject_t* bp = lv_xml_get_subject(nullptr, "ui_breakpoint");
    REQUIRE(bp != nullptr);
    const int original_bp = lv_subject_get_int(bp);

    // Medium (800x480): Row 2 is tight — short form.
    set(bp, 3);
    CHECK(button_label_text() == "Cam");

    // Large (1024x600) and up: breathing room — full word.
    set(bp, 4);
    CHECK(button_label_text() == "Camera");
    set(bp, 6);
    CHECK(button_label_text() == "Camera");

    // Back to the cramped tiers, including the Micro row.
    set(bp, 0);
    CHECK(button_label_text() == "Cam");

    // ui_breakpoint is a theme global that outlives this fixture — restore.
    set(bp, original_bp);
}
