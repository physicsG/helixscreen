// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025-2026 356C LLC
// TEST_MIRROR_OK: exercises patches/lvgl_obj_event_null_guards.patch, shipped LVGL code with
//                 no HelixScreen header to include

/**
 * @file test_obj_event_null_obj.cpp
 * @brief The object event accessors bail on a NULL object instead of faulting on it.
 *
 * Every accessor in lv_obj_event.c opens with LV_ASSERT_NULL(obj) and then
 * reads obj->spec_attr. LV_ASSERT_NULL is compiled out of release builds
 * entirely, and this tree's LV_ASSERT_HANDLER logs and continues rather than
 * halting, so in BOTH build kinds a NULL object reaches the field load and
 * faults at address 0. A widget pointer zeroed between a deferred callback
 * being queued and running arrives exactly that way.
 *
 * The guards in patches/lvgl_obj_event_null_guards.patch return the
 * function's empty value and report the call through helix_lvgl_anomaly().
 * A regression SIGSEGVs the test process; the assertions pin the return
 * values, and the fixture's live object pins that the happy path still works
 * (a guard that swallowed every call would pass the NULL cases alone).
 *
 * @see lib/lvgl/src/core/lv_obj_event.c
 */

#include "../lvgl_test_fixture.h"
#include "lvgl/lvgl.h"

#include "../catch_amalgamated.hpp"

namespace {

int g_calls = 0;

void counting_cb(lv_event_t*) {
    ++g_calls;
}

} // namespace

class ObjEventNullObjFixture : public LVGLTestFixture {
  public:
    lv_obj_t* obj = nullptr;
    lv_event_dsc_t* dsc = nullptr;

    ObjEventNullObjFixture() {
        g_calls = 0;
        obj = lv_obj_create(test_screen());
        dsc = lv_obj_add_event_cb(obj, counting_cb, LV_EVENT_CLICKED, nullptr);
        REQUIRE(dsc != nullptr);
    }
};

TEST_CASE_METHOD(ObjEventNullObjFixture, "Event accessors return empty values for a NULL object",
                 "[lvgl][event][null_obj]") {
    // Each of these reads obj->spec_attr without the guard.
    CHECK(lv_obj_get_event_count(nullptr) == 0);
    CHECK(lv_obj_get_event_dsc(nullptr, 0) == nullptr);
    CHECK(lv_obj_remove_event(nullptr, 0) == false);
    CHECK(lv_obj_remove_event_dsc(nullptr, dsc) == false);
    CHECK(lv_obj_remove_event_cb(nullptr, counting_cb) == 0);
    CHECK(lv_obj_remove_event_cb_with_user_data(nullptr, counting_cb, nullptr) == 0);
}

TEST_CASE_METHOD(ObjEventNullObjFixture, "A NULL descriptor is rejected without touching the list",
                 "[lvgl][event][null_obj]") {
    CHECK(lv_obj_remove_event_dsc(obj, nullptr) == false);

    // The real callback is still installed and still fires.
    CHECK(lv_obj_get_event_count(obj) == 1);
    lv_obj_send_event(obj, LV_EVENT_CLICKED, nullptr);
    CHECK(g_calls == 1);
}

TEST_CASE_METHOD(ObjEventNullObjFixture, "Guarding NULL leaves the live object's accessors intact",
                 "[lvgl][event][null_obj]") {
    REQUIRE(lv_obj_get_event_count(obj) == 1);
    CHECK(lv_obj_get_event_dsc(obj, 0) == dsc);

    lv_obj_send_event(obj, LV_EVENT_CLICKED, nullptr);
    CHECK(g_calls == 1);

    CHECK(lv_obj_remove_event_cb(obj, counting_cb) == 1);
    CHECK(lv_obj_get_event_count(obj) == 0);

    lv_obj_send_event(obj, LV_EVENT_CLICKED, nullptr);
    CHECK(g_calls == 1);
}
