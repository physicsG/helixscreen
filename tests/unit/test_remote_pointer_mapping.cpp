// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_remote_pointer_mapping.cpp
 * @brief The synthetic pointer lands under a LOGICAL point on a rotated display.
 *
 * LVGL rotates every pointer indev's reading from physical to logical
 * (lv_display_rotate_point). A remote point is already logical: the screen
 * server serves the upright image, and ctl names widget coordinates. So the
 * pointer is fed the inverse rotation and LVGL's own rotation brings it back.
 */

#include "../lvgl_test_fixture.h"
#include "../test_helpers/remote_pointer_test_access.h"
#include "input_settings_manager.h"
#include "input_settings_test_helpers.h"
#include "lvgl/src/indev/lv_indev_private.h"
#include "remote_pointer.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "../catch_amalgamated.hpp"

using helix::remote::remote_image_to_indev_point;
using helix::remote::RemotePointer;
using namespace std::chrono_literals;

namespace {

/// Restores the shared test display's rotation however the test exits.
struct RotationScope {
    lv_display_t* disp;
    lv_display_rotation_t saved;
    RotationScope(lv_display_t* d, lv_display_rotation_t r)
        : disp(d), saved(lv_display_get_rotation(d)) {
        lv_display_set_rotation(disp, r);
    }
    ~RotationScope() {
        lv_display_set_rotation(disp, saved);
    }
};

} // namespace

TEST_CASE_METHOD(LVGLTestFixture,
                 "mapping round-trips through lv_display_rotate_point at 0/90/180/270",
                 "[remote_pointer]") {
    lv_display_t* disp = lv_display_get_default();
    REQUIRE(disp != nullptr);
    const int32_t ow = lv_display_get_original_horizontal_resolution(disp);
    const int32_t oh = lv_display_get_original_vertical_resolution(disp);

    for (auto rot : {LV_DISPLAY_ROTATION_0, LV_DISPLAY_ROTATION_90, LV_DISPLAY_ROTATION_180,
                     LV_DISPLAY_ROTATION_270}) {
        RotationScope scope(disp, rot);
        const int32_t lw = lv_display_get_horizontal_resolution(disp);
        const int32_t lh = lv_display_get_vertical_resolution(disp);
        const lv_point_t logical[] = {
            {0, 0}, {lw - 1, 0}, {0, lh - 1}, {lw - 1, lh - 1}, {lw / 2, lh / 3}};
        for (const auto& p : logical) {
            CAPTURE(static_cast<int>(rot), p.x, p.y);
            lv_point_t raw = remote_image_to_indev_point(rot, ow, oh, p.x, p.y);
            lv_display_rotate_point(disp, &raw);
            CHECK(raw.x == p.x);
            CHECK(raw.y == p.y);
        }
    }
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "set_logical_state on a 90-degree display clicks the button under the point",
                 "[remote_pointer]") {
    lv_display_t* disp = lv_display_get_default();
    RotationScope scope(disp, LV_DISPLAY_ROTATION_90);

    lv_obj_t* btn = lv_button_create(lv_screen_active());
    lv_obj_set_pos(btn, 40, 40);
    lv_obj_set_size(btn, 60, 60);
    lv_obj_update_layout(btn);
    static std::atomic<int> clicks{0};
    clicks = 0;
    lv_obj_add_event_cb(btn, [](lv_event_t*) { clicks.fetch_add(1); }, LV_EVENT_CLICKED, nullptr);

    auto& ptr = RemotePointer::instance();
    REQUIRE(ptr.ensure_created());
    // The test harness runs only finite-repeat timers, so the indev read timer never
    // fires on its own: read every indev by hand, twice, as LVGL would.
    auto sample_twice = [&] {
        const uint64_t base = ptr.read_count();
        for (int pass = 0; pass < 2; ++pass) {
            for (lv_indev_t* i = lv_indev_get_next(nullptr); i; i = lv_indev_get_next(i)) {
                lv_indev_read(i);
            }
        }
        REQUIRE(ptr.read_count() >= base + 2);
    };
    ptr.set_logical_state(70, 70, true);
    sample_twice();
    ptr.set_logical_state(70, 70, false);
    sample_twice();
    CHECK(clicks.load() == 1);

    lv_obj_delete(btn);
}

TEST_CASE("wait_for_samples returns Stopped when keep_going flips", "[remote_pointer]") {
    auto& ptr = RemotePointer::instance();
    std::atomic<bool> go{true};
    std::thread stopper([&] {
        std::this_thread::sleep_for(20ms);
        go = false;
    });
    // Nothing samples the pointer here, so only keep_going can end the wait early.
    const auto start = std::chrono::steady_clock::now();
    auto r = ptr.wait_for_samples(ptr.read_count(), 5000ms, [&] { return go.load(); });
    stopper.join();
    CHECK(r == RemotePointer::WaitResult::Stopped);
    CHECK(std::chrono::steady_clock::now() - start < 2s);
}

TEST_CASE("wait_for_samples times out when nothing samples", "[remote_pointer]") {
    auto& ptr = RemotePointer::instance();
    auto r = ptr.wait_for_samples(ptr.read_count(), 30ms, [] { return true; });
    CHECK(r == RemotePointer::WaitResult::TimedOut);
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "the remote pointer is created with the Touch & Input long-press time",
                 "[remote_pointer][input_settings][tap_hold]") {
    using helix::remote::RemotePointerTestAccess;
    auto& ptr = RemotePointer::instance();
    RemotePointerTestAccess::destroy_device(ptr);

    // Set while no remote pointer exists, so only its creation can apply it.
    helix_test::reset_input_settings_to_defaults();
    auto& input = helix::InputSettingsManager::instance();
    input.set_long_press_time(1000);
    REQUIRE(input.get_long_press_time() == 1000);
    REQUIRE(ptr.ensure_created());
    lv_indev_t* indev = RemotePointerTestAccess::device(ptr);
    REQUIRE(indev != nullptr);
    CHECK(indev->long_press_time == 1000);

    helix_test::reset_input_settings_to_defaults();
}
