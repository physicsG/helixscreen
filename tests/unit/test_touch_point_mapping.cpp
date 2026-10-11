// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_touch_point_mapping.cpp
 * @brief Multi-touch gesture points land where the single pointer's samples land
 *
 * A gesture's fingers reach the recognizers in the driver's frame. These map them
 * through calibration, the scanout plane and LVGL's display rotation, and check the
 * reader hands the gesture callbacks screen-frame pan and anchor.
 */

#include "../lvgl_test_fixture.h"
#include "lvgl/src/indev/lv_indev_gesture_private.h"
#include "lvgl/src/indev/lv_indev_private.h"
#include "lvgl/src/misc/lv_event_private.h"
#include "pointer_frame_hook.h"
#include "touch_calibration_wrapper.h"
#include "view_gestures.h"

#include "../catch_amalgamated.hpp"

using helix::map_touch_point_to_screen;
using helix::PointerFrameHook;
using helix::input::PointerKind;

namespace {

constexpr lv_point_t A{100, 50};
constexpr lv_point_t B{110, 50}; // A plus a 10px drag to the right, in the driver's frame

void touch_driver_read(lv_indev_t* /*indev*/, lv_indev_data_t* data) {
    data->point = A;
    data->state = LV_INDEV_STATE_PRESSED;
}

class TouchPointMappingFixture : public LVGLTestFixture {
  public:
    TouchPointMappingFixture() {
        disp = lv_display_get_default();
        lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_0);
        touch = lv_indev_create();
        lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(touch, touch_driver_read);
    }
    ~TouchPointMappingFixture() override {
        helix::uninstall_calibration_wrapper(touch, calibration);
        hook.restore_all();
        lv_indev_delete(touch);
        lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_0);
    }

    lv_point_t drag() const {
        const lv_point_t a = map_touch_point_to_screen(touch, A);
        const lv_point_t b = map_touch_point_to_screen(touch, B);
        return {b.x - a.x, b.y - a.y};
    }

    lv_display_t* disp = nullptr;
    lv_indev_t* touch = nullptr;
    PointerFrameHook hook;
    helix::CalibrationContext calibration;
};

} // namespace

TEST_CASE_METHOD(TouchPointMappingFixture,
                 "A touch point on an unrotated, uncalibrated display maps to itself",
                 "[gesture][rotation]") {
    const lv_point_t mapped = map_touch_point_to_screen(touch, A);
    CHECK(mapped.x == A.x);
    CHECK(mapped.y == A.y);
}

TEST_CASE_METHOD(TouchPointMappingFixture, "A gesture drag turns with LVGL's display rotation",
                 "[gesture][rotation]") {
    SECTION("90") {
        lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);
        const lv_point_t d = drag();
        CHECK(d.x == 0);
        CHECK(d.y == 10);
    }
    SECTION("180") {
        lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_180);
        const lv_point_t d = drag();
        CHECK(d.x == -10);
        CHECK(d.y == 0);
        const lv_point_t mapped = map_touch_point_to_screen(touch, A);
        CHECK(mapped.x == TEST_DISPLAY_WIDTH - 1 - A.x);
        CHECK(mapped.y == TEST_DISPLAY_HEIGHT - 1 - A.y);
    }
    SECTION("270") {
        lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);
        const lv_point_t d = drag();
        CHECK(d.x == 0);
        CHECK(d.y == -10);
    }
}

TEST_CASE_METHOD(TouchPointMappingFixture, "A gesture drag turns with the scanout plane",
                 "[gesture][rotation]") {
    hook.set_plane_rotation(180, TEST_DISPLAY_WIDTH, TEST_DISPLAY_HEIGHT);
    REQUIRE(hook.install(touch, PointerKind::PanelAbsolute));

    const lv_point_t d = drag();
    CHECK(d.x == -10);
    CHECK(d.y == 0);
    const lv_point_t mapped = map_touch_point_to_screen(touch, A);
    CHECK(mapped.x == TEST_DISPLAY_WIDTH - 1 - A.x);
    CHECK(mapped.y == TEST_DISPLAY_HEIGHT - 1 - A.y);
}

TEST_CASE_METHOD(TouchPointMappingFixture, "A gesture point carries the touch calibration",
                 "[gesture][rotation]") {
    helix::TouchCalibration cal;
    cal.valid = true;
    cal.c = 5.0f;
    cal.f = -3.0f;
    helix::install_calibration_wrapper(touch, calibration, cal, TEST_DISPLAY_WIDTH,
                                       TEST_DISPLAY_HEIGHT);

    const lv_point_t mapped = map_touch_point_to_screen(touch, A);
    CHECK(mapped.x == A.x + 5);
    CHECK(mapped.y == A.y - 3);
    const lv_point_t d = drag();
    CHECK(d.x == 10);
    CHECK(d.y == 0);
}

#if LV_USE_GESTURE_RECOGNITION
TEST_CASE_METHOD(TouchPointMappingFixture,
                 "The gesture reader hands out screen-frame pan and anchor on a rotated display",
                 "[gesture][rotation]") {
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_180);

    lv_indev_gesture_t info{};
    info.center = A;
    info.delta_x = 10.0f;
    info.delta_y = 0.0f;
    lv_indev_gesture_recognizer_t pinch{};
    pinch.type = LV_INDEV_GESTURE_PINCH;
    pinch.state = LV_INDEV_GESTURE_STATE_RECOGNIZED;
    pinch.scale = 1.2f;
    pinch.info = &info;
    touch->gesture_data[LV_INDEV_GESTURE_PINCH] = &pinch;
    touch->cur_gesture = LV_INDEV_GESTURE_PINCH;

    lv_event_t e{};
    e.code = LV_EVENT_GESTURE;
    e.param = touch;

    const auto sample = helix::ui::read_two_finger_sample(&e);
    touch->gesture_data[LV_INDEV_GESTURE_PINCH] = nullptr;
    touch->cur_gesture = LV_INDEV_GESTURE_NONE;

    REQUIRE(sample.has_value());
    CHECK(sample->kind == helix::ui::TwoFingerSample::Kind::Pinch);
    CHECK(sample->phase == helix::ui::TwoFingerSample::Phase::Recognized);
    CHECK(sample->scale == Catch::Approx(1.2f));
    CHECK(sample->start_x == TEST_DISPLAY_WIDTH - 1 - A.x);
    CHECK(sample->start_y == TEST_DISPLAY_HEIGHT - 1 - A.y);
    CHECK(sample->delta_x == Catch::Approx(-10.0f));
    CHECK(sample->delta_y == Catch::Approx(0.0f));
}
#endif
