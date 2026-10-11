// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../lvgl_test_fixture.h"
#include "display_backend.h"
#include "lvgl/src/indev/lv_indev_gesture_private.h"
#include "lvgl/src/indev/lv_indev_private.h"

#include "../catch_amalgamated.hpp"

using Catch::Approx;

#if LV_USE_GESTURE_RECOGNITION
TEST_CASE_METHOD(LVGLTestFixture,
                 "configure_touch_gestures: pinch thresholds set, rotate out of reach",
                 "[display][gesture]") {
    lv_indev_t* indev = lv_indev_create();
    REQUIRE(indev != nullptr);

    DisplayBackend::configure_touch_gestures(indev);

    const auto* pinch = indev->recognizers[LV_INDEV_GESTURE_PINCH].config;
    REQUIRE(pinch != nullptr);
    CHECK(pinch->pinch_up_threshold == Approx(1.15f));
    CHECK(pinch->pinch_down_threshold == Approx(0.85f));

    const auto* rotate = indev->recognizers[LV_INDEV_GESTURE_ROTATE].config;
    REQUIRE(rotate != nullptr);
    CHECK(rotate->rotation_angle_rad_threshold == Approx(3.14f));

    lv_indev_delete(indev);
}

TEST_CASE_METHOD(LVGLTestFixture, "configure_touch_gestures: null indev is a no-op",
                 "[display][gesture]") {
    DisplayBackend::configure_touch_gestures(nullptr);
    SUCCEED("no crash");
}
#endif
