// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TEST_MIRROR_OK: instantiates the shipped ui_xml/setting_text_row.xml component
//                 through lv_xml_create() and asserts on real LVGL state.
//                 ../lvgl_ui_test_fixture.h pulls in include/moonraker_api.h.

#include "../lvgl_ui_test_fixture.h"

#include <string>

#include "../catch_amalgamated.hpp"

extern "C" {
#include "helix-xml/src/xml/lv_xml.h"
}

namespace {

int g_text_row_fired = 0;

void text_row_cb(lv_event_t*) {
    ++g_text_row_fired;
}

} // namespace

TEST_CASE_METHOD(LVGLUITestFixture, "setting_text_row shows its value and reports edits",
                 "[setting_text_row]") {
    lv_xml_register_event_cb(nullptr, "test_text_row_cb", &text_row_cb);
    const char* attrs[] = {"label", "Name", "value", "hello", "callback", "test_text_row_cb",
                           nullptr};
    auto* row = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "setting_text_row", attrs));
    REQUIRE(row != nullptr);

    lv_obj_t* input = lv_obj_find_by_name(row, "value_input");
    REQUIRE(input != nullptr);
    CHECK(std::string(lv_textarea_get_text(input)) == "hello");

    g_text_row_fired = 0;
    lv_obj_send_event(input, LV_EVENT_READY, nullptr);
    CHECK(g_text_row_fired == 1);
    lv_obj_send_event(input, LV_EVENT_DEFOCUSED, nullptr);
    CHECK(g_text_row_fired == 2);

    lv_obj_delete(row);
}
