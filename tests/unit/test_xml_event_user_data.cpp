// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// An event_cb's user_data reaches the callback as the string it names; an
// empty one (a prop left at its "" default) reaches it as NULL.
//
// TEST_MIRROR_OK: exercises the XML engine in lib/helix-xml, which ships no header under include/

#include "../lvgl_ui_test_fixture.h"
#include "helix-xml/src/xml/lv_xml.h"

#include <string>

#include "../catch_amalgamated.hpp"

namespace {

bool g_called = false;
const void* g_user_data = reinterpret_cast<const void*>(0x1);

void record_user_data(lv_event_t* e) {
    g_called = true;
    g_user_data = lv_event_get_user_data(e);
}

lv_obj_t* make_button(const char* component, const char* user_data_attr) {
    lv_xml_register_event_cb(nullptr, "test_record_user_data", record_user_data);
    const std::string xml =
        std::string("<component><view extends=\"lv_button\">"
                    "<event_cb trigger=\"clicked\" callback=\"test_record_user_data\" ") +
        user_data_attr + "/></view></component>";
    lv_xml_register_component_from_data(component, xml.c_str());
    return static_cast<lv_obj_t*>(lv_xml_create(lv_screen_active(), component, nullptr));
}

} // namespace

TEST_CASE_METHOD(LVGLUITestFixture, "event_cb: an empty user_data reaches the callback as NULL",
                 "[xml][event_cb]") {
    lv_obj_t* btn = make_button("test_empty_user_data_btn", "user_data=\"\"");
    REQUIRE(btn != nullptr);
    g_called = false;
    lv_obj_send_event(btn, LV_EVENT_CLICKED, nullptr);
    REQUIRE(g_called);
    CHECK(g_user_data == nullptr);
}

TEST_CASE_METHOD(LVGLUITestFixture, "event_cb: a user_data string reaches the callback",
                 "[xml][event_cb]") {
    lv_obj_t* btn = make_button("test_named_user_data_btn", "user_data=\"go:3\"");
    REQUIRE(btn != nullptr);
    g_called = false;
    lv_obj_send_event(btn, LV_EVENT_CLICKED, nullptr);
    REQUIRE(g_called);
    REQUIRE(g_user_data != nullptr);
    CHECK(std::string(static_cast<const char*>(g_user_data)) == "go:3");
}
