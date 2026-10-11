// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_indev_read_hook.cpp
 * @brief One read hook chained onto several pointer devices
 *
 * PointerFrameHook fronts the touch pointer and the USB mouse with a single
 * static callback. Each device keeps the read callback the hook replaced, so one
 * device's sample is never produced by the other's driver, and teardown hands
 * each device back its own callback (prestonbrown/helixscreen#1275).
 */

#include "../lvgl_test_fixture.h"
#include "indev_read_hook.h"

#include <algorithm>
#include <vector>

#include "../catch_amalgamated.hpp"

using helix::IndevReadHook;

namespace {

void touch_driver_read(lv_indev_t* /*indev*/, lv_indev_data_t* data) {
    data->point = {11, 22};
    data->state = LV_INDEV_STATE_PRESSED;
}

void mouse_driver_read(lv_indev_t* /*indev*/, lv_indev_data_t* data) {
    data->point = {333, 444};
    data->state = LV_INDEV_STATE_PRESSED;
}

/// Stands in for a wrapper someone else installs on top, the way DisplayManager
/// wraps the pointer for sleep handling.
void outer_wrapper_read(lv_indev_t* /*indev*/, lv_indev_data_t* /*data*/) {}

/// LVGL stores only a function pointer, so the hook under test is reached
/// through file scope, the way the backend reaches itself through
/// DisplayBackend::active().
IndevReadHook* g_hook = nullptr;
int g_hook_depth = 0;
int g_hook_max_depth = 0;

/// Refuses to recurse, so a hook that became its own original shows up as a
/// depth of 2 instead of a stack overflow.
void hook_read(lv_indev_t* indev, lv_indev_data_t* data) {
    ++g_hook_depth;
    g_hook_max_depth = std::max(g_hook_max_depth, g_hook_depth);
    if (g_hook_depth == 1 && g_hook != nullptr) {
        g_hook->read_original(indev, data);
    }
    --g_hook_depth;
}

class IndevReadHookFixture : public LVGLTestFixture {
  public:
    IndevReadHookFixture() {
        g_hook = &hook;
        g_hook_depth = 0;
        g_hook_max_depth = 0;
    }
    ~IndevReadHookFixture() override {
        for (lv_indev_t* indev : created) {
            lv_indev_delete(indev);
        }
        g_hook = nullptr;
    }

    lv_indev_t* make_pointer(lv_indev_read_cb_t driver_read) {
        lv_indev_t* indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, driver_read);
        created.push_back(indev);
        return indev;
    }

    /// One read, through whatever callback the device currently carries.
    static lv_indev_data_t read(lv_indev_t* indev) {
        lv_indev_data_t data{};
        lv_indev_get_read_cb(indev)(indev, &data);
        return data;
    }

    IndevReadHook hook{hook_read};
    std::vector<lv_indev_t*> created;
};

} // namespace

TEST_CASE_METHOD(IndevReadHookFixture, "Each hooked pointer reads through its own driver",
                 "[display][indev][rotation]") {
    lv_indev_t* touch = make_pointer(touch_driver_read);
    lv_indev_t* mouse = make_pointer(mouse_driver_read);

    // The backend logs an installed hook on this answer.
    CHECK(hook.install(touch));
    CHECK(hook.install(mouse));

    // Both devices really run the hook, or the reads below prove nothing.
    REQUIRE(lv_indev_get_read_cb(touch) == hook_read);
    REQUIRE(lv_indev_get_read_cb(mouse) == hook_read);

    const lv_indev_data_t from_touch = read(touch);
    CHECK(from_touch.point.x == 11);
    CHECK(from_touch.point.y == 22);

    const lv_indev_data_t from_mouse = read(mouse);
    CHECK(from_mouse.point.x == 333);
    CHECK(from_mouse.point.y == 444);
}

TEST_CASE_METHOD(IndevReadHookFixture,
                 "A pointer already running the hook function is never linked to it",
                 "[display][indev][rotation]") {
    lv_indev_t* touch = make_pointer(touch_driver_read);

    // A second hook on the same function fronts the device first, so the
    // callback the hook under test finds there is the hook function itself.
    // The same hook installing twice meets exactly this.
    IndevReadHook first{hook_read};
    CHECK(first.install(touch));
    REQUIRE(lv_indev_get_read_cb(touch) == hook_read);

    // Reported as not installed, so nothing logs a hook that is not there.
    CHECK_FALSE(hook.install(touch));

    read(touch);
    CHECK(g_hook_max_depth == 1);

    lv_indev_data_t data{};
    CHECK_FALSE(hook.read_original(touch, &data));
}

TEST_CASE_METHOD(IndevReadHookFixture, "A pointer with no driver behind the hook reads nothing",
                 "[display][indev][rotation]") {
    lv_indev_t* touch = make_pointer(touch_driver_read);
    lv_indev_t* stranger = make_pointer(mouse_driver_read);
    lv_indev_t* driverless = make_pointer(nullptr);

    hook.install(touch);
    hook.install(driverless);
    REQUIRE(lv_indev_get_read_cb(touch) == hook_read);
    REQUIRE(lv_indev_get_read_cb(driverless) == hook_read);

    lv_indev_data_t data{};
    data.point = {-7, -9};
    CHECK_FALSE(hook.read_original(stranger, &data));
    CHECK_FALSE(hook.read_original(driverless, &data));
    CHECK(data.point.x == -7);
    CHECK(data.point.y == -9);
    CHECK(lv_indev_get_read_cb(stranger) == mouse_driver_read);
}

TEST_CASE_METHOD(IndevReadHookFixture, "Restore hands each pointer back its own driver callback",
                 "[display][indev][rotation]") {
    lv_indev_t* touch = make_pointer(touch_driver_read);
    lv_indev_t* mouse = make_pointer(mouse_driver_read);
    hook.install(touch);
    hook.install(mouse);
    REQUIRE(lv_indev_get_read_cb(touch) == hook_read);
    REQUIRE(lv_indev_get_read_cb(mouse) == hook_read);

    hook.restore_all();

    CHECK(lv_indev_get_read_cb(touch) == touch_driver_read);
    CHECK(lv_indev_get_read_cb(mouse) == mouse_driver_read);

    // A restored hook forgets the devices, so a stale call through it reads nothing.
    lv_indev_data_t data{};
    CHECK_FALSE(hook.read_original(touch, &data));
    CHECK_FALSE(hook.read_original(mouse, &data));
}

TEST_CASE_METHOD(IndevReadHookFixture,
                 "Restore leaves a pointer alone when another wrapper sits on top",
                 "[display][indev][rotation]") {
    lv_indev_t* touch = make_pointer(touch_driver_read);
    lv_indev_t* mouse = make_pointer(mouse_driver_read);
    hook.install(touch);
    hook.install(mouse);
    lv_indev_set_read_cb(touch, outer_wrapper_read);

    hook.restore_all();

    // Overwriting the outer wrapper would silently remove it; only its owner
    // can take it off. The device nobody wrapped still gets its driver back.
    CHECK(lv_indev_get_read_cb(touch) == outer_wrapper_read);
    CHECK(lv_indev_get_read_cb(mouse) == mouse_driver_read);
}
